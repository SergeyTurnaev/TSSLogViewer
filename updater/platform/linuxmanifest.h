// SPDX-License-Identifier: MIT
#pragma once
#include <QByteArray>
#include <QString>

namespace tss::appimage {
struct Release {
    QString version, image, zsync, sha256;
    qint64 build = 0, expires = 0, size = 0;
};
// Исключение означает отказ от обновления: неподписанные данные не используются.
Release verify(const QByteArray& envelope, const QByteArray& publicKey,
               const QString& app, const QString& version, qint64 build, qint64 now);
bool matches(const QString& path, const Release& release);
}
