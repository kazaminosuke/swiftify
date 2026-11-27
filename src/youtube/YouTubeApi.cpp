// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/YouTubeApi.h"
#include "network/BoundedReply.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextDocumentFragment>

#include <utility>

namespace swiftify::youtube {

namespace {

constexpr QLatin1StringView musicCategory("10");

constexpr qint64 maxReplyBytes = 2 * 1024 * 1024;
constexpr int maxPageItems = 25;

ApiFailure failureFor(int status, const QJsonObject &payload)
{
    if (status == 401) return ApiFailure::Unauthorized;
    const auto error = payload.value(QStringLiteral("error")).toObject();
    for (const auto &entry : error.value(QStringLiteral("errors")).toArray()) {
        const QString reason = entry.toObject().value(QStringLiteral("reason")).toString();
        if (reason == QLatin1StringView("quotaExceeded")
            || reason == QLatin1StringView("dailyLimitExceeded")
            || reason == QLatin1StringView("dailyLimitExceededUnreg")) {
            return ApiFailure::Quota;
        }
        if (reason == QLatin1StringView("rateLimitExceeded")
            || reason == QLatin1StringView("userRateLimitExceeded")) {
            return ApiFailure::Throttled;
        }
    }
    if (status == 429) return ApiFailure::Throttled;
    if (status == 403) return ApiFailure::Forbidden;
    return ApiFailure::Other;
}

QString errorMessage(int status, ApiFailure failure)
{
    if (failure == ApiFailure::Unauthorized || failure == ApiFailure::Forbidden) {
        return QCoreApplication::translate("YouTubeApi", "YouTube denied access. Check the API configuration or account permissions.");
    }
    if (failure == ApiFailure::Quota) {
        return QCoreApplication::translate("YouTubeApi", "The YouTube API quota has been reached. Try again after it resets.");
    }
    if (failure == ApiFailure::Throttled) {
        return QCoreApplication::translate("YouTubeApi", "YouTube is limiting requests. Try again after the cooldown.");
    }
    if (status == 0) {
        return QCoreApplication::translate("YouTubeApi", "Could not connect to YouTube. Check your network and try again.");
    }
    if (status == 404) {
        return QCoreApplication::translate("YouTubeApi", "YouTube could not find this collection.");
    }
    return QCoreApplication::translate("YouTubeApi", "The YouTube API request failed (HTTP %1).").arg(status);
}

bool validVideoId(const QString &id)
{
    static const QRegularExpression format(QStringLiteral("\\A[A-Za-z0-9_-]{11}\\z"));
    return format.match(id).hasMatch();
}

QString searchText(QString text)
{
    // Search snippets may contain HTML entities. Treat literal markup as text.
    text.replace(u'<', QStringLiteral("&lt;")).replace(u'>', QStringLiteral("&gt;"));
    return QTextDocumentFragment::fromHtml(text).toPlainText();
}

QUrl thumbnail(const QJsonObject &snippet)
{
    const QJsonObject thumbnails = snippet.value(QStringLiteral("thumbnails")).toObject();
    for (const auto size : {"medium", "default", "high"}) {
        const QJsonObject image = thumbnails.value(QLatin1StringView(size)).toObject();
        const QUrl url(image.value(QStringLiteral("url")).toString());
        if (url.isValid() && url.scheme() == QLatin1StringView("https") && !url.host().isEmpty()
            && url.userInfo().isEmpty()) {
            return url;
        }
    }
    return {};
}

Item parseItem(Collection collection, const QJsonObject &object)
{
    const QJsonObject snippet = object.value(QStringLiteral("snippet")).toObject();
    const auto string = [&snippet](QLatin1StringView key) { return snippet.value(key).toString(); };
    Item result{object.value(QStringLiteral("id")).toString(), string(QLatin1StringView("title")),
                string(QLatin1StringView("channelTitle")), thumbnail(snippet),
                string(QLatin1StringView("description"))};
    switch (collection) {
    case Collection::Search:
    case Collection::SearchChannels:
    case Collection::SearchPlaylists: {
        const char *field = collection == Collection::Search ? "videoId"
            : collection == Collection::SearchChannels ? "channelId" : "playlistId";
        result.id = object.value(QStringLiteral("id")).toObject().value(QLatin1StringView(field)).toString();
        result.title = searchText(result.title);
        result.channel = searchText(result.channel);
        result.description = searchText(result.description);
        break;
    }
    case Collection::Likes:
        if (string(QLatin1StringView("categoryId")) != musicCategory) return {};
        break;
    case Collection::PlaylistItems:
        result.id = snippet.value(QStringLiteral("resourceId")).toObject()
                        .value(QStringLiteral("videoId")).toString();
        result.channel = string(QLatin1StringView("videoOwnerChannelTitle"));
        break;
    case Collection::Playlists:
    case Collection::Channel:
        break;
    }
    return result;
}

} // namespace

Api::Api(QNetworkAccessManager &network, QUrl base)
    : m_network(network), m_base(std::move(base))
{
}

QNetworkReply *Api::fetch(Collection collection, const QString &value, const QString &page,
                          const QByteArray &accessToken, const QByteArray &apiKey) const
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("part"), QStringLiteral("snippet"));
    query.addQueryItem(QStringLiteral("maxResults"), QStringLiteral("25"));
    if (!page.isEmpty()) {
        query.addQueryItem(QStringLiteral("pageToken"), QString::fromLatin1(QUrl::toPercentEncoding(page)));
    }

    QString path;
    switch (collection) {
    case Collection::Search:
    case Collection::SearchChannels:
    case Collection::SearchPlaylists: {
        path = QStringLiteral("search");
        const char *type = collection == Collection::Search ? "video"
            : collection == Collection::SearchChannels ? "channel" : "playlist";
        query.addQueryItem(QStringLiteral("type"), QLatin1StringView(type));
        if (collection == Collection::Search) {
            query.addQueryItem(QStringLiteral("videoCategoryId"), musicCategory.toString());
        }
        query.addQueryItem(QStringLiteral("q"), QString::fromLatin1(QUrl::toPercentEncoding(value)));
        break;
    }
    case Collection::Likes:
        path = QStringLiteral("videos");
        query.addQueryItem(QStringLiteral("myRating"), QStringLiteral("like"));
        break;
    case Collection::Playlists:
        path = QStringLiteral("playlists");
        query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
        break;
    case Collection::Channel:
        path = QStringLiteral("channels");
        query.addQueryItem(QStringLiteral("mine"), QStringLiteral("true"));
        break;
    case Collection::PlaylistItems:
        path = QStringLiteral("playlistItems");
        query.addQueryItem(QStringLiteral("playlistId"), QString::fromLatin1(QUrl::toPercentEncoding(value)));
        break;
    }
    QUrl url = m_base.resolved(QUrl(path));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(20'000);
    privateRequest(request);
    if (!accessToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + accessToken);
    } else if (!apiKey.isEmpty() && (path == QLatin1StringView("search") || collection == Collection::PlaylistItems)) {
        request.setRawHeader("X-Goog-Api-Key", apiKey);
    }
    return boundReply(m_network.get(request), maxReplyBytes, 25'000);
}

Page Api::read(QNetworkReply &reply, Collection collection)
{
    Page page;
    page.status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    page.retryAfter = retryAfterMs(reply.rawHeader("Retry-After")).value_or(-1);
    const auto limit = replyLimit(reply, maxReplyBytes);
    const QJsonObject response = limit == ReplyLimit::None
        ? QJsonDocument::fromJson(reply.read(maxReplyBytes + 1)).object() : QJsonObject();
    if (page.status != 200 || reply.error() != QNetworkReply::NoError || limit != ReplyLimit::None) {
        page.failure = failureFor(page.status, response);
        if (page.failure == ApiFailure::Other && limit == ReplyLimit::Size) {
            page.error = QCoreApplication::translate("YouTubeApi", "The YouTube response is too large.");
        } else if (page.failure == ApiFailure::Other && limit == ReplyLimit::Time) {
            page.error = QCoreApplication::translate("YouTubeApi", "The YouTube request timed out.");
        } else {
            page.error = errorMessage(page.status, page.failure);
        }
        return page;
    }

    const QJsonValue items = response.value(QStringLiteral("items"));
    if (!items.isArray() || items.toArray().size() > maxPageItems) {
        page.error = QCoreApplication::translate("YouTubeApi", "YouTube returned an unreadable response.");
        return page;
    }
    page.nextPage = response.value(QStringLiteral("nextPageToken")).toString();
    if (page.nextPage.size() > 1024) page.nextPage.clear();
    for (const QJsonValue entry : items.toArray()) {
        Item item = parseItem(collection, entry.toObject());
        const bool namedCollection = collection == Collection::Playlists || collection == Collection::Channel
            || collection == Collection::SearchChannels || collection == Collection::SearchPlaylists;
        const bool validId = namedCollection ? validCollectionId(item.id) : validVideoId(item.id);
        if (validId && !item.title.isEmpty()) {
            page.items.append(std::move(item));
        }
    }
    return page;
}

bool validCollectionId(const QString &id)
{
    static const QRegularExpression format(QStringLiteral("\\A[A-Za-z0-9_-]{1,128}\\z"));
    return format.match(id).hasMatch();
}

} // namespace swiftify::youtube
