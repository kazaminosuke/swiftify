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

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QElapsedTimer>
#include <QObject>
#include <QVariantMap>

namespace swiftify {

class AudioShaping;
class PlaybackController;

/// the player on the session bus as org.mpris.MediaPlayer2.swiftify, so media
/// keys, playerctl and shell media widgets (end4's included) can see and drive
/// it. the job SystemMediaSession.swift does with MPNowPlayingInfoCenter.
class Mpris : public QObject {
    Q_OBJECT

public:
    explicit Mpris(PlaybackController *player, AudioShaping *sound, QObject *parent = nullptr);
    ~Mpris() override;

    bool isRegistered() const { return m_registered; }
    PlaybackController *player() const { return m_player; }

    /// an audio device just arrived. headphones, car stereos and some speakers
    /// send play as they connect, and obeying that would start the player on
    /// its own, so play is refused for a few seconds after.
    void noteOutputArrived();
    bool startIsRefused() const;

    QString playbackStatus() const;
    QVariantMap metadata() const;
    qlonglong positionMicroseconds() const;
    double playbackRate() const;
    void setPlaybackRate(double rate);
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool hasSong() const;
    bool canSeek() const;

signals:
    void raiseRequested();
    void quitRequested();

private:
    void update();
    void checkForJump();
    void emitPropertiesChanged(const QString &interface, const QVariantMap &changed);

    PlaybackController *m_player;
    AudioShaping *m_sound;
    bool m_registered = false;
    QElapsedTimer m_outputArrivedAt;

    // what was last published, to send only what changed
    QString m_publishedStatus;
    QVariantMap m_publishedMetadata;
    bool m_publishedCanGoNext = false;
    bool m_publishedCanGoPrevious = false;
    bool m_publishedHasSong = false;
    bool m_publishedCanSeek = false;
    double m_publishedVolume = -1;
    bool m_publishedShuffle = false;
    QString m_publishedLoop;
    qlonglong m_lastPositionUs = 0;
    QElapsedTimer m_lastPositionAt;
    quint64 m_positionRevision = 0;
    double m_publishedRate = 1.0;
    double m_lastPositionRate = 1.0;
    bool m_lastPositionWasPlaying = false;
};

/// org.mpris.MediaPlayer2
class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

public:
    explicit MprisRootAdaptor(Mpris *mpris);

    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Swiftify"); }
    QString desktopEntry() const { return QStringLiteral("swiftify"); }
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }

public slots:
    void Raise();
    void Quit();

private:
    Mpris *m_mpris;
};

/// org.mpris.MediaPlayer2.Player
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ hasSong)
    Q_PROPERTY(bool CanPause READ hasSong)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    explicit MprisPlayerAdaptor(Mpris *mpris);

    QString playbackStatus() const { return m_mpris->playbackStatus(); }
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    double rate() const { return m_mpris->playbackRate(); }
    void setRate(double rate) { m_mpris->setPlaybackRate(rate); }
    double minimumRate() const { return 0.5; }
    double maximumRate() const { return 2.0; }
    bool shuffle() const;
    void setShuffle(bool shuffle);
    QVariantMap metadata() const { return m_mpris->metadata(); }
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const { return m_mpris->positionMicroseconds(); }
    bool canGoNext() const { return m_mpris->canGoNext(); }
    bool canGoPrevious() const { return m_mpris->canGoPrevious(); }
    bool hasSong() const { return m_mpris->hasSong(); }
    bool canSeek() const { return m_mpris->canSeek(); }
    bool canControl() const { return true; }

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void OpenUri(const QString &) {}

signals:
    void Seeked(qlonglong position);

private:
    Mpris *m_mpris;
};

} // namespace swiftify
