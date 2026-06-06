/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "render.h"
#include "util.h"
#include "version.h"

#include <QDateTime>
#include <QUrl>

namespace ircabot::render {

namespace {

const QString BLINDED_MARKER = QStringLiteral("Blinded message");

QString esc(const QString& s)
{
    return s.toHtmlEscaped();
}

QString themeSwitcher(const Site& site)
{
    const QString back = QString::fromUtf8(
        QUrl::toPercentEncoding(site.path.isEmpty() ? QStringLiteral("/") : site.path));

    const struct { const char* mode; const char* label; } MODES[] = {
        {"auto", "auto"}, {"dark", "dark"}, {"light", "light"},
    };
    QString html = QStringLiteral("<div class=\"side-theme\">theme:");
    for (const auto& m : MODES) {
        const QString mode = QString::fromUtf8(m.mode);
        const bool current = (site.theme == mode) || (site.theme.isEmpty() && mode == QStringLiteral("auto"));
        html += QStringLiteral(" <a class=\"side-theme-link%1\" href=\"/~theme/%2?back=%3\">%4</a>")
                    .arg(current ? QStringLiteral(" cur") : QString(),
                         mode, back, QString::fromUtf8(m.label));
    }
    html += QStringLiteral("</div>\n");
    return html;
}

QString sidebar(const Site& site, const PageRef& ref)
{
    QString html;
    html += QStringLiteral(
        "<nav class=\"side\">\n"
        "<div class=\"side-top\">\n"
        "<a class=\"side-brand\" href=\"/\"><span class=\"side-brand-emoji\">%1</span> %2</a>\n"
        "<label class=\"nav-burger\" for=\"nav-toggle\" title=\"menu\">&#9776;</label>\n"
        "</div>\n"
        "<div class=\"side-body\">\n")
                .arg(site.serviceEmoji, esc(site.serviceName));

    const QList<ServerSnapshot> servers = site.state->snapshotAll();
    for (const ServerSnapshot& srv : servers) {
        html += QStringLiteral("<div class=\"side-server\">\n");
        html += QStringLiteral("<a class=\"side-server-name\" href=\"/%1\" title=\"%2\">"
                               "<span class=\"dot %3\"></span>%4</a>\n")
                    .arg(srv.slug,
                         srv.connected ? QStringLiteral("online") : QStringLiteral("offline"),
                         srv.connected ? QStringLiteral("on") : QStringLiteral("off"),
                         esc(srv.displayName));
        for (const QString& ch : srv.channels) {
            const bool active = (srv.slug == ref.slug && ch == ref.channel);
            html += QStringLiteral("<a class=\"side-chan%1\" href=\"/%2/%3\">#%4</a>\n")
                        .arg(active ? QStringLiteral(" active") : QString(),
                             srv.slug, ch, esc(ch));
        }
        html += QStringLiteral("</div>\n");
    }
    html += themeSwitcher(site);
    html += QStringLiteral("<a class=\"side-foot\" href=\"%1\" rel=\"nofollow noopener\">IRCaBot %2<br>GPLv3 &copy; acetone, %3</a>\n")
                .arg(QString::fromUtf8(SOURCE_URL), QString::fromUtf8(VERSION), QString::fromUtf8(COPYRIGHT_YEARS));
    html += QStringLiteral("</div>\n</nav>\n");
    return html;
}

QString page(const Site& site, const PageRef& ref, const QString& title,
             const QString& content, const QString& headExtra = QString())
{
    QString htmlClass;
    if (site.theme == QStringLiteral("dark")) {
        htmlClass = QStringLiteral(" class=\"force-dark\"");
    } else if (site.theme == QStringLiteral("light")) {
        htmlClass = QStringLiteral(" class=\"force-light\"");
    }

    QString html;
    html += QStringLiteral(
        "<!DOCTYPE html>\n"
        "<html lang=\"en\"%1>\n"
        "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "<meta name=\"color-scheme\" content=\"dark light\">\n"
        "<title>%2</title>\n"
        "<link rel=\"stylesheet\" href=\"/style.css\">\n"
        "<link rel=\"icon\" href=\"/favicon.svg\" type=\"image/svg+xml\">\n"
        "%3"
        "</head>\n<body>\n<div class=\"crt\"></div>\n"
        "<input type=\"checkbox\" id=\"nav-toggle\" class=\"nav-toggle\">\n"
        "<div class=\"frame\">\n")
                .arg(htmlClass, esc(title), headExtra);
    html += sidebar(site, ref);
    html += QStringLiteral("<main class=\"content\">\n") + content + QStringLiteral("</main>\n");
    html += QStringLiteral("</div>\n</body>\n</html>\n");
    return html;
}

QString channelHeader(const Site& site, const ServerSnapshot& server, const QString& channel,
                      const QString& subtitle)
{
    const ChannelSnapshot chan = site.state->channelSnapshot(server.slug, channel);

    QString html;
    html += QStringLiteral("<header class=\"chan-head\">\n<div class=\"chan-title\">\n");
    html += QStringLiteral("<h1>#%1</h1><span class=\"chan-at\">@</span>"
                           "<a class=\"chan-server\" href=\"/%2\">%3</a>\n")
                .arg(esc(channel), server.slug, esc(server.displayName));
    if (!subtitle.isEmpty()) {
        html += QStringLiteral("<span class=\"chan-sub\">%1</span>\n").arg(subtitle);
    }
    html += QStringLiteral("</div>\n");
    if (!chan.topic.isEmpty()) {
        html += QStringLiteral("<p class=\"chan-topic\">%1</p>\n").arg(util::escapeAndLinkify(chan.topic));
    }

    // Search form (plain GET, parameter names are v1-compatible)
    html += QStringLiteral(
        "<form class=\"search\" method=\"get\" action=\"/%1/%2\">\n"
        "<input class=\"search-input\" type=\"search\" name=\"toSearch\" placeholder=\"search in #%3\">\n"
        "<label class=\"search-rgx\"><input type=\"checkbox\" name=\"isRegexp\" value=\"on\"> regexp</label>\n"
        "<button class=\"search-btn\" type=\"submit\">grep</button>\n"
        "</form>\n")
                .arg(server.slug, channel, esc(channel));

    // Online list, JS-free expandable
    if (!chan.online.isEmpty()) {
        html += QStringLiteral("<details class=\"online\"><summary>online: %1</summary><div class=\"online-list\">\n")
                    .arg(chan.online.size());
        for (const QString& rawNick : chan.online) {
            const QString plain = util::stripNickPrefix(rawNick);
            const QString prefix = rawNick.size() != plain.size() ? QString(rawNick.front()) : QString();
            html += QStringLiteral("<span class=\"online-nick\" style=\"--h:%1\">%2%3</span>\n")
                        .arg(QString::number(util::nickHue(plain)), esc(prefix), esc(plain));
        }
        html += QStringLiteral("</div></details>\n");
    }
    html += QStringLiteral("</header>\n");
    return html;
}

QString logLineHtml(int number, const QString& nick, const QString& text)
{
    const QString anchor = QStringLiteral("msg%1").arg(number);
    QString body;
    if (text == BLINDED_MARKER) {
        body = QStringLiteral("<span class=\"blinded\">[blinded message]</span>");
    } else if (text.startsWith(QStringLiteral("*** ")) && text.endsWith(QStringLiteral(" ***"))) {
        body = QStringLiteral("<span class=\"action\">%1</span>")
                   .arg(util::escapeAndLinkify(text.mid(4, text.size() - 8)));
    } else {
        body = util::escapeAndLinkify(text);
    }
    return QStringLiteral(
        "<div class=\"line\" id=\"%1\">"
        "<a class=\"ln\" href=\"#%1\">%2</a>"
        "<span class=\"nick\" style=\"--h:%3\">%4</span>"
        "<span class=\"msg\">%5</span>"
        "</div>\n")
        .arg(anchor, QString::number(number), QString::number(util::nickHue(nick)), esc(nick), body);
}

} // namespace

QString mainPage(const Site& site, const QString& mainPageText)
{
    QString content;
    content += QStringLiteral("<header class=\"chan-head\"><div class=\"chan-title\">"
                              "<h1 class=\"glow\">%1</h1></div></header>\n")
                   .arg(esc(site.serviceName));

    QString welcome = mainPageText;
    welcome.replace(QStringLiteral("%LOCAL_TIME%"),
                    QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
    welcome.replace(QStringLiteral("%DAILY_REQUESTS%"),
                    QString::number(site.state->requestsServedToday()));
    content += QStringLiteral("<section class=\"panel\">%1</section>\n").arg(welcome);

    content += QStringLiteral("<section class=\"grid\">\n");
    const QList<ServerSnapshot> servers = site.state->snapshotAll();
    for (const ServerSnapshot& srv : servers) {
        content += QStringLiteral("<div class=\"card\">\n");
        content += QStringLiteral("<a class=\"card-title\" href=\"/%1\"><span class=\"dot %2\"></span>%3</a>\n")
                       .arg(srv.slug,
                            srv.connected ? QStringLiteral("on") : QStringLiteral("off"),
                            esc(srv.displayName));
        content += QStringLiteral("<div class=\"card-bot\">bot: %1</div>\n").arg(esc(srv.botNick));
        content += QStringLiteral("<div class=\"card-chans\">\n");
        for (const QString& ch : srv.channels) {
            const qsizetype online = srv.byChannel.value(ch).online.size();
            content += QStringLiteral("<a href=\"/%1/%2\">#%3<span class=\"card-online\">%4</span></a>\n")
                           .arg(srv.slug, ch, esc(ch),
                                online > 0 ? QString::number(online) : QString());
        }
        content += QStringLiteral("</div>\n</div>\n");
    }
    content += QStringLiteral("</section>\n");

    return page(site, {}, site.serviceName, content);
}

QString aboutPage(const Site& site, const ServerSnapshot& server, const QString& aboutHtml)
{
    QString content;
    content += QStringLiteral("<header class=\"chan-head\"><div class=\"chan-title\">"
                              "<h1>%1</h1><span class=\"chan-sub\">%2</span></div></header>\n")
                   .arg(esc(server.displayName),
                        server.connected ? QStringLiteral("<span class=\"dot on\"></span>connected")
                                         : QStringLiteral("<span class=\"dot off\"></span>offline"));
    content += QStringLiteral("<section class=\"panel\">%1</section>\n").arg(aboutHtml);

    content += QStringLiteral("<section class=\"panel\"><div class=\"card-chans\">\n");
    for (const QString& ch : server.channels) {
        content += QStringLiteral("<a href=\"/%1/%2\">#%3</a>\n").arg(server.slug, ch, esc(ch));
    }
    content += QStringLiteral("</div></section>\n");

    return page(site, {server.slug, {}}, server.displayName, content);
}

QString calendarPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                     const LogStore& store, const QString& openYear, const QString& openMonth)
{
    static constexpr const char* MONTH_NAMES[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                                  "jul", "aug", "sep", "oct", "nov", "dec"};
    QString content = channelHeader(site, server, channel, QStringLiteral("archive"));

    const QDate today = QDate::currentDate();
    QString quickNav = QStringLiteral("<div class=\"daynav\">\n");
    if (store.dayExists(channel, today)) {
        quickNav += QStringLiteral("<a class=\"daynav-link\" href=\"/%1/%2/%3\">today</a>\n")
                        .arg(server.slug, channel, today.toString(QStringLiteral("yyyy/MM/dd")));
    }
    if (!site.realtimeDisabled) {
        quickNav += QStringLiteral("<a class=\"daynav-link live\" href=\"/~realtime/%1/%2\">live</a>\n")
                        .arg(server.slug, channel);
    }
    quickNav += QStringLiteral("</div>\n");
    content += quickNav;

    const QStringList years = store.years(channel);
    if (years.isEmpty()) {
        content += QStringLiteral("<section class=\"panel\">No messages logged yet.</section>\n");
    }
    for (auto yearIt = years.rbegin(); yearIt != years.rend(); ++yearIt) {
        const QString& y = *yearIt;
        const bool yOpen = (y == openYear) || (openYear.isEmpty() && yearIt == years.rbegin());
        content += QStringLiteral("<details class=\"year\"%1><summary>%2</summary>\n")
                       .arg(yOpen ? QStringLiteral(" open") : QString(), y);
        const QStringList months = store.months(channel, y);
        for (auto monthIt = months.rbegin(); monthIt != months.rend(); ++monthIt) {
            const QString& m = *monthIt;
            const int monthNumber = m.toInt();
            const char* const monthName = (monthNumber >= 1 && monthNumber <= 12)
                                              ? MONTH_NAMES[monthNumber - 1] : "???";
            const bool mOpen = yOpen && ((m == openMonth) || (openMonth.isEmpty() && monthIt == months.rbegin()));
            content += QStringLiteral("<details class=\"month\"%1><summary>%2 <span class=\"month-name\">%3</span></summary>"
                                      "<div class=\"days\">\n")
                           .arg(mOpen ? QStringLiteral(" open") : QString(), m, QString::fromUtf8(monthName));
            for (const QString& d : store.days(channel, y, m)) {
                content += QStringLiteral("<a class=\"day\" href=\"/%1/%2/%3/%4/%5\">%5</a>\n")
                               .arg(server.slug, channel, y, m, d);
            }
            content += QStringLiteral("</div></details>\n");
        }
        content += QStringLiteral("</details>\n");
    }

    return page(site, {server.slug, channel}, '#' + channel + " @ " + server.displayName, content);
}

QString dayPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                const LogStore& store, const QDate& date)
{
    const QString dateStr = date.toString(QStringLiteral("yyyy-MM-dd"));
    QString content = channelHeader(site, server, channel, dateStr);

    const QString base = '/' + server.slug + '/' + channel;
    const QDate prev = store.adjacentDay(channel, date, false);
    const QDate next = store.adjacentDay(channel, date, true);
    const QDate today = QDate::currentDate();

    QString nav = QStringLiteral("<div class=\"daynav\">\n");
    nav += prev.isValid()
               ? QStringLiteral("<a class=\"daynav-link\" href=\"%1/%2\" title=\"%2\">&larr; prev</a>\n")
                     .arg(base, prev.toString(QStringLiteral("yyyy/MM/dd")))
               : QStringLiteral("<span class=\"daynav-link disabled\">&larr; prev</span>\n");
    nav += QStringLiteral("<a class=\"daynav-link\" href=\"%1/%2\">archive</a>\n")
               .arg(base, date.toString(QStringLiteral("yyyy/MM")));
    nav += next.isValid()
               ? QStringLiteral("<a class=\"daynav-link\" href=\"%1/%2\" title=\"%2\">next &rarr;</a>\n")
                     .arg(base, next.toString(QStringLiteral("yyyy/MM/dd")))
               : QStringLiteral("<span class=\"daynav-link disabled\">next &rarr;</span>\n");
    if (date != today && store.dayExists(channel, today)) {
        nav += QStringLiteral("<a class=\"daynav-link\" href=\"%1/%2\">today</a>\n")
                   .arg(base, today.toString(QStringLiteral("yyyy/MM/dd")));
    }
    nav += QStringLiteral("<a class=\"daynav-link\" href=\"%1/%2.txt\">.txt</a>\n")
               .arg(base, date.toString(QStringLiteral("yyyy/MM/dd")));
    if (!site.realtimeDisabled) {
        nav += QStringLiteral("<a class=\"daynav-link live\" href=\"/~realtime/%1/%2\">live</a>\n")
                   .arg(server.slug, channel);
    }
    nav += QStringLiteral("</div>\n");
    content += nav;

    const QList<LogLine> lines = store.readDay(channel, date);
    if (lines.isEmpty()) {
        content += QStringLiteral("<section class=\"panel\">No messages this day.</section>\n");
    } else {
        content += QStringLiteral("<section class=\"log\">\n");
        int n = 0;
        for (const LogLine& line : lines) {
            content += logLineHtml(++n, line.nick, line.text);
        }
        content += QStringLiteral("</section>\n");
    }

    return page(site, {server.slug, channel},
                '#' + channel + ' ' + dateStr + " @ " + server.displayName, content);
}

QString searchPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                   const QString& query, bool regexp, const SearchResult& result)
{
    QString content = channelHeader(site, server, channel, QStringLiteral("search"));

    QString status;
    if (result.badPattern) {
        status = QStringLiteral("invalid regular expression");
    } else {
        status = QStringLiteral("%1 match%2 for \"%3\"%4")
                     .arg(result.hits.size())
                     .arg(result.hits.size() == 1 ? QString() : QStringLiteral("es"),
                          esc(query),
                          regexp ? QStringLiteral(" (regexp)") : QString());
        if (result.truncated) {
            status += QStringLiteral(", stopped at limit");
        }
        if (result.timedOut) {
            status += QStringLiteral(", stopped by timeout");
        }
    }
    content += QStringLiteral("<div class=\"search-status\">%1</div>\n").arg(status);

    QDate currentDate;
    bool sectionOpen = false;
    const QString base = '/' + server.slug + '/' + channel;
    for (const SearchHit& hit : result.hits) {
        if (hit.date != currentDate) {
            if (sectionOpen) {
                content += QStringLiteral("</section>\n");
            }
            currentDate = hit.date;
            content += QStringLiteral("<h2 class=\"search-date\"><a href=\"%1/%2\">%3</a></h2>\n<section class=\"log\">\n")
                           .arg(base, hit.date.toString(QStringLiteral("yyyy/MM/dd")),
                                hit.date.toString(QStringLiteral("yyyy-MM-dd")));
            sectionOpen = true;
        }
        const QString lineUrl = QStringLiteral("%1/%2#msg%3")
                                    .arg(base, hit.date.toString(QStringLiteral("yyyy/MM/dd")))
                                    .arg(hit.lineNumber);
        content += QStringLiteral(
            "<div class=\"line\">"
            "<a class=\"ln\" href=\"%1\">%2</a>"
            "<span class=\"nick\" style=\"--h:%3\">%4</span>"
            "<span class=\"msg\">%5</span>"
            "</div>\n")
                       .arg(lineUrl, QString::number(hit.lineNumber),
                            QString::number(util::nickHue(hit.nick)), esc(hit.nick),
                            util::escapeAndLinkify(hit.text));
    }
    if (sectionOpen) {
        content += QStringLiteral("</section>\n");
    }

    return page(site, {server.slug, channel},
                QStringLiteral("search: %1 @ #%2").arg(query, channel), content);
}

QString livePage(const Site& site, const ServerSnapshot& server, const QString& channel)
{
    QString content = channelHeader(site, server, channel,
                                    QStringLiteral("<span class=\"live-badge\">live</span>"));

    const QDate today = QDate::currentDate();
    QString nav = QStringLiteral("<div class=\"daynav\">\n");
    nav += QStringLiteral("<a class=\"daynav-link\" href=\"/%1/%2\">archive</a>\n").arg(server.slug, channel);
    nav += QStringLiteral("<a class=\"daynav-link\" href=\"/%1/%2/%3\">today log</a>\n")
               .arg(server.slug, channel, today.toString(QStringLiteral("yyyy/MM/dd")));
    nav += QStringLiteral("<span class=\"daynav-link disabled\" id=\"live-status\">connecting...</span>\n");
    nav += QStringLiteral("</div>\n");
    content += nav;

    content += QStringLiteral("<section class=\"log\" id=\"live-log\" "
                              "data-server=\"%1\" data-channel=\"%2\"></section>\n")
                   .arg(server.slug, channel);

    return page(site, {server.slug, channel},
                QStringLiteral("live: #%1 @ %2").arg(channel, server.displayName), content,
                QStringLiteral("<script src=\"/live.js\" defer></script>\n"));
}

QString errorPage(const Site& site, const QString& title, const QString& text)
{
    const QString content = QStringLiteral(
        "<section class=\"panel error\">\n"
        "<h1 class=\"glow\">%1</h1>\n"
        "<p>%2</p>\n"
        "<p><a href=\"/\">[back to main page]</a></p>\n"
        "</section>\n")
                                .arg(esc(title), esc(text));
    return page(site, {}, title, content);
}

} // namespace ircabot::render
