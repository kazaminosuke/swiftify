// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/GoogleAuthorization.h"
#include "youtube/YouTubeController.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

using swiftify::youtube::GoogleAuthorization;

namespace {

class TokenReply final : public QNetworkReply {
public:
    TokenReply(const QNetworkRequest &request, QByteArray body, QObject *parent,
               int status = 200, QByteArray retryAfter = {}, int delay = 0)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!retryAfter.isEmpty()) setRawHeader("Retry-After", retryAfter);
        if (status >= 400) setError(ContentAccessDenied, QStringLiteral("fixture"));
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        m_body.setData(std::move(body));
        m_body.open(ReadOnly);
        open(ReadOnly);
        QTimer::singleShot(delay, this, [this] {
            if (isFinished()) return;
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override
    {
        if (isFinished()) return;
        setError(OperationCanceledError, QStringLiteral("cancelled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override { return m_body.bytesAvailable() + QNetworkReply::bytesAvailable(); }

protected:
    qint64 readData(char *data, qint64 count) override { return m_body.read(data, count); }

private:
    QBuffer m_body;
};

class TokenNetwork final : public QNetworkAccessManager {
public:
    QList<QUrl> requests;
    QList<QByteArray> bodies;
    int revokeStatus = 200;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        if (operation != PostOperation || !data) return QNetworkAccessManager::createRequest(operation, request, data);
        requests.append(request.url());
        bodies.append(data->readAll());
        if (request.url().path() == QStringLiteral("/revoke")) {
            return new TokenReply(request, "{}", this, revokeStatus);
        }
        return new TokenReply(request, R"({"access_token":"example-access","expires_in":3600,
                                           "refresh_token":"example-refresh","token_type":"Bearer"})", this);
    }
};

class CatalogueNetwork final : public QNetworkAccessManager {
public:
    QList<QNetworkRequest> requests;
    QPointer<QNetworkReply> lastReply;
    QByteArray body = R"({"items":[]})";
    QByteArray retryAfter;
    int status = 200;
    int delay = 0;

protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override
    {
        requests.append(request);
        lastReply = new TokenReply(request, body, this, status, retryAfter, delay);
        return lastReply;
    }
};

} // namespace

class GoogleAuthorizationTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void captureUrl(const QUrl &url);
    void usesLoopbackAndExchangesPkceCode();
    void mismatchedStateDoesNotExchangeCode();
    void cancellationClosesListener();
    void revocationFailureLeavesTheAccountSignedOut();
    void cooldownRetainsTheRequestedPage();
    void authFailureAndDisconnectStopOwnedRequests();

private:
    void callback(QTcpSocket &socket, const QString &state);
    QUrl m_url;
};

void GoogleAuthorizationTest::init()
{
    m_url.clear();
    QDesktopServices::setUrlHandler(QStringLiteral("https"), this, "captureUrl");
}

void GoogleAuthorizationTest::cleanup()
{
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
}

void GoogleAuthorizationTest::captureUrl(const QUrl &url)
{
    m_url = url;
}

void GoogleAuthorizationTest::callback(QTcpSocket &socket, const QString &state)
{
    const QUrl redirect(QUrlQuery(m_url).queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(redirect.port()));
    QTRY_COMPARE(socket.state(), QAbstractSocket::ConnectedState);

    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("code"), QStringLiteral("example-code"));
    parameters.addQueryItem(QStringLiteral("state"), QString::fromLatin1(QUrl::toPercentEncoding(state)));
    const QByteArray request = "GET " + redirect.path().toUtf8() + '?'
        + parameters.toString(QUrl::FullyEncoded).toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n";
    QCOMPARE(socket.write(request), request.size());
}

void GoogleAuthorizationTest::usesLoopbackAndExchangesPkceCode()
{
    TokenNetwork network;
    GoogleAuthorization authorization(QStringLiteral("example-id"), QStringLiteral("example-client"), false, &network);
    authorization.signIn();
    QCOMPARE(m_url.host(), QStringLiteral("accounts.google.com"));

    const QUrlQuery query(m_url);
    const QUrl redirect(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    QCOMPARE(redirect.host(), QStringLiteral("127.0.0.1"));
    QVERIFY(redirect.port() > 0);
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("access_type")));
    QVERIFY(!query.hasQueryItem(QStringLiteral("prompt")));
    QVERIFY(!m_url.toEncoded().contains("example-client"));

    QTcpSocket socket;
    callback(socket, query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded));
    QTRY_COMPARE(authorization.accessToken(), QByteArray("example-access"));
    QCOMPARE(network.requests.constFirst(), QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    const QUrlQuery exchange(QString::fromLatin1(network.bodies.constFirst()));
    const QByteArray verifier = exchange.queryItemValue(QStringLiteral("code_verifier")).toUtf8();
    QVERIFY(verifier.size() >= 43);
    const QByteArray challenge = QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
                                      .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge")).toUtf8(), challenge);
    QCOMPARE(exchange.queryItemValue(QStringLiteral("code")), QStringLiteral("example-code"));
    QVERIFY(!authorization.busy());

    authorization.disconnectAccount();
    QVERIFY(authorization.accessToken().isEmpty());
    QTRY_VERIFY(!authorization.busy());
    QVERIFY(authorization.error().contains(QStringLiteral("revoked")));
    QCOMPARE(network.requests.constLast(), QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")));
    QCOMPARE(network.bodies.constLast(), QByteArray("token=example-access"));
}

void GoogleAuthorizationTest::mismatchedStateDoesNotExchangeCode()
{
    TokenNetwork network;
    GoogleAuthorization authorization(QStringLiteral("example-id"), QStringLiteral("example-client"), false, &network);
    authorization.signIn();
    QTcpSocket socket;
    callback(socket, QStringLiteral("wrong-state"));
    QTRY_VERIFY(!authorization.busy());
    QVERIFY(authorization.accessToken().isEmpty());
    QVERIFY(!authorization.error().isEmpty());
    QVERIFY(network.requests.isEmpty());
}

void GoogleAuthorizationTest::cancellationClosesListener()
{
    TokenNetwork network;
    GoogleAuthorization authorization(QStringLiteral("example-id"), QStringLiteral("example-client"), false, &network);
    authorization.signIn();
    QVERIFY(authorization.signingIn());
    const QUrl redirect(QUrlQuery(m_url).queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    authorization.cancelSignIn();
    QVERIFY(!authorization.signingIn());

    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(redirect.port()));
    QTRY_COMPARE(socket.error(), QAbstractSocket::ConnectionRefusedError);
    QVERIFY(network.requests.isEmpty());
}

void GoogleAuthorizationTest::revocationFailureLeavesTheAccountSignedOut()
{
    TokenNetwork network;
    network.revokeStatus = 400;
    GoogleAuthorization authorization(QStringLiteral("example-id"), QStringLiteral("example-client"), true, &network);
    authorization.signIn();
    const QUrlQuery query(m_url);
    QCOMPARE(query.queryItemValue(QStringLiteral("access_type")), QStringLiteral("offline"));
    QVERIFY(!query.hasQueryItem(QStringLiteral("prompt")));
    QTcpSocket socket;
    callback(socket, query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded));
    QTRY_VERIFY(authorization.signedIn());

    authorization.disconnectAccount();
    QVERIFY(!authorization.signedIn());
    QVERIFY(authorization.accessToken().isEmpty());
    QTRY_VERIFY(!authorization.busy());
    QVERIFY(authorization.error().contains(QStringLiteral("did not confirm")));
    QCOMPARE(network.bodies.constLast(), QByteArray("token=example-refresh"));
}

void GoogleAuthorizationTest::cooldownRetainsTheRequestedPage()
{
    qputenv("SWIFTIFY_YOUTUBE_API_KEY", "example-key");
    CatalogueNetwork network;
    swiftify::youtube::YouTubeController controller(nullptr, &network);
    qunsetenv("SWIFTIFY_YOUTUBE_API_KEY");

    network.body = R"({"nextPageToken":"page2","items":[]})";
    controller.search(QStringLiteral("music"));
    QTRY_VERIFY(controller.hasMore());
    QCOMPARE(network.requests.constLast().rawHeader("X-Goog-Api-Key"), "example-key");
    QVERIFY(!network.requests.constLast().hasRawHeader("Authorization"));

    network.status = 429;
    network.retryAfter = "1";
    controller.loadMore();
    QTRY_VERIFY(!controller.loading());
    QVERIFY(controller.error().contains(QStringLiteral("cooldown")));
    controller.retry();
    QCOMPARE(network.requests.size(), 2);

    QTest::qWait(1050);
    network.status = 200;
    network.body = R"({"items":[]})";
    controller.retry();
    QTRY_VERIFY(!controller.loading());
    QCOMPARE(network.requests.size(), 3);
    QCOMPARE(QUrlQuery(network.requests.constLast().url()).queryItemValue(QStringLiteral("pageToken")),
             QStringLiteral("page2"));
    QVERIFY(!controller.hasMore());

    network.status = 403;
    network.body = R"({"error":{"errors":[{"reason":"forbidden"}]}})";
    controller.search(QStringLiteral("music"));
    QTRY_VERIFY(!controller.loading());
    controller.search(QStringLiteral("another"));
    QCOMPARE(network.requests.size(), 4);
}

void GoogleAuthorizationTest::authFailureAndDisconnectStopOwnedRequests()
{
    qputenv("SWIFTIFY_GOOGLE_CLIENT_ID", "example-id");
    qputenv("SWIFTIFY_GOOGLE_CLIENT_SECRET", "example-client");
    qputenv("SWIFTIFY_YOUTUBE_API_KEY", "example-key");
    CatalogueNetwork network;
    TokenNetwork tokenNetwork;
    swiftify::youtube::YouTubeController controller(nullptr, &network, &tokenNetwork);
    qunsetenv("SWIFTIFY_GOOGLE_CLIENT_ID");
    qunsetenv("SWIFTIFY_GOOGLE_CLIENT_SECRET");
    qunsetenv("SWIFTIFY_YOUTUBE_API_KEY");

    network.body = R"({"items":[{"id":"UC_example","snippet":{"title":"Example"}}]})";
    controller.signIn();
    QTcpSocket initial;
    callback(initial, QUrlQuery(m_url).queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded));
    QTRY_COMPARE(controller.channelName(), QStringLiteral("Example"));
    QVERIFY(!network.requests.constLast().hasRawHeader("X-Goog-Api-Key"));

    network.status = 401;
    controller.showLikes();
    QTRY_VERIFY(!controller.signedIn());
    QVERIFY(controller.accountError().contains(QStringLiteral("expired")));
    controller.showLikes();
    QCOMPARE(network.requests.size(), 2);
    QCOMPARE(tokenNetwork.requests.size(), 1);

    network.status = 200;
    controller.signIn();
    QTcpSocket second;
    callback(second, QUrlQuery(m_url).queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded));
    QTRY_COMPARE(controller.channelName(), QStringLiteral("Example"));

    network.delay = 1000;
    controller.showLikes();
    QPointer<QNetworkReply> pending = network.lastReply;
    controller.signOut();
    QVERIFY(!controller.signedIn());
    QVERIFY(!controller.loading());
    QVERIFY(pending && pending->error() == QNetworkReply::OperationCanceledError);
    QTRY_VERIFY(!controller.accountBusy());
    QCOMPARE(tokenNetwork.requests.size(), 3);
}

QTEST_MAIN(GoogleAuthorizationTest)
#include "tst_googleauthorization.moc"
