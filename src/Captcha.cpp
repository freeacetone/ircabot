/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "Captcha.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QList>
#include <QMutexLocker>
#include <QRandomGenerator>

#include <openssl/evp.h>

namespace ircabot {

namespace {

// No I, L, O, 0, 1: they read the same on a low-resolution image.
constexpr const char* ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
constexpr int AES_KEY_LEN = 32; // AES-256
constexpr int AES_IV_LEN = 16;

constexpr QByteArray::Base64Options B64 =
    QByteArray::Base64Options(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    for (int i = 0; i < n; ++i) {
        b[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return b;
}

// AES-256-CBC with PKCS#7 padding. Returns empty on failure.
QByteArray aesEncrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& plain)
{
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return {};
    }
    QByteArray out(plain.size() + AES_IV_LEN, Qt::Uninitialized); // room for padding
    int len = 0;
    int total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                                 reinterpret_cast<const unsigned char*>(key.constData()),
                                 reinterpret_cast<const unsigned char*>(iv.constData())) == 1
              && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len,
                                   reinterpret_cast<const unsigned char*>(plain.constData()),
                                   static_cast<int>(plain.size())) == 1;
    if (ok) {
        total = len;
        if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + total, &len) == 1) {
            total += len;
        } else {
            ok = false;
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        return {};
    }
    out.truncate(total);
    return out;
}

QByteArray aesDecrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& cipher, bool* ok)
{
    *ok = false;
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return {};
    }
    QByteArray out(cipher.size() + AES_IV_LEN, Qt::Uninitialized);
    int len = 0;
    int total = 0;
    bool good = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                                   reinterpret_cast<const unsigned char*>(key.constData()),
                                   reinterpret_cast<const unsigned char*>(iv.constData())) == 1
                && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(out.data()), &len,
                                     reinterpret_cast<const unsigned char*>(cipher.constData()),
                                     static_cast<int>(cipher.size())) == 1;
    if (good) {
        total = len;
        if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(out.data()) + total, &len) == 1) {
            total += len;
        } else {
            good = false; // bad padding / tampered ciphertext
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!good) {
        return {};
    }
    out.truncate(total);
    *ok = true;
    return out;
}

// Length-independent comparison for the signature check.
bool constEq(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

QByteArray identityHash(const QString& identity)
{
    return QCryptographicHash::hash(identity.toLower().toUtf8(), QCryptographicHash::Md5).toHex();
}

} // namespace

const QByteArray& Captcha::key()
{
    const QMutexLocker locker(&m_keyMutex);
    if (m_key.isEmpty()) {
        m_key = randomBytes(AES_KEY_LEN);
    }
    return m_key;
}

Captcha::Challenge Captcha::issue(const QString& identity, int length, int ttlSeconds)
{
    const int alphaLen = static_cast<int>(qstrlen(ALPHABET));
    QString answer;
    answer.reserve(length);
    for (int i = 0; i < length; ++i) {
        answer += QLatin1Char(ALPHABET[QRandomGenerator::system()->bounded(alphaLen)]);
    }

    const qint64 expiry = QDateTime::currentSecsSinceEpoch() + ttlSeconds;
    const QByteArray iv = randomBytes(AES_IV_LEN);
    const QByteArray cipher = aesEncrypt(key(), iv, answer.toUtf8());

    const QByteArray body = identityHash(identity) + '.' + QByteArray::number(expiry) + '.'
                            + iv.toBase64(B64) + '.' + cipher.toBase64(B64);
    const QByteArray sig = QCryptographicHash::hash(body + key(), QCryptographicHash::Sha256).toBase64(B64);

    Challenge c;
    c.answer = answer;
    c.nonce = QString::fromLatin1(body + '.' + sig);
    return c;
}

bool Captcha::verify(const QString& identity, const QString& nonce, const QString& answer)
{
    const QList<QByteArray> parts = nonce.toLatin1().split('.');
    if (parts.size() != 5) {
        return false;
    }
    const QByteArray body = parts[0] + '.' + parts[1] + '.' + parts[2] + '.' + parts[3];
    const QByteArray expectSig = QCryptographicHash::hash(body + key(), QCryptographicHash::Sha256).toBase64(B64);
    if (!constEq(parts[4], expectSig)) {
        return false; // tampered or forged
    }

    bool okNum = false;
    const qint64 expiry = parts[1].toLongLong(&okNum);
    if (!okNum || QDateTime::currentSecsSinceEpoch() > expiry) {
        return false;
    }
    if (!constEq(parts[0], identityHash(identity))) {
        return false; // issued for a different identity (server/nick/host)
    }

    const QByteArray iv = QByteArray::fromBase64(parts[2], B64);
    const QByteArray cipher = QByteArray::fromBase64(parts[3], B64);
    if (iv.size() != AES_IV_LEN || cipher.isEmpty()) {
        return false;
    }
    bool decOk = false;
    const QByteArray plain = aesDecrypt(key(), iv, cipher, &decOk);
    if (!decOk) {
        return false;
    }
    return QString::fromUtf8(plain).compare(answer.trimmed(), Qt::CaseInsensitive) == 0;
}

} // namespace ircabot
