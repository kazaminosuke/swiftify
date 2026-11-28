// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

namespace swiftify::youtube {

enum class ApiFailure { None, Unauthorized, Forbidden, Quota, Throttled, Other };

enum class Collection { Search, SearchChannels, SearchPlaylists, Likes, Playlists, PlaylistItems, Channel };

struct Item {
    QString id;
    QString title;
    QString channel;
    QUrl artwork;
    QString description;
};

struct Page {
    QList<Item> items;
    QString nextPage;
    QString error;
    int status = 0;
    ApiFailure failure = ApiFailure::None;
    qint64 retryAfter = -1;
};

class Api {
public:
    explicit Api(QNetworkAccessManager &network,
                 QUrl base = QUrl(QStringLiteral("https://www.googleapis.com/youtube/v3/")));

    QNetworkReply *fetch(Collection collection, const QString &value, const QString &page,
                         const QByteArray &accessToken, const QByteArray &apiKey = {}) const;

    static Page read(QNetworkReply &reply, Collection collection);

private:
    QNetworkAccessManager &m_network;
    QUrl m_base;
};

bool validCollectionId(const QString &id);

} // namespace swiftify::youtube
