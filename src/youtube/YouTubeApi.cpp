// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/YouTubeApi.h"

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

QString errorMessage(int status, const QJsonObject &payload)
{
    const QJsonObject error = payload.value(QStringLiteral("error")).toObject();
    const QJsonArray errors = error.value(QStringLiteral("errors")).toArray();
    const QString reason = errors.isEmpty() ? QString()
        : errors.first().toObject().value(QStringLiteral("reason")).toString();
    if (status == 401) {
        return QCoreApplication::translate("YouTubeApi", "YouTube authorization expired. Sign in again.");
    }
    if (status == 429 || reason == QLatin1StringView("quotaExceeded")
        || reason == QLatin1StringView("dailyLimitExceeded")) {
        return QCoreApplication::translate("YouTubeApi", "The YouTube API quota has been reached. Try again later.");
    }
    if (status == 403) {
        return QCoreApplication::translate("YouTubeApi", "YouTube denied access. Check the API configuration or account permissions.");
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

bool validPlaylistId(const QString &id)
{
    static const QRegularExpression format(QStringLiteral("\\A[A-Za-z0-9_-]{1,128}\\z"));
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
                string(QLatin1StringView("channelTitle")), thumbnail(snippet)};
    switch (collection) {
    case Collection::Search:
        result.id = object.value(QStringLiteral("id")).toObject().value(QStringLiteral("videoId")).toString();
        result.title = searchText(result.title);
        result.channel = searchText(result.channel);
        break;
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
        path = QStringLiteral("search");
        query.addQueryItem(QStringLiteral("type"), QStringLiteral("video"));
        query.addQueryItem(QStringLiteral("videoCategoryId"), musicCategory.toString());
        query.addQueryItem(QStringLiteral("q"), QString::fromLatin1(QUrl::toPercentEncoding(value)));
        break;
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
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    if (!accessToken.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + accessToken);
    } else if (collection == Collection::Search && !apiKey.isEmpty()) {
        request.setRawHeader("X-Goog-Api-Key", apiKey);
    }
    return m_network.get(request);
}

Page Api::read(QNetworkReply &reply, Collection collection)
{
    Page page;
    page.status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject response = QJsonDocument::fromJson(reply.readAll()).object();
    if (page.status != 200 || reply.error() != QNetworkReply::NoError) {
        page.error = errorMessage(page.status, response);
        return page;
    }

    const QJsonValue items = response.value(QStringLiteral("items"));
    if (!items.isArray()) {
        page.error = QCoreApplication::translate("YouTubeApi", "YouTube returned an unreadable response.");
        return page;
    }
    page.nextPage = response.value(QStringLiteral("nextPageToken")).toString();
    for (const QJsonValue entry : items.toArray()) {
        Item item = parseItem(collection, entry.toObject());
        const bool namedCollection = collection == Collection::Playlists || collection == Collection::Channel;
        const bool validId = namedCollection ? validPlaylistId(item.id) : validVideoId(item.id);
        if (validId && !item.title.isEmpty()) {
            page.items.append(std::move(item));
        }
    }
    return page;
}

QUrl watchUrl(const QString &videoId)
{
    if (!validVideoId(videoId)) return {};
    QUrl url(QStringLiteral("https://music.youtube.com/watch"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), videoId);
    url.setQuery(query);
    return url;
}

QUrl playlistUrl(const QString &playlistId)
{
    if (!validPlaylistId(playlistId)) return {};
    QUrl url(QStringLiteral("https://www.youtube.com/playlist"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("list"), playlistId);
    url.setQuery(query);
    return url;
}

} // namespace swiftify::youtube
