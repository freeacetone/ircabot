/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QString>

#include <functional>
#include <list>

namespace ircabot {

// Shared, size-bounded LRU cache of raw log-file bytes. Only immutable archive
// files are ever stored - today's still-growing file is never cached - so a
// cached entry can never go stale. Thread-safe: log reads come from HTTP worker
// threads. The byte budget is global across every server and channel.
class LogCache
{
public:
    explicit LogCache(qint64 maxBytes);

    // Bytes for `path`. On a miss `loader` reads them from disk; the result is
    // then stored, evicting the least-recently-used entries to stay within the
    // budget. Every archive read populates the cache, so the first full-history
    // sweep pulls the whole channel into the LRU where it stays until evicted.
    QByteArray get(const QString& path, const std::function<QByteArray()>& loader);

    qint64 maxBytes() const { return m_maxBytes; }

private:
    struct Entry { QString path; QByteArray data; };

    const qint64 m_maxBytes;
    qint64 m_bytes = 0;
    QMutex m_mutex;
    std::list<Entry> m_lru;                             // front = most recently used
    QHash<QString, std::list<Entry>::iterator> m_index;
};

} // namespace ircabot
