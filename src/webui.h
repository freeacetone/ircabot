/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#pragma once

#include "config.h"
#include "logstore.h"
#include "render.h"
#include "state.h"

#include <QHash>
#include <QHttpServer>
#include <QObject>
#include <QUrlQuery>

#include <memory>

namespace ircabot {

// HTTP layer on top of QHttpServer. Heavy handlers (log reading, search)
// return QFuture<QHttpServerResponse> and run on the global QThreadPool,
// so the server is multithreaded and never blocks the main event loop.
class WebUi : public QObject
{
    Q_OBJECT
public:
    WebUi(const Config& config, RuntimeState* state,
          const QHash<QString, std::shared_ptr<LogStore>>& stores, QObject* parent = nullptr);

    bool listen();

private:
    void setupRoutes();
    render::Site siteFor(const QHttpServerRequest& request) const;
    QHttpServerResponse servePage(const render::Site& site,
                                  const QString& serverSlug, const QString& channel,
                                  const QString& year, const QString& month, const QString& day,
                                  const QUrlQuery& query);
    QHttpServerResponse serveAjax(const QString& serverSlug, const QString& channel, quint64 afterId);
    QString readMainPageText() const;
    void ensureMainPageFile() const;

    QString m_dataPath;
    QString m_bindAddress;
    quint16 m_bindPort;
    render::Site m_site;
    RuntimeState* m_state;
    QHash<QString, std::shared_ptr<LogStore>> m_stores;
    QHttpServer m_server;
};

} // namespace ircabot
