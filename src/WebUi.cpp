/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "WebUi.h"
#include "Util.h"
#include "VoiceGate.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QMutex>
#include <QRegularExpression>
#include <QTcpServer>
#include <QtConcurrent>

namespace ircabot {

namespace {

constexpr const char* DEFAULT_MAIN_PAGE =
    "# Main page content. HTML is supported, for line breaks use <br>.\n"
    "# Placeholders: %LOCAL_TIME% - current time in the logging timezone (UTC by default),\n"
    "# %DAILY_REQUESTS% - requests served today.\n\n"
    "<h2>Welcome to IRC chat logger</h2>\n"
    "Pick a channel on the left to read the logs.<br><br>\n"
    "Server time: %LOCAL_TIME%<br>\n"
    "Requests served today: %DAILY_REQUESTS%\n";

constexpr const char* THEME_COOKIE = "ircabot_theme";
constexpr int CAPTCHA_TTL_SEC = 600;

// Path segments come from the network: never let them reach the filesystem raw
bool safeSegment(const QString& s)
{
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9._^`{}|\\[\\]\\\\-]+$"));
    return !s.isEmpty() && !s.contains(QStringLiteral("..")) && ok.match(s).hasMatch();
}

// A captcha nick may be non-ASCII (Cyrillic, ...). It is HTML-escaped in the
// page, hashed into the nonce and sanitized by VoiceGate for the filesystem, so
// here it only has to be free of path/routing hazards.
bool safeNick(const QString& s)
{
    if (s.isEmpty() || s.size() > 64
        || s.contains(QLatin1Char('/')) || s.contains(QLatin1Char('\\'))
        || s.contains(QStringLiteral(".."))) {
        return false;
    }
    for (const QChar c : s) {
        if (c.isSpace() || c.category() == QChar::Other_Control) {
            return false;
        }
    }
    return true;
}

// The style sheet and live.js live in the binary's Qt resources; read each one
// once and keep the bytes, so repeated requests never touch the resource FS.
QByteArray qrcFile(const QString& path)
{
    static QMutex mutex;
    static QHash<QString, QByteArray> cache;
    const QMutexLocker locker(&mutex);
    const auto it = cache.constFind(path);
    if (it != cache.constEnd()) {
        return *it;
    }
    QFile f(QStringLiteral(":/") + path);
    f.open(QIODevice::ReadOnly);
    return *cache.insert(path, f.readAll());
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
    // Must be a local absolute path. Reject "//host" and "/\host": browsers
    // normalise '\' to '/', so both resolve to a scheme-relative URL (open redirect).
    const bool backIsLocal = back.startsWith('/')
        && !back.startsWith(QStringLiteral("//"))
        && !back.startsWith(QStringLiteral("/\\"));
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
             const QHash<QString, std::shared_ptr<LogStore>>& stores,
             VoiceGate* voiceGate, QObject* parent)
    : QObject(parent),
      m_dataPath(config.dataPath()),
      m_bindAddress(config.bindAddress()),
      m_bindPort(config.bindPort()),
      m_state(state),
      m_stores(stores),
      m_voiceGate(voiceGate)
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
    const QString path = m_dataPath + QStringLiteral("_ircabot/web/main_page.txt");
    if (!QFile::exists(path)) {
        QDir().mkpath(m_dataPath + QStringLiteral("_ircabot/web"));
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
    {
        const QReadLocker readLock(&m_cacheLock);
        if (m_mainPageCached) {
            return m_mainPageCache;
        }
    }
    QFile f(m_dataPath + QStringLiteral("_ircabot/web/main_page.txt"));
    if (!f.open(QIODevice::ReadOnly)) {
        // Do not cache the error: a permission hiccup should be able to recover.
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
    const QWriteLocker writeLock(&m_cacheLock);
    m_mainPageCache = result;
    m_mainPageCached = true;
    return m_mainPageCache;
}

// about_server.txt is optional; an empty result (no file) is cached too, so the
// per-server page never keeps re-opening a file that is not there.
QString WebUi::cachedAboutHtml(const QString& slug) const
{
    {
        const QReadLocker readLock(&m_cacheLock);
        const auto it = m_aboutCache.constFind(slug);
        if (it != m_aboutCache.constEnd()) {
            return *it;
        }
    }
    const std::shared_ptr<LogStore> store = m_stores.value(slug);
    const QString htmlText = store ? store->aboutServerHtml() : QString();
    const QWriteLocker writeLock(&m_cacheLock);
    return *m_aboutCache.insert(slug, htmlText);
}

// Only successful reads are cached: a missing image stays a live 404 so that a
// newly added file is picked up without a restart (editing one still needs it).
std::optional<WebUi::CachedImage> WebUi::cachedImage(const QString& name) const
{
    {
        const QReadLocker readLock(&m_cacheLock);
        const auto it = m_imageCache.constFind(name);
        if (it != m_imageCache.constEnd()) {
            return *it;
        }
    }
    QFile f(m_dataPath + QStringLiteral("_ircabot/web/images/") + name);
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    CachedImage image;
    image.mime = QMimeDatabase().mimeTypeForFile(QFileInfo(f)).name().toUtf8();
    image.data = f.readAll();
    const QWriteLocker writeLock(&m_cacheLock);
    return *m_imageCache.insert(name, image);
}

render::Site WebUi::siteFor(const QHttpServerRequest& request) const
{
    render::Site site = m_site;
    site.theme = themeFromRequest(request);
    site.path = request.url().path();
    return site;
}

// The configured service_emoji drawn as SVG text; the glyph comes from the
// viewer's own emoji font, so nothing is fetched from the network.
QByteArray WebUi::faviconSvg() const
{
    return QByteArrayLiteral(
               "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">"
               "<text x=\"50\" y=\"52\" font-size=\"80\" text-anchor=\"middle\" "
               "dominant-baseline=\"central\">")
        + m_site.serviceEmoji.toUtf8()
        + QByteArrayLiteral("</text></svg>");
}

void WebUi::setupRoutes()
{
    m_server.route(QStringLiteral("/style.css"), []() {
        return cachedAsset(QByteArrayLiteral("text/css; charset=utf-8"), qrcFile(QStringLiteral("style.css")));
    });
    m_server.route(QStringLiteral("/favicon.svg"), [this]() {
        return cachedAsset(QByteArrayLiteral("image/svg+xml"), faviconSvg());
    });
    m_server.route(QStringLiteral("/favicon.ico"), [this]() {
        return cachedAsset(QByteArrayLiteral("image/svg+xml"), faviconSvg());
    });
    if (!m_site.realtimeDisabled) {
        m_server.route(QStringLiteral("/live.js"), []() {
            return cachedAsset(QByteArrayLiteral("text/javascript; charset=utf-8"),
                               qrcFile(QStringLiteral("live.js")));
        });
    }

    m_server.route(QStringLiteral("/~theme/<arg>"),
                   [](const QString& mode, const QHttpServerRequest& request) {
        return themeRedirect(mode, request.query());
    });

    if (m_voiceGate && m_voiceGate->enabled()) {
        m_server.route(QStringLiteral("/~captcha/<arg>/<arg>/<arg>"),
                       [this](const QString& server, const QString& nick, const QString& hostHash,
                              const QHttpServerRequest& request) {
            const bool isPost = request.method() == QHttpServerRequest::Method::Post;
            const QByteArray body = request.body();
            return QtConcurrent::run([this, server, nick, hostHash, isPost, body, site = siteFor(request)] {
                return serveCaptcha(site, server, nick, hostHash, isPost, body);
            });
        });
    }

    m_server.route(QStringLiteral("/"), [this](const QHttpServerRequest& request) {
        return QtConcurrent::run([this, site = siteFor(request)] {
            m_state->countRequest();
            return html(render::mainPage(site, readMainPageText()));
        });
    });

    m_server.route(QStringLiteral("/~images/<arg>"), [this](const QUrl& rest, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, rest, site = siteFor(request)] {
            const QString name = rest.path();
            if (name.contains(QStringLiteral("..")) || name.startsWith('/')) {
                return html(render::errorPage(site, QStringLiteral("403"), QStringLiteral("Forbidden")),
                            QHttpServerResponse::StatusCode::Forbidden);
            }
            const std::optional<CachedImage> image = cachedImage(name);
            if (!image) {
                return html(render::errorPage(site, QStringLiteral("404"), QStringLiteral("Image not found")),
                            QHttpServerResponse::StatusCode::NotFound);
            }
            return cachedAsset(image->mime, image->data);
        });
    });

    // Real time reading: the only pages with JavaScript
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

        m_server.route(QStringLiteral("/~api/<arg>/<arg>"),
                       [this](const QString& slug, const QString& channel, const QHttpServerRequest& request) {
            return QtConcurrent::run([this, slug, channel,
                                      after = request.query().queryItemValue(QStringLiteral("after")).toULongLong()] {
                return serveApi(slug, channel, after);
            });
        });
    }

    m_server.route(QStringLiteral("/<arg>"), [this](const QString& slug, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, site = siteFor(request)] {
            m_state->countRequest();
            bool found = false;
            const ServerSnapshot server = m_state->snapshot(slug, &found);
            if (!found || !safeSegment(slug)) {
                return html(render::errorPage(site, QStringLiteral("404"), QStringLiteral("No such server: ") + slug),
                            QHttpServerResponse::StatusCode::NotFound);
            }
            return html(render::aboutPage(site, server, cachedAboutHtml(slug)));
        });
    });

    // Channel pages: /<server>/<channel>[/yyyy[/MM[/dd[.txt]]]]
    m_server.route(QStringLiteral("/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, query = request.query(), site = siteFor(request)] {
            return servePage(site, slug, channel, {}, {}, {}, query);
        });
    });
    m_server.route(QStringLiteral("/<arg>/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QString& year,
                          const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, year, query = request.query(), site = siteFor(request)] {
            return servePage(site, slug, channel, year, {}, {}, query);
        });
    });
    m_server.route(QStringLiteral("/<arg>/<arg>/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QString& year, const QString& month,
                          const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, year, month, query = request.query(), site = siteFor(request)] {
            return servePage(site, slug, channel, year, month, {}, query);
        });
    });
    m_server.route(QStringLiteral("/<arg>/<arg>/<arg>/<arg>/<arg>"),
                   [this](const QString& slug, const QString& channel, const QString& year,
                          const QString& month, const QString& day, const QHttpServerRequest& request) {
        return QtConcurrent::run([this, slug, channel, year, month, day, query = request.query(), site = siteFor(request)] {
            return servePage(site, slug, channel, year, month, day, query);
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
    // A .txt day log is a raw-log grab, not a page view: give it its own tally
    // so the main-page breakdown does not fold plain-text hits into html.
    QString day = dayRaw;
    const bool plainText = day.endsWith(QStringLiteral(".txt"));
    if (plainText) {
        day.chop(4);
        m_state->countTxtRequest();
    } else {
        m_state->countRequest();
    }

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

    // The date segments select the day to render and also scope a search, so
    // validate them up front: both paths feed them to the filesystem.
    if ((!year.isEmpty() && !safeSegment(year)) || (!month.isEmpty() && !safeSegment(month))) {
        return notFound(QStringLiteral("Bad date"));
    }
    if (!day.isEmpty() && !safeSegment(day)) {
        return notFound(QStringLiteral("Bad date"));
    }

    // Search: /<server>/<channel>[/yyyy[/MM[/dd]]]?toSearch=...&isRegexp=on
    // The date path anchors the scan to where the reader is (whole channel, a
    // year, a month or one day), so a huge chronicle stays searchable within
    // the hit limit instead of the cap being spent on recent matches alone.
    const QString searchQuery = query.queryItemValue(QStringLiteral("toSearch"), QUrl::FullyDecoded)
                                    .replace('+', ' ')
                                    .trimmed();
    if (!searchQuery.isEmpty()) {
        const bool regexp = query.queryItemValue(QStringLiteral("isRegexp")) == QStringLiteral("on");
        const SearchResult result = store.search(channel, searchQuery, regexp, year, month, day);
        return html(render::searchPage(site, server, channel, year, month, day,
                                       searchQuery, regexp, result));
    }

    if (year.isEmpty()) {
        return html(render::calendarPage(site, server, channel, store));
    }
    if (month.isEmpty()) {
        return html(render::yearPage(site, server, channel, store, year));
    }
    if (day.isEmpty()) {
        return html(render::monthPage(site, server, channel, store, year, month));
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

QHttpServerResponse WebUi::serveApi(const QString& slug, const QString& channel, quint64 afterId)
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

QHttpServerResponse WebUi::serveCaptcha(const render::Site& site, const QString& server,
                                        const QString& nick, const QString& hostHash,
                                        bool isPost, const QByteArray& body)
{
    m_state->countRequest();

    static const QRegularExpression hexHash(QStringLiteral("^[0-9a-f]{32}$"));
    if (!m_voiceGate || !m_voiceGate->enabled()) {
        return html(render::errorPage(site, QStringLiteral("404"), QStringLiteral("Voice gate is disabled")),
                    QHttpServerResponse::StatusCode::NotFound);
    }
    if (!safeSegment(server) || !safeNick(nick) || !hexHash.match(hostHash).hasMatch()) {
        return html(render::errorPage(site, QStringLiteral("400"), QStringLiteral("Bad request")),
                    QHttpServerResponse::StatusCode::BadRequest);
    }
    bool found = false;
    const ServerSnapshot snap = m_state->snapshot(server, &found);
    if (!found) {
        return html(render::errorPage(site, QStringLiteral("404"), QStringLiteral("No such server: ") + server),
                    QHttpServerResponse::StatusCode::NotFound);
    }
    const QString& serverName = snap.displayName;
    // The nonce is bound to server + nick + host hash, so it cannot be replayed
    // for a different server, nick or host.
    const QString identity = server + '/' + nick + '/' + hostHash;

    // Already voiced (this exact nick+host on this server): no fresh captcha.
    if (m_voiceGate->isVerified(server, nick, hostHash)) {
        return html(render::captchaPage(
            site, server, serverName, nick, hostHash, QString(), QString(),
            QStringLiteral("You are already verified - you have a voice on moderated channels in ") + serverName + '.',
            true));
    }

    // Checking presence is cheaper than drawing a captcha, so do it first: refuse
    // to issue or accept one unless this exact nick+host is online here.
    if (!m_voiceGate->isPresent(server, nick, hostHash)) {
        return html(render::captchaPage(
            site, server, serverName, nick, hostHash, QString(), QString(),
            QStringLiteral("This nick is not online on ") + serverName + QStringLiteral(" - join the channel first."),
            false));
    }

    const int length = m_voiceGate->config().captchaLength;

    if (isPost) {
        const QUrlQuery form(QString::fromUtf8(body));
        const QString nonce = form.queryItemValue(QStringLiteral("nonce"), QUrl::FullyDecoded);
        const QString answer = form.queryItemValue(QStringLiteral("answer"), QUrl::FullyDecoded);
        if (m_captcha.verify(identity, nonce, answer)) {
            m_voiceGate->reportSolved(server, nick, hostHash);
            return html(render::captchaPage(
                site, server, serverName, nick, hostHash, QString(), QString(),
                QStringLiteral("Correct. You will be voiced on moderated channels in ") + serverName
                    + QStringLiteral(" shortly."),
                true));
        }
        const Captcha::Challenge c = m_captcha.issue(identity, length, CAPTCHA_TTL_SEC);
        return html(render::captchaPage(site, server, serverName, nick, hostHash, c.answer, c.nonce,
                                        QStringLiteral("Wrong answer, please try again."), false));
    }

    const Captcha::Challenge c = m_captcha.issue(identity, length, CAPTCHA_TTL_SEC);
    return html(render::captchaPage(site, server, serverName, nick, hostHash, c.answer, c.nonce, QString(), false));
}

} // namespace ircabot
