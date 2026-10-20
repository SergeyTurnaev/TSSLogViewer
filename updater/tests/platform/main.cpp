// SPDX-License-Identifier: MIT
#include "linuxmanifest.h"
#include "linuxinstall.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

QByteArray read(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing fixture");
    return f.readAll();
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        if (argc != 2) return 2;
        const QDir dir(QString::fromLocal8Bit(argv[1]));
        const auto key = read(dir.filePath("public.pem"));
        const auto valid = read(dir.filePath("valid.tssupdate"));
        const auto verify = [&](const QByteArray& bytes) {
            return tss::appimage::verify(bytes, key, "TSSLogViewer", "26.2", 100, 1000);
        };
        const auto release = verify(valid);
        if (release.build != 101 || release.version != "26.3"
            || !tss::appimage::matches(dir.filePath("image"), release)
            || tss::appimage::matches(dir.filePath("corrupt-image"), release)) return 3;
        auto damaged = valid; damaged[damaged.size() - 2] ^= 1;
        auto signature = valid; signature[2] = signature[2] == 'A' ? 'B' : 'A';
        QList<QByteArray> invalid{damaged, signature, valid.mid(1), QByteArray(40000, 'x')};
        for (const auto& file : dir.entryList({"reject-*.tssupdate"}, QDir::Files)) invalid << read(dir.filePath(file));
        if (invalid.size() < 12) return 4;
        for (const auto& fixture : invalid) {
            bool rejected = false;
            try { verify(fixture); } catch (const std::exception&) { rejected = true; }
            if (!rejected) return 5;
        }
        QTemporaryDir work;
        if (!work.isValid()) return 7;
        const auto original = work.filePath("test.AppImage");
        const auto old = read(dir.filePath("corrupt-image"));
        if (!QFile::copy(dir.filePath("corrupt-image"), original)) return 8;
        bool refused = false;
        try { tss::appimage::install(original, dir.filePath("corrupt-image"), release); }
        catch (const std::exception&) { refused = true; }
        if (!refused || read(original) != old) return 9;
        const auto backup = tss::appimage::install(original, dir.filePath("image"), release);
        if (!tss::appimage::matches(original, release) || read(backup) != old) return 10;
        std::cout << "Signature, corruption, identity, expiry, downgrade, atomic replacement and backup checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 6; }
}
