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

#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QQueue>
#include <QThreadPool>
#include <QUrl>
#include <QCoroTask>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace swiftify {

/// cover art, fetched once and kept (ArtworkDataRepository.swift).
///
/// encoded bytes come through a disk cache under ~/.cache/Swiftify and a small
/// memory cache; requests for the same url share one download. decoded images
/// are cached per size bucket, decoded off the gui thread straight at the size
/// they are shown, never at the full size spotify serves.
class ArtworkCache : public QObject {
    Q_OBJECT

public:
    using BytesCallback = std::function<void(const QByteArray &)>;
    using ImageCallback = std::function<void(const QImage &)>;

    enum class Remote { Playback, YouTube, Disabled };
    explicit ArtworkCache(QNetworkAccessManager *network, QObject *parent = nullptr,
                          Remote remote = Remote::Playback);
    static bool allowsRemote(const QUrl &url, Remote remote);
    void cancelRemote();
    ~ArtworkCache() override;

    static ArtworkCache *instance();

    /// `pixels` rounded up to 32, from 32 up to 2048
    static int bucket(int pixels);

    void bytes(const QUrl &url, BytesCallback callback);
    QCoro::Task<QByteArray> bytes(QUrl url);
    /// an image at least `pixels` on its longer side, from memory when there
    void image(const QUrl &url, int pixels, ImageCallback callback);
    /// drops a url whose bytes changed under it, as a playlist cover's does
    void invalidate(const QUrl &url);

private:
    void pumpDownloads();
    void finishDownload(const QUrl &url, const QByteArray &data);
    void deliver(const QUrl &url, const QByteArray &data);

    QNetworkAccessManager *m_network;
    const Remote m_remote;
    QThreadPool m_workers;
    QQueue<QUrl> m_waiting;
    QHash<QUrl, QNetworkReply *> m_replies;
    int m_active = 0;
    qint64 m_decodingBytes = 0;
    QCache<QUrl, QByteArray> m_encoded;
    QCache<QString, QImage> m_decoded;
    QHash<QUrl, QList<BytesCallback>> m_downloads;
    QHash<QString, QList<ImageCallback>> m_decodes;
};

/// image://art/<size>/<url> for qml images
class ArtworkImageProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

} // namespace swiftify
