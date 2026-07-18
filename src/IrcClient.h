/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include "Config.h"
#include "LogStore.h"
#include "State.h"

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QSslSocket>
#include <QTimer>

namespace ircabot {

class VoiceGate;

// One IRC connection. Fully event-driven: no worker threads, no blocking
// waitFor*() calls. Lives in the main thread together with all other clients.
class IrcClient : public QObject
{
    Q_OBJECT
public:
    IrcClient(const ServerConfig& config, RuntimeState* state, LogStore* store,
              VoiceGate* voiceGate, QObject* parent = nullptr);
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
    void onVoiceGateTick();
    void onSendQueue();

private:
    struct IrcMessage
    {
        QString prefixNick; // sender nick from ":nick!user@host"
        QString prefixHost; // host from ":nick!user@host" (for the voice gate)
        QString command;
        QStringList params;
        QString trailing;
    };
    static IrcMessage parseLine(const QString& line);

    struct OutMsg { QString line; bool log; };

    void send(const QString& line, bool log = true);      // rate-limited, queued
    void sendNow(const QString& line, bool log = false);  // bypass the queue (PONG/PING)
    void sendRaw(const QString& line, bool log);          // write straight to the socket
    void scheduleReconnect();
    void processLine(const QString& line);
    void onRegistered();
    void handlePrivmsg(const IrcMessage& msg);
    void handlePrivateQuery(const QString& nick, const QString& host);
    void handleTrigger(const QString& channel, const QString& nick, const QString& request);
    void publishOnline(const QString& channel);
    void removeUserEverywhere(const QString& nick);
    QString currentNick() const;
    void consoleLog(const QString& message) const;

    // --- Voice gate ---------------------------------------------------------
    bool voiceGateActive() const;
    bool botCanVoiceIn(const QString& channel) const;      // bot holds op/halfop here
    bool isUserPresent(const QString& nick) const;         // in any channel we see
    void sendAction(const QString& target, const QString& text);
    void grantVoice(const QString& channel, const QString& nick);
    void sendCaptchaPm(const QString& nick, const QString& host);
    void userWentOffline(const QString& nick);             // start TTL, forget host

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
    static constexpr int VOICEGATE_TICK_MS = 3000;    // reconcile cadence
    static constexpr int VOICE_COOLDOWN_MS = 8000;    // don't re-issue +v this fast
    static constexpr int WHO_THROTTLE_MS = 30000;     // refresh missing hosts at most this often
    static constexpr int SWEEP_INTERVAL_MS = 600000;  // TTL cleanup every 10 min
    static constexpr int SEND_INTERVAL_MS = 1200;     // outbound pacing: 1 msg / 1.2s...
    static constexpr int SEND_BURST = 6;              // ...after an initial burst
    static constexpr int PM_REPLY_COOLDOWN_MS = 8000; // per-sender PM reply throttle

    ServerConfig m_config;
    RuntimeState* m_state;
    LogStore* m_store;
    VoiceGate* m_voiceGate;

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

    // Voice-gate bookkeeping (lowercased keys throughout).
    QHash<QString, QString> m_userHost;          // nick -> host, from JOIN/WHO
    QSet<QString> m_moderated;                    // channels currently +m
    QSet<QString> m_gated;                        // channels currently in voice-gate mode
    QSet<QString> m_setModerated;                 // channels we already sent +m to
    QHash<QString, qint64> m_pendingSince;        // nick -> first seen unvoiced (ms)
    QHash<QString, qint64> m_lastWho;             // channel -> last WHO (ms)
    QHash<QString, qint64> m_lastPmReply;         // nick -> last PM reply (ms), anti-flood
    qint64 m_lastSweep = 0;

    QTimer m_reconnectTimer;
    QTimer m_watchdogTimer;
    QTimer m_namesTimer;
    QTimer m_nickRecoverTimer;
    QTimer m_joinTimer;
    QTimer m_voiceGateTimer;

    QQueue<OutMsg> m_sendQueue;   // rate-limited outbound; PONG/PING bypass it
    QTimer m_sendTimer;
    double m_sendTokens = 0;
    qint64 m_lastSendRefill = 0;
};

} // namespace ircabot
