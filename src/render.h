/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include "logstore.h"
#include "state.h"

#include <QString>

namespace ircabot::render {

// Per-request rendering context. The static part is copied from WebUi,
// theme/path are filled from the request. State is read via thread-safe
// snapshots, so rendering is safe on HTTP worker threads.
struct Site
{
    QString serviceName;
    QString serviceEmoji; // HTML entity, e.g. "&#128193;"
    bool realtimeDisabled = false;
    const RuntimeState* state = nullptr;

    QString theme; // "dark", "light" or empty (auto, from prefers-color-scheme)
    QString path;  // request path, used as "back" target by the theme switcher
};

struct PageRef // what is highlighted in the sidebar
{
    QString slug;
    QString channel; // without '#'
};

QString mainPage(const Site& site, const QString& mainPageText);
QString aboutPage(const Site& site, const ServerSnapshot& server, const QString& aboutHtml);
QString calendarPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                     const LogStore& store, const QString& openYear, const QString& openMonth);
QString dayPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                const LogStore& store, const QDate& date);
QString searchPage(const Site& site, const ServerSnapshot& server, const QString& channel,
                   const QString& query, bool regexp, const SearchResult& result);
QString livePage(const Site& site, const ServerSnapshot& server, const QString& channel);
QString errorPage(const Site& site, const QString& title, const QString& text);

} // namespace ircabot::render
