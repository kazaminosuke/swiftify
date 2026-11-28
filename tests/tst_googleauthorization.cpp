// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/GoogleAuthorization.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

using swiftify::youtube::GoogleAuthorization;

namespace {

class TokenReply final : public QNetworkReply {
public:
    TokenReply(const QNetworkRequest &request, QByteArray body, QObject *parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        m_body.setData(std::move(body));
        m_body.open(ReadOnly);
        open(ReadOnly);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit readyRead();
            emit finished();
        });
    }

    void abort() override {}
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

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        if (operation != PostOperation || !data) return QNetworkAccessManager::createRequest(operation, request, data);
        requests.append(request.url());
        bodies.append(data->readAll());
        if (request.url().path() == QStringLiteral("/revoke")) {
            return new TokenReply(request, "{}", this);
        }
        return new TokenReply(request, R"({"access_token":"example-access","expires_in":3600,
                                           "refresh_token":"example-refresh","token_type":"Bearer"})", this);
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
    GoogleAuthorization authorization(network, QStringLiteral("example-id"), QStringLiteral("example-client"));
    authorization.signIn();
    QCOMPARE(m_url.host(), QStringLiteral("accounts.google.com"));

    const QUrlQuery query(m_url);
    const QUrl redirect(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
    QCOMPARE(redirect.host(), QStringLiteral("127.0.0.1"));
    QVERIFY(redirect.port() > 0);
    QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
    QCOMPARE(query.queryItemValue(QStringLiteral("access_type")), QStringLiteral("offline"));
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
    QVERIFY(authorization.error().isEmpty());
    QCOMPARE(network.requests.constLast(), QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")));
    QCOMPARE(network.bodies.constLast(), QByteArray("token=example-refresh"));
}

void GoogleAuthorizationTest::mismatchedStateDoesNotExchangeCode()
{
    TokenNetwork network;
    GoogleAuthorization authorization(network, QStringLiteral("example-id"), QStringLiteral("example-client"));
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
    GoogleAuthorization authorization(network, QStringLiteral("example-id"), QStringLiteral("example-client"));
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

QTEST_MAIN(GoogleAuthorizationTest)
#include "tst_googleauthorization.moc"
