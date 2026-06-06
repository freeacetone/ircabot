/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "config.h"
#include "util.h"
#include "version.h"

#include <QDebug>
#include <QDir>
#include <QFile>

#include <stdexcept>

namespace ircabot {

namespace {

struct Section
{
    QString name;
    QMap<QString, QString> values;
};

QList<Section> tokenize(const QString& text)
{
    QList<Section> sections;
    const QStringList lines = text.split('\n');
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            sections.push_back({line.mid(1, line.size() - 2).trimmed(), {}});
            continue;
        }
        const qsizetype eq = line.indexOf('=');
        if (eq <= 0 || sections.isEmpty()) {
            continue;
        }
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (!key.isEmpty()) {
            sections.back().values[key] = value;
        }
    }
    return sections;
}

QMap<QString, QString> parseTriggers(const QString& line, const QString& context)
{
    QMap<QString, QString> result;
    const QStringList pairs = line.split(QStringLiteral("<!>"));
    for (const QString& p : pairs) {
        const QStringList pair = p.split(QStringLiteral(":::"));
        if (pair.size() != 2) {
            continue;
        }
        const QString request = pair.first().trimmed();
        const QString answer = pair.last().trimmed();
        if (request.isEmpty() || answer.isEmpty()) {
            continue;
        }
        result[request] = answer;
        qInfo().noquote() << "[" + context + "] Trigger:" << request << ":::" << answer;
    }
    return result;
}

bool toBool(const QString& v)
{
    return v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || v.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0
        || v == QStringLiteral("1");
}

} // namespace

QString Config::exampleText()
{
    return QStringLiteral(
        "[GLOBAL]\n"
        "data_path = /srv/ircabot/data\n\n"
        "# Web interface (multithreaded, JS-free; JS is used only on /~realtime/ pages).\n"
        "bind_to_address = 127.0.0.1\n"
        "bind_to_port = 8080\n"
        "service_name = IRCaBot\n"
        "service_emoji = &#128193;\n"
        "# Uncomment to remove real time reading mode (and all JavaScript with it):\n"
        "#realtime_disabled = true\n\n"
        "# Defaults for all servers:\n"
        "nick = default_nickname\n"
        "user = default_ident\n"
        "real_name = default_real_name\n"
        "# If empty - logging in without NickServ password:\n"
        "password =\n\n"
        "# Bot answers when addressed: \"botnick, version\". Format: request ::: answer <!> ...\n"
        "# %CHANNEL_FOR_URL% is replaced with server_slug/channel of the requesting chat.\n"
        "triggers = version ::: IRCaBot ") + VERSION + QStringLiteral(" <!> webui ::: http://example.com/%CHANNEL_FOR_URL%\n\n"
        "[Displayed server name]\n"
        "address = 127.0.0.1\n"
        "port = 6667\n"
        "# TLS connection to IRC server:\n"
        "#ssl = true\n"
        "# Channels are splitted with comma:\n"
        "channels = #general,#test\n"
        "# Optional per-server overrides:\n"
        "#nick = unique_nickname\n"
        "#user = unique_ident\n"
        "#real_name = unique_real_name\n"
        "#password = password_for_this_server\n"
        "#triggers = hi ::: hello\n");
}

Config::Config(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Can't open configuration file: " + path.toStdString());
    }
    parse(QString::fromUtf8(file.readAll()));
}

void Config::parse(const QString& text)
{
    const QList<Section> sections = tokenize(text);

    auto globalIt = std::find_if(sections.begin(), sections.end(),
                                 [](const Section& s) { return s.name == QStringLiteral("GLOBAL"); });
    if (globalIt == sections.end()) {
        throw std::runtime_error("Wrong config: [GLOBAL] section not exist");
    }
    const QMap<QString, QString>& g = globalIt->values;

    m_dataPath = g.value(QStringLiteral("data_path"));
    if (m_dataPath.isEmpty()) {
        throw std::runtime_error("'data_path' in [GLOBAL] is undefined");
    }
    if (!m_dataPath.endsWith('/')) {
        m_dataPath += '/';
    }
    if (!QDir().mkpath(m_dataPath)) {
        throw std::runtime_error("Can't create data_path: " + m_dataPath.toStdString());
    }

    m_bindAddress = g.value(QStringLiteral("bind_to_address"));
    if (m_bindAddress.isEmpty()) {
        throw std::runtime_error("'bind_to_address' in [GLOBAL] is undefined");
    }
    bool portOk = false;
    m_bindPort = g.value(QStringLiteral("bind_to_port")).toUShort(&portOk);
    if (!portOk || m_bindPort == 0) {
        throw std::runtime_error("'bind_to_port' in [GLOBAL] is incorrect");
    }

    m_serviceName = g.value(QStringLiteral("service_name"), QStringLiteral("IRCaBot"));
    m_serviceEmoji = g.value(QStringLiteral("service_emoji"), QStringLiteral("&#128193;"));
    m_realtimeDisabled = toBool(g.value(QStringLiteral("realtime_disabled")))
                      || toBool(g.value(QStringLiteral("AJAXIsDisabled"))); // v1 compatibility

    const QString defaultNick = g.value(QStringLiteral("nick")).replace(' ', '_');
    const QString defaultUser = g.value(QStringLiteral("user"));
    const QString defaultRealName = g.value(QStringLiteral("real_name"));
    const QString defaultPassword = g.value(QStringLiteral("password"));
    const QMap<QString, QString> globalTriggers =
        parseTriggers(g.value(QStringLiteral("triggers")), QStringLiteral("GLOBAL"));

    for (const Section& s : sections) {
        if (s.name == QStringLiteral("GLOBAL")) {
            continue;
        }

        ServerConfig srv;
        srv.displayName = s.name;
        srv.slug = util::slugify(s.name);

        srv.address = s.values.value(QStringLiteral("address"));
        if (srv.address.isEmpty()) {
            qWarning().noquote() << "[" + s.name + "] ignored (empty 'address')";
            continue;
        }
        bool ok = false;
        srv.port = s.values.value(QStringLiteral("port")).toUShort(&ok);
        if (!ok || srv.port == 0) {
            qWarning().noquote() << "[" + s.name + "] ignored (wrong 'port')";
            continue;
        }
        srv.ssl = toBool(s.values.value(QStringLiteral("ssl")));

        const QStringList rawChannels = s.values.value(QStringLiteral("channels")).split(',', Qt::SkipEmptyParts);
        for (QString ch : rawChannels) {
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
            qWarning().noquote() << "[" + s.name + "] ignored (empty 'channels')";
            continue;
        }

        srv.nick = s.values.value(QStringLiteral("nick"), defaultNick).replace(' ', '_');
        srv.user = s.values.value(QStringLiteral("user"), defaultUser);
        srv.realName = s.values.value(QStringLiteral("real_name"), defaultRealName);
        srv.password = s.values.value(QStringLiteral("password"), defaultPassword);
        if (srv.nick.isEmpty() || srv.user.isEmpty() || srv.realName.isEmpty()) {
            qWarning().noquote() << "[" + s.name + "] ignored (empty 'nick', 'user' or 'real_name', local and global)";
            continue;
        }

        srv.triggers = parseTriggers(s.values.value(QStringLiteral("triggers")), s.name);
        for (auto it = globalTriggers.constBegin(); it != globalTriggers.constEnd(); ++it) {
            if (!srv.triggers.contains(it.key())) {
                srv.triggers[it.key()] = it.value();
            }
        }

        m_servers.push_back(srv);
    }

    if (m_servers.isEmpty()) {
        throw std::runtime_error("No valid server sections in configuration file");
    }
}

} // namespace ircabot
