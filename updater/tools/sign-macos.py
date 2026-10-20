#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Подписать вложенные компоненты, затем внешнее приложение."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("app", type=Path)
parser.add_argument("--identity", default="-")
args = parser.parse_args()
app = args.app.resolve(strict=True)
if app.suffix != ".app":
    parser.error("Expected .app")
macho = {bytes.fromhex(m) for m in ("feedface", "cefaedfe", "feedfacf", "cffaedfe",
                                   "cafebabe", "bebafeca", "cafebabf", "bfbafeca")}
files, bundles = [], []
for folder, directories, names in os.walk(app, followlinks=False):
    for name in directories:
        path = Path(folder) / name
        if not path.is_symlink() and path.suffix in (".app", ".xpc", ".framework"):
            bundles.append(path)
    for name in names:
        path = Path(folder) / name
        if path.is_symlink():
            continue
        with path.open("rb") as stream:
            if stream.read(4) in macho:
                files.append(path)
for path in sorted(files, key=lambda p: len(p.parts), reverse=True) + sorted(
        bundles, key=lambda p: len(p.parts), reverse=True) + [app]:
    command = ["codesign", "--force", "--sign", args.identity, "--preserve-metadata=entitlements"]
    if args.identity != "-":
        command += ["--options", "runtime", "--timestamp"]
    subprocess.run(command + [str(path)], check=True)
subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
