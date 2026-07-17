/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "state.h"

#include "util.h"

namespace ircabot {

void RuntimeState::registerServer(const QString& displayName, const QString& slug, const QStringList& channels)
{
    const QWriteLocker locker(&m_lock);
    ServerEntry& entry = m_servers[slug];
    entry.data.displayName = displayName;
    entry.data.slug = slug;
    for (QString ch : channels) {
        ch.remove('#');
        entry.data.channels.push_back(ch);
        entry.data.byChannel.insert(ch, {});
    }
    if (!m_order.contains(slug)) {
        m_order.push_back(slug);
    }
}

void RuntimeState::setConnected(const QString& slug, bool connected)
{
    const QWriteLocker locker(&m_lock);
    const auto it = m_servers.find(slug);
    if (it != m_servers.end()) {
        it->data.connected = connected;
    }
}

void RuntimeState::setBotNick(const QString& slug, const QString& nick)
{
    const QWriteLocker locker(&m_lock);
    const auto it = m_servers.find(slug);
    if (it != m_servers.end()) {
        it->data.botNick = nick;
    }
}

void RuntimeState::setTopic(const QString& slug, const QString& channel, const QString& topic)
{
    const QWriteLocker locker(&m_lock);
    const auto it = m_servers.find(slug);
    if (it != m_servers.end()) {
        it->data.byChannel[channel].topic = topic;
    }
}

void RuntimeState::setOnline(const QString& slug, const QString& channel, const QStringList& sortedUsers)
{
    const QWriteLocker locker(&m_lock);
    const auto it = m_servers.find(slug);
    if (it != m_servers.end()) {
        it->data.byChannel[channel].online = sortedUsers;
    }
}

void RuntimeState::pushLiveMessage(const QString& slug, const QString& channel,
                                   const QString& nick, const QString& text)
{
    const QWriteLocker locker(&m_lock);
    const auto it = m_servers.find(slug);
    if (it == m_servers.end()) {
        return;
    }
    QList<LiveMessage>& ring = it->liveCache[channel];
    ring.push_back({m_nextMessageId++, QDateTime::currentSecsSinceEpoch(), nick, text});
    while (ring.size() > LIVE_CACHE_SIZE) {
        ring.pop_front();
    }
}

QList<ServerSnapshot> RuntimeState::snapshotAll() const
{
    const QReadLocker locker(&m_lock);
    QList<ServerSnapshot> result;
    result.reserve(m_order.size());
    for (const QString& slug : m_order) {
        result.push_back(m_servers[slug].data);
    }
    return result;
}

ServerSnapshot RuntimeState::snapshot(const QString& slug, bool* found) const
{
    const QReadLocker locker(&m_lock);
    const auto it = m_servers.constFind(slug);
    if (found) {
        *found = (it != m_servers.constEnd());
    }
    return it != m_servers.constEnd() ? it->data : ServerSnapshot{};
}

ChannelSnapshot RuntimeState::channelSnapshot(const QString& slug, const QString& channel) const
{
    const QReadLocker locker(&m_lock);
    const auto it = m_servers.constFind(slug);
    if (it == m_servers.constEnd()) {
        return {};
    }
    return it->data.byChannel.value(channel);
}

QList<LiveMessage> RuntimeState::liveMessagesAfter(const QString& slug, const QString& channel,
                                                   quint64 afterId) const
{
    const QReadLocker locker(&m_lock);
    QList<LiveMessage> result;
    const auto it = m_servers.constFind(slug);
    if (it == m_servers.constEnd()) {
        return result;
    }
    const QList<LiveMessage> ring = it->liveCache.value(channel);
    for (const LiveMessage& msg : ring) {
        if (msg.id > afterId) {
            result.push_back(msg);
        }
    }
    return result;
}

quint64 RuntimeState::requestsServedToday() const
{
    const QReadLocker locker(&m_counterLock);
    return m_requestCounterDate == util::currentLogDate() ? m_requestCounter : 0;
}

quint64 RuntimeState::ajaxRequestsServedToday() const
{
    const QReadLocker locker(&m_counterLock);
    return m_requestCounterDate == util::currentLogDate() ? m_ajaxRequestCounter : 0;
}

void RuntimeState::countRequest() const
{
    const QWriteLocker locker(&m_counterLock);
    const QDate today = util::currentLogDate();
    if (m_requestCounterDate != today) {
        m_requestCounterDate = today;
        m_requestCounter = 0;
        m_ajaxRequestCounter = 0;
    }
    ++m_requestCounter;
}

void RuntimeState::countAjaxRequest() const
{
    const QWriteLocker locker(&m_counterLock);
    const QDate today = util::currentLogDate();
    if (m_requestCounterDate != today) {
        m_requestCounterDate = today;
        m_requestCounter = 0;
        m_ajaxRequestCounter = 0;
    }
    ++m_ajaxRequestCounter;
}

} // namespace ircabot
