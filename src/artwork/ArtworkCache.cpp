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

#include "artwork/ArtworkCache.h"

#include "app/Paths.h"
#include "artwork/PlaylistCovers.h"
#include "artwork/SafeImage.h"
#include "network/BoundedReply.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPointer>
#include <QThreadPool>
#include <QUrlQuery>
#include <QtConcurrent>

#include <algorithm>
#include <optional>
#include <utility>
#include <QCoroSignal>

namespace swiftify {

Q_LOGGING_CATEGORY(lcArtwork, "swiftify.artwork")

namespace {

ArtworkCache *s_instance = nullptr;

constexpr qint64 kDiskCacheBytes = 200LL * 1024 * 1024;
// the disk cache holds every cover fetched; these only spare it a read, or a
// decode, for what was shown a moment ago. what is on screen is on the gpu,
// its decoded pixels let go once there (QSG_TRANSIENT_IMAGES), so decoded
// covers kept here would only be kept twice. what reads a cover's bytes
// again (another size of it, its colours) does so within moments
constexpr int kEncodedCacheBytes = 2 * 1024 * 1024;
constexpr int kDecodedCacheBytes = 4 * 1024 * 1024;
constexpr int kMaxDownloads = 4;
constexpr int kMaxPending = 128;
constexpr int kMaxWaiters = 32;
constexpr qint64 kMaxRemoteBytes = 8 * 1024 * 1024;
constexpr qint64 kMaxDecodingBytes = 64 * 1024 * 1024;

QImage decode(const QByteArray &data, int pixels)
{
    QBuffer buffer;
    buffer.setData(data);
    const QImage image = readSafeImage(buffer, pixels);
    // the format the scene graph uploads without converting
    return image.isNull() ? image : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

ArtworkCache::ArtworkCache(QNetworkAccessManager *network, QObject *parent, Remote remote)
    : QObject(parent)
    , m_network(network)
    , m_remote(remote)
    , m_encoded(kEncodedCacheBytes)
    , m_decoded(kDecodedCacheBytes)
{
    if (!s_instance) {
        s_instance = this;
    }
    m_workers.setMaxThreadCount(2);
    auto *disk = new QNetworkDiskCache(this);
    disk->setCacheDirectory(paths::cacheDirectory() + QStringLiteral("/artwork"));
    disk->setMaximumCacheSize(kDiskCacheBytes);
    m_network->setCache(disk);
}

ArtworkCache::~ArtworkCache()
{
    for (auto *reply : std::as_const(m_replies)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ArtworkCache *ArtworkCache::instance()
{
    return s_instance;
}

int ArtworkCache::bucket(int pixels)
{
    // the size ArtworkImage asks for, rounded up to 32 as it rounds it: a
    // cover is decoded at the size it is shown, give or take a few pixels
    return (std::clamp(pixels, 1, 2048) + 31) / 32 * 32;
}

bool ArtworkCache::allowsRemote(const QUrl &url, Remote remote)
{
    if (!url.isValid() || url.scheme() != QLatin1StringView("https") || !url.userInfo().isEmpty()
        || url.port(443) != 443 || url.hasFragment() || url.toEncoded().size() > 4096) return false;
    const QString host = url.host();
    switch (remote) {
    case Remote::YouTube:
        return host.endsWith(QLatin1StringView(".ytimg.com")) || host == QLatin1StringView("yt3.ggpht.com")
            || host == QLatin1StringView("yt3.googleusercontent.com");
    case Remote::Playback:
        return host.endsWith(QLatin1StringView(".scdn.co")) || host.endsWith(QLatin1StringView(".spotifycdn.com"))
            || host.endsWith(QLatin1StringView(".stats.fm"));
    case Remote::Disabled:
        return false;
    }
    return false;
}

void ArtworkCache::bytes(const QUrl &url, BytesCallback callback)
{
    const bool local = url.isLocalFile() && url.host().isEmpty() && !url.hasQuery() && !url.hasFragment()
        && QDir::isAbsolutePath(url.toLocalFile());
    if (!local && !allowsRemote(url, m_remote)) {
        callback({});
        return;
    }
    if (const QByteArray *cached = m_encoded.object(url)) {
        callback(*cached);
        return;
    }
    if (auto pending = m_downloads.find(url); pending != m_downloads.end()) {
        if (pending->size() >= kMaxWaiters) callback({});
        else pending->append(std::move(callback));
        return;
    }
    if (m_downloads.size() >= kMaxPending) {
        callback({});
        return;
    }
    m_downloads.insert(url, {std::move(callback)});
    m_waiting.enqueue(url);
    pumpDownloads();
}

void ArtworkCache::pumpDownloads()
{
    while (m_active < kMaxDownloads && !m_waiting.isEmpty()) {
        const QUrl url = m_waiting.dequeue();
        ++m_active;
        if (url.isLocalFile()) {
            QtConcurrent::run(&m_workers, [path = url.toLocalFile()] {
                const QFileInfo info(path);
                if (!info.isFile() || info.isSymLink() || info.size() > maxImageFileBytes) return QByteArray();
                QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) return QByteArray();
                auto data = file.read(maxImageFileBytes + 1);
                return data.size() <= maxImageFileBytes ? data : QByteArray();
            }).then(this, [this, url](const QByteArray &data) { finishDownload(url, data); });
            continue;
        }

        QNetworkRequest request(url);
        privateRequest(request);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::PreferCache);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, true);
        request.setTransferTimeout(15'000);
        auto *reply = boundReply(m_network->get(request), kMaxRemoteBytes, 20'000);
        m_replies.insert(url, reply);
        connect(reply, &QNetworkReply::finished, this, [this, url, reply] {
            m_replies.remove(url);
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool usable = status >= 200 && status < 300 && reply->error() == QNetworkReply::NoError
                && replyLimit(*reply, kMaxRemoteBytes) == ReplyLimit::None;
            finishDownload(url, usable ? reply->read(kMaxRemoteBytes + 1) : QByteArray());
        });
    }
}

void ArtworkCache::cancelRemote()
{
    QQueue<QUrl> local;
    QList<QUrl> cancelled;
    while (!m_waiting.isEmpty()) {
        const auto url = m_waiting.dequeue();
        if (url.isLocalFile()) local.enqueue(url);
        else cancelled.append(url);
    }
    m_waiting = std::move(local);
    for (const auto &url : cancelled) deliver(url, {});
    for (auto *reply : m_replies.values()) reply->abort();
}

/// what a coroutine waits on while the callback form fetches
class BytesWaiter : public QObject {
    Q_OBJECT

signals:
    void done();
};

QCoro::Task<QByteArray> ArtworkCache::bytes(QUrl url)
{
    const auto waiter = std::make_shared<BytesWaiter>();
    const auto result = std::make_shared<std::optional<QByteArray>>();
    bytes(url, [waiter, result](const QByteArray &data) {
        *result = data;
        emit waiter->done();
    });
    if (!result->has_value()) {
        co_await qCoro(waiter.get(), &BytesWaiter::done);
    }
    co_return result->value_or(QByteArray());
}

void ArtworkCache::finishDownload(const QUrl &url, const QByteArray &data)
{
    --m_active;
    if (!data.isEmpty() && data.size() <= kEncodedCacheBytes) {
        m_encoded.insert(url, new QByteArray(data), int(data.size()));
    }
    deliver(url, data);
    pumpDownloads();
}

void ArtworkCache::deliver(const QUrl &url, const QByteArray &data)
{
    const QList<BytesCallback> callbacks = m_downloads.take(url);
    for (const BytesCallback &callback : callbacks) {
        callback(data);
    }
}

void ArtworkCache::image(const QUrl &url, int pixels, ImageCallback callback)
{
    const int size = bucket(pixels);
    const QString key = QString::number(size) + u':' + url.toString();
    if (const QImage *cached = m_decoded.object(key)) {
        callback(*cached);
        return;
    }
    auto pending = m_decodes.find(key);
    if (pending != m_decodes.end()) {
        if (pending->size() >= kMaxWaiters) callback({});
        else pending->append(std::move(callback));
        return;
    }
    if (m_decodes.size() >= kMaxPending) {
        callback({});
        return;
    }
    m_decodes.insert(key, {std::move(callback)});

    if (url.scheme() == QStringLiteral("swiftify-cover")) {
        const QString identity = QUrlQuery(url).queryItemValue(QStringLiteral("key"), QUrl::FullyDecoded);
        const bool dark = url.path() == QStringLiteral("/dark");
        QtConcurrent::run(&m_workers, covers::fallback, identity, dark, size)
            .then(this, [this, key](const QImage &image) {
                m_decoded.insert(key, new QImage(image), int(image.sizeInBytes()));
                const QList<ImageCallback> callbacks = m_decodes.take(key);
                for (const ImageCallback &callback : callbacks) {
                    callback(image);
                }
            });
        return;
    }

    bytes(url, [this, key, size](const QByteArray &data) {
        if (data.isEmpty() || m_decodingBytes + data.size() > kMaxDecodingBytes) {
            const QList<ImageCallback> callbacks = m_decodes.take(key);
            for (const ImageCallback &callback : callbacks) {
                callback({});
            }
            return;
        }
        const auto bytes = data.size();
        m_decodingBytes += bytes;
        QtConcurrent::run(&m_workers, decode, data, size)
            .then(this, [this, key, bytes](const QImage &image) {
                m_decodingBytes -= bytes;
                if (!image.isNull()) {
                    m_decoded.insert(key, new QImage(image), int(std::min<qsizetype>(image.sizeInBytes(), kDecodedCacheBytes)));
                }
                const QList<ImageCallback> callbacks = m_decodes.take(key);
                for (const ImageCallback &callback : callbacks) {
                    callback(image);
                }
            });
    });
}

void ArtworkCache::invalidate(const QUrl &url)
{
    m_encoded.remove(url);
    const QString suffix = u':' + url.toString();
    for (const QString &key : m_decoded.keys()) {
        if (key.endsWith(suffix)) {
            m_decoded.remove(key);
        }
    }
    if (QAbstractNetworkCache *disk = m_network->cache()) {
        disk->remove(url);
    }
}

namespace {

class ArtworkResponse : public QQuickImageResponse {
public:
    ArtworkResponse(const QUrl &url, int pixels)
    {
        // the provider may be asked from the image loader's thread; the cache
        // and its network live on the gui thread.
        QPointer<ArtworkResponse> self(this);
        QMetaObject::invokeMethod(ArtworkCache::instance(), [self, url, pixels] {
            ArtworkCache::instance()->image(url, pixels, [self](const QImage &image) {
                if (self) {
                    self->m_image = image;
                    emit self->finished();
                }
            });
        }, Qt::QueuedConnection);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("The artwork could not be loaded.") : QString();
    }

private:
    QImage m_image;
};

} // namespace

QQuickImageResponse *ArtworkImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // id: <url, percent-encoded>. requestedSize carries the device pixels wanted.
    const QUrl url(QUrl::fromPercentEncoding(id.toUtf8()));
    const int pixels = std::max({requestedSize.width(), requestedSize.height(), 48});
    return new ArtworkResponse(url, pixels);
}

} // namespace swiftify

#include "ArtworkCache.moc"
