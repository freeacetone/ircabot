/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "util.h"

#include <QCryptographicHash>
#include <QRegularExpression>

namespace ircabot::util {

QString slugify(const QString& name)
{
    QString result {name};
    result.replace(' ', '_');
    return result.toLower();
}

QString escapeAndLinkify(const QString& text)
{
    const QString escaped = text.toHtmlEscaped();

    // Conservative URL pattern over already-escaped text.
    // &, " and ' are escaped entities now, so exclude raw <>" only.
    static const QRegularExpression urlRgx(
        QStringLiteral(R"((https?://[^\s<>]+))"));

    QString result;
    qsizetype last = 0;
    auto it = urlRgx.globalMatch(escaped);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString url = m.captured(1);
        // Trailing punctuation is almost never part of the URL
        while (!url.isEmpty() && QStringLiteral(".,;:!?)").contains(url.back())) {
            url.chop(1);
        }
        if (url.size() < 11) { // "http://x.io"
            continue;
        }
        const qsizetype start = m.capturedStart(1);
        result += escaped.mid(last, start - last);
        result += QStringLiteral("<a href=\"%1\" rel=\"nofollow noopener\" target=\"_blank\">%1</a>").arg(url);
        last = start + url.size();
    }
    result += escaped.mid(last);
    return result;
}

int nickHue(const QString& nick)
{
    const QByteArray hash = QCryptographicHash::hash(nick.toUtf8(), QCryptographicHash::Md5);
    return static_cast<quint8>(hash[0]) * 360 / 256;
}

QString stripNickPrefix(const QString& nick)
{
    if (!nick.isEmpty() && QStringLiteral("~&@%+").contains(nick.front())) {
        return nick.mid(1);
    }
    return nick;
}

} // namespace ircabot::util
