/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include <QByteArray>
#include <QString>

namespace ircabot {

// Render the captcha text as a distorted indexed-color PNG. The answer is drawn
// as pixels only - it never appears as text in the page, so the HTML source does
// not leak it. Self-contained: an embedded 5x7 font and a tiny PNG writer with a
// stored (uncompressed) zlib stream, so no QtGui and no extra libraries.
QByteArray renderCaptchaPng(const QString& text);

} // namespace ircabot
