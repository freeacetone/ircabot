/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace ircabot {

struct LogLine
{
    QString nick;
    QString text;
};

struct SearchHit
{
    QDate date;
    int lineNumber = 0; // 1-based position in the day file, used for #msgN anchors
    QString nick;
    QString text;
};

struct DayEntry
{
    QString day;       // "14"
    qint64 bytes = 0;
};

struct MonthEntry
{
    QString month;     // "05"
    int dayCount = 0;
    qint64 bytes = 0;  // total month size
};

struct SearchResult
{
    QList<SearchHit> hits;
    bool truncated = false;     // hit limit reached
    bool timedOut = false;      // time budget exhausted
    bool badPattern = false;    // invalid regular expression
    int scannedDays = 0;
};

// On-disk format:
//   <data>/<server_slug>/<channel>/<yyyy>/<MM>/<dd>.txt with lines "[nick] message"
//   <data>/<server_slug>/about_server.txt
//   <data>/main_page.txt
//
// Writes happen from the main thread (IRC clients), reads from HTTP worker
// threads. A line append is a single O_APPEND write, so readers never see
// a torn line on POSIX filesystems.
class LogStore
{
public:
    static constexpr int SEARCH_MAX_HITS = 300;
    static constexpr qint64 SEARCH_TIME_BUDGET_MS = 5000;

    LogStore(const QString& dataPath, const QString& serverSlug, const QStringList& channels);

    const QString& serverSlug() const { return m_serverSlug; }

    // Writing (main thread)
    void append(const QString& channel, const QString& nick, const QString& text);

    // Reading (any thread)
    QStringList years(const QString& channel) const;
    QStringList months(const QString& channel, const QString& year) const;
    QStringList days(const QString& channel, const QString& year, const QString& month) const;
    QList<DayEntry> dayEntries(const QString& channel, const QString& year, const QString& month) const;
    QList<MonthEntry> monthEntries(const QString& channel, const QString& year) const;
    bool dayExists(const QString& channel, const QDate& date) const;
    QDate adjacentDay(const QString& channel, const QDate& from, bool forward) const;
    QList<LogLine> readDay(const QString& channel, const QDate& date) const;
    QByteArray readDayRaw(const QString& channel, const QDate& date) const;
    QString aboutServerHtml() const;

    // Whole-channel search, newest days first. Safe to run on a worker thread.
    SearchResult search(const QString& channel, const QString& query, bool regexp) const;

    static LogLine parseLine(const QString& raw);

private:
    QString channelDir(const QString& channel) const;
    QString dayPath(const QString& channel, const QDate& date) const;

    QString m_serverDir; // <data>/<slug>/
    QString m_serverSlug;
};

} // namespace ircabot
