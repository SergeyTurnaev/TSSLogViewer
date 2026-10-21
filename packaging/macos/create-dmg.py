#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Создать и проверить DMG с перетаскиванием приложения в «Программы»."""
import argparse
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile


def verify_image(image, app_name, info):
    subprocess.run(["hdiutil", "verify", str(image)], check=True)
    with tempfile.TemporaryDirectory(prefix="tss-dmg-check-") as folder:
        mount = Path(folder) / "volume"
        mount.mkdir()
        subprocess.run(["hdiutil", "attach", "-readonly", "-nobrowse", "-noautoopen",
                        "-mountpoint", str(mount), str(image)], check=True)
        try:
            application = mount / app_name
            link = mount / "Applications"
            if not link.is_symlink() or str(link.readlink()) != "/Applications":
                raise ValueError("В DMG отсутствует ссылка на /Applications")
            if not (mount / ".DS_Store").is_file():
                raise ValueError("В DMG отсутствуют настройки окна Finder")
            with (application / "Contents/Info.plist").open("rb") as stream:
                if plistlib.load(stream) != info:
                    raise ValueError("Приложение в DMG отличается от исходного")
            # Проверяем сохранность подписи и ссылок внутри Qt/Sparkle после копирования.
            subprocess.run(["codesign", "--verify", "--deep", "--strict", str(application)], check=True)
        finally:
            subprocess.run(["hdiutil", "detach", str(mount)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("Создание DMG требует macOS")
    application = args.app.resolve(strict=True)
    if application.suffix != ".app":
        parser.error("Ожидается приложение .app")
    output = args.output.resolve()
    if output.suffix != ".dmg" or output.exists():
        parser.error("Нужен новый выходной файл .dmg")
    with (application / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    name = info["CFBundleName"]
    if name not in ("TSSLimiterX", "TSSLogViewer"):
        parser.error("Неизвестное приложение")
    app_name = f"{name}.app"
    icon = Path(info["CFBundleIconFile"])
    if not icon.suffix:
        icon = icon.with_suffix(".icns")
    settings = {
        "format": "ULFO",
        "filesystem": "APFS",
        "files": [(str(application), app_name)],
        "symlinks": {"Applications": "/Applications"},
        "icon": str(application / "Contents/Resources" / icon),
        "background": "builtin-arrow",
        "icon_locations": {app_name: (140, 120), "Applications": (500, 120)},
        "window_rect": ((200, 120), (640, 280)),
        "default_view": "icon-view",
        "arrange_by": None,
        "icon_size": 96,
        "text_size": 14,
        "show_icon_preview": False,
        "show_status_bar": False,
        "show_tab_view": False,
        "show_toolbar": False,
        "show_pathbar": False,
        "show_sidebar": False,
    }
    # dmgbuild записывает оформление Finder без запуска графического интерфейса в CI.
    import dmgbuild
    dmgbuild.build_dmg(str(output), f"{name} {info['CFBundleShortVersionString']}", settings=settings)
    verify_image(output, app_name, info)


if __name__ == "__main__":
    main()
