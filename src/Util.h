/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QDate>
#include <QString>

namespace ircabot::util {

// Day-rotation timezone. Set once at startup from config (default: UTC).
// Every "current day / current time" decision routes through the helpers
// below, so log rotation, the "today" links and the daily request counter
// all agree on where a day starts.
void setLogLocalTime(bool enabled);
bool logLocalTime();

// "Today" in the configured logging timezone (UTC unless local time is on).
QDate currentLogDate();

// Current wall clock in the logging timezone, e.g. "2026-07-18 02:03:04 UTC"
// (the " UTC" suffix is dropped when local time is configured).
QString currentLogTimeString();

// "Ilita IRC" -> "ilita_irc" (folder name and URL path segment)
QString slugify(const QString& name);

// HTML-escape, then turn http(s):// URLs into <a> links
QString escapeAndLinkify(const QString& text);

// Deterministic hue (0-359) for a nickname. Saturation and lightness are
// theme-dependent and applied in CSS: style="--h:<hue>".
int nickHue(const QString& nick);

// Strip IRC mode prefix (~&@%+) from a nickname
QString stripNickPrefix(const QString& nick);

} // namespace ircabot::util
