// SPDX-License-Identifier: MIT
#include "linuxmanifest.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <memory>
#include <stdexcept>

namespace tss::appimage {
namespace {
void require(bool value) { if (!value) throw std::runtime_error("Invalid or untrusted update metadata"); }
}
Release verify(const QByteArray& envelope, const QByteArray& publicKey,
               const QString& app, const QString& version, qint64 build, qint64 now) {
    require(envelope.size() <= 32768);
    const auto newline = envelope.indexOf('\n');
    require(newline > 0 && newline < 1024);
    const auto sig = QByteArray::fromBase64(envelope.left(newline), QByteArray::AbortOnBase64DecodingErrors);
    const auto payload = envelope.mid(newline + 1);
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_mem_buf(publicKey.constData(), int(publicKey.size())), BIO_free);
    require(bool(bio));
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require(bool(key) && bool(ctx) && sig.size() == EVP_PKEY_size(key.get()));
    EVP_PKEY_CTX* pctx = nullptr;
    require(EVP_DigestVerifyInit(ctx.get(), &pctx, EVP_sha256(), nullptr, key.get()) == 1);
    require(EVP_PKEY_CTX_set_rsa_padding(pctx, RSA_PKCS1_PSS_PADDING) == 1);
    require(EVP_PKEY_CTX_set_rsa_pss_saltlen(pctx, 32) == 1);
    require(EVP_DigestVerify(ctx.get(), reinterpret_cast<const unsigned char*>(sig.constData()), sig.size(),
        reinterpret_cast<const unsigned char*>(payload.constData()), payload.size()) == 1);
    const auto doc = QJsonDocument::fromJson(payload);
    require(doc.isObject());
    const auto o = doc.object();
    require(o.value("schema").toInt() == 1 && o.value("app").toString() == app
        && o.value("arch").toString() == "x86_64");
    Release r;
    r.version = o.value("version").toString();
    r.build = o.value("build").toString().toLongLong();
    r.expires = o.value("expires").toString().toLongLong();
    r.size = o.value("size").toString().toLongLong();
    r.image = o.value("image").toString();
    r.zsync = o.value("zsync").toString();
    r.sha256 = o.value("sha256").toString();
    const QRegularExpression numericVersion("^[0-9]+(\\.[0-9]+){1,3}$");
    const QRegularExpression hash("^[0-9a-f]{64}$");
    require(numericVersion.match(r.version).hasMatch() && hash.match(r.sha256).hasMatch());
    require(r.build > 0 && r.expires > now && r.size > 0 && r.size <= 2LL * 1024 * 1024 * 1024);
    require(r.image == "files/" + r.sha256 + ".AppImage" && r.zsync == r.image + ".zsync");
    require(QVersionNumber::compare(QVersionNumber::fromString(r.version), QVersionNumber::fromString(version)) >= 0);
    // Старое зеркало не должно предлагать откат; равная сборка допустима как «обновлений нет».
    require(r.build >= build);
    return r;
}
bool matches(const QString& path, const Release& release) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() != release.size) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) && hash.result().toHex() == release.sha256.toLatin1();
}
}
