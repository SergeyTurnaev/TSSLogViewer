#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Временный тестовый ключ никогда не совпадает с ключом выпуска."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("destination", type=Path)
parser.add_argument("--openssl", default="openssl")
args = parser.parse_args()
dest = args.destination.resolve()
dest.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location("package", Path(__file__).resolve().parents[2] / "tools/platform-package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)
subprocess.run([args.openssl, "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048",
                "-out", str(dest / "test-private.pem")], check=True, capture_output=True)
subprocess.run([args.openssl, "pkey", "-in", str(dest / "test-private.pem"), "-pubout",
                "-out", str(dest / "public.pem")], check=True, capture_output=True)
image = b"AppImage test data\n" * 1024
(dest / "image").write_bytes(image)
(dest / "corrupt-image").write_bytes(image[:-1] + b"x")
sha = hashlib.sha256(image).hexdigest()
manifest = dict(schema=1, app="TSSLogViewer", arch="x86_64", version="26.3",
                build="101", expires="2000", size=str(len(image)), sha256=sha,
                image=f"files/{sha}.AppImage", zsync=f"files/{sha}.AppImage.zsync")
private = (dest / "test-private.pem").read_text()
cases = {"valid": {}}
for field, value in (("app", "TSSLimiterX"), ("arch", "arm64"), ("expires", "999"),
                     ("version", "26.1"), ("build", "99"), ("image", "../bad"),
                     ("zsync", "https://example.com/bad"), ("size", "-1"),
                     ("schema", 2), ("sha256", "bad")):
    cases["reject-" + field] = {field: value}
for name, override in cases.items():
    payload = json.dumps(manifest | override, separators=(",", ":")).encode()
    (dest / (name + ".tssupdate")).write_bytes(package.sign_rsa(
        payload, private, args.openssl, dest / "public.pem"))
print("Prepared signed fixtures with an independent test key")
