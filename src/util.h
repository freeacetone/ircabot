/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QString>

namespace ircabot::util {

// "Ilita IRC" -> "ilita_irc" (folder name and URL path segment, v1-compatible)
QString slugify(const QString& name);

// HTML-escape, then turn http(s):// URLs into <a> links
QString escapeAndLinkify(const QString& text);

// Deterministic hue (0-359) for a nickname. Saturation and lightness are
// theme-dependent and applied in CSS: style="--h:<hue>".
int nickHue(const QString& nick);

// Strip IRC mode prefix (~&@%+) from a nickname
QString stripNickPrefix(const QString& nick);

} // namespace ircabot::util
