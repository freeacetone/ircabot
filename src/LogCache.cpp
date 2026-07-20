/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "LogCache.h"

namespace ircabot {

LogCache::LogCache(qint64 maxBytes)
    : m_maxBytes(maxBytes)
{
}

QByteArray LogCache::get(const QString& path, const std::function<QByteArray()>& loader)
{
    {
        const QMutexLocker locker(&m_mutex);
        const auto it = m_index.constFind(path);
        if (it != m_index.constEnd()) {
            m_lru.splice(m_lru.begin(), m_lru, *it); // promote to most-recently-used
            return m_lru.front().data;               // implicitly shared, no deep copy
        }
    }

    // Miss: read from disk without holding the lock, so a slow read does not
    // serialize parallel reads of other files.
    QByteArray data = loader();
    if (m_maxBytes <= 0 || static_cast<qint64>(data.size()) > m_maxBytes) {
        return data; // caching disabled, or a single file larger than the whole budget
    }

    const QMutexLocker locker(&m_mutex);
    const auto it = m_index.find(path);
    if (it != m_index.end()) {
        // Another thread inserted it while we were reading: keep theirs, drop ours.
        m_lru.splice(m_lru.begin(), m_lru, *it);
        return m_lru.front().data;
    }
    m_lru.push_front({path, data});
    m_index.insert(path, m_lru.begin());
    m_bytes += data.size();

    while (m_bytes > m_maxBytes && !m_lru.empty()) {
        const Entry& victim = m_lru.back();
        m_bytes -= victim.data.size();
        m_index.remove(victim.path);
        m_lru.pop_back();
    }
    return data;
}

} // namespace ircabot
