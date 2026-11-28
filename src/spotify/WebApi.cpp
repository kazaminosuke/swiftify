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

#include "spotify/WebApi.h"
#include "network/BoundedReply.h"

#include "spotify/SpotifyError.h"
#include "spotify/SpotifyJson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QScopeGuard>
#include <QSet>
#include <QCoroNetworkReply>
#include <QCoroTimer>

#include <chrono>
#include <climits>
#include <exception>
#include <memory>
#include <vector>

namespace swiftify {

namespace {

QByteArray jsonBody(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

WebApi::WebApi(QNetworkAccessManager *network)
    : m_network(network)
{
}

WebApi::~WebApi()
{
    cancelRequests();
}

QUrl WebApi::apiUrl(const QString &path, const QUrlQuery &query)
{
    QUrl url;
    url.setScheme(QStringLiteral("https"));
    url.setHost(QStringLiteral("api.spotify.com"));
    url.setPath(path);
    if (!query.isEmpty()) {
        url.setQuery(query);
    }
    return url;
}

namespace {
constexpr qint64 maxApiBytes = 4 * 1024 * 1024;
constexpr int maxPages = 200;
constexpr int maxPageItems = 50;
constexpr int maxInFlight = 8;
constexpr int maxPaginationMs = 180'000;
}

void WebApi::cancelRequests()
{
    ++m_generation;
    QList<QPointer<QNetworkReply>> replies;
    for (auto *reply : std::as_const(m_replies)) replies.append(reply);
    for (const auto &reply : replies) {
        if (reply && !reply->isFinished()) reply->abort();
    }
}

QCoro::Task<QByteArray> WebApi::requestData(QUrl url, QByteArray method, QString accessToken,
                                            QByteArray body, QByteArray contentType, CancelToken cancel, int totalMs)
{
#ifdef SWIFTIFY_HAS_SPOTIFY
    throwIfCancelled(cancel);
    if (url.scheme() != QLatin1StringView("https") || url.host() != QLatin1StringView("api.spotify.com")
        || url.port(443) != 443 || !url.userInfo().isEmpty() || url.hasFragment()
        || !url.path().startsWith(QLatin1StringView("/v1/")) || url.toEncoded().size() > 8192) {
        throw SpotifyError(SpotifyError::Kind::InvalidUrl, QStringLiteral("Spotify returned an invalid API URL."));
    }
    if (!m_backoff.hasExpired()) {
        const int seconds = int(qMin(qint64(INT_MAX), (m_backoff.remainingTime() + 999) / 1000));
        throw SpotifyError(SpotifyError::Kind::RateLimited, QStringLiteral("Spotify is limiting requests. Try again later."),
                           seconds);
    }
    if (m_replies.size() >= maxInFlight) {
        throw SpotifyError::requestFailed(QStringLiteral("Too many Spotify requests are in progress."));
    }

    QNetworkRequest request(url);
    privateRequest(request);
    request.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    request.setTransferTimeout(20'000);
    if (!body.isEmpty() || !contentType.isEmpty()) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType.isEmpty() ? QByteArray("application/json") : contentType);
    }

    const auto generation = m_generation;
    auto *pending = boundReply(m_network->sendCustomRequest(request, method, body), maxApiBytes, totalMs);
    // QCoro can resume after the reply's manager and the API owner are gone.
    const QPointer<QNetworkReply> reply(pending);
    const QPointer<WebApi> owner(this);
    m_replies.insert(pending);
    connect(pending, &QObject::destroyed, this, [this, pending] { m_replies.remove(pending); });
    if (cancel) cancel->watch(pending);
    const auto retire = qScopeGuard([owner, reply] {
        if (!reply) return;
        if (owner) owner->m_replies.remove(reply.data());
        reply->deleteLater();
    });
    co_await qCoro(pending).waitForFinished();
    if (!owner || !reply || generation != m_generation) throw Cancelled();
    throwIfCancelled(cancel);

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    switch (status) {
    case 401:
        throw SpotifyError(SpotifyError::Kind::Unauthorized, QStringLiteral("Spotify API authorization failed."));
    case 403:
        throw SpotifyError(SpotifyError::Kind::Forbidden, QStringLiteral("Spotify refused the request."));
    case 429: {
        const qint64 delay = qMax(qint64(1000), retryAfterMs(reply->rawHeader("Retry-After")).value_or(1000));
        m_backoff.setRemainingTime(qMax(delay, m_backoff.remainingTime()), Qt::PreciseTimer);
        throw SpotifyError(SpotifyError::Kind::RateLimited, QStringLiteral("Spotify is limiting requests. Try again later."),
                           int(qMin(qint64(INT_MAX), (delay + 999) / 1000)));
    }
    default:
        break;
    }
    if (replyLimit(*reply, maxApiBytes) != ReplyLimit::None) {
        throw SpotifyError::requestFailed(QStringLiteral("The Spotify response exceeded the size or time limit."));
    }
    if (status < 200 || status >= 300 || reply->error() != QNetworkReply::NoError) {
        throw SpotifyError::requestFailed(status == 0 ? QStringLiteral("Could not reach Spotify.")
                                                       : QStringLiteral("HTTP %1").arg(status));
    }
    co_return reply->read(maxApiBytes + 1);
#else
    Q_UNUSED(url);
    Q_UNUSED(method);
    Q_UNUSED(accessToken);
    Q_UNUSED(body);
    Q_UNUSED(contentType);
    Q_UNUSED(cancel);
    Q_UNUSED(totalMs);
    throw SpotifyError(SpotifyError::Kind::Session, QStringLiteral("Spotify support is disabled."));
#endif
}

QCoro::Task<QJsonDocument> WebApi::requestJson(QUrl url, QByteArray method, QString accessToken,
                                               QByteArray body, QByteArray contentType, CancelToken cancel, int totalMs)
{
    const QByteArray data = co_await requestData(url, method, accessToken, body, contentType, cancel, totalMs);
    QJsonParseError error;
    QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        throw SpotifyError::invalidData(error.errorString());
    }
    co_return document;
}

QCoro::Task<> WebApi::collectPages(QUrl firstUrl, QString accessToken,
                                   std::function<void(const QJsonObject &)> onPage, CancelToken cancel)
{
    const QDeadlineTimer deadline(maxPaginationMs);
    QSet<QUrl> visited;
    QUrl next = firstUrl;
    while (next.isValid() && !next.isEmpty()) {
        if (visited.contains(next)) {
            throw SpotifyError::invalidData(QStringLiteral("Spotify repeated a page URL."));
        }
        if (visited.size() >= maxPages) throw SpotifyError::invalidData(QStringLiteral("Spotify returned too many pages."));
        visited.insert(next);
        const QJsonObject page = co_await requestPage(next, accessToken, cancel, deadline);
        if (page.value(QStringLiteral("items")).toArray().size() > maxPageItems) {
            throw SpotifyError::invalidData(QStringLiteral("Spotify returned too many items in a page."));
        }
        onPage(page);
        const QJsonValue nextValue = page.value(QStringLiteral("next"));
        next = nextValue.isString() ? QUrl(nextValue.toString()) : QUrl();
    }
}

QCoro::Task<QJsonObject> WebApi::requestPage(QUrl url, QString accessToken, CancelToken cancel, QDeadlineTimer deadline)
{
    const QPointer<WebApi> owner(this);
    const auto generation = m_generation;
    if (deadline.hasExpired()) throw SpotifyError::requestFailed(QStringLiteral("Spotify pagination timed out."));
    const auto requestLimit = [deadline] { return int(qBound(qint64(1), deadline.remainingTime(), qint64(30'000))); };
    // set in the handler and waited out after it: there is no co_await in a catch
    int retryAfter = 0;
    try {
        co_return (co_await requestJson(url, "GET", accessToken, {}, {}, cancel, requestLimit())).object();
    } catch (const SpotifyError &error) {
        // a short wait is worth it; a long one is the caller's to report
        if (error.kind() != SpotifyError::Kind::RateLimited || error.retryAfterSeconds() > 10) {
            throw;
        }
        retryAfter = std::max(1, error.retryAfterSeconds());
    }
    if (deadline.remainingTime() <= retryAfter * 1000) {
        throw SpotifyError::requestFailed(QStringLiteral("Spotify pagination timed out."));
    }
    co_await QCoro::sleepFor(std::chrono::seconds(retryAfter));
    if (!owner || generation != m_generation) throw Cancelled();
    throwIfCancelled(cancel);
    co_return (co_await requestJson(url, "GET", accessToken, {}, {}, cancel, requestLimit())).object();
}

QCoro::Task<> WebApi::collectOffsetPages(QString path, QUrlQuery query, QString accessToken,
                                         std::function<void(const QJsonObject &)> onPage, CancelToken cancel)
{
    constexpr int kPageSize = 50;
    constexpr int kAtOnce = 4;
    query.removeAllQueryItems(QStringLiteral("limit"));
    query.addQueryItem(QStringLiteral("limit"), QString::number(kPageSize));
    const auto pageUrl = [&](int offset) {
        QUrlQuery paged = query;
        paged.addQueryItem(QStringLiteral("offset"), QString::number(offset));
        return apiUrl(path, paged);
    };

    const QDeadlineTimer deadline(maxPaginationMs);
    const QUrl firstUrl = pageUrl(0);
    const QJsonObject first = co_await requestPage(firstUrl, accessToken, cancel, deadline);
    const int total = first.value(QStringLiteral("total")).toInt();
    if (total < 0 || total > kPageSize * maxPages || first.value(QStringLiteral("items")).toArray().size() > maxPageItems) {
        throw SpotifyError::invalidData(QStringLiteral("Spotify returned too many items."));
    }
    onPage(first);

    for (int start = kPageSize; start < total; start += kPageSize * kAtOnce) {
        // started together, then each awaited in turn, every one to the end
        // even when one fails: a request is not left running unowned
        std::vector<QCoro::Task<QJsonObject>> batch;
        for (int offset = start; offset < std::min(total, start + kPageSize * kAtOnce); offset += kPageSize) {
            batch.push_back(requestPage(pageUrl(offset), accessToken, cancel, deadline));
        }
        std::vector<QJsonObject> pages;
        std::exception_ptr failure;
        for (QCoro::Task<QJsonObject> &task : batch) {
            try {
                pages.push_back(co_await task);
            } catch (...) {
                if (!failure) {
                    failure = std::current_exception();
                }
            }
        }
        if (failure) {
            std::rethrow_exception(failure);
        }
        for (const QJsonObject &page : pages) {
            if (page.value(QStringLiteral("items")).toArray().size() > maxPageItems) {
                throw SpotifyError::invalidData(QStringLiteral("Spotify returned too many items in a page."));
            }
            onPage(page);
        }
    }
}

QCoro::Task<Profile> WebApi::profile(QString accessToken)
{
    const QJsonDocument document = co_await requestJson(apiUrl(QStringLiteral("/v1/me")), "GET", accessToken);
    const Profile result = json::profile(document.object());
    if (!result.isValid()) {
        throw SpotifyError::invalidData(QStringLiteral("the profile had no id."));
    }
    co_return result;
}

QCoro::Task<QList<Playlist>> WebApi::playlists(QString accessToken)
{
    QList<Playlist> result;
    const QUrlQuery requestQuery = QUrlQuery {{QStringLiteral("limit"), QStringLiteral("50")}};
    co_await collectPages(apiUrl(QStringLiteral("/v1/me/playlists"), requestQuery),
                          accessToken, [&result](const QJsonObject &page) {
        for (const QJsonValue &value : page.value(QStringLiteral("items")).toArray()) {
            if (value.isObject()) {
                const Playlist playlist = json::playlist(value.toObject());
                if (playlist.isValid()) {
                    result.append(playlist);
                }
            }
        }
    });
    co_return result;
}

namespace {

/// the account's own market, so spotify says which tracks it will play there
const std::pair<QString, QString> kMarket {QStringLiteral("market"), QStringLiteral("from_token")};

void appendWrappedTracks(QList<Song> &songs, const QJsonObject &page)
{
    for (const QJsonValue &value : page.value(QStringLiteral("items")).toArray()) {
        if (const auto song = json::wrappedTrack(value.toObject())) {
            songs.append(*song);
        }
    }
}

} // namespace

QCoro::Task<QList<Song>> WebApi::playlistSongs(QString playlistId, QString accessToken, CancelToken cancel)
{
    QList<Song> result;
    const QUrlQuery query {{QStringLiteral("limit"), QStringLiteral("50")},
                           {QStringLiteral("additional_types"), QStringLiteral("track")},
                           kMarket};
    co_await collectOffsetPages(QStringLiteral("/v1/playlists/%1/items").arg(playlistId), query, accessToken,
                                [&result](const QJsonObject &page) { appendWrappedTracks(result, page); }, cancel);
    co_return result;
}

QCoro::Task<QList<Song>> WebApi::savedSongs(QString accessToken)
{
    QList<Song> result;
    const QUrlQuery requestQuery {kMarket};
    co_await collectOffsetPages(QStringLiteral("/v1/me/tracks"), requestQuery, accessToken,
                                [&result](const QJsonObject &page) { appendWrappedTracks(result, page); });
    co_return result;
}

QCoro::Task<int> WebApi::savedSongTotal(QString accessToken)
{
    const QUrlQuery requestQuery = QUrlQuery {{QStringLiteral("limit"), QStringLiteral("1")}};
    const QJsonDocument document = co_await requestJson(
        apiUrl(QStringLiteral("/v1/me/tracks"), requestQuery),
        "GET", accessToken);
    co_return document.object().value(QStringLiteral("total")).toInt();
}

QCoro::Task<QList<Song>> WebApi::recentlyPlayed(QString accessToken)
{
    const QUrlQuery requestQuery = QUrlQuery {{QStringLiteral("limit"), QStringLiteral("20")}};
    const QJsonDocument document = co_await requestJson(
        apiUrl(QStringLiteral("/v1/me/player/recently-played"), requestQuery),
        "GET", accessToken);
    QList<Song> result;
    appendWrappedTracks(result, document.object());
    co_return result;
}

QCoro::Task<SearchResults> WebApi::search(QString query, QString accessToken, CancelToken cancel)
{
    const QUrlQuery requestQuery = QUrlQuery {{QStringLiteral("q"), query},
                                                       {QStringLiteral("type"), QStringLiteral("track,album,artist,playlist")},
                                                       {QStringLiteral("limit"), QStringLiteral("10")},
                                                       kMarket};
    const QJsonObject response = (co_await requestJson(
        apiUrl(QStringLiteral("/v1/search"), requestQuery),
        "GET", accessToken, {}, {}, cancel)).object();

    const auto items = [&response](const char *key) {
        return response.value(QLatin1StringView(key)).toObject().value(QStringLiteral("items")).toArray();
    };
    SearchResults results;
    for (const QJsonValue &value : items("tracks")) {
        if (const auto song = json::track(value.toObject())) results.songs.append(*song);
    }
    for (const QJsonValue &value : items("albums")) {
        if (const auto album = json::album(value.toObject())) results.albums.append(*album);
    }
    for (const QJsonValue &value : items("artists")) {
        if (const auto artist = json::artist(value.toObject())) results.artists.append(*artist);
    }
    for (const QJsonValue &value : items("playlists")) {
        if (value.isObject()) {
            const Playlist playlist = json::playlist(value.toObject());
            if (playlist.isValid()) results.playlists.append(playlist);
        }
    }
    co_return results;
}

QCoro::Task<Song> WebApi::song(QString trackId, QString accessToken, CancelToken cancel)
{
    const QJsonDocument document = co_await requestJson(apiUrl(QStringLiteral("/v1/tracks/%1").arg(trackId), QUrlQuery {kMarket}), "GET", accessToken, {}, {}, cancel);
    const auto song = json::track(document.object());
    if (!song) {
        throw SpotifyError::invalidData(QStringLiteral("Spotify returned an unsupported track."));
    }
    co_return *song;
}

QCoro::Task<Album> WebApi::album(QString albumId, QString accessToken, CancelToken cancel)
{
    const QJsonDocument document = co_await requestJson(apiUrl(QStringLiteral("/v1/albums/%1").arg(albumId), QUrlQuery {kMarket}), "GET", accessToken, {}, {}, cancel);
    const auto album = json::album(document.object());
    if (!album) {
        throw SpotifyError::invalidData(QStringLiteral("Spotify returned an unsupported album."));
    }
    co_return *album;
}

QCoro::Task<Artist> WebApi::artist(QString artistId, QString accessToken, CancelToken cancel)
{
    const QJsonDocument document = co_await requestJson(apiUrl(QStringLiteral("/v1/artists/%1").arg(artistId), QUrlQuery {kMarket}), "GET", accessToken, {}, {}, cancel);
    const auto artist = json::artist(document.object());
    if (!artist) {
        throw SpotifyError::invalidData(QStringLiteral("Spotify returned an unsupported artist."));
    }
    co_return *artist;
}

QCoro::Task<Playlist> WebApi::playlist(QString playlistId, QString accessToken, CancelToken cancel)
{
    // json::playlist needs both "items" and "tracks"
    const QUrlQuery query {{QStringLiteral("fields"),
                            QStringLiteral("id,name,description,images,uri,public,collaborative,"
                                           "owner(id,display_name),items(total),tracks(total)")},
                           kMarket};
    const QJsonDocument document = co_await requestJson(apiUrl(QStringLiteral("/v1/playlists/%1").arg(playlistId), query), "GET", accessToken, {}, {}, cancel);
    const Playlist playlist = json::playlist(document.object());
    if (playlist.id.isEmpty()) {
        throw SpotifyError::invalidData(QStringLiteral("Spotify returned an unsupported playlist."));
    }
    co_return playlist;
}

QCoro::Task<QList<Song>> WebApi::albumSongs(Album album, QString accessToken, CancelToken cancel)
{
    QList<Song> result;
    const QUrlQuery requestQuery = QUrlQuery {{QStringLiteral("limit"), QStringLiteral("50")}, kMarket};
    co_await collectPages(apiUrl(QStringLiteral("/v1/albums/%1/tracks").arg(album.id), requestQuery),
                          accessToken, [&result, &album](const QJsonObject &page) {
        for (const QJsonValue &value : page.value(QStringLiteral("items")).toArray()) {
            if (const auto song = json::track(value.toObject(), album)) result.append(*song);
        }
    }, cancel);
    co_return result;
}

QCoro::Task<QList<Album>> WebApi::artistAlbums(Artist artist, QString accessToken, CancelToken cancel)
{
    QList<Album> result;
    QSet<QString> seen;
    const QUrlQuery query {{QStringLiteral("include_groups"), QStringLiteral("album,single")},
                           {QStringLiteral("limit"), QStringLiteral("10")}};
    co_await collectPages(apiUrl(QStringLiteral("/v1/artists/%1/albums").arg(artist.id), query), accessToken,
                          [&result, &seen](const QJsonObject &page) {
        for (const QJsonValue &value : page.value(QStringLiteral("items")).toArray()) {
            const auto album = json::album(value.toObject());
            if (album && !seen.contains(album->id)) {
                seen.insert(album->id);
                result.append(*album);
            }
        }
    }, cancel);
    co_return result;
}

QCoro::Task<QHash<QString, QStringList>> WebApi::artistGenres(QStringList artistIds, QString accessToken,
                                                              QHash<QString, QUrl> *artwork)
{
    QHash<QString, QStringList> result;
    // fifty at a time is as many as spotify takes
    for (qsizetype start = 0; start < artistIds.size(); start += 50) {
        const QUrlQuery query {{QStringLiteral("ids"), artistIds.mid(start, 50).join(u',')}};
        const QJsonDocument document = co_await requestJson(apiUrl(QStringLiteral("/v1/artists"), query), "GET", accessToken);
        for (const QJsonValue &value : document.object().value(QStringLiteral("artists")).toArray()) {
            const QJsonObject object = value.toObject();
            const QString id = object.value(QStringLiteral("id")).toString();
            if (id.isEmpty()) {
                continue;
            }
            QStringList genres;
            for (const QJsonValue &genre : object.value(QStringLiteral("genres")).toArray()) {
                genres.append(genre.toString());
            }
            result.insert(id, genres);
            if (artwork) {
                const QUrl image = json::preferredArtworkUrl(object.value(QStringLiteral("images")).toArray(), 320);
                if (image.isValid()) {
                    artwork->insert(id, image);
                }
            }
        }
    }
    co_return result;
}

QCoro::Task<Playlist> WebApi::createPlaylist(QString name, QString description, bool isPublic, QString accessToken)
{
    const QByteArray requestBody = jsonBody({{QStringLiteral("name"), name}, {QStringLiteral("description"), description}, {QStringLiteral("public"), isPublic}});
    const QJsonDocument document = co_await requestJson(
        apiUrl(QStringLiteral("/v1/me/playlists")), "POST", accessToken,
        requestBody);
    const Playlist playlist = json::playlist(document.object());
    if (!playlist.isValid()) {
        throw SpotifyError::invalidData(QStringLiteral("the new playlist had no id."));
    }
    co_return playlist;
}

QCoro::Task<> WebApi::updatePlaylist(QString id, QString name, QString description, bool isPublic, QString accessToken)
{
    const QByteArray requestBody = jsonBody({{QStringLiteral("name"), name}, {QStringLiteral("description"), description}, {QStringLiteral("public"), isPublic}});
    co_await requestData(apiUrl(QStringLiteral("/v1/playlists/%1").arg(id)), "PUT", accessToken,
                         requestBody);
}

QCoro::Task<> WebApi::addToPlaylist(QString songUri, QString playlistId, QString accessToken)
{
    const QByteArray requestBody = jsonBody({{QStringLiteral("uris"), QJsonArray {songUri}}});
    co_await requestData(apiUrl(QStringLiteral("/v1/playlists/%1/items").arg(playlistId)), "POST", accessToken,
                         requestBody);
}

QCoro::Task<> WebApi::removeFromPlaylist(QString songUri, QString playlistId, QString accessToken)
{
    const QByteArray requestBody = jsonBody({{QStringLiteral("items"), QJsonArray {QJsonObject {{QStringLiteral("uri"), songUri}}}}});
    co_await requestData(apiUrl(QStringLiteral("/v1/playlists/%1/items").arg(playlistId)), "DELETE", accessToken,
                         requestBody);
}

QCoro::Task<> WebApi::reorderPlaylist(QString playlistId, int rangeStart, int insertBefore, int length, QString accessToken)
{
    const QByteArray requestBody = jsonBody({{QStringLiteral("range_start"), rangeStart},
                                   {QStringLiteral("insert_before"), insertBefore},
                                   {QStringLiteral("range_length"), length}});
    co_await requestData(apiUrl(QStringLiteral("/v1/playlists/%1/items").arg(playlistId)), "PUT", accessToken,
                         requestBody);
}

QCoro::Task<> WebApi::setPlaylistImage(QString playlistId, QByteArray jpeg, QString accessToken)
{
    if (jpeg.size() > kMaximumCoverBytes) {
        throw SpotifyError::requestFailed(QStringLiteral("That cover image is too large."));
    }
    co_await requestData(apiUrl(QStringLiteral("/v1/playlists/%1/images").arg(playlistId)), "PUT", accessToken,
                         jpeg.toBase64(), "image/jpeg");
}

QCoro::Task<> WebApi::deletePlaylist(QString id, QString accessToken)
{
    co_await setSaved(false, QStringLiteral("spotify:playlist:%1").arg(id), accessToken);
}

QCoro::Task<> WebApi::setSaved(bool saved, QString uri, QString accessToken)
{
    const QUrlQuery requestQuery = QUrlQuery {{QStringLiteral("uris"), uri}};
    co_await requestData(apiUrl(QStringLiteral("/v1/me/library"), requestQuery),
                         saved ? QByteArray("PUT") : QByteArray("DELETE"), accessToken);
}

QCoro::Task<QHash<QString, bool>> WebApi::savedStatuses(QStringList uris, QString accessToken)
{
    uris.removeDuplicates();
    uris = uris.mid(0, 40);
    QHash<QString, bool> result;
    if (uris.isEmpty()) {
        co_return result;
    }
    const QUrlQuery requestQuery = QUrlQuery {{QStringLiteral("uris"), uris.join(u',')}};
    const QJsonArray statuses = (co_await requestJson(
        apiUrl(QStringLiteral("/v1/me/library/contains"), requestQuery),
        "GET", accessToken)).array();
    for (qsizetype index = 0; index < uris.size() && index < statuses.size(); ++index) {
        result.insert(uris[index], statuses[index].toBool());
    }
    co_return result;
}

} // namespace swiftify
