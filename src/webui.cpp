/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "webui.h"
#include "util.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QTcpServer>
#include <QtConcurrent>

namespace ircabot {

namespace {

constexpr const char* DEFAULT_MAIN_PAGE =
    "# Main page content. HTML is supported, for line breaks use <br>.\n"
    "# Placeholders: %LOCAL_TIME% - server time, %DAILY_REQUESTS% - requests served today.\n\n"
    "<h2>Welcome to IRC chat logger</h2>\n"
    "Pick a channel on the left to read the logs.<br><br>\n"
    "Server time: %LOCAL_TIME%<br>\n"
    "Requests served today: %DAILY_REQUESTS%\n";

constexpr const char* THEME_COOKIE = "ircabot_theme";

// Path segments come from the network: never let them reach the filesystem raw
bool safeSegment(const QString& s)
{
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9._^`{}|\\[\\]\\\\-]+$"));
    return !s.isEmpty() && !s.contains(QStringLiteral("..")) && ok.match(s).hasMatch();
}

QByteArray qrcFile(const QString& path)
{
    QFile f(QStringLiteral(":/") + path);
    f.open(QIODevice::ReadOnly);
    return f.readAll();
}

QHttpServerResponse html(const QString& body,
                         QHttpServerResponse::StatusCode code = QHttpServerResponse::StatusCode::Ok)
{
    return QHttpServerResponse(QByteArrayLiteral("text/html; charset=utf-8"), body.toUtf8(), code);
}

QHttpServerResponse cachedAsset(const QByteArray& mime, const QByteArray& data)
{
    QHttpServerResponse response(mime, data);
    QHttpHeaders headers = response.headers();
    headers.append(QHttpHeaders::WellKnownHeader::CacheControl, QByteArrayLiteral("public, max-age=86400"));
    response.setHeaders(std::move(headers));
    return response;
}

QString themeFromRequest(const QHttpServerRequest& request)
{
    const QByteArray cookies = request.headers().value(QHttpHeaders::WellKnownHeader::Cookie).toByteArray();
    const QList<QByteArray> parts = cookies.split(';');
    for (const QByteArray& rawPart : parts) {
        const QByteArray part = rawPart.trimmed();
        const QByteArray prefix = QByteArray(THEME_COOKIE) + '=';
        if (part.startsWith(prefix)) {
            const QByteArray value = part.mid(prefix.size());
            if (value == "dark" || value == "light") {
                return QString::fromUtf8(value);
            }
        }
    }
    return {};
}

QHttpServerResponse themeRedirect(const QString& mode, const QUrlQuery& query)
{
    const QString back = query.queryItemValue(QStringLiteral("back"), QUrl::FullyDecoded);
    const bool backIsLocal = back.startsWith('/') && !back.startsWith(QStringLiteral("//"));
    const QByteArray location = backIsLocal ? back.toUtf8() : QByteArrayLiteral("/");

    QByteArray cookie(THEME_COOKIE);
    if (mode == QStringLiteral("dark") || mode == QStringLiteral("light")) {
        cookie += '=' + mode.toUtf8() + "; Path=/; Max-Age=31536000; SameSite=Lax";
    } else { // "auto": drop the cookie, prefers-color-scheme decides again
        cookie += "=; Path=/; Max-Age=0; SameSite=Lax";
    }

    QHttpServerResponse response(QHttpServerResponse::StatusCode::Found);
    QHttpHeaders headers = response.headers();
    headers.append(QHttpHeaders::WellKnownHeader::Location, location);
    headers.append(QHttpHeaders::WellKnownHeader::SetCookie, cookie);
    response.setHeaders(std::move(headers));
    return response;
}

} // namespace

WebUi::WebUi(const Config& config, RuntimeState* state,
             const QHash<QString, std::shared_ptr<LogStore>>& stores, QObject* parent)
    : QObject(parent),
      m_dataPath(config.dataPath()),
      m_bindAddress(config.bindAddress()),
      m_bindPort(config.bindPort()),
      m_state(state),
      m_stores(stores)
{
    m_site.serviceName = config.serviceName();
    m_site.serviceEmoji = config.serviceEmoji();
    m_site.realtimeDisabled = config.realtimeDisabled();
    m_site.state = state;

    ensureMainPageFile();
    setupRoutes();
}

bool WebUi::listen()
{
    auto tcpServer = std::make_unique<QTcpServer>();
    if (!tcpServer->listen(QHostAddress(m_bindAddress), m_bindPort)) {
        qCritical().noquote() << "Web interface: can't listen on"
                              << m_bindAddress + ':' + QString::number(m_bindPort)
                              << '(' + tcpServer->errorString() + ')';
        return false;
    }
    if (!m_server.bind(tcpServer.get())) {
        qCritical().noquote() << "Web interface: QHttpServer bind failed";
        return false;
    }
    QTcpServer* const ownedByHttpServer = tcpServer.release(); // ownership moved to QHttpServer
    Q_UNUSED(ownedByHttpServer);
    qInfo().noquote() << "Web interface:" << m_bindAddress + ':' + QString::number(m_bindPort);
    return true;
}

void WebUi::ensureMainPageFile() const
{
    const QString path = m_dataPath + QStringLiteral("main_page.txt");
    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(DEFAULT_MAIN_PAGE);
        } else {
            qWarning().noquote() << "Can't create" << path;
        }
    }
}

QString WebUi::readMainPageText() const
{
    QFile f(m_dataPath + QStringLiteral("main_page.txt"));
    if (!f.open(QIODevice::ReadOnly)) {
        return QStringLiteral("main_page.txt is not readable");
    }
    QString result;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine());
        if (line.startsWith('#')) {
            continue;
        }
        result += line;
    }
    return result;
}

render::Site WebUi::siteFor(const QHttpServerRequest& request) const
{
    render::Site site = m_site;
    site.theme = themeFromRequest(request);
    site.path = request.url().path();
    return site;
}

void WebUi::setupRoutes()
{
    // --- Static assets (tiny, served from memory) ---
    m_server.route(QStringLiteral("/style.css"), []() {
        return cachedAsset(QByteArrayLiteral("text/css; charset=utf-8"), qrcFile(QStringLiteral("style.css")));
    });
    m_server.route(QStringLiteral("/favicon.svg"), []() {
        return cachedAsset(QByteArrayLiteral("image/svg+xml"), qrcFile(QStringLiteral("favicon.svg")));
    });
    m_server.route(QStringLiteral("/favicon.ico"), []() {
        return cachedAsset(QByteArrayLiteral("image/svg+xml"), qrcFile(QStringLiteral("favicon.svg")));
    });
    if (!m_site.realtimeDisabled) {
        m_server.route(QStringLiteral("/live.js"), []() {
            return cachedAsset(QByteArrayLiteral("text/javascript; charset=utf-8"),
                               qrcFile(QStringLiteral("live.js")));
        });
    }

    // --- Theme switch: sets a persistent cookie and redirects back ---
    m_server.route(QStringLiteral("/~theme/<arg>"),
                   [](const QString& mode, const QHttpServerRequest& request) {
        return themeRedirect(mode, request.query());
    });

    // --- Main page ---
    m_server.route(QStringLiteral("/"), [this](const QHttpServerRequest& request) {
        return QtConcurrent::run([this, site = siteFor(request)] {
            m_state->countRequest();
            return html(render::mainPage(site, readMainPageText()));
        });
    });

    // --- Custom images from <data>/custom_images/ (v1 compatibility) ---
    m_server.route(QStringLiteral("/~images/<arg>"), [this](const QUrl& rest, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, rest, site = siteFor(request)] {
            const QString name = rest.path();
            if (name.contains(QStringLiteral("..")) || name.startsWith('/')) {
                return html(render::errorPage(site, QStringLiteral("403"), QStringLiteral("Forbidden")),
                            QHttpServerResponse::StatusCode::Forbidden);
            }
            QFile f(m_dataPath + QStringLiteral("custom_images/") + name);
            if (!f.open(QIODevice::ReadOnly)) {
                return html(render::errorPage(site, QStringLiteral("404"), QStringLiteral("Image not found")),
                            QHttpServerResponse::StatusCode::NotFound);
            }
            const QByteArray mime = QMimeDatabase().mimeTypeForFile(QFileInfo(f)).name().toUtf8();
            return cachedAsset(mime, f.readAll());
        });
    });

    // --- Real time reading: the only pages with JavaScript ---
    if (!m_site.realtimeDisabled) {
        m_server.route(QStringLiteral("/~realtime/<arg>/<arg>"),
                       [this](const QString& slug, const QString& channel, const QHttpServerRequest& request) {
            return QtConcurrent::run([this, slug, channel, site = siteFor(request)] {
                m_state->countRequest();
                bool found = false;
                const ServerSnapshot server = m_state->snapshot(slug, &found);
                if (!found || !server.channels.contains(channel)) {
                    return html(render::errorPage(site, QStringLiteral("404"), QStringLiteral("No such channel")),
                                QHttpServerResponse::StatusCode::NotFound);
                }
                return html(render::livePage(site, server, channel));
            });
        });

        m_server.route(QStringLiteral("/ajax/<arg>/<arg>"),
                       [this](const QString& slug, const QString& channel, const QHttpServerRequest& request) {
            return QtConcurrent::run([this, slug, channel,
                                      after = request.query().queryItemValue(QStringLiteral("after")).toULongLong()] {
                return serveAjax(slug, channel, after);
            });
        });
    }

    // --- Server about page ---
    m_server.route(QStringLiteral("/<arg>"), [this](const QString& slug, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, site = siteFor(request)] {
            m_state->countRequest();
            bool found = false;
            const ServerSnapshot server = m_state->snapshot(slug, &found);
            if (!found || !safeSegment(slug)) {
                return html(render::errorPage(site, QStringLiteral("404"), QStringLiteral("No such server: ") + slug),
                            QHttpServerResponse::StatusCode::NotFound);
            }
            return html(render::aboutPage(site, server, m_stores[slug]->aboutServerHtml()));
        });
    });

    // --- Channel pages: /<server>/<channel>[/yyyy[/MM[/dd[.txt]]]] ---
    m_server.route(QStringLiteral("/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, query = request.query(), site = siteFor(request)] {
            return servePage(site, slug, channel, {}, {}, {}, query);
        });
    });
    m_server.route(QStringLiteral("/<arg>/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QString& year,
                          const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, year, site = siteFor(request)] {
            return servePage(site, slug, channel, year, {}, {}, {});
        });
    });
    m_server.route(QStringLiteral("/<arg>/<arg>/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QString& year, const QString& month,
                          const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, year, month, site = siteFor(request)] {
            return servePage(site, slug, channel, year, month, {}, {});
        });
    });
    m_server.route(QStringLiteral("/<arg>/<arg>/<arg>/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QString& year,
                          const QString& month, const QString& day, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, year, month, day, site = siteFor(request)] {
            return servePage(site, slug, channel, year, month, day, {});
        });
    });

    m_server.setMissingHandler(this, [this](const QHttpServerRequest& request, QHttpServerResponder& responder) {
        responder.write(render::errorPage(siteFor(request), QStringLiteral("404"),
                                          QStringLiteral("Page not found")).toUtf8(),
                        QByteArrayLiteral("text/html; charset=utf-8"),
                        QHttpServerResponder::StatusCode::NotFound);
    });
}

QHttpServerResponse WebUi::servePage(const render::Site& site,
                                     const QString& slug, const QString& channel,
                                     const QString& year, const QString& month, const QString& dayRaw,
                                     const QUrlQuery& query)
{
    m_state->countRequest();

    const auto notFound = [&site](const QString& what) {
        return html(render::errorPage(site, QStringLiteral("404"), what),
                    QHttpServerResponse::StatusCode::NotFound);
    };

    bool found = false;
    const ServerSnapshot server = m_state->snapshot(slug, &found);
    if (!found || !safeSegment(slug)) {
        return notFound(QStringLiteral("No such server: ") + slug);
    }
    if (!server.channels.contains(channel) || !safeSegment(channel)) {
        return notFound(QStringLiteral("No such channel: ") + channel);
    }
    const LogStore& store = *m_stores[slug];

    // Search: /<server>/<channel>?toSearch=...&isRegexp=on
    const QString searchQuery = query.queryItemValue(QStringLiteral("toSearch"), QUrl::FullyDecoded)
                                    .replace('+', ' ')
                                    .trimmed();
    if (!searchQuery.isEmpty()) {
        const bool regexp = query.queryItemValue(QStringLiteral("isRegexp")) == QStringLiteral("on");
        const SearchResult result = store.search(channel, searchQuery, regexp);
        return html(render::searchPage(site, server, channel, searchQuery, regexp, result));
    }

    if (year.isEmpty()) {
        return html(render::calendarPage(site, server, channel, store));
    }
    if (!safeSegment(year) || (!month.isEmpty() && !safeSegment(month))) {
        return notFound(QStringLiteral("Bad date"));
    }
    if (month.isEmpty()) {
        return html(render::yearPage(site, server, channel, store, year));
    }
    if (dayRaw.isEmpty()) {
        return html(render::monthPage(site, server, channel, store, year, month));
    }

    QString day = dayRaw;
    const bool plainText = day.endsWith(QStringLiteral(".txt"));
    if (plainText) {
        day.chop(4);
    }

    const QDate date(year.toInt(), month.toInt(), day.toInt());
    if (!date.isValid()) {
        return notFound(QStringLiteral("Bad date"));
    }

    if (plainText) {
        if (!store.dayExists(channel, date)) {
            return notFound(QStringLiteral("No log for this day"));
        }
        return QHttpServerResponse(QByteArrayLiteral("text/plain; charset=utf-8"), store.readDayRaw(channel, date));
    }
    if (!store.dayExists(channel, date)) {
        return notFound(QStringLiteral("No log for ") + date.toString(QStringLiteral("yyyy-MM-dd")));
    }
    return html(render::dayPage(site, server, channel, store, date));
}

QHttpServerResponse WebUi::serveAjax(const QString& slug, const QString& channel, quint64 afterId)
{
    m_state->countAjaxRequest();

    bool found = false;
    const ServerSnapshot server = m_state->snapshot(slug, &found);
    if (!found || !server.channels.contains(channel)) {
        return QHttpServerResponse(QJsonObject{{QStringLiteral("ok"), false}},
                                   QHttpServerResponse::StatusCode::NotFound);
    }

    const ChannelSnapshot chan = m_state->channelSnapshot(slug, channel);
    const QList<LiveMessage> messages = m_state->liveMessagesAfter(slug, channel, afterId);

    QJsonArray messagesJson;
    quint64 last = afterId;
    for (const LiveMessage& msg : messages) {
        messagesJson.push_back(QJsonObject{
            {QStringLiteral("id"), QString::number(msg.id)},
            {QStringLiteral("time"), msg.unixTime},
            {QStringLiteral("nick"), msg.nick},
            {QStringLiteral("hue"), util::nickHue(msg.nick)},
            {QStringLiteral("text"), msg.text},
        });
        last = msg.id;
    }

    QJsonArray onlineJson;
    for (const QString& nick : chan.online) {
        onlineJson.push_back(nick);
    }

    return QHttpServerResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("connected"), server.connected},
        {QStringLiteral("last"), QString::number(last)},
        {QStringLiteral("topic"), chan.topic},
        {QStringLiteral("online"), onlineJson},
        {QStringLiteral("messages"), messagesJson},
    });
}

} // namespace ircabot
