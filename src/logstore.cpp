/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "logstore.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
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

LogStore::LogStore(const QString& dataPath, const QString& serverSlug, const QStringList& channels)
    : m_serverDir(dataPath + serverSlug + '/'),
      m_serverSlug(serverSlug)
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

    const QDate today = QDate::currentDate();
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

QList<LogLine> LogStore::readDay(const QString& channel, const QDate& date) const
{
    QList<LogLine> result;
    QFile file(dayPath(channel, date));
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        while (line.endsWith('\n') || line.endsWith('\r')) {
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
    QFile file(dayPath(channel, date));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
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

SearchResult LogStore::search(const QString& channel, const QString& query, bool regexp) const
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

    // Newest first: recent logs are what people search for
    QStringList yearList = years(channel);
    std::reverse(yearList.begin(), yearList.end());

    for (const QString& y : yearList) {
        QStringList monthList = months(channel, y);
        std::reverse(monthList.begin(), monthList.end());
        for (const QString& m : monthList) {
            QStringList dayList = days(channel, y, m);
            std::reverse(dayList.begin(), dayList.end());
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

                QFile file(dayPath(channel, date));
                if (!file.open(QIODevice::ReadOnly)) {
                    continue;
                }
                int lineNumber = 0;
                while (!file.atEnd()) {
                    QString line = QString::fromUtf8(file.readLine());
                    ++lineNumber;
                    while (line.endsWith('\n') || line.endsWith('\r')) {
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
