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

struct ServerConfig
{
    QString displayName;   // section header, e.g. [Ilita IRC]
    QString slug;          // "ilita_irc" - folder name and URL segment
    QString address;
    quint16 port = 0;
    bool ssl = false;
    QStringList channels;  // with leading '#'
    QString nick;
    QString user;
    QString realName;
    QString password;      // NickServ
    QMap<QString, QString> triggers; // request -> answer
};

// INI-style configuration, file format compatible with IRCaBot v1/v2.
// New optional keys: "ssl" (per server), "realtime_disabled" (alias of AJAXIsDisabled).
class Config
{
public:
    static QString exampleText();

    // Throws std::runtime_error with a human-readable message on fatal errors.
    explicit Config(const QString& path);

    const QString& dataPath() const        { return m_dataPath; }
    const QString& bindAddress() const     { return m_bindAddress; }
    quint16 bindPort() const               { return m_bindPort; }
    const QString& serviceName() const     { return m_serviceName; }
    const QString& serviceEmoji() const    { return m_serviceEmoji; }
    bool realtimeDisabled() const          { return m_realtimeDisabled; }
    const QList<ServerConfig>& servers() const { return m_servers; }

private:
    void parse(const QString& text);

    QString m_dataPath;
    QString m_bindAddress;
    quint16 m_bindPort = 0;
    QString m_serviceName;
    QString m_serviceEmoji;
    bool m_realtimeDisabled = false;
    QList<ServerConfig> m_servers;
};

} // namespace ircabot
