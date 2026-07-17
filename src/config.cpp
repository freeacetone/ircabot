/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "config.h"
#include "util.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>

namespace ircabot {

namespace {

QMap<QString, QString> readTriggers(const QJsonObject& obj, const QString& context)
{
    QMap<QString, QString> result;
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.key().startsWith('_')) { // "_comment" and friends
            continue;
        }
        const QString request = it.key().trimmed();
        const QString answer = it.value().toString().trimmed();
        if (request.isEmpty() || answer.isEmpty()) {
            qWarning().noquote() << "[" + context + "] Trigger" << it.key() << "ignored (empty request or answer)";
            continue;
        }
        result[request] = answer;
        qInfo().noquote() << "[" + context + "] Trigger:" << request << "->" << answer;
    }
    return result;
}

int lineNumberAt(const QByteArray& raw, int offset)
{
    return static_cast<int>(raw.left(offset).count('\n')) + 1;
}

} // namespace

QString Config::exampleText()
{
    // "_comment" keys are ignored by the parser: JSON has no real comments
    return QStringLiteral(R"({
    "data_path": "/srv/ircabot/data",

    "_comment_log_local_time": "Day rotation timezone: false (default) = UTC, true = server local time",
    "log_local_time": false,

    "web": {
        "_comment": "JS is used only on /~realtime/ pages; realtime_disabled removes it entirely",
        "address": "127.0.0.1",
        "port": 8080,
        "service_name": "IRCaBot",
        "service_emoji": "&#128193;",
        "realtime_disabled": false
    },

    "defaults": {
        "_comment": "Used for every server unless overridden. Empty password: no NickServ login",
        "nick": "default_nickname",
        "user": "default_ident",
        "real_name": "default_real_name",
        "password": ""
    },

    "triggers": {
        "_comment": "Bot answers when addressed: 'botnick, version'. %CHANNEL_FOR_URL% -> server_slug/channel, %VERSION% -> running version",
        "version": "IRCaBot %VERSION%",
        "webui": "http://example.com/%CHANNEL_FOR_URL%"
    },

    "servers": [
        {
            "_comment": "nick, user, real_name, password and triggers can be overridden per server",
            "name": "Displayed server name",
            "address": "127.0.0.1",
            "port": 6667,
            "ssl": false,
            "channels": ["#general", "#test"]
        }
    ]
}
)");
}

Config::Config(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Can't open configuration file: " + path.toStdString());
    }
    parse(file.readAll());
}

void Config::parse(const QByteArray& raw)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (doc.isNull()) {
        throw std::runtime_error("Config is not valid JSON: "
                                 + parseError.errorString().toStdString()
                                 + " (line " + std::to_string(lineNumberAt(raw, parseError.offset)) + ")");
    }
    if (!doc.isObject()) {
        throw std::runtime_error("Config root must be a JSON object");
    }
    const QJsonObject root = doc.object();

    m_dataPath = root.value(QStringLiteral("data_path")).toString();
    if (m_dataPath.isEmpty()) {
        throw std::runtime_error("'data_path' is undefined");
    }
    if (!m_dataPath.endsWith('/')) {
        m_dataPath += '/';
    }
    if (!QDir().mkpath(m_dataPath)) {
        throw std::runtime_error("Can't create data_path: " + m_dataPath.toStdString());
    }

    // Day rotation defaults to UTC; opt into the server's local time explicitly.
    m_logLocalTime = root.value(QStringLiteral("log_local_time")).toBool(false);

    const QJsonObject web = root.value(QStringLiteral("web")).toObject();
    m_bindAddress = web.value(QStringLiteral("address")).toString();
    if (m_bindAddress.isEmpty()) {
        throw std::runtime_error("'web.address' is undefined");
    }
    const int port = web.value(QStringLiteral("port")).toInt();
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("'web.port' is incorrect");
    }
    m_bindPort = static_cast<quint16>(port);
    m_serviceName = web.value(QStringLiteral("service_name")).toString(QStringLiteral("IRCaBot"));
    m_serviceEmoji = web.value(QStringLiteral("service_emoji")).toString(QStringLiteral("&#128193;"));
    m_realtimeDisabled = web.value(QStringLiteral("realtime_disabled")).toBool(false);

    const QJsonObject defaults = root.value(QStringLiteral("defaults")).toObject();
    const QString defaultNick = defaults.value(QStringLiteral("nick")).toString().replace(' ', '_');
    const QString defaultUser = defaults.value(QStringLiteral("user")).toString();
    const QString defaultRealName = defaults.value(QStringLiteral("real_name")).toString();
    const QString defaultPassword = defaults.value(QStringLiteral("password")).toString();

    const QMap<QString, QString> globalTriggers =
        readTriggers(root.value(QStringLiteral("triggers")).toObject(), QStringLiteral("GLOBAL"));

    const QJsonArray servers = root.value(QStringLiteral("servers")).toArray();
    for (const QJsonValue& value : servers) {
        const QJsonObject s = value.toObject();

        ServerConfig srv;
        srv.displayName = s.value(QStringLiteral("name")).toString().trimmed();
        if (srv.displayName.isEmpty()) {
            qWarning().noquote() << "Server entry ignored (empty 'name')";
            continue;
        }
        srv.slug = util::slugify(srv.displayName);

        srv.address = s.value(QStringLiteral("address")).toString();
        if (srv.address.isEmpty()) {
            qWarning().noquote() << "[" + srv.displayName + "] ignored (empty 'address')";
            continue;
        }
        const int srvPort = s.value(QStringLiteral("port")).toInt();
        if (srvPort <= 0 || srvPort > 65535) {
            qWarning().noquote() << "[" + srv.displayName + "] ignored (wrong 'port')";
            continue;
        }
        srv.port = static_cast<quint16>(srvPort);
        srv.ssl = s.value(QStringLiteral("ssl")).toBool(false);

        const QJsonArray channels = s.value(QStringLiteral("channels")).toArray();
        for (const QJsonValue& chValue : channels) {
            QString ch = chValue.toString();
            ch.remove(' ');
            if (ch.isEmpty()) {
                continue;
            }
            if (!ch.startsWith('#')) {
                ch.prepend('#');
            }
            srv.channels.push_back(ch);
        }
        if (srv.channels.isEmpty()) {
            qWarning().noquote() << "[" + srv.displayName + "] ignored (empty 'channels')";
            continue;
        }

        srv.nick = s.value(QStringLiteral("nick")).toString(defaultNick).replace(' ', '_');
        srv.user = s.value(QStringLiteral("user")).toString(defaultUser);
        srv.realName = s.value(QStringLiteral("real_name")).toString(defaultRealName);
        srv.password = s.value(QStringLiteral("password")).toString(defaultPassword);
        if (srv.nick.isEmpty() || srv.user.isEmpty() || srv.realName.isEmpty()) {
            qWarning().noquote() << "[" + srv.displayName + "] ignored "
                                    "(empty 'nick', 'user' or 'real_name', local and defaults)";
            continue;
        }

        srv.triggers = readTriggers(s.value(QStringLiteral("triggers")).toObject(), srv.displayName);
        for (auto it = globalTriggers.constBegin(); it != globalTriggers.constEnd(); ++it) {
            if (!srv.triggers.contains(it.key())) {
                srv.triggers[it.key()] = it.value();
            }
        }

        m_servers.push_back(srv);
    }

    if (m_servers.isEmpty()) {
        throw std::runtime_error("No valid entries in 'servers'");
    }
}

} // namespace ircabot
