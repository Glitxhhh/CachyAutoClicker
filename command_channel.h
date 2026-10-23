#pragma once

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>

#include <cstdio>
#include <functional>

// ---------------------------------------------------------------------------------------------------------
// Command channel: `CachyAutoClicker --toggle` (also --start, --stop, --status) drives the running app.
// This is the fallback for desktops without a GlobalShortcuts portal: bind a key to the command in their own
// config (Sway `bindsym F6 exec ... --toggle`, niri `spawn`, KDE Custom Shortcuts, ...).
// ---------------------------------------------------------------------------------------------------------
namespace command_channel {

inline QString serverName() { return "CachyAutoClicker-" + qEnvironmentVariable("USER", "user"); }

inline bool isCommand(const QString &arg) {
    return arg == "--toggle" || arg == "--start" || arg == "--stop" || arg == "--status";
}

// Sends the command to the running app and prints its reply ("running" / "stopped").
// Exit code: 0 ok (--status: running), 1 (--status: stopped), 2 the app isn't running.
inline int runClient(const QString &command) {
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(1500)) {
        std::fprintf(stderr, "CachyAutoClicker isn't running.\n");
        return 2;
    }
    socket.write(command.mid(2).toUtf8() + "\n");
    socket.flush();
    if (!socket.waitForReadyRead(3000)) {
        std::fprintf(stderr, "No answer from CachyAutoClicker.\n");
        return 2;
    }
    const QString reply = QString::fromUtf8(socket.readLine()).trimmed();
    std::printf("%s\n", reply.toUtf8().constData());
    return command == "--status" && reply != "running" ? 1 : 0;
}

}  // namespace command_channel

class CommandServer : public QObject {
    Q_OBJECT
public:
    // handler receives "toggle" / "start" / "stop" / "status" and returns the reply ("running" or "stopped")
    CommandServer(std::function<QString(const QString &)> handler, QObject *parent)
        : QObject(parent), handle(std::move(handler)) {
        // If something already answers on the name, another copy owns the channel: leave it alone.
        QLocalSocket probe;
        probe.connectToServer(command_channel::serverName());
        if (probe.waitForConnected(200)) return;

        QLocalServer::removeServer(command_channel::serverName());   // stale socket from a crash
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(command_channel::serverName())) return;
        connect(&server, &QLocalServer::newConnection, this, &CommandServer::onConnection);
    }

private slots:
    void onConnection() {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                if (!socket->canReadLine()) return;
                const QString command = QString::fromUtf8(socket->readLine()).trimmed().toLower();
                const bool known = command == "toggle" || command == "start" || command == "stop" || command == "status";
                socket->write((known ? handle(command) : QString("error: unknown command")).toUtf8() + "\n");
                socket->flush();
                socket->disconnectFromServer();
            });
        }
    }

private:
    QLocalServer server;
    std::function<QString(const QString &)> handle;
};
