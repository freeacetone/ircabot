/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "VoiceGate.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutexLocker>

namespace ircabot {

namespace {

// A just-solved captcha waits at most this long for the IRC side to bind a host
// (the user is expected to be online in a gated channel when they solve it).
constexpr qint64 SOLVED_TTL_SEC = 3600;

qint64 nowSec()
{
    return QDateTime::currentSecsSinceEpoch();
}

qint64 readNum(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        return 0;
    }
    bool ok = false;
    const qint64 value = f.readAll().trimmed().toLongLong(&ok);
    return ok ? value : 0;
}

void writeNum(const QString& file, qint64 value)
{
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QByteArray::number(value));
    }
}

} // namespace

VoiceGate::VoiceGate(const QString& dataPath, const VoiceGateConfig& config, const QString& captchaBaseUrl)
    : m_root(dataPath + QStringLiteral("_voicegate/")),
      m_captchaBase(captchaBaseUrl),
      m_config(config)
{
    while (m_captchaBase.endsWith('/')) {
        m_captchaBase.chop(1);
    }
}

QString VoiceGate::hostHash(const QString& host)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(host.toLower().toUtf8(), QCryptographicHash::Md5).toHex());
}

QString VoiceGate::folderNick(const QString& nick)
{
    QString out;
    out.reserve(nick.size());
    for (const QChar c : nick.toLower()) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            out += c;
        } else {
            out += QLatin1Char('_'); // keep the folder name filesystem-safe
        }
    }
    return out.isEmpty() ? QStringLiteral("_") : out;
}

QString VoiceGate::captchaUrl(const QString& server, const QString& nick, const QString& host) const
{
    return m_captchaBase + QStringLiteral("/~captcha/") + server + '/' + nick + '/' + hostHash(host);
}

namespace {
QString solvedKey(const QString& server, const QString& nick, const QString& hostHash)
{
    return server + '\n' + nick.toLower() + '\n' + hostHash;
}
} // namespace

QString VoiceGate::serverDir(const QString& server) const
{
    return m_root + server + '/';
}

QString VoiceGate::recordDir(const QString& server, const QString& nick, const QString& host) const
{
    return serverDir(server) + folderNick(nick) + '_' + hostHash(host) + '/';
}

bool VoiceGate::isGranted(const QString& server, const QString& nick, const QString& host) const
{
    const QMutexLocker locker(&m_mutex);
    const QString dir = recordDir(server, nick, host);
    const qint64 grantedAt = readNum(dir + QStringLiteral("granted_at"));
    if (grantedAt <= 0) {
        return false;
    }
    const qint64 offlineSince = readNum(dir + QStringLiteral("offline_since"));
    if (offlineSince > 0) {
        const qint64 ttl = static_cast<qint64>(m_config.offlineTtlHours) * 3600;
        if (nowSec() - offlineSince > ttl) {
            return false;
        }
    }
    return true;
}

void VoiceGate::grant(const QString& server, const QString& nick, const QString& host)
{
    const QMutexLocker locker(&m_mutex);
    const QString dir = recordDir(server, nick, host);
    QDir().mkpath(dir);
    if (readNum(dir + QStringLiteral("granted_at")) <= 0) {
        writeNum(dir + QStringLiteral("granted_at"), nowSec());
    }
    writeNum(dir + QStringLiteral("offline_since"), 0);
}

void VoiceGate::markOnline(const QString& server, const QString& nick, const QString& host)
{
    const QMutexLocker locker(&m_mutex);
    const QString dir = recordDir(server, nick, host);
    if (!QFile::exists(dir + QStringLiteral("granted_at"))) {
        return;
    }
    // Only touch the file on the offline -> online transition; this is called
    // every tick, and rewriting "0" each time would be needless churn.
    if (readNum(dir + QStringLiteral("offline_since")) != 0) {
        writeNum(dir + QStringLiteral("offline_since"), 0);
    }
}

void VoiceGate::markOffline(const QString& server, const QString& nick, const QString& host)
{
    const QMutexLocker locker(&m_mutex);
    const QString dir = recordDir(server, nick, host);
    if (readNum(dir + QStringLiteral("granted_at")) <= 0) {
        return; // only voiced users have a TTL to start
    }
    if (readNum(dir + QStringLiteral("offline_since")) <= 0) {
        writeNum(dir + QStringLiteral("offline_since"), nowSec());
    }
}

bool VoiceGate::pmDue(const QString& server, const QString& nick, const QString& host)
{
    const QMutexLocker locker(&m_mutex);
    const QString dir = recordDir(server, nick, host);
    QDir().mkpath(dir);
    const QString file = dir + QStringLiteral("last_pm");
    const qint64 last = readNum(file);
    const qint64 interval = static_cast<qint64>(m_config.pmIntervalHours) * 3600;
    if (last > 0 && nowSec() - last < interval) {
        return false;
    }
    writeNum(file, nowSec());
    return true;
}

void VoiceGate::sweep(const QString& server)
{
    const QMutexLocker locker(&m_mutex);
    const qint64 ttl = static_cast<qint64>(m_config.offlineTtlHours) * 3600;
    const qint64 now = nowSec();
    QDir dir(serverDir(server));
    const QStringList records = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& name : records) {
        const QString recPath = serverDir(server) + name + '/';
        const qint64 grantedAt = readNum(recPath + QStringLiteral("granted_at"));
        const qint64 offlineSince = readNum(recPath + QStringLiteral("offline_since"));
        const qint64 lastPm = readNum(recPath + QStringLiteral("last_pm"));

        bool drop = false;
        if (grantedAt > 0) {
            drop = offlineSince > 0 && now - offlineSince > ttl;
        } else {
            drop = lastPm <= 0 || now - lastPm > ttl; // stale/garbage pending record
        }
        if (drop) {
            QDir(recPath).removeRecursively();
        }
    }
}

void VoiceGate::reportSolved(const QString& server, const QString& nick, const QString& hostHash)
{
    const QMutexLocker locker(&m_mutex);
    m_solved.insert(solvedKey(server, nick, hostHash), nowSec());
}

bool VoiceGate::consumeSolved(const QString& server, const QString& nick, const QString& hostHash)
{
    const QMutexLocker locker(&m_mutex);
    const auto it = m_solved.find(solvedKey(server, nick, hostHash));
    if (it == m_solved.end()) {
        return false;
    }
    const bool fresh = nowSec() - it.value() <= SOLVED_TTL_SEC;
    m_solved.erase(it);
    return fresh;
}

bool VoiceGate::isVerified(const QString& server, const QString& nick, const QString& hostHash) const
{
    const QMutexLocker locker(&m_mutex);
    const auto it = m_solved.constFind(solvedKey(server, nick, hostHash));
    if (it != m_solved.constEnd() && nowSec() - it.value() <= SOLVED_TTL_SEC) {
        return true;
    }
    // The exact (nick, host) grant record on this server - a direct match, no
    // scan and no cross-user nick collision.
    const QString dir = serverDir(server) + folderNick(nick) + '_' + hostHash + '/';
    if (readNum(dir + QStringLiteral("granted_at")) <= 0) {
        return false;
    }
    const qint64 offlineSince = readNum(dir + QStringLiteral("offline_since"));
    const qint64 ttl = static_cast<qint64>(m_config.offlineTtlHours) * 3600;
    return offlineSince <= 0 || nowSec() - offlineSince <= ttl;
}

} // namespace ircabot
