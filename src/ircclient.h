/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include "config.h"
#include "logstore.h"
#include "state.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSslSocket>
#include <QTimer>

namespace ircabot {

// One IRC connection. Fully event-driven: no worker threads, no blocking
// waitFor*() calls. Lives in the main thread together with all other clients.
class IrcClient : public QObject
{
    Q_OBJECT
public:
    IrcClient(const ServerConfig& config, RuntimeState* state, LogStore* store, QObject* parent = nullptr);
    ~IrcClient() override;

    void start();

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onWatchdog();
    void onNamesRefresh();
    void onNickRecover();
    void onEnsureJoined();

private:
    struct IrcMessage
    {
        QString prefixNick; // sender nick from ":nick!user@host"
        QString command;    // "PRIVMSG", "001", ...
        QStringList params; // middle params
        QString trailing;   // text after " :"
    };
    static IrcMessage parseLine(const QString& line);

    void send(const QString& line, bool log = true);
    void scheduleReconnect();
    void processLine(const QString& line);
    void onRegistered();
    void handlePrivmsg(const IrcMessage& msg);
    void handleTrigger(const QString& channel, const QString& nick, const QString& request);
    void publishOnline(const QString& channel);
    void removeUserEverywhere(const QString& nick);
    QString currentNick() const;
    void consoleLog(const QString& message) const;

    static constexpr int RECONNECT_DELAY_MS = 10000;
    static constexpr int WATCHDOG_INTERVAL_MS = 30000;
    static constexpr int KEEPALIVE_SILENCE_MS = 120000;
    static constexpr int DEAD_SILENCE_MS = 360000;
    static constexpr int NAMES_REFRESH_MS = 90000;
    static constexpr int NICK_RECOVER_MS = 60000;
    static constexpr int TRIGGER_COOLDOWN_MS = 3000;
    // Servers may reject early JOIN (UnrealIRCd: "you must be connected for
    // at least 10 seconds"), so missing channels are re-joined periodically.
    static constexpr int JOIN_RETRY_MS = 11000;

    ServerConfig m_config;
    RuntimeState* m_state;
    LogStore* m_store;

    QSslSocket* m_socket = nullptr;
    QByteArray m_buffer;
    QString m_altNick;
    bool m_registered = false;
    bool m_reconnectLogged = false;
    qint64 m_lastActivity = 0;
    bool m_keepAliveSent = false;
    qint64 m_lastTriggerTime = 0;

    QHash<QString, QStringList> m_online;        // "#channel" -> nicks with prefixes
    QHash<QString, QStringList> m_namesAccum;    // NAMES replies being accumulated
    QSet<QString> m_joined;                      // really joined channels, lowercased

    QTimer m_reconnectTimer;
    QTimer m_watchdogTimer;
    QTimer m_namesTimer;
    QTimer m_nickRecoverTimer;
    QTimer m_joinTimer;
};

} // namespace ircabot
