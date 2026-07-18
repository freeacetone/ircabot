/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "LogStore.h"

#include "LogCache.h"
#include "Util.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace ircabot {

namespace {

QStringList sortedNumericEntries(const QString& path, int width)
{
    QStringList result = QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    result.erase(std::remove_if(result.begin(), result.end(), [width](const QString& e) {
                     QString name = e;
                     name.remove(QStringLiteral(".txt"));
                     if (name.size() != width) {
                         return true;
                     }
                     bool ok = false;
                     name.toInt(&ok);
                     return !ok;
                 }),
                 result.end());
    for (QString& e : result) {
        e.remove(QStringLiteral(".txt"));
    }
    return result;
}

} // namespace

LogStore::LogStore(const QString& dataPath, const QString& serverSlug, const QStringList& channels,
                   LogCache* cache)
    : m_serverDir(dataPath + serverSlug + '/'),
      m_serverSlug(serverSlug),
      m_cache(cache)
{
    for (const QString& ch : channels) {
        QDir().mkpath(channelDir(ch));
    }

    const QString aboutPath = m_serverDir + QStringLiteral("about_server.txt");
    if (!QFile::exists(aboutPath)) {
        QFile about(aboutPath);
        if (about.open(QIODevice::WriteOnly)) {
            about.write("# Server description file.\n"
                        "# HTML is supported. For line breaks, use <br>.\n\n"
                        "<center>\xC2\xAF\\_(\xE3\x83\x84)_/\xC2\xAF</center>\n");
        } else {
            qWarning().noquote() << "Can't create" << aboutPath;
        }
    }
}

QString LogStore::channelDir(const QString& channel) const
{
    QString ch = channel;
    ch.remove('#');
    return m_serverDir + ch + '/';
}

QString LogStore::dayPath(const QString& channel, const QDate& date) const
{
    return channelDir(channel) + date.toString(QStringLiteral("yyyy/MM/dd")) + QStringLiteral(".txt");
}

void LogStore::append(const QString& channel, const QString& nick, const QString& text)
{
    QString line = text;
    line.remove('\r');
    line.remove('\n');
    if (line.trimmed().isEmpty()) {
        return;
    }

    const QDate today = util::currentLogDate();
    const QString path = dayPath(channel, today);
    QDir().mkpath(path.left(path.lastIndexOf('/')));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        qWarning().noquote() << "Can't open log file for appending:" << path;
        return;
    }
    file.write('[' + nick.toUtf8() + "] " + line.toUtf8() + '\n');
}

QStringList LogStore::years(const QString& channel) const
{
    return sortedNumericEntries(channelDir(channel), 4);
}

QStringList LogStore::months(const QString& channel, const QString& year) const
{
    return sortedNumericEntries(channelDir(channel) + year, 2);
}

QStringList LogStore::days(const QString& channel, const QString& year, const QString& month) const
{
    return sortedNumericEntries(channelDir(channel) + year + '/' + month, 2);
}

QList<DayEntry> LogStore::dayEntries(const QString& channel, const QString& year, const QString& month) const
{
    QList<DayEntry> result;
    const QString dir = channelDir(channel) + year + '/' + month + '/';
    const QStringList dayList = days(channel, year, month);
    for (const QString& d : dayList) {
        result.push_back({d, QFileInfo(dir + d + QStringLiteral(".txt")).size()});
    }
    return result;
}

QList<MonthEntry> LogStore::monthEntries(const QString& channel, const QString& year) const
{
    QList<MonthEntry> result;
    const QStringList monthList = months(channel, year);
    for (const QString& m : monthList) {
        MonthEntry entry;
        entry.month = m;
        const QList<DayEntry> dayList = dayEntries(channel, year, m);
        entry.dayCount = static_cast<int>(dayList.size());
        for (const DayEntry& d : dayList) {
            entry.bytes += d.bytes;
        }
        result.push_back(entry);
    }
    return result;
}

bool LogStore::dayExists(const QString& channel, const QDate& date) const
{
    return QFile::exists(dayPath(channel, date));
}

QDate LogStore::adjacentDay(const QString& channel, const QDate& from, bool forward) const
{
    // Walk the on-disk date index instead of probing day by day:
    // a gap of months between logged days is normal for quiet channels.
    const QStringList yearList = years(channel);

    QList<QDate> candidates;
    for (const QString& y : yearList) {
        if (forward && y.toInt() < from.year()) {
            continue;
        }
        if (!forward && y.toInt() > from.year()) {
            continue;
        }
        const QStringList monthList = months(channel, y);
        for (const QString& m : monthList) {
            const QStringList dayList = days(channel, y, m);
            for (const QString& d : dayList) {
                const QDate date(y.toInt(), m.toInt(), d.toInt());
                if (!date.isValid()) {
                    continue;
                }
                if (forward && date > from) {
                    candidates.push_back(date);
                }
                if (!forward && date < from) {
                    candidates.push_back(date);
                }
            }
        }
    }
    if (candidates.isEmpty()) {
        return QDate();
    }
    return forward ? *std::min_element(candidates.begin(), candidates.end())
                   : *std::max_element(candidates.begin(), candidates.end());
}

LogLine LogStore::parseLine(const QString& raw)
{
    LogLine result;
    if (raw.startsWith('[')) {
        const qsizetype close = raw.indexOf(QStringLiteral("] "));
        if (close > 0) {
            result.nick = raw.mid(1, close - 1);
            result.text = raw.mid(close + 2);
            return result;
        }
    }
    result.text = raw; // malformed line: show as is, without a nickname
    return result;
}

QByteArray LogStore::dayBytes(const QString& channel, const QDate& date, bool store) const
{
    const QString path = dayPath(channel, date);
    const auto readFromDisk = [&path] {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    };
    // No cache, or today's still-growing file: always read straight from disk.
    if (!m_cache || date >= util::currentLogDate()) {
        return readFromDisk();
    }
    return m_cache->get(path, store, readFromDisk);
}

QList<LogLine> LogStore::readDay(const QString& channel, const QDate& date) const
{
    QList<LogLine> result;
    const QList<QByteArray> lines = dayBytes(channel, date, true).split('\n');
    for (const QByteArray& raw : lines) {
        QString line = QString::fromUtf8(raw);
        while (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        result.push_back(parseLine(line));
    }
    return result;
}

QByteArray LogStore::readDayRaw(const QString& channel, const QDate& date) const
{
    return dayBytes(channel, date, true);
}

QString LogStore::aboutServerHtml() const
{
    QFile file(m_serverDir + QStringLiteral("about_server.txt"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QString result;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine());
        if (line.startsWith('#')) {
            continue;
        }
        result += line;
    }
    return result.trimmed();
}

SearchResult LogStore::search(const QString& channel, const QString& query, bool regexp,
                              const QString& scopeYear, const QString& scopeMonth,
                              const QString& scopeDay) const
{
    SearchResult result;

    QRegularExpression rgx;
    if (regexp) {
        rgx = QRegularExpression(query, QRegularExpression::CaseInsensitiveOption);
        if (!rgx.isValid()) {
            result.badPattern = true;
            return result;
        }
        rgx.optimize();
    }

    QElapsedTimer clock;
    clock.start();

    // The scope pins the scan to a subtree (whole channel, one year, one month
    // or a single day); within it, newest first, as recent logs are what people
    // search for. A fixed level is used as is; the rest is listed and reversed.
    QStringList yearList;
    if (scopeYear.isEmpty()) {
        yearList = years(channel);
        std::reverse(yearList.begin(), yearList.end());
    } else {
        yearList = {scopeYear};
    }

    for (const QString& y : yearList) {
        QStringList monthList;
        if (scopeMonth.isEmpty()) {
            monthList = months(channel, y);
            std::reverse(monthList.begin(), monthList.end());
        } else {
            monthList = {scopeMonth};
        }
        for (const QString& m : monthList) {
            QStringList dayList;
            if (scopeDay.isEmpty()) {
                dayList = days(channel, y, m);
                std::reverse(dayList.begin(), dayList.end());
            } else {
                dayList = {scopeDay};
            }
            for (const QString& d : dayList) {
                if (clock.elapsed() > SEARCH_TIME_BUDGET_MS) {
                    result.timedOut = true;
                    return result;
                }
                const QDate date(y.toInt(), m.toInt(), d.toInt());
                if (!date.isValid()) {
                    continue;
                }
                ++result.scannedDays;

                // Scan reads through the cache but does not populate it, so a
                // deep search cannot evict the archive days people are reading.
                const QList<QByteArray> lines = dayBytes(channel, date, false).split('\n');
                int lineNumber = 0;
                for (const QByteArray& raw : lines) {
                    QString line = QString::fromUtf8(raw);
                    ++lineNumber;
                    while (line.endsWith('\r')) {
                        line.chop(1);
                    }
                    if (line.isEmpty()) {
                        continue;
                    }
                    const bool matched = regexp ? rgx.match(line).hasMatch()
                                                : line.contains(query, Qt::CaseInsensitive);
                    if (!matched) {
                        continue;
                    }

                    const LogLine parsed = parseLine(line);
                    result.hits.push_back({date, lineNumber, parsed.nick, parsed.text});
                    if (result.hits.size() >= SEARCH_MAX_HITS) {
                        result.truncated = true;
                        return result;
                    }
                }
            }
        }
    }
    return result;
}

} // namespace ircabot
