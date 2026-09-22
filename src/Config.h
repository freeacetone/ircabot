/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QList>

namespace ircabot {

// One channel the bot joins. The language tag becomes the <html lang> of that
// channel's log pages, so a browser can offer to translate a chat that is
// known to be held in one language; the interface itself stays English.
// A moderation-only channel is joined for the voice gate alone: nothing is
// written to disk and the web interface never exposes it as a log.
struct ChannelConfig
{
    QString name;                        // with leading '#'
    QString lang = QStringLiteral("en"); // BCP47 tag, "en" unless overridden
    bool moderationOnly = false;         // join to moderate, never log
};

struct ServerConfig
{
    QString displayName;   // "name" field, e.g. "Ilita IRC"
    QString slug;          // "ilita_irc" - folder name and URL segment
    QString address;
    quint16 port = 0;
    bool ssl = false;
    QList<ChannelConfig> channels;
    QString nick;
    QString user;
    QString realName;
    QString password;      // NickServ
    QMap<QString, QString> triggers; // request -> answer
};

// Voice gate: on moderated (+m) channels where the bot can grant +v, new users
// must solve a captcha to be voiced.
struct VoiceGateConfig
{
    bool enabled = true;
    bool setModerated = true;     // set +m on a gated channel once the bot is op
    int connectDelaySeconds = 10; // wait after a join before the first captcha PM
    int captchaLength = 4;        // number of characters in the captcha image
    int offlineTtlHours = 24;     // drop a voiced user's grant after this offline time
    int pmIntervalHours = 24;     // do not PM the same user more often; 0 = never PM
    QString captchaUrl;           // public base URL for the PM link; empty -> derive
    QString privateMessage;       // captcha PM body; the link is appended after it
};

// Names of the channels that are actually logged, in config order: what the
// log store has to create directories for. Moderation-only channels are left
// out, so no folder of theirs ever appears on disk.
QStringList loggedChannelNames(const QList<ChannelConfig>& channels);

// JSON configuration file.
// Top-level keys: data_path, web{}, defaults{}, triggers{}, servers[].
class Config
{
public:
    static QString exampleText();

    // Throws std::runtime_error with a human-readable message on fatal errors.
    explicit Config(const QString& path);

    const QString& dataPath() const        { return m_dataPath; }
    bool logLocalTime() const              { return m_logLocalTime; }
    qint64 logCacheBytes() const           { return m_logCacheBytes; }
    const QString& bindAddress() const     { return m_bindAddress; }
    quint16 bindPort() const               { return m_bindPort; }
    const QString& serviceName() const     { return m_serviceName; }
    const QString& serviceEmoji() const    { return m_serviceEmoji; }
    bool realtimeDisabled() const          { return m_realtimeDisabled; }
    const VoiceGateConfig& voiceGate() const { return m_voiceGate; }
    const QList<ServerConfig>& servers() const { return m_servers; }

private:
    void parse(const QByteArray& raw);

    QString m_dataPath;
    bool m_logLocalTime = false; // false: rotate days in UTC; true: server local time
    qint64 m_logCacheBytes = 30LL * 1024 * 1024; // archive log-file RAM cache budget
    QString m_bindAddress;
    quint16 m_bindPort = 0;
    QString m_serviceName;
    QString m_serviceEmoji;
    bool m_realtimeDisabled = false;
    VoiceGateConfig m_voiceGate;
    QList<ServerConfig> m_servers;
};

} // namespace ircabot
