/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QDate>
#include <QHash>
#include <QList>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>

namespace ircabot {

struct LiveMessage
{
    quint64 id = 0;
    QString nick;
    QString text;
};

struct ChannelSnapshot
{
    QStringList online;   // sorted, with mode prefixes
    QString topic;
};

struct ServerSnapshot
{
    QString displayName;
    QString slug;
    bool connected = false;
    QString botNick;
    QStringList channels; // without '#', stable config order
    QHash<QString, ChannelSnapshot> byChannel;
};

// Shared between IRC clients (writers, main thread) and HTTP handlers
// (readers, QThreadPool worker threads). All access goes through a
// QReadWriteLock, snapshots are returned by value.
class RuntimeState
{
public:
    static constexpr int LIVE_CACHE_SIZE = 50;

    void registerServer(const QString& displayName, const QString& slug, const QStringList& channels);

    // IRC-side updates (channel without '#')
    void setConnected(const QString& slug, bool connected);
    void setBotNick(const QString& slug, const QString& nick);
    void setTopic(const QString& slug, const QString& channel, const QString& topic);
    void setOnline(const QString& slug, const QString& channel, const QStringList& sortedUsers);
    void pushLiveMessage(const QString& slug, const QString& channel, const QString& nick, const QString& text);

    // HTTP-side reads
    QList<ServerSnapshot> snapshotAll() const;
    ServerSnapshot snapshot(const QString& slug, bool* found = nullptr) const;
    ChannelSnapshot channelSnapshot(const QString& slug, const QString& channel) const;
    QList<LiveMessage> liveMessagesAfter(const QString& slug, const QString& channel, quint64 afterId) const;

    // Daily request counters (reset at midnight). Html pages, plain .txt day
    // logs and ajax polls are counted separately: one live reader produces
    // ~20 polls/min, and a .txt fetch is a raw-log grab, not a page view;
    // mixing them in would say nothing about the real attendance.
    quint64 requestsServedToday() const;
    quint64 txtRequestsServedToday() const;
    quint64 ajaxRequestsServedToday() const;
    void countRequest() const;
    void countTxtRequest() const;
    void countAjaxRequest() const;

private:
    struct ServerEntry
    {
        ServerSnapshot data;
        QHash<QString, QList<LiveMessage>> liveCache; // channel -> ring of last messages
    };

    mutable QReadWriteLock m_lock;
    QList<QString> m_order;
    QHash<QString, ServerEntry> m_servers;
    quint64 m_nextMessageId = 1;

    mutable QReadWriteLock m_counterLock;
    mutable quint64 m_requestCounter = 0;
    mutable quint64 m_txtRequestCounter = 0;
    mutable quint64 m_ajaxRequestCounter = 0;
    mutable QDate m_requestCounterDate;
};

} // namespace ircabot
