// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

class QNetworkAccessManager;
class QNetworkReply;

namespace swiftify::youtube {

enum class Collection { Search, Likes, Playlists, PlaylistItems };

struct Item {
    QString id;
    QString title;
    QString channel;
    QUrl artwork;
};

struct Page {
    QList<Item> items;
    QString nextPage;
    QString error;
    int status = 0;
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

QUrl watchUrl(const QString &videoId);
QUrl playlistUrl(const QString &playlistId);

} // namespace swiftify::youtube
