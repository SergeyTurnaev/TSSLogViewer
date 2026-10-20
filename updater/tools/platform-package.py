#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Подготовка пакетов Sparkle/AppImageUpdate. Ничего не публикует на сервер."""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import time
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
HELPER_URL = ("https://github.com/AppImageCommunity/AppImageUpdate/releases/download/"
              "2.0.0-alpha-1-20251018/appimageupdatetool-x86_64.AppImage")
HELPER_SHA = "d976cdac667b03dee8cb23fb95ef74b042c406c5cbab3ff294d2b16efeaff84f"
SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"


def run(*args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, **kwargs).stdout


def digest(path):
    result = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def prepare_helper(appdir):
    target = Path(appdir) / "usr/libexec/appimageupdatetool.AppImage"
    target.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(HELPER_URL, timeout=120) as response, target.open("wb") as output:
        shutil.copyfileobj(response, output)
    if digest(target) != HELPER_SHA:
        target.unlink()
        raise ValueError("AppImageUpdate SHA-256 mismatch")
    target.chmod(0o755)
    licenses = Path(appdir) / "usr/share/licenses/TSSUpdater"
    shutil.copytree(ROOT / "licenses", licenses, dirs_exist_ok=True)
    shutil.copy2(ROOT / "LICENSE", licenses / "TSSUpdater.txt")
    shutil.copy2("/usr/share/doc/libssl3/copyright", licenses / "OpenSSL.txt")


def sign_rsa(payload, private_key, openssl="openssl", public_key=None):
    # Файл ключа имеет права 0600 и удаляется до завершения шага.
    with tempfile.TemporaryDirectory(prefix="tss-sign-") as temp:
        work = Path(temp)
        key, data, sig = work / "private.pem", work / "payload", work / "signature"
        fd = os.open(key, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(fd, "w") as stream:
            stream.write(private_key)
        data.write_bytes(payload)
        signature = run(openssl, "dgst", "-sha256", "-sign", str(key),
                        "-sigopt", "rsa_padding_mode:pss", "-sigopt", "rsa_pss_saltlen:32", str(data))
        sig.write_bytes(signature)
        run(openssl, "dgst", "-sha256", "-verify", str(public_key or ROOT / "keys/release-public.pem"),
            "-signature", str(sig), "-sigopt", "rsa_padding_mode:pss",
            "-sigopt", "rsa_pss_saltlen:32", str(data))
        return base64.b64encode(signature) + b"\n" + payload


def linux(args, output):
    sha = digest(args.artifact)
    image = f"files/{sha}.AppImage"
    target = output / image
    target.parent.mkdir(parents=True)
    shutil.copy2(args.artifact, target)
    run("zsyncmake", "-u", f"{sha}.AppImage", "-o", str(target) + ".zsync", str(target))
    # Указывает на неизменяемый файл в том же каталоге. Имя не зависит от номера версии.
    control = Path(str(target) + ".zsync")
    contents = control.read_bytes()
    header, blocks = contents.split(b"\n\n", 1)
    header = re.sub(rb"(?m)^Filename:.*$", f"Filename: {sha}.AppImage".encode(), header)
    control.write_bytes(header + b"\n\n" + blocks)
    manifest = dict(schema=1, app=args.app, arch="x86_64", version=args.version,
                    build=str(args.build), expires=str(int(time.time()) + 180 * 86400),
                    size=str(target.stat().st_size), sha256=sha, image=image, zsync=image + ".zsync")
    payload = json.dumps(manifest, separators=(",", ":"), sort_keys=True).encode()
    (output / "latest.tssupdate").write_bytes(sign_rsa(payload, os.environ["TSS_UPDATE_PRIVATE_KEY"]))
    # Внешний AppImageUpdate использует стабильную ссылку из .upd_info.
    # Наша программа проверяет подпись latest.tssupdate и SHA-256 результата.
    stable = header.replace(f"URL: {sha}.AppImage".encode(), f"URL: files/{sha}.AppImage".encode())
    (output / "latest.AppImage.zsync").write_bytes(stable + b"\n\n" + blocks)


def macos(args, output):
    # Архив уже подписан codesign; производственный пакет также нотарифицирован.
    with zipfile.ZipFile(args.artifact) as archive:
        plists = [n for n in archive.namelist() if n.count("/") == 2 and n.endswith(".app/Contents/Info.plist")]
        if len(plists) != 1:
            raise ValueError("Archive must contain one application")
        info = plistlib.loads(archive.read(plists[0]))
    expected_key = (ROOT / "keys/sparkle-public.txt").read_text().strip()
    bundle_version = f"{args.build // 100000000}.{args.build // 10000 % 10000}.{args.build % 10000}"
    if info.get("SUPublicEDKey") != expected_key or info["CFBundleVersion"] != bundle_version:
        raise ValueError("Sparkle key/build does not match the application")
    if info["CFBundleShortVersionString"] != args.version:
        raise ValueError("Sparkle version does not match the application")
    sign = str(Path(args.sparkle) / "bin/sign_update")
    key = os.environ["TSS_SPARKLE_PRIVATE_KEY"].strip().encode()
    signature = run(sign, "-p", "--ed-key-file", "-", str(args.artifact), input=key).decode().strip()
    if len(base64.b64decode(signature, validate=True)) != 64:
        raise ValueError("Invalid Sparkle signature")
    run(sign, "--verify", "--ed-key-file", "-", str(args.artifact), signature, input=key)
    # Проверяем соответствие закрытого ключа тому, который встроен в приложение.
    seed = base64.b64decode(key, validate=True)
    if len(seed) != 32:
        raise ValueError("Expected a Sparkle Ed25519 seed (32 bytes)")
    der = bytes.fromhex("302e020100300506032b657004220420") + seed
    public = run("openssl", "pkey", "-inform", "DER", "-pubout", "-outform", "DER", input=der)
    if base64.b64encode(public[-32:]).decode() != expected_key:
        raise ValueError("Sparkle signing key does not match the embedded public key")
    sha = digest(args.artifact)
    relative = f"files/{args.app}-{args.build}-{sha[:16]}.app.zip"
    target = output / relative
    target.parent.mkdir(parents=True)
    shutil.copy2(args.artifact, target)
    ET.register_namespace("sparkle", SPARKLE_NS)
    for host in ("tsslimiter.com", "tsslimiter.ru"):
        rss = ET.Element("rss", version="2.0")
        channel = ET.SubElement(rss, "channel")
        ET.SubElement(channel, "title").text = args.app
        item = ET.SubElement(channel, "item")
        ET.SubElement(item, "title").text = f"{args.app} {args.version}"
        ET.SubElement(item, f"{{{SPARKLE_NS}}}version").text = bundle_version
        ET.SubElement(item, f"{{{SPARKLE_NS}}}shortVersionString").text = args.version
        ET.SubElement(item, f"{{{SPARKLE_NS}}}minimumSystemVersion").text = "12.0"
        ET.SubElement(item, "enclosure", {
            "url": f"https://{host}/updates/macos/{args.channel}/{relative}",
            "length": str(target.stat().st_size), "type": "application/octet-stream",
            f"{{{SPARKLE_NS}}}edSignature": signature,
        })
        feed = output / host / "appcast.xml"
        feed.parent.mkdir()
        ET.ElementTree(rss).write(feed, encoding="utf-8", xml_declaration=True)
        run(sign, "--ed-key-file", "-", str(feed), input=key)
        run(sign, "--verify", "--ed-key-file", "-", str(feed), input=key)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=["linux", "macos", "prepare-linux"])
    parser.add_argument("--appdir")
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--app", choices=["TSSLimiterX", "TSSLogViewer"])
    parser.add_argument("--version")
    parser.add_argument("--build", type=int)
    parser.add_argument("--channel", choices=["limiter", "viewer", "viewer-community"])
    parser.add_argument("--sparkle")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-signature", action="store_true")
    args = parser.parse_args()
    if args.platform == "prepare-linux":
        if not args.appdir:
            parser.error("--appdir is required")
        prepare_helper(args.appdir)
        return
    if not all((args.artifact, args.app, args.version, args.build, args.channel, args.output)):
        parser.error("artifact, app, version, build, channel and output are required")
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}", args.version) or args.build <= 0:
        parser.error("Invalid version/build")
    secret = "TSS_UPDATE_PRIVATE_KEY" if args.platform == "linux" else "TSS_SPARKLE_PRIVATE_KEY"
    if not os.environ.get(secret):
        if args.require_signature:
            raise SystemExit(f"Missing {secret}; refusing to produce an unsigned update feed")
        print(f"{secret} is unavailable: portable artifact only, no update feed")
        return
    if args.output.exists():
        raise SystemExit(f"Output already exists: {args.output}")
    args.output.mkdir(parents=True)
    (linux if args.platform == "linux" else macos)(args, args.output)
    (args.output / "publication.json").write_text(json.dumps({
        "app": args.app, "version": args.version, "build": args.build,
        "platform": args.platform, "channel": args.channel,
        "destination": f"/updates/{args.platform}/{args.channel}/",
        "instructions": "Upload files/ first, then the feed. For macOS use each host's appcast.xml. "
                        "Keep older files/ for clients with cached feeds. Never publish viewer-community into viewer.",
    }, indent=2) + "\n")
    shutil.make_archive(str(args.output), "zip", args.output)
    print(f"Prepared {args.output}.zip")


if __name__ == "__main__":
    main()
