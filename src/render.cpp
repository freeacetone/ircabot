/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "render.h"
#include "util.h"
#include "version.h"

#include <QDate>
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

    // Without an explicit choice the browser preference (prefers-color-scheme)
    // applies and nothing is highlighted here
    QString html = QStringLiteral("<div class=\"side-theme\">");
    for (const char* mode : {"dark", "light"}) {
        const QString modeStr = QString::fromUtf8(mode);
        html += QStringLiteral("<a class=\"side-theme-link%1\" href=\"/~theme/%2?back=%3\">%2</a> ")
                    .arg(site.theme == modeStr ? QStringLiteral(" cur") : QString(),
                         modeStr, back);
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
        // The server page itself is selected (no channel picked)
        const bool serverActive = (srv.slug == ref.slug && ref.channel.isEmpty());
        html += QStringLiteral("<div class=\"side-server\">\n");
        html += QStringLiteral("<a class=\"side-server-name%1\" href=\"/%2\" title=\"%3\">"
                               "<span class=\"dot %4\"></span>%5</a>\n")
                    .arg(serverActive ? QStringLiteral(" active") : QString(),
                         srv.slug,
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
    html += QStringLiteral("<a class=\"side-foot\" href=\"%1\" rel=\"nofollow noopener\" target=\"_blank\">IRCaBot %2<br>GPLv3 &copy; acetone, %3</a>\n")
                .arg(QString::fromUtf8(SOURCE_URL), QString::fromUtf8(VERSION), QString::fromUtf8(COPYRIGHT_YEARS));
    html += QStringLiteral("</div>\n</nav>\n");
    return html;
}

QString page(const Site& site, const PageRef& ref, const QString& title,
             const QString& content, const QString& headExtra = QString(),
             const QString& mainClass = QString())
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
    html += QStringLiteral("<main class=\"content%1\">\n").arg(mainClass.isEmpty() ? QString() : ' ' + mainClass)
          + content + QStringLiteral("</main>\n");
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

    // Search form (plain GET, parameter names are v1-compatible).
    // Input on the left; on the right a column: grep button stretched
    // to the width of the regexp checkbox under it
    html += QStringLiteral(
        "<form class=\"search\" method=\"get\" action=\"/%1/%2\">\n"
        "<input class=\"search-input\" type=\"search\" name=\"toSearch\" placeholder=\"search in #%3\">\n"
        "<div class=\"search-side\">\n"
        "<button class=\"search-btn\" type=\"submit\">grep</button>\n"
        "<label class=\"search-rgx\"><input type=\"checkbox\" name=\"isRegexp\" value=\"on\"> regexp</label>\n"
        "</div>\n"
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

QString monthName(const QString& month)
{
    static constexpr const char* MONTH_NAMES[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                                  "jul", "aug", "sep", "oct", "nov", "dec"};
    const int number = month.toInt();
    return QString::fromUtf8((number >= 1 && number <= 12) ? MONTH_NAMES[number - 1] : "???");
}

QString humanSize(qint64 bytes)
{
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
}

// Clickable path as in v1: "/" is the channel root, then /year/month/day,
// every segment except the last one is a link
QString breadcrumbs(const QString& base, const QString& year,
                    const QString& month = QString(), const QString& day = QString())
{
    QString html = QStringLiteral("<span class=\"crumbs\">");
    html += QStringLiteral("<a href=\"%1\" title=\"archive\">/</a>").arg(base);
    if (!year.isEmpty()) {
        if (month.isEmpty()) {
            html += QStringLiteral("<b>%1</b>").arg(year);
        } else {
            html += QStringLiteral("<a href=\"%1/%2\">%2</a>").arg(base, year);
            if (day.isEmpty()) {
                html += QStringLiteral("/<b>%1</b>").arg(month);
            } else {
                html += QStringLiteral("/<a href=\"%1/%2/%3\">%3</a>/<b>%4</b>")
                            .arg(base, year, month, day);
            }
        }
    }
    html += QStringLiteral("</span>\n");
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
    welcome.replace(QStringLiteral("%LOCAL_TIME%"), util::currentLogTimeString());
    // Same format as v1: "N (html) / M (ajax)" while real time mode is enabled
    QString requestsCounter = QString::number(site.state->requestsServedToday());
    if (!site.realtimeDisabled) {
        requestsCounter += QStringLiteral(" (html) / %1 (ajax)")
                               .arg(site.state->ajaxRequestsServedToday());
    }
    welcome.replace(QStringLiteral("%DAILY_REQUESTS%"), requestsCounter);
    content += QStringLiteral("<section class=\"panel\">%1</section>\n").arg(welcome);

    // No server list here: the sidebar menu already shows it
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
                     const LogStore& store)
{
    QString content = channelHeader(site, server, channel, QStringLiteral("archive"));

    const QDate today = util::currentLogDate();
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
    } else {
        content += QStringLiteral("<section class=\"panel arch\">\n");
        for (auto yearIt = years.rbegin(); yearIt != years.rend(); ++yearIt) {
            const QList<MonthEntry> monthList = store.monthEntries(channel, *yearIt);
            int dayCount = 0;
            qint64 bytes = 0;
            for (const MonthEntry& m : monthList) {
                dayCount += m.dayCount;
                bytes += m.bytes;
            }
            content += QStringLiteral("<a class=\"arch-row\" href=\"/%1/%2/%3\">"
                                      "<span class=\"arch-name\">%3</span>"
                                      "<span class=\"arch-meta\">%4 month%5, %6 day%7, %8</span></a>\n")
                           .arg(server.slug, channel, *yearIt,
                                QString::number(monthList.size()),
                                monthList.size() == 1 ? QString() : QStringLiteral("s"),
                                QString::number(dayCount),
                                dayCount == 1 ? QString() : QStringLiteral("s"),
                                humanSize(bytes));
        }
        content += QStringLiteral("</section>\n");
    }

    return page(site, {server.slug, channel}, '#' + channel + " @ " + server.displayName, content);
}

QString yearPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                 const LogStore& store, const QString& year)
{
    QString content = channelHeader(site, server, channel, QStringLiteral("archive"));

    const QString base = '/' + server.slug + '/' + channel;
    QString nav = QStringLiteral("<div class=\"daynav\">\n");
    nav += breadcrumbs(base, year);
    nav += QStringLiteral("</div>\n");
    content += nav;

    const QList<MonthEntry> monthList = store.monthEntries(channel, year);
    if (monthList.isEmpty()) {
        content += QStringLiteral("<section class=\"panel\">No logs for this year.</section>\n");
    } else {
        content += QStringLiteral("<section class=\"panel arch\">\n");
        for (auto it = monthList.rbegin(); it != monthList.rend(); ++it) {
            content += QStringLiteral("<a class=\"arch-row\" href=\"%1/%2/%3\">"
                                      "<span class=\"arch-name\">%3 <span class=\"month-name\">%4</span></span>"
                                      "<span class=\"arch-meta\">%5 day%6, %7</span></a>\n")
                           .arg(base, year, it->month,
                                monthName(it->month),
                                QString::number(it->dayCount),
                                it->dayCount == 1 ? QString() : QStringLiteral("s"),
                                humanSize(it->bytes));
        }
        content += QStringLiteral("</section>\n");
    }

    return page(site, {server.slug, channel},
                '#' + channel + ' ' + year + " @ " + server.displayName, content);
}

QString monthPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                  const LogStore& store, const QString& year, const QString& month)
{
    QString content = channelHeader(site, server, channel, QStringLiteral("archive"));

    const QString base = '/' + server.slug + '/' + channel;
    QString nav = QStringLiteral("<div class=\"daynav\">\n");
    nav += breadcrumbs(base, year, month);
    nav += QStringLiteral("</div>\n");
    content += nav;

    const QList<DayEntry> dayList = store.dayEntries(channel, year, month);
    if (dayList.isEmpty()) {
        content += QStringLiteral("<section class=\"panel\">No logs for this month.</section>\n");
    } else {
        content += QStringLiteral("<section class=\"panel arch\">\n");
        for (auto it = dayList.rbegin(); it != dayList.rend(); ++it) {
            content += QStringLiteral("<a class=\"arch-row\" href=\"%1/%2/%3/%4\">"
                                      "<span class=\"arch-name\">%4</span>"
                                      "<span class=\"arch-meta\">%5</span></a>\n")
                           .arg(base, year, month, it->day, humanSize(it->bytes));
        }
        content += QStringLiteral("</section>\n");
    }

    return page(site, {server.slug, channel},
                '#' + channel + ' ' + year + '-' + month + " @ " + server.displayName, content);
}

QString dayPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                const LogStore& store, const QDate& date)
{
    const QString dateStr = date.toString(QStringLiteral("yyyy-MM-dd"));
    QString content = channelHeader(site, server, channel, dateStr);

    const QString base = '/' + server.slug + '/' + channel;
    const QDate prev = store.adjacentDay(channel, date, false);
    const QDate next = store.adjacentDay(channel, date, true);
    const QDate today = util::currentLogDate();

    QString nav = QStringLiteral("<div class=\"daynav\">\n");
    nav += prev.isValid()
               ? QStringLiteral("<a class=\"daynav-link\" href=\"%1/%2\" title=\"%2\">&larr; prev</a>\n")
                     .arg(base, prev.toString(QStringLiteral("yyyy/MM/dd")))
               : QStringLiteral("<span class=\"daynav-link disabled\">&larr; prev</span>\n");
    nav += breadcrumbs(base, date.toString(QStringLiteral("yyyy")),
                       date.toString(QStringLiteral("MM")), date.toString(QStringLiteral("dd")));
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
                '#' + channel + ' ' + dateStr + " @ " + server.displayName, content,
                QString(), QStringLiteral("chat"));
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

    QString nav = QStringLiteral("<div class=\"daynav\">\n");
    // live.js points it to the page the reader came from (same-origin referrer);
    // the channel archive is the no-referrer fallback
    nav += QStringLiteral("<a class=\"daynav-link\" id=\"live-back\" href=\"/%1/%2\">&larr; back</a>\n")
               .arg(server.slug, channel);
    // Animated dots, green while the network works, red otherwise (as in v1)
    nav += QStringLiteral("<span class=\"live-dots\" id=\"live-status\">.</span>\n");
    nav += QStringLiteral("</div>\n");
    content += nav;

    content += QStringLiteral("<section class=\"log\" id=\"live-log\" "
                              "data-server=\"%1\" data-channel=\"%2\"></section>\n")
                   .arg(server.slug, channel);

    return page(site, {server.slug, channel},
                QStringLiteral("live: #%1 @ %2").arg(channel, server.displayName), content,
                QStringLiteral("<script src=\"/live.js\" defer></script>\n"),
                QStringLiteral("chat"));
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
