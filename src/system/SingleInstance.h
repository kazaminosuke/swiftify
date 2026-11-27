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

#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QStringList>

namespace swiftify {

/// one swiftify per user and mode. a second launch hands its command to the first and
/// exits: no command raises the window, and the others are for compositor
/// key binds (`swiftify --immersive`, `swiftify --play-pause`).
class SingleInstance : public QObject {
    Q_OBJECT

public:
    enum class Mode { Playback, Catalogue };
    enum class Start { Primary, Forwarded, Failed };
    explicit SingleInstance(Mode mode, bool spotifyEnabled = false, QObject *parent = nullptr);

    Start start(const QStringList &arguments, bool requirePlaybackMode = false);
    /// the command a launch's arguments ask for; "raise" without one
    static QString commandFrom(const QStringList &arguments);

signals:
    void commandReceived(const QString &command);

private:
    const QString m_serverName;
    const QByteArray m_mode;
    QLockFile m_lock;
    int m_connections = 0;
    QLocalServer m_server;
};

} // namespace swiftify
