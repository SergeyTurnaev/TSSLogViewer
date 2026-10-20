// SPDX-License-Identifier: MIT
#include "linuxinstall.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>

namespace tss::appimage {
QString install(const QString& original, const QString& candidate, const Release& release) {
    const QFileInfo current(original);
    if (!current.isFile() || current.isSymLink() || !matches(candidate, release))
        throw std::runtime_error("The installed or downloaded AppImage has changed");
    // Уникальное имя позволяет повторить попытку после ошибки записи без удаления копии.
    const auto backup = original + "." + QUuid::createUuid().toString(QUuid::Id128) + ".backup";
    if (!QFile::copy(original, backup)) throw std::runtime_error("Cannot create an AppImage backup");
    QFile input(candidate);
    QSaveFile output(original);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)
        || !output.setPermissions(current.permissions()))
        throw std::runtime_error("Cannot replace this AppImage");
    QCryptographicHash copiedHash(QCryptographicHash::Sha256);
    qint64 copied = 0;
    while (!input.atEnd()) {
        const auto block = input.read(1024 * 1024);
        if (block.isEmpty() || output.write(block) != block.size())
            throw std::runtime_error("Cannot write the updated AppImage");
        copiedHash.addData(block);
        copied += block.size();
    }
    if (input.error() != QFile::NoError || copied != release.size
        || copiedHash.result().toHex() != release.sha256.toLatin1())
        throw std::runtime_error("AppImage changed while copying");
    if (!output.commit()) throw std::runtime_error("Cannot commit the updated AppImage");
    return backup;
}
}
