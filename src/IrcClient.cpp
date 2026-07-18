/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "IrcClient.h"
#include "Util.h"
#include "VoiceGate.h"

#include "Version.h"

#include <QDateTime>
#include <QDebug>
#include <QRandomGenerator>

#include <algorithm>

namespace ircabot {

namespace {

constexpr const char* BLINDED_MESSAGE_MARKER = "Blinded message";
constexpr const char* TRIGGER_CHANNEL_FOR_URL = "%CHANNEL_FOR_URL%";
constexpr const char* TRIGGER_VERSION = "%VERSION%";

int nickRank(const QString& nick)
{
    if (nick.isEmpty()) {
        return 5;
    }
    switch (nick.front().toLatin1()) {
    case '~': return 0; // owner
    case '&': return 1; // admin
    case '@': return 2; // operator
    case '%': return 3; // half-op
    case '+': return 4; // voiced
    default:  return 5;
    }
}

QStringList sortedByRank(QStringList nicks)
{
    nicks.removeAll(QString());
    std::sort(nicks.begin(), nicks.end(), [](const QString& a, const QString& b) {
        const int ra = nickRank(a);
        const int rb = nickRank(b);
        if (ra != rb) {
            return ra < rb;
        }
        return util::stripNickPrefix(a).compare(util::stripNickPrefix(b), Qt::CaseInsensitive) < 0;
    });
    return nicks;
}

} // namespace

IrcClient::IrcClient(const ServerConfig& config, RuntimeState* state, LogStore* store,
                     VoiceGate* voiceGate, QObject* parent)
    : QObject(parent), m_config(config), m_state(state), m_store(store), m_voiceGate(voiceGate)
{
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &IrcClient::start);
    connect(&m_watchdogTimer, &QTimer::timeout, this, &IrcClient::onWatchdog);
    connect(&m_namesTimer, &QTimer::timeout, this, &IrcClient::onNamesRefresh);
    connect(&m_nickRecoverTimer, &QTimer::timeout, this, &IrcClient::onNickRecover);
    connect(&m_joinTimer, &QTimer::timeout, this, &IrcClient::onEnsureJoined);
    connect(&m_voiceGateTimer, &QTimer::timeout, this, &IrcClient::onVoiceGateTick);
    m_sendTimer.setSingleShot(true);
    connect(&m_sendTimer, &QTimer::timeout, this, &IrcClient::onSendQueue);

    m_state->registerServer(m_config.displayName, m_config.slug, m_config.channels);
    m_state->setBotNick(m_config.slug, m_config.nick);
}

IrcClient::~IrcClient()
{
    // Graceful shutdown so the server does not keep a ghost session.
    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        // Detach our slots first: disconnectFromHost() emits disconnected()
        // synchronously, and onDisconnected() would touch collaborators
        // (RuntimeState, ...) that may already be gone during teardown.
        m_socket->disconnect(this);
        m_socket->write(QByteArray("QUIT :IRCaBot ") + VERSION + ": shutting down\r\n");
        m_socket->flush();
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->waitForDisconnected(1000);
        }
    }
}

void IrcClient::start()
{
    if (m_socket) {
        // The old socket must not drive the new session: a late disconnected()
        // or errorOccurred() from it would schedule a bogus reconnect
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
    }
    m_socket = new QSslSocket(this);
    m_buffer.clear();
    m_registered = false;
    m_keepAliveSent = false;
    m_online.clear();
    m_namesAccum.clear();
    m_joined.clear();
    m_userHost.clear();
    m_moderated.clear();
    m_gated.clear();
    m_setModerated.clear();
    m_pendingSince.clear();
    m_lastWho.clear();
    m_lastPmReply.clear();
    m_sendQueue.clear();
    m_sendTimer.stop();
    m_sendTokens = SEND_BURST;
    m_lastSendRefill = 0;

    connect(m_socket, &QSslSocket::connected, this, &IrcClient::onConnected);
    connect(m_socket, &QSslSocket::disconnected, this, &IrcClient::onDisconnected);
    connect(m_socket, &QSslSocket::readyRead, this, &IrcClient::onReadyRead);
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_socket->state() != QAbstractSocket::ConnectedState) {
            onDisconnected();
        }
    });

    if (m_config.ssl) {
        connect(m_socket, &QSslSocket::encrypted, this, &IrcClient::onConnected);
        m_socket->connectToHostEncrypted(m_config.address, m_config.port);
    } else {
        m_socket->connectToHost(m_config.address, m_config.port);
    }
}

void IrcClient::onConnected()
{
    if (m_config.ssl && !m_socket->isEncrypted()) {
        return; // wait for the encrypted() signal
    }
    m_reconnectLogged = false;
    m_lastActivity = QDateTime::currentMSecsSinceEpoch();
    m_watchdogTimer.start(WATCHDOG_INTERVAL_MS);

    consoleLog("Socket connected, registering...");
    send("USER " + m_config.user + " 0 * :" + m_config.realName);
    send("NICK " + m_config.nick);
}

void IrcClient::onDisconnected()
{
    if (m_reconnectTimer.isActive()) {
        return;
    }
    m_watchdogTimer.stop();
    m_namesTimer.stop();
    m_nickRecoverTimer.stop();
    m_joinTimer.stop();
    m_voiceGateTimer.stop();
    m_sendTimer.stop();
    m_registered = false;
    m_state->setConnected(m_config.slug, false);

    if (!m_reconnectLogged) {
        consoleLog("Disconnected. Reconnecting every " + QString::number(RECONNECT_DELAY_MS / 1000) + " sec...");
        m_reconnectLogged = true;
    }
    scheduleReconnect();
}

void IrcClient::scheduleReconnect()
{
    m_reconnectTimer.start(RECONNECT_DELAY_MS);
}

void IrcClient::onReadyRead()
{
    m_lastActivity = QDateTime::currentMSecsSinceEpoch();
    m_keepAliveSent = false;
    m_buffer += m_socket->readAll();

    qsizetype pos = 0;
    while ((pos = m_buffer.indexOf('\n')) != -1) {
        QByteArray rawLine = m_buffer.left(pos);
        m_buffer.remove(0, pos + 1);
        while (rawLine.endsWith('\r')) {
            rawLine.chop(1);
        }
        if (rawLine.isEmpty()) {
            continue;
        }
        processLine(QString::fromUtf8(rawLine));
    }
}

void IrcClient::onWatchdog()
{
    const qint64 silence = QDateTime::currentMSecsSinceEpoch() - m_lastActivity;
    if (silence > DEAD_SILENCE_MS) {
        consoleLog("No data from server for " + QString::number(silence / 1000) + " sec, reconnecting");
        m_socket->abort();
        onDisconnected();
    } else if (silence > KEEPALIVE_SILENCE_MS && !m_keepAliveSent && m_registered) {
        sendNow("PING :keepalive", false);
        m_keepAliveSent = true;
    }
}

void IrcClient::onNamesRefresh()
{
    for (const QString& ch : m_config.channels) {
        send("NAMES " + ch, false);
    }
}

void IrcClient::onNickRecover()
{
    send("NICK " + m_config.nick);
    if (!m_config.password.isEmpty()) {
        send("PRIVMSG NickServ :IDENTIFY " + m_config.password, false);
    }
}

QString IrcClient::currentNick() const
{
    return m_altNick.isEmpty() ? m_config.nick : m_altNick;
}

void IrcClient::sendRaw(const QString& line, bool log)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (log) {
        consoleLog("<- " + line);
    }
    m_socket->write(line.toUtf8() + "\r\n");
}

// PONG and the keepalive PING must not wait behind the queue, or the server
// could time us out while a burst drains.
void IrcClient::sendNow(const QString& line, bool log)
{
    sendRaw(line, log);
}

void IrcClient::send(const QString& line, bool log)
{
    m_sendQueue.enqueue({line, log});
    onSendQueue();
}

// Token bucket: an initial burst of SEND_BURST, then one message per
// SEND_INTERVAL_MS, so a mass join/who/mode/+v/PM never trips excess flood.
void IrcClient::onSendQueue()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastSendRefill == 0) {
        m_lastSendRefill = now;
    }
    m_sendTokens = qMin<double>(SEND_BURST,
                                m_sendTokens + double(now - m_lastSendRefill) / SEND_INTERVAL_MS);
    m_lastSendRefill = now;

    while (!m_sendQueue.isEmpty() && m_sendTokens >= 1.0) {
        const OutMsg m = m_sendQueue.dequeue();
        sendRaw(m.line, m.log);
        m_sendTokens -= 1.0;
    }
    if (!m_sendQueue.isEmpty()) {
        m_sendTimer.start(SEND_INTERVAL_MS);
    }
}

IrcClient::IrcMessage IrcClient::parseLine(const QString& line)
{
    IrcMessage msg;
    QString rest = line;

    if (rest.startsWith(':')) {
        const qsizetype space = rest.indexOf(' ');
        if (space == -1) {
            return msg;
        }
        const QString prefix = rest.mid(1, space - 1);
        const qsizetype bang = prefix.indexOf('!');
        msg.prefixNick = bang == -1 ? prefix : prefix.left(bang);
        const qsizetype at = prefix.indexOf('@');
        if (at != -1) {
            msg.prefixHost = prefix.mid(at + 1);
        }
        rest = rest.mid(space + 1);
    }

    const qsizetype trailingPos = rest.indexOf(QStringLiteral(" :"));
    if (trailingPos != -1) {
        msg.trailing = rest.mid(trailingPos + 2);
        rest = rest.left(trailingPos);
    }

    QStringList parts = rest.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return msg;
    }
    msg.command = parts.takeFirst().toUpper();
    msg.params = parts;
    return msg;
}

void IrcClient::onRegistered()
{
    if (m_registered) {
        return;
    }
    m_registered = true;
    consoleLog("Connected to server!");
    m_state->setConnected(m_config.slug, true);

    if (!m_config.password.isEmpty()) {
        send("PRIVMSG NickServ :IDENTIFY " + m_config.password, false);
    }
    send("MODE " + currentNick() + " +B", false); // mark as bot, ignored by servers without +B

    onEnsureJoined();
    m_joinTimer.start(JOIN_RETRY_MS);
    m_namesTimer.start(NAMES_REFRESH_MS);
    if (voiceGateActive()) {
        m_voiceGateTimer.start(VOICEGATE_TICK_MS);
    }
}

void IrcClient::onEnsureJoined()
{
    for (const QString& ch : m_config.channels) {
        if (!m_joined.contains(ch.toLower())) {
            send("JOIN " + ch);
        }
    }
}

void IrcClient::processLine(const QString& line)
{
    const IrcMessage msg = parseLine(line);

    if (msg.command == QStringLiteral("PING")) {
        sendNow("PONG :" + (msg.trailing.isEmpty() ? (msg.params.isEmpty() ? QString() : msg.params.first())
                                                : msg.trailing), false);
        return;
    }
    if (msg.command == QStringLiteral("PONG")) {
        return;
    }
    if (msg.command == QStringLiteral("ERROR")) {
        consoleLog("Server error: " + msg.trailing);
        return;
    }

    if (msg.command == QStringLiteral("001")) { // RPL_WELCOME
        onRegistered();
        return;
    }
    if (msg.command == QStringLiteral("433")) { // nickname already in use
        if (m_altNick.isEmpty()) {
            m_altNick = m_config.nick + '_' + QString::number(QRandomGenerator::global()->bounded(100, 999));
            consoleLog("Nickname is busy, using " + m_altNick);
            m_state->setBotNick(m_config.slug, m_altNick);
            m_nickRecoverTimer.start(NICK_RECOVER_MS);
            send("NICK " + m_altNick);
        }
        return;
    }

    if (msg.command == QStringLiteral("332")) { // RPL_TOPIC: <me> <channel> :<topic>
        if (msg.params.size() >= 2) {
            QString channel = msg.params[1];
            m_state->setTopic(m_config.slug, channel.remove('#'), msg.trailing);
        }
        return;
    }
    if (msg.command == QStringLiteral("TOPIC")) { // :<nick> TOPIC <channel> :<topic>
        if (!msg.params.isEmpty()) {
            QString channel = msg.params[0];
            consoleLog("Topic at " + channel + ": " + msg.trailing);
            m_state->setTopic(m_config.slug, channel.remove('#'), msg.trailing);
        }
        return;
    }

    if (msg.command == QStringLiteral("353")) { // RPL_NAMREPLY: <me> = <channel> :nick1 nick2
        if (!msg.params.isEmpty()) {
            const QString channel = msg.params.last();
            m_namesAccum[channel] += msg.trailing.split(' ', Qt::SkipEmptyParts);
        }
        return;
    }
    if (msg.command == QStringLiteral("366")) { // RPL_ENDOFNAMES
        if (msg.params.size() >= 2) {
            const QString channel = msg.params[1];
            m_online[channel] = m_namesAccum.take(channel);
            publishOnline(channel);
        }
        return;
    }

    if (msg.command == QStringLiteral("324")) { // RPL_CHANNELMODEIS: <me> <channel> <modes> ...
        if (msg.params.size() >= 3) {
            const QString channel = msg.params[1].toLower();
            if (msg.params[2].contains('m')) {
                m_moderated.insert(channel);
            } else {
                m_moderated.remove(channel);
            }
        }
        return;
    }
    if (msg.command == QStringLiteral("352")) { // RPL_WHOREPLY: <me> <chan> <user> <host> <srv> <nick> <flags>
        if (msg.params.size() >= 6 && !msg.params[3].isEmpty()) {
            m_userHost.insert(msg.params[5].toLower(), msg.params[3]);
        }
        return;
    }

    if (msg.command == QStringLiteral("PRIVMSG")) {
        handlePrivmsg(msg);
        return;
    }

    if (msg.command == QStringLiteral("JOIN")) {
        const QString channel = msg.params.isEmpty() ? msg.trailing : msg.params.first();
        if (msg.prefixNick == currentNick()) {
            consoleLog("I joined to " + channel);
            m_joined.insert(channel.toLower());
            if (voiceGateActive()) {
                send("MODE " + channel, false); // -> 324, learn +m
                send("WHO " + channel, false);  // -> 352, learn hosts
            }
            return;
        }
        if (!m_online[channel].contains(msg.prefixNick)) {
            m_online[channel].push_back(msg.prefixNick);
        }
        if (!msg.prefixHost.isEmpty()) {
            m_userHost.insert(msg.prefixNick.toLower(), msg.prefixHost);
        }
        publishOnline(channel);
        return;
    }
    if (msg.command == QStringLiteral("PART")) {
        const QString channel = msg.params.isEmpty() ? msg.trailing : msg.params.first();
        if (msg.prefixNick == currentNick()) {
            consoleLog("I left " + channel);
            m_joined.remove(channel.toLower());
            m_moderated.remove(channel.toLower());
            m_gated.remove(channel.toLower());
            m_setModerated.remove(channel.toLower());
            return;
        }
        auto& list = m_online[channel];
        list.removeAll(msg.prefixNick);
        for (const char* p : {"~", "&", "@", "%", "+"}) {
            list.removeAll(p + msg.prefixNick);
        }
        publishOnline(channel);
        if (!isUserPresent(msg.prefixNick)) {
            userWentOffline(msg.prefixNick);
        }
        return;
    }
    if (msg.command == QStringLiteral("KICK")) { // KICK <channel> <victim> :reason
        if (msg.params.size() >= 2) {
            const QString channel = msg.params[0];
            const QString victim = msg.params[1];
            if (victim == currentNick()) {
                consoleLog("I was kicked from " + channel + " (" + msg.trailing + "), rejoining...");
                m_joined.remove(channel.toLower());
                m_moderated.remove(channel.toLower());
                m_gated.remove(channel.toLower());
                m_setModerated.remove(channel.toLower());
                return;
            }
            auto& list = m_online[channel];
            list.removeAll(victim);
            for (const char* p : {"~", "&", "@", "%", "+"}) {
                list.removeAll(p + victim);
            }
            publishOnline(channel);
            if (!isUserPresent(victim)) {
                userWentOffline(victim);
            }
        }
        return;
    }
    if (msg.command == QStringLiteral("QUIT")) {
        userWentOffline(msg.prefixNick);
        removeUserEverywhere(msg.prefixNick);
        return;
    }
    if (msg.command == QStringLiteral("NICK")) {
        const QString newNick = msg.trailing.isEmpty()
                                    ? (msg.params.isEmpty() ? QString() : msg.params.first())
                                    : msg.trailing;
        if (newNick.isEmpty()) {
            return;
        }

        if (msg.prefixNick == currentNick()) {
            if (newNick == m_config.nick) {
                consoleLog("Default nickname (" + m_config.nick + ") is recovered!");
                m_altNick.clear();
                m_nickRecoverTimer.stop();
            } else {
                m_altNick = newNick;
                m_nickRecoverTimer.start(NICK_RECOVER_MS);
            }
            m_state->setBotNick(m_config.slug, currentNick());
        } else {
            for (auto it = m_online.begin(); it != m_online.end(); ++it) {
                bool changed = false;
                for (QString& nick : it.value()) {
                    if (util::stripNickPrefix(nick) == msg.prefixNick) {
                        nick = newNick;
                        changed = true;
                    }
                }
                if (changed) {
                    publishOnline(it.key());
                }
            }
            // The host follows the connection; the grant is nick+host bound, so
            // under the new nick the user is a fresh, ungated identity.
            const QString oldKey = msg.prefixNick.toLower();
            const QString newKey = newNick.toLower();
            if (oldKey != newKey) {
                if (m_userHost.contains(oldKey)) {
                    m_userHost.insert(newKey, m_userHost.take(oldKey));
                }
                m_pendingSince.remove(oldKey);
            }
        }
        return;
    }
    if (msg.command == QStringLiteral("MODE")) {
        // Channel mode change can grant/remove prefixes or toggle +m: refresh both
        if (!msg.params.isEmpty() && msg.params.first().startsWith('#')) {
            send("NAMES " + msg.params.first(), false);
            if (voiceGateActive()) {
                send("MODE " + msg.params.first(), false);
            }
        }
        return;
    }

    // Error numerics (4xx/5xx) must be visible: UnrealIRCd, for example,
    // rejects JOIN during the first seconds after connect (numeric 421)
    if (msg.command.size() == 3 && (msg.command.startsWith('4') || msg.command.startsWith('5'))) {
        consoleLog("Server numeric " + msg.command + ": "
                   + msg.params.mid(1).join(' ')
                   + (msg.trailing.isEmpty() ? QString() : " :" + msg.trailing));
        return;
    }
}

void IrcClient::handlePrivmsg(const IrcMessage& msg)
{
    if (msg.params.isEmpty()) {
        return;
    }
    const QString target = msg.params.first();
    if (!target.startsWith('#')) {
        handlePrivateQuery(msg.prefixNick, msg.prefixHost); // PM to the bot: captcha reply
        return;
    }

    QString channel = target;
    channel.remove('#');
    QString text = msg.trailing;

    // Addressed to the bot: "botnick: request" / "botnick, request"
    const QString me = currentNick();
    if (text.startsWith(me) && text.size() > me.size()
        && QStringLiteral(":,!").contains(text.at(me.size()))) {
        handleTrigger(target, msg.prefixNick, text.mid(me.size() + 1).trimmed());
        return;
    }

    if (text.startsWith('.')) {
        text = QString::fromUtf8(BLINDED_MESSAGE_MARKER);
    } else if (text.startsWith(QStringLiteral("\x01" "ACTION")) && text.endsWith('\x01')) {
        text = "*** " + text.mid(8, text.size() - 9).trimmed() + " ***";
    }

    consoleLog(target + " (" + msg.prefixNick + "): " + text);
    m_store->append(channel, msg.prefixNick, text);
    m_state->pushLiveMessage(m_config.slug, channel, msg.prefixNick, text);
}

void IrcClient::handleTrigger(const QString& channel, const QString& nick, const QString& request)
{
    if (m_config.triggers.isEmpty() || request.isEmpty()) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastTriggerTime < TRIGGER_COOLDOWN_MS) {
        consoleLog("Trigger request ignored (anti-flood)");
        return;
    }
    m_lastTriggerTime = now;

    for (auto it = m_config.triggers.constBegin(); it != m_config.triggers.constEnd(); ++it) {
        if (request.contains(it.key(), Qt::CaseInsensitive)) {
            QString answer = it.value();
            answer.replace(QString::fromUtf8(TRIGGER_CHANNEL_FOR_URL),
                           m_config.slug + '/' + QString(channel).remove('#'));
            answer.replace(QString::fromUtf8(TRIGGER_VERSION), QString::fromUtf8(VERSION));
            send("PRIVMSG " + channel + " :" + nick + ", " + answer);
            return;
        }
    }
    QStringList known = m_config.triggers.keys();
    for (QString& k : known) {
        k = '\'' + k + '\'';
    }
    send("PRIVMSG " + channel + " :" + nick + ", try it: " + known.join(QStringLiteral(", ")));
}

void IrcClient::publishOnline(const QString& channel)
{
    QString plain = channel;
    plain.remove('#');
    m_state->setOnline(m_config.slug, plain, sortedByRank(m_online.value(channel)));
}

void IrcClient::removeUserEverywhere(const QString& nick)
{
    for (auto it = m_online.begin(); it != m_online.end(); ++it) {
        const auto sizeBefore = it.value().size();
        it.value().removeAll(nick);
        for (const char* p : {"~", "&", "@", "%", "+"}) {
            it.value().removeAll(p + nick);
        }
        if (it.value().size() != sizeBefore) {
            publishOnline(it.key());
        }
    }
}

void IrcClient::consoleLog(const QString& message) const
{
    qInfo().noquote() << "[" + m_config.displayName + "]" << message;
}

// --- Voice gate -------------------------------------------------------------

bool IrcClient::voiceGateActive() const
{
    return m_voiceGate && m_voiceGate->enabled();
}

bool IrcClient::botCanVoiceIn(const QString& channel) const
{
    const QString me = currentNick();
    for (const QString& entry : m_online.value(channel)) {
        if (util::stripNickPrefix(entry).compare(me, Qt::CaseInsensitive) == 0) {
            // Owner (~), admin (&), operator (@) and half-op (%) can set +v.
            return !entry.isEmpty() && QStringLiteral("~&@%").contains(entry.front());
        }
    }
    return false;
}

bool IrcClient::isUserPresent(const QString& nick) const
{
    for (auto it = m_online.constBegin(); it != m_online.constEnd(); ++it) {
        for (const QString& entry : it.value()) {
            if (util::stripNickPrefix(entry).compare(nick, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
    }
    return false;
}

void IrcClient::sendAction(const QString& target, const QString& text)
{
    send("PRIVMSG " + target + " :\x01" "ACTION " + text + "\x01");
}

void IrcClient::grantVoice(const QString& channel, const QString& nick)
{
    send("MODE " + channel + " +v " + nick);
    // Reflect it locally at once so we do not re-issue +v before NAMES catches
    // up; a failed +v (lost op) is corrected by the next NAMES refresh.
    QStringList& list = m_online[channel];
    for (QString& entry : list) {
        if (nickRank(entry) == 5 && util::stripNickPrefix(entry).compare(nick, Qt::CaseInsensitive) == 0) {
            entry = '+' + entry;
        }
    }
    publishOnline(channel);
    consoleLog("Voice gate: voiced " + nick + " on " + channel);
}

void IrcClient::sendCaptchaPm(const QString& nick, const QString& host)
{
    const QString url = m_voiceGate->captchaUrl(m_config.slug, nick, host);
    const QString text = m_voiceGate->config().privateMessage;
    // The link always goes at the end; a space separates it only when there is
    // preceding text, so an empty message sends just the bare link.
    const QString body = text.isEmpty() ? url : text + ' ' + url;
    send("PRIVMSG " + nick + " :" + body);
    consoleLog("Voice gate: sent captcha link to " + nick);
}

// A PM to the bot: answer only when the gate is on. Already-voiced (in our DB)
// gets a confirmation, everyone else gets their captcha link. Throttled per
// sender so a flood of PMs cannot turn into a flood of replies.
void IrcClient::handlePrivateQuery(const QString& nick, const QString& host)
{
    if (!voiceGateActive() || nick.isEmpty() || host.isEmpty()) {
        return;
    }
    const QString key = nick.toLower();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastPmReply.value(key, 0) < PM_REPLY_COOLDOWN_MS) {
        return;
    }
    m_lastPmReply.insert(key, now);

    if (m_voiceGate->isGranted(m_config.slug, nick, host)) {
        send("PRIVMSG " + nick
             + " :You have already passed the captcha and have a voice on the channels I moderate.");
    } else {
        sendCaptchaPm(nick, host);
    }
}

void IrcClient::userWentOffline(const QString& nick)
{
    const QString key = nick.toLower();
    if (voiceGateActive()) {
        const QString host = m_userHost.value(key);
        if (!host.isEmpty()) {
            m_voiceGate->markOffline(m_config.slug, nick, host);
        }
    }
    m_userHost.remove(key);
    m_pendingSince.remove(key);
}

void IrcClient::onVoiceGateTick()
{
    if (!voiceGateActive() || !m_registered) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (m_lastSweep == 0 || now - m_lastSweep > SWEEP_INTERVAL_MS) {
        m_voiceGate->sweep(m_config.slug);
        m_lastSweep = now;
    }

    // Publish present users (with a known host) so the web can refuse captchas
    // for a nick+host that is not actually online.
    QSet<QString> present;
    for (auto it = m_online.constBegin(); it != m_online.constEnd(); ++it) {
        for (const QString& entry : it.value()) {
            const QString nick = util::stripNickPrefix(entry);
            const QString host = m_userHost.value(nick.toLower());
            if (!host.isEmpty()) {
                present.insert(VoiceGate::presenceKey(nick, VoiceGate::hostHash(host)));
            }
        }
    }
    m_voiceGate->setPresent(m_config.slug, present);

    const qint64 delayMs = static_cast<qint64>(m_voiceGate->config().connectDelaySeconds) * 1000;
    const QString me = currentNick();

    for (const QString& ch : m_config.channels) {
        if (!m_joined.contains(ch.toLower())) {
            continue;
        }
        const QString lc = ch.toLower();
        const bool botOp = botCanVoiceIn(ch);
        // With ops in hand, enforce +m once so the gate can engage (opt-out via
        // voicegate.set_moderated). A later manual -m is respected, not re-forced.
        if (botOp && m_voiceGate->config().setModerated
            && !m_moderated.contains(lc) && !m_setModerated.contains(lc)) {
            m_setModerated.insert(lc);
            send("MODE " + ch + " +m");
        }
        const bool gated = botOp && m_moderated.contains(lc);
        const bool wasGated = m_gated.contains(lc);
        if (gated && !wasGated) {
            m_gated.insert(ch.toLower());
            sendAction(ch, QStringLiteral("Voice gate mode activated"));
            consoleLog("Voice gate activated on " + ch);
        } else if (!gated && wasGated) {
            m_gated.remove(ch.toLower());
            sendAction(ch, QStringLiteral("Voice gate mode deactivated"));
            consoleLog("Voice gate deactivated on " + ch);
        }
        if (!gated) {
            continue;
        }

        bool whoNeeded = false;
        for (const QString& entry : m_online.value(ch)) {
            const QString nick = util::stripNickPrefix(entry);
            if (nick.compare(me, Qt::CaseInsensitive) == 0) {
                continue;
            }
            const QString key = nick.toLower();
            const QString host = m_userHost.value(key);
            if (host.isEmpty()) {
                whoNeeded = true;
                continue; // cannot bind anything without the host
            }
            const bool voiced = nickRank(entry) <= 4; // +v or higher: may speak in +m

            // A captcha solved on the web becomes a host-bound grant here. The
            // solve is keyed by this exact host hash, so a same-nick user on a
            // different host cannot inherit it.
            if (m_voiceGate->consumeSolved(m_config.slug, nick, VoiceGate::hostHash(host))) {
                m_voiceGate->grant(m_config.slug, nick, host);
            }

            if (m_voiceGate->isGranted(m_config.slug, nick, host)) {
                m_voiceGate->markOnline(m_config.slug, nick, host); // freeze TTL while online
                m_pendingSince.remove(key);
                if (!voiced) {
                    grantVoice(ch, nick);
                }
                continue;
            }
            if (voiced) {
                continue; // voiced by an operator, nothing owed to the gate
            }

            // Unvoiced and ungranted: wait the delay, then PM (pmDue throttles).
            const qint64 since = m_pendingSince.value(key, 0);
            if (since == 0) {
                m_pendingSince.insert(key, now);
            } else if (now - since >= delayMs && m_voiceGate->pmDue(m_config.slug, nick, host)) {
                sendCaptchaPm(nick, host);
            }
        }

        if (whoNeeded && now - m_lastWho.value(ch.toLower(), 0) > WHO_THROTTLE_MS) {
            m_lastWho.insert(ch.toLower(), now);
            send("WHO " + ch, false);
        }
    }
}

} // namespace ircabot
