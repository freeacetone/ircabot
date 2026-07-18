/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>

namespace ircabot {

// Stateless captcha. The whole challenge lives in a self-describing nonce that
// travels in the form, so the server keeps no per-challenge database. The nonce
// carries a hash of the identity (e.g. "server/nick"), the AES-256 encrypted
// answer and an expiry, signed with a lazily generated runtime key. It therefore
// cannot be forged, cannot be used for a different identity, and expires on its
// own. Because voicing happens the moment the right identity solves it, replay
// within the short lifetime is moot.
class Captcha
{
public:
    struct Challenge
    {
        QString answer; // plaintext - only for drawing the image
        QString nonce;  // opaque token to embed in the form
    };

    // identity: opaque binding string (server/nick); length: characters in the
    // image; ttlSeconds: nonce lifetime.
    Challenge issue(const QString& identity, int length, int ttlSeconds);

    // True only when the nonce is intact, unexpired, bound to <identity> and the
    // submitted answer matches (case-insensitive).
    bool verify(const QString& identity, const QString& nonce, const QString& answer);

private:
    const QByteArray& key(); // AES + signature key, generated on first use

    QMutex m_keyMutex;
    QByteArray m_key;
};

} // namespace ircabot
