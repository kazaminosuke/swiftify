/*
 * Swiftify, a spotify player for desktop
 * Copyright (C) 2026  shred0s
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "system/SingleInstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QLocalSocket>

namespace swiftify {

QString SingleInstance::commandFrom(const QStringList &arguments)
{
    for (const QString &argument : arguments.mid(1)) {
        if (argument == QLatin1StringView("--immersive")) return QStringLiteral("immersive");
        if (argument == QLatin1StringView("--play-pause")) return QStringLiteral("play-pause");
        if (argument == QLatin1StringView("--next")) return QStringLiteral("next");
        if (argument == QLatin1StringView("--previous")) return QStringLiteral("previous");
        if (argument == QLatin1StringView("--stats")) return QStringLiteral("stats");
    }
    return QStringLiteral("raise");
}

namespace {

constexpr int maxConnections = 8;
constexpr int maxCommandBytes = 40;

QString runtimeDirectory()
{
    auto directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory;
}

QString serverName(SingleInstance::Mode mode)
{
    const QByteArray identity = QCryptographicHash::hash(runtimeDirectory().toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return (mode == SingleInstance::Mode::Catalogue ? QStringLiteral("swiftify-catalogue-")
                                                     : QStringLiteral("swiftify-")) + QString::fromLatin1(identity);
}

bool knownCommand(const QByteArray &command)
{
    return command == "raise" || command == "immersive" || command == "play-pause"
        || command == "next" || command == "previous" || command == "stats";
}

} // namespace

SingleInstance::SingleInstance(Mode mode, bool spotifyEnabled, QObject *parent)
    : QObject(parent)
    , m_serverName(serverName(mode))
    , m_mode(spotifyEnabled ? "spotify" : "local")
    , m_lock(QDir(runtimeDirectory()).filePath(m_serverName + QStringLiteral(".lock")))
{
    m_lock.setStaleLockTime(0);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(maxConnections);
    m_server.setListenBacklogSize(maxConnections);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = m_server.nextPendingConnection()) {
            if (m_connections >= maxConnections) {
                socket->abort();
                socket->deleteLater();
                continue;
            }
            ++m_connections;
            connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
                --m_connections;
                socket->deleteLater();
            });
            socket->setReadBufferSize(maxCommandBytes + 1);
            QTimer::singleShot(1000, socket, [socket] { socket->abort(); });
            const auto receive = [this, socket] {
                if (socket->bytesAvailable() > maxCommandBytes) {
                    socket->abort();
                    return;
                }
                if (!socket->canReadLine()) return;
                const auto line = socket->readLine(maxCommandBytes + 1).trimmed();
                const auto fields = line.split(' ');
                const bool accepted = fields.size() <= 2 && knownCommand(fields.value(0))
                    && (fields.size() == 1 || fields.at(1) == m_mode);
                if (socket->bytesAvailable() > 0) {
                    socket->abort();
                    return;
                }
                socket->write(accepted ? "ok\n" : "rejected\n");
                socket->disconnectFromServer();
                if (accepted) emit commandReceived(QString::fromLatin1(fields.at(0)));
            };
            connect(socket, &QLocalSocket::readyRead, this, receive);
            receive();
        }
    });
}

SingleInstance::Start SingleInstance::start(const QStringList &arguments, bool requirePlaybackMode)
{
    const auto directory = runtimeDirectory();
    if (directory.isEmpty() || !QDir().mkpath(directory)) return Start::Failed;

    QElapsedTimer startup;
    startup.start();
    do {
        const bool lockOwned = m_lock.tryLock(0);
        if (lockOwned && m_server.listen(m_serverName)) return Start::Primary;

        QLocalSocket socket;
        socket.setReadBufferSize(maxCommandBytes + 1);
        socket.connectToServer(m_serverName);
        if (socket.waitForConnected(100)) {
            QByteArray command = commandFrom(arguments).toLatin1();
            if (requirePlaybackMode) command += ' ' + m_mode;
            command += '\n';
            if (socket.write(command) != command.size()) return Start::Failed;
            if (socket.bytesToWrite() > 0 && !socket.waitForBytesWritten(250)) return Start::Failed;
            while (!socket.canReadLine() && !startup.hasExpired(2500)) {
                if (!socket.waitForReadyRead(qMax(1, 2500 - int(startup.elapsed())))) break;
            }
            return socket.readLine(maxCommandBytes + 1) == "ok\n" ? Start::Forwarded : Start::Failed;
        }
        if (lockOwned) {
            // Only the lock holder can remove an abandoned endpoint.
            if (socket.error() == QLocalSocket::ConnectionRefusedError
                || socket.error() == QLocalSocket::ServerNotFoundError) {
                QLocalServer::removeServer(m_serverName);
                if (m_server.listen(m_serverName)) return Start::Primary;
            }
            return Start::Failed;
        }
        if (m_lock.error() != QLockFile::LockFailedError) return Start::Failed;
        QThread::msleep(30);
    } while (!startup.hasExpired(2500));
    return Start::Failed;
}

} // namespace swiftify
