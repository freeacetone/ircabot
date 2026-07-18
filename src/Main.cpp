/*
 * IRCaBot Reborn - IRC chat logger with a JS-free web interface.
 * Copyright (C) acetone, 2021-2026. GPLv3.
 */

#include "Config.h"
#include "IrcClient.h"
#include "LogStore.h"
#include "State.h"
#include "Util.h"
#include "Version.h"
#include "VoiceGate.h"
#include "WebUi.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

using namespace ircabot;

namespace {

// Self-pipe: convert SIGTERM/SIGINT into a clean event loop exit, so that
// IrcClient destructors run and send QUIT to the servers
int signalSocketPair[2] = {-1, -1};

void unixSignalHandler(int)
{
    const char byte = 1;
    const ssize_t unused = ::write(signalSocketPair[1], &byte, 1);
    (void)unused;
}

void installSignalHandlers(QCoreApplication& app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalSocketPair) != 0) {
        qWarning().noquote() << "Can't install signal handlers (socketpair failed)";
        return;
    }
    static QSocketNotifier notifier(signalSocketPair[0], QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&app]() {
        qInfo().noquote() << "Shutdown signal received, closing IRC connections...";
        app.quit();
    });
    std::signal(SIGTERM, unixSignalHandler);
    std::signal(SIGINT, unixSignalHandler);
}

void printHelp()
{
    qInfo().noquote() <<
        "Usage: ircabot --config <file>\n"
        "Arguments:\n"
        "  -c --config <file>   path to configuration file\n"
        "  -e --example <file>  create example config file\n"
        "  -v --version         print version\n"
        "  -h --help            this message";
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QString configFile;
    for (int i = 1; i < argc; ++i) {
        const QString param = QString::fromUtf8(argv[i]);
        if ((param == QStringLiteral("--example") || param == QStringLiteral("-e")) && i + 1 < argc) {
            QFile out(QString::fromUtf8(argv[i + 1]));
            if (!out.open(QIODevice::WriteOnly)) {
                qCritical().noquote() << "Can't write example config to" << argv[i + 1];
                return 1;
            }
            out.write(Config::exampleText().toUtf8());
            qInfo().noquote() << "Example configuration written to" << argv[i + 1];
            return 0;
        }
        if (param == QStringLiteral("--help") || param == QStringLiteral("-h")) {
            printHelp();
            return 0;
        }
        if (param == QStringLiteral("--version") || param == QStringLiteral("-v")) {
            qInfo().noquote() << "IRCaBot" << VERSION;
            return 0;
        }
        if ((param == QStringLiteral("--config") || param == QStringLiteral("-c")) && i + 1 < argc) {
            configFile = QString::fromUtf8(argv[i + 1]);
        }
    }

    if (configFile.isEmpty()) {
        printHelp();
        return 1;
    }

    qInfo().noquote() << "IRCaBot" << VERSION << "| Source code:" << SOURCE_URL;
    qInfo().noquote() << "GPLv3 (c) acetone," << COPYRIGHT_YEARS << "\n";

    installSignalHandlers(app);

    try {
        const Config config(configFile);
        util::setLogLocalTime(config.logLocalTime());
        qInfo().noquote() << "Log day rotation timezone:"
                          << (config.logLocalTime() ? "server local time" : "UTC");
        RuntimeState state;

        // Public base URL used in captcha PMs; fall back to the bind address.
        QString captchaBase = config.voiceGate().captchaUrl;
        if (captchaBase.isEmpty()) {
            captchaBase = QStringLiteral("http://%1:%2")
                              .arg(config.bindAddress()).arg(config.bindPort());
        }
        VoiceGate voiceGate(config.dataPath(), config.voiceGate(), captchaBase);
        qInfo().noquote() << "Voice gate:" << (config.voiceGate().enabled ? "enabled" : "disabled");

        QHash<QString, std::shared_ptr<LogStore>> stores;
        QList<IrcClient*> clients;
        for (const ServerConfig& serverConfig : config.servers()) {
            auto store = std::make_shared<LogStore>(config.dataPath(), serverConfig.slug, serverConfig.channels);
            stores.insert(serverConfig.slug, store);
            clients.push_back(new IrcClient(serverConfig, &state, store.get(), &voiceGate, &app));
        }

        WebUi web(config, &state, stores, &voiceGate, &app);
        if (!web.listen()) {
            return 1;
        }

        for (IrcClient* client : clients) {
            client->start();
        }

        const int rc = app.exec();
        // Destroy the clients (each sends a QUIT) while the RuntimeState, stores
        // and voice gate they hold pointers to are still alive - otherwise the
        // app would delete them last, after those locals have unwound.
        qDeleteAll(clients);
        clients.clear();
        return rc;
    } catch (const std::exception& e) {
        qCritical().noquote() << "Fatal:" << e.what();
        return 1;
    }
}
