/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QHash>
#include <QMutex>
#include <QString>

namespace ircabot {

// Per-client attempt counter for the captcha form. An entry is a short
// key plus 12 bytes of state, dropped as soon as it stops meaning anything:
// window expired, block expired, or the client solved the captcha.
class AttemptLimiter
{
public:
    AttemptLimiter(int maxAttempts, int windowSeconds, int blockSeconds);

    // Seconds left on the block, 0 when the client may try.
    int blockedFor(const QString& client) const;

    // Counts one attempt - a challenge handed out or a wrong answer. Returns the
    // block length in seconds when this attempt triggered a block, 0 otherwise.
    int registerAttempt(const QString& client);

    void forget(const QString& client);

private:
    struct Entry
    {
        quint32 windowStart = 0;  // secs since epoch
        quint32 blockedUntil = 0; // secs since epoch, 0 = not blocked
        quint16 attempts = 0;
    };

    // Distinct I2P destinations are cheap to create, so the map needs a ceiling
    // of its own; expired entries are swept before it is enforced.
    static constexpr int MAX_TRACKED = 10000;

    void sweep(qint64 now);

    const int m_maxAttempts;
    const int m_windowSeconds;
    const int m_blockSeconds;
    mutable QMutex m_mutex;
    mutable QHash<QString, Entry> m_entries;
};

} // namespace ircabot
