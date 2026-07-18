/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include "Config.h"

#include <QHash>
#include <QMutex>
#include <QString>

namespace ircabot {

// File-backed voice-gate database and the web<->IRC "captcha solved" bridge.
//
// Records live under <dataPath>/_voicegate/<server>/<nick>_<hosthash>/ as a few
// tiny text files: granted_at, last_pm, offline_since. The binding is deliberately
// hard: nick AND an md5 of the host, so a stolen nick alone does not inherit voice.
//
// Thread-safe. The IRC client (main thread) writes grants and PM stamps; the web
// captcha handler (worker threads) reads verification state and reports solves.
// A solve only carries the nick (the browser cannot know the IRC host), so the
// IRC side binds the host it saw for that nick when it applies the grant.
class VoiceGate
{
public:
    VoiceGate(const QString& dataPath, const VoiceGateConfig& config, const QString& captchaBaseUrl);

    const VoiceGateConfig& config() const { return m_config; }
    bool enabled() const { return m_config.enabled; }

    // Public captcha link handed to a user in the PM. Server, nick and the host
    // hash are all explicit, so the link identifies exactly one user.
    QString captchaUrl(const QString& server, const QString& nick, const QString& host) const;

    static QString hostHash(const QString& host); // md5 hex of the host
    static QString folderNick(const QString& nick); // filesystem-safe, lowercased

    // Grant state for one (server, nick, host). "Granted" means verified and not
    // expired: a voiced user keeps voice while online and for offline_ttl_hours
    // after going offline.
    bool isGranted(const QString& server, const QString& nick, const QString& host) const;
    void grant(const QString& server, const QString& nick, const QString& host);
    void markOnline(const QString& server, const QString& nick, const QString& host);
    void markOffline(const QString& server, const QString& nick, const QString& host);

    // last_pm throttle: stamps now and returns true only if a PM is due.
    bool pmDue(const QString& server, const QString& nick, const QString& host);

    // Delete expired records for a server (offline grants past TTL, stale
    // pending records). Cheap enough to call on a timer.
    void sweep(const QString& server);

    // Web -> IRC bridge, keyed by server + nick + host hash so a solve applies
    // to exactly one (server, nick, host) - no collision if a same-nick user on
    // a different host joins later. The host hash travels in the link, so the
    // browser carries it without ever knowing the real host.
    void reportSolved(const QString& server, const QString& nick, const QString& hostHash);
    bool consumeSolved(const QString& server, const QString& nick, const QString& hostHash);
    bool isVerified(const QString& server, const QString& nick, const QString& hostHash) const; // web GET

private:
    QString serverDir(const QString& server) const;
    QString recordDir(const QString& server, const QString& nick, const QString& host) const;

    QString m_root;          // <dataPath>/_voicegate/
    QString m_captchaBase;   // e.g. "http://example.com" (no trailing slash)
    VoiceGateConfig m_config;
    mutable QMutex m_mutex;
    QHash<QString, qint64> m_solved; // "server\n<lownick>\n<hosthash>" -> solve time
};

} // namespace ircabot
