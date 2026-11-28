// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/YouTubeApi.h"
#include "network/BoundedReply.h"

#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTest>

#include <memory>
#include <tuple>

using namespace swiftify::youtube;

namespace {

class Response final : public QNetworkReply {
public:
    Response(const QNetworkRequest &request, int status, QByteArray body)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (status >= 400) setError(ContentAccessDenied, QStringLiteral("fixture"));
        m_body.setData(std::move(body));
        m_body.open(ReadOnly);
        open(ReadOnly);
    }

    void abort() override {}

    qint64 bytesAvailable() const override { return m_body.bytesAvailable() + QNetworkReply::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 count) override { return m_body.read(data, count); }

private:
    QBuffer m_body;
};

class Network final : public QNetworkAccessManager {
public:
    int status = 200;
    QByteArray body = R"({"items":[]})";

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        return new Response(request, status, body);
    }
};

} // namespace

class YouTubeApiTest : public QObject {
    Q_OBJECT

private slots:
    void searchUsesMusicCategoryAndKeepsCredentialsOutOfUrl();
    void authorizedCollectionsUseExpectedEndpoints();
    void parsesSearchEntitiesAndRejectsInvalidVideoIds();
    void emptyMusicPageRetainsPagination();
    void playlistItemsUseVideoIdAndOwner();
    void reportsQuotaWithoutEchoingResponse();
    void rejectsOversizedResponse();
    void understandsRetryAfterDatesAndSeconds();
    void rejectsInvalidCollectionIds();
    void searchTypesHaveSeparateIdsAndFilters();
};

void YouTubeApiTest::searchUsesMusicCategoryAndKeepsCredentialsOutOfUrl()
{
    Network network;
    const Api api(network);
    const QString term = QStringLiteral("a+b & 日本語 %");
    const std::unique_ptr<QNetworkReply> reply(api.fetch(Collection::Search, term,
                                                        QStringLiteral("next+page"), "unused-test-token", "test-key"));
    const QUrl url = reply->request().url();
    const QUrlQuery query(url);
    QCOMPARE(url.path(), QStringLiteral("/youtube/v3/search"));
    QCOMPARE(query.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), term);
    QVERIFY(url.toEncoded().contains("q=a%2Bb%20%26%20"));
    QVERIFY(url.toEncoded().contains("pageToken=next%2Bpage"));
    QCOMPARE(query.queryItemValue(QStringLiteral("videoCategoryId")), QStringLiteral("10"));
    QCOMPARE(query.queryItemValue(QStringLiteral("type")), QStringLiteral("video"));
    QCOMPARE(reply->request().rawHeader("X-Goog-Api-Key"), QByteArray("test-key"));
    QVERIFY(!url.toEncoded().contains("test-key"));
    QVERIFY(!reply->request().hasRawHeader("Authorization"));
}

void YouTubeApiTest::searchTypesHaveSeparateIdsAndFilters()
{
    Network network;
    const Api api(network);
    network.body = R"({"items":[{"id":{"channelId":"UC_artist"},"snippet":{"title":"Artist","description":"Hello &amp; music"}},
        {"id":{"playlistId":"PL_music"},"snippet":{"title":"Mix"}}]})";
    for (const auto [collection, type, expectedId] :
         {std::tuple{Collection::SearchChannels, "channel", "UC_artist"},
          std::tuple{Collection::SearchPlaylists, "playlist", "PL_music"}}) {
        const std::unique_ptr<QNetworkReply> reply(api.fetch(collection, QStringLiteral("music"), {}, {}, "test-key"));
        const QUrlQuery query(reply->url());
        QCOMPARE(query.queryItemValue(QStringLiteral("type")), QString::fromLatin1(type));
        QVERIFY(!query.hasQueryItem(QStringLiteral("videoCategoryId")));
        QCOMPARE(reply->request().rawHeader("X-Goog-Api-Key"), QByteArray("test-key"));
        const Page page = Api::read(*reply, collection);
        QCOMPARE(page.items.size(), 1);
        QCOMPARE(page.items.first().id, QString::fromLatin1(expectedId));
        if (collection == Collection::SearchChannels) {
            QCOMPARE(page.items.first().description, QStringLiteral("Hello & music"));
        }
    }
    const std::unique_ptr<QNetworkReply> playlist(api.fetch(Collection::PlaylistItems,
        QStringLiteral("PL_music"), {}, {}, "test-key"));
    QCOMPARE(playlist->request().rawHeader("X-Goog-Api-Key"), QByteArray("test-key"));
}

void YouTubeApiTest::authorizedCollectionsUseExpectedEndpoints()
{
    Network network;
    const Api api(network);
    for (const auto [collection, path, key, value] :
         {std::tuple{Collection::Likes, "videos", "myRating", "like"},
          std::tuple{Collection::Playlists, "playlists", "mine", "true"},
          std::tuple{Collection::Channel, "channels", "mine", "true"},
          std::tuple{Collection::PlaylistItems, "playlistItems", "playlistId", "PL_123"}}) {
        const std::unique_ptr<QNetworkReply> reply(api.fetch(collection, QStringLiteral("PL_123"),
                                                            {}, "test-token", "test-key"));
        QCOMPARE(reply->request().rawHeader("Authorization"), QByteArray("Bearer test-token"));
        QVERIFY(!reply->request().hasRawHeader("X-Goog-Api-Key"));
        QCOMPARE(reply->url().path(), QStringLiteral("/youtube/v3/") + QString::fromLatin1(path));
        QCOMPARE(QUrlQuery(reply->url()).queryItemValue(QString::fromLatin1(key)), QString::fromLatin1(value));
    }
}

void YouTubeApiTest::parsesSearchEntitiesAndRejectsInvalidVideoIds()
{
    Network network;
    network.body = R"({"nextPageToken":"next","items":[
        {"id":{"videoId":"dQw4w9WgXcQ"},"snippet":{"title":"A &amp; B <live>","channelTitle":"Artist","thumbnails":{
            "medium":{"url":"http://example.com/insecure"},
            "default":{"url":"https://i.ytimg.com/vi/dQw4w9WgXcQ/default.jpg"}}}},
        {"id":{"videoId":"../invalid"},"snippet":{"title":"Ignore this"}}
    ]})";
    const Api api(network);
    const std::unique_ptr<QNetworkReply> reply(api.fetch(Collection::Search, QStringLiteral("term"), {}, {}, "test-key"));
    const Page page = Api::read(*reply, Collection::Search);
    QVERIFY(page.error.isEmpty());
    QCOMPARE(page.nextPage, QStringLiteral("next"));
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.first().title, QStringLiteral("A & B <live>"));
    QCOMPARE(page.items.first().channel, QStringLiteral("Artist"));
    QCOMPARE(page.items.first().artwork.scheme(), QStringLiteral("https"));
}

void YouTubeApiTest::emptyMusicPageRetainsPagination()
{
    Network network;
    network.body = R"({"nextPageToken":"next","items":[
        {"id":"dQw4w9WgXcQ","snippet":{"title":"Not music","categoryId":"22"}}
    ]})";
    const Api api(network);
    const std::unique_ptr<QNetworkReply> reply(api.fetch(Collection::Likes, {}, {}, "test-token"));
    const Page page = Api::read(*reply, Collection::Likes);
    QVERIFY(page.error.isEmpty());
    QVERIFY(page.items.isEmpty());
    QCOMPARE(page.nextPage, QStringLiteral("next"));
}

void YouTubeApiTest::playlistItemsUseVideoIdAndOwner()
{
    Network network;
    network.body = R"({"items":[
        {"id":"playlist-entry","snippet":{"title":"Track","channelTitle":"Playlist owner",
         "videoOwnerChannelTitle":"Artist","resourceId":{"videoId":"dQw4w9WgXcQ"}}}
    ]})";
    const Api api(network);
    const std::unique_ptr<QNetworkReply> reply(api.fetch(Collection::PlaylistItems, QStringLiteral("PL_123"),
                                                        {}, "test-token"));
    const Page page = Api::read(*reply, Collection::PlaylistItems);
    QVERIFY(page.error.isEmpty());
    QCOMPARE(page.items.size(), 1);
    QCOMPARE(page.items.first().id, QStringLiteral("dQw4w9WgXcQ"));
    QCOMPARE(page.items.first().channel, QStringLiteral("Artist"));
}

void YouTubeApiTest::reportsQuotaWithoutEchoingResponse()
{
    Network network;
    network.status = 403;
    network.body = R"({"error":{"message":"private debug data","errors":[{"reason":"quotaExceeded"}]}})";
    const Api api(network);
    const std::unique_ptr<QNetworkReply> reply(api.fetch(Collection::Search, {}, {}, {}, "test-key"));
    const Page page = Api::read(*reply, Collection::Search);
    QCOMPARE(page.status, 403);
    QVERIFY(page.error.contains(QStringLiteral("quota")));
    QVERIFY(!page.error.contains(QStringLiteral("private debug data")));
}

void YouTubeApiTest::rejectsOversizedResponse()
{
    Network network;
    network.body = QByteArray(2 * 1024 * 1024 + 1, ' ');
    const Api api(network);
    const std::unique_ptr<QNetworkReply> reply(api.fetch(Collection::Search, {}, {}, {}, "test-key"));
    const auto result = Api::read(*reply, Collection::Search);
    QVERIFY(result.items.isEmpty());
    QVERIFY(result.error.contains(QStringLiteral("too large")));
}

void YouTubeApiTest::understandsRetryAfterDatesAndSeconds()
{
    using swiftify::retryAfterMs;
    const QDateTime now = QDateTime::fromString(QStringLiteral("2026-11-27T12:00:00Z"), Qt::ISODate);
    QCOMPARE(retryAfterMs("9", now).value_or(-1), 9000);
    QCOMPARE(retryAfterMs("Fri, 27 Nov 2026 12:02:00 GMT", now).value_or(-1), 120000);
    QCOMPARE(retryAfterMs("Friday, 27-Nov-26 12:02:00 GMT", now).value_or(-1), 120000);
    QCOMPARE(retryAfterMs("Fri Nov 27 12:02:00 2026", now).value_or(-1), 120000);
    QCOMPARE(retryAfterMs("Fri, 27 Nov 2026 11:59:59 GMT", now).value_or(-1), 0);
    QVERIFY(!retryAfterMs("could not parse", now).has_value());
}

void YouTubeApiTest::rejectsInvalidCollectionIds()
{
    QVERIFY(validCollectionId(QStringLiteral("PL_123")));
    QVERIFY(validCollectionId(QStringLiteral("UC-test")));
    QVERIFY(!validCollectionId(QStringLiteral("https://example.org/")));
    QVERIFY(!validCollectionId(QStringLiteral("PL_123&x=y")));
}

QTEST_MAIN(YouTubeApiTest)
#include "tst_youtubeapi.moc"
