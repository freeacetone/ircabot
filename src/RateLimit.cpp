/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "RateLimit.h"

#include <QDateTime>
#include <QMutexLocker>

namespace ircabot {

namespace {

qint64 nowSec()
{
    return QDateTime::currentSecsSinceEpoch();
}

} // namespace

AttemptLimiter::AttemptLimiter(int maxFails, int windowSeconds, int blockSeconds)
    : m_maxFails(maxFails),
      m_windowSeconds(windowSeconds),
      m_blockSeconds(blockSeconds)
{
}

void AttemptLimiter::sweep(qint64 now)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        const bool blocked = it->blockedUntil > 0 && static_cast<qint64>(it->blockedUntil) > now;
        const bool counting = it->blockedUntil == 0
                              && now - static_cast<qint64>(it->windowStart) < m_windowSeconds;
        if (blocked || counting) {
            ++it;
        } else {
            it = m_entries.erase(it);
        }
    }
}

int AttemptLimiter::blockedFor(const QString& client) const
{
    if (client.isEmpty()) {
        return 0;
    }
    const QMutexLocker locker(&m_mutex);
    const auto it = m_entries.find(client);
    if (it == m_entries.end() || it->blockedUntil == 0) {
        return 0;
    }
    const qint64 left = static_cast<qint64>(it->blockedUntil) - nowSec();
    if (left <= 0) {
        m_entries.erase(it);
        return 0;
    }
    return static_cast<int>(left);
}

int AttemptLimiter::registerFailure(const QString& client)
{
    if (client.isEmpty()) {
        return 0;
    }
    const qint64 now = nowSec();
    const QMutexLocker locker(&m_mutex);

    auto it = m_entries.find(client);
    if (it == m_entries.end()) {
        if (m_entries.size() >= MAX_TRACKED) {
            sweep(now);
            if (m_entries.size() >= MAX_TRACKED) {
                return 0;
            }
        }
        it = m_entries.insert(client, Entry{static_cast<quint32>(now), 0, 0});
    } else if (it->blockedUntil > 0 || now - static_cast<qint64>(it->windowStart) >= m_windowSeconds) {
        *it = Entry{static_cast<quint32>(now), 0, 0};
    }

    if (++it->fails < m_maxFails) {
        return 0;
    }
    it->blockedUntil = static_cast<quint32>(now + m_blockSeconds);
    it->fails = 0;
    return m_blockSeconds;
}

void AttemptLimiter::forget(const QString& client)
{
    const QMutexLocker locker(&m_mutex);
    m_entries.remove(client);
}

} // namespace ircabot
