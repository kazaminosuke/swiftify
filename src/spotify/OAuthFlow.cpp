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

#include "spotify/OAuthFlow.h"

#include "spotify/SpotifyError.h"
#include "network/BoundedReply.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QScopeGuard>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QCoroNetworkReply>

#include <memory>
#include <utility>

namespace swiftify {

namespace {

constexpr int maxCallbackBytes = 8192;
constexpr int maxConnections = 8;
constexpr qint64 maxTokenReply = 64 * 1024;

QByteArray callbackPage()
{
    return QByteArray("<!doctype html><meta charset=utf-8><title>Swiftify</title>"
                      "<style>body{font:16px system-ui,sans-serif;background:#141313;color:#e6e1e1;"
                      "display:grid;place-items:center;height:100vh;margin:0}</style><p>")
        + QCoreApplication::translate("OAuthFlow", "You're signed in. You can close this tab and return to Swiftify.")
              .toHtmlEscaped().toUtf8()
        + "</p>";
}

QByteArray httpResponse(int status, const QByteArray &reason, const QByteArray &body)
{
    return "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Cache-Control: no-store\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
}

QByteArray base64Url(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

} // namespace

QUrl OAuthFlow::redirectUri()
{
    return QUrl(QStringLiteral("http://127.0.0.1:8898/login"));
}

namespace {
QUrl s_tokenEndpoint {QStringLiteral("https://accounts.spotify.com/api/token")};
}

QUrl OAuthFlow::tokenEndpoint()
{
    return s_tokenEndpoint;
}

void OAuthFlow::setTokenEndpoint(const QUrl &url)
{
    s_tokenEndpoint = url;
}

OAuthFlow::OAuthFlow(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
    m_server.setMaxPendingConnections(maxConnections);
    m_loginDeadline.setSingleShot(true);
    connect(&m_loginDeadline, &QTimer::timeout, this, [this] {
        cancel();
        emit authorizationTimedOut();
    });
    connect(&m_server, &QTcpServer::newConnection, this, &OAuthFlow::acceptConnection);
}

OAuthFlow::~OAuthFlow()
{
    cancel();
}

bool OAuthFlow::isCallbackUrl(const QUrl &url)
{
    const QUrl redirect = redirectUri();
    return url.isValid() && url.scheme() == redirect.scheme() && url.host() == redirect.host()
        && url.port() == redirect.port() && url.path() == redirect.path() && url.userInfo().isEmpty()
        && !url.hasFragment();
}

QString OAuthFlow::randomUrlSafeString(int byteCount)
{
    QByteArray bytes(byteCount, Qt::Uninitialized);
    QRandomGenerator::system()->generate(bytes.begin(), bytes.end());
    return QString::fromLatin1(base64Url(bytes));
}

QUrl OAuthFlow::beginAuthorization(const QString &rawClientId, const QStringList &scopes)
{
    const QString clientId = rawClientId.trimmed();
    if (clientId.isEmpty()) {
        throw SpotifyError(SpotifyError::Kind::OAuth, QStringLiteral("Enter the Client ID from your Spotify developer app."));
    }

    stopListening();
    if (!m_server.listen(QHostAddress::LocalHost, kRedirectPort)) {
        throw SpotifyError(SpotifyError::Kind::OAuth,
                           QStringLiteral("Swiftify needs port %1 for the Spotify login, but %2. Close whatever is using it and try again.")
                               .arg(kRedirectPort)
                               .arg(m_server.errorString().toLower()));
    }

    const QString verifier = randomUrlSafeString(32);
    const QString challenge = QString::fromLatin1(
        base64Url(QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256)));
    const QString state = randomUrlSafeString(24);

    QUrl url(QStringLiteral("https://accounts.spotify.com/authorize"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("client_id"), clientId);
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri().toString());
    query.addQueryItem(QStringLiteral("scope"), scopes.join(u' '));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    query.addQueryItem(QStringLiteral("code_challenge"), challenge);
    query.addQueryItem(QStringLiteral("state"), state);
    url.setQuery(query);

    m_codeVerifier = verifier;
    m_expectedState = state;
    m_receivedCallback = false;
    m_loginDeadline.start(180'000);
    return url;
}

void OAuthFlow::acceptConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        if (m_sockets.size() >= maxConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        m_sockets.insert(socket);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_sockets.remove(socket);
            socket->deleteLater();
        });
        socket->setReadBufferSize(maxCallbackBytes + 1);
        QTimer::singleShot(3000, socket, [socket] { socket->abort(); });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleRequest(socket); });
        handleRequest(socket);
    }
}

void OAuthFlow::handleRequest(QTcpSocket *socket)
{
    if (socket->bytesAvailable() > maxCallbackBytes) {
        socket->abort();
        return;
    }
    if (!socket->canReadLine()) return;

    const auto line = socket->readLine(maxCallbackBytes + 1).trimmed().split(' ');
    QUrl url;
    if (line.size() == 3 && line[0] == "GET" && line[1].startsWith("/login?")
        && (line[2] == "HTTP/1.1" || line[2] == "HTTP/1.0")) {
        url = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(kRedirectPort) + QString::fromLatin1(line[1]),
                   QUrl::StrictMode);
    }
    const auto states = QUrlQuery(url).allQueryItemValues(QStringLiteral("state"), QUrl::FullyDecoded);
    if (!isCallbackUrl(url) || states.size() != 1 || states.front() != m_expectedState || m_receivedCallback) {
        socket->write(httpResponse(400, "Bad Request", QByteArray()));
        socket->disconnectFromHost();
        return;
    }
    socket->write(httpResponse(200, "OK", callbackPage()));
    socket->disconnectFromHost();
    m_receivedCallback = true;
    stopListening();
    emit callbackReceived(url);
}

void OAuthFlow::stopListening()
{
    m_loginDeadline.stop();
    if (m_server.isListening()) {
        m_server.close();
    }
}

void OAuthFlow::cancel()
{
    ++m_generation;
    stopListening();
    m_codeVerifier.clear();
    m_expectedState.clear();
    for (auto *socket : m_sockets.values()) socket->abort();
    QList<QPointer<QNetworkReply>> replies;
    for (auto *reply : std::as_const(m_replies)) replies.append(reply);
    for (const auto &reply : replies) {
        if (reply && !reply->isFinished()) reply->abort();
    }
}

QCoro::Task<OAuthToken> OAuthFlow::exchangeCallback(QUrl callbackUrl, QString clientId)
{
    if (!isCallbackUrl(callbackUrl) || m_codeVerifier.isEmpty() || m_expectedState.isEmpty()) {
        throw SpotifyError(SpotifyError::Kind::OAuth, QStringLiteral("Spotify returned an invalid authorization callback."));
    }
    const QString verifier = std::exchange(m_codeVerifier, {});
    const QString expectedState = std::exchange(m_expectedState, {});

    const QUrlQuery query(callbackUrl);
    if (query.allQueryItemValues(QStringLiteral("state"), QUrl::FullyDecoded) != QStringList{expectedState}) {
        throw SpotifyError(SpotifyError::Kind::OAuth,
                           QStringLiteral("Spotify returned an authorization response with an invalid state."));
    }
    if (query.hasQueryItem(QStringLiteral("error"))) {
        throw SpotifyError(SpotifyError::Kind::OAuth, QStringLiteral("Spotify authorization was denied."));
    }
    const QString code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    if (code.isEmpty() || query.allQueryItemValues(QStringLiteral("code")).size() != 1) {
        throw SpotifyError(SpotifyError::Kind::OAuth, QStringLiteral("Spotify returned an invalid authorization callback."));
    }

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri().toString());
    form.addQueryItem(QStringLiteral("code_verifier"), verifier);
    co_return co_await requestToken(form);
}

QCoro::Task<OAuthToken> OAuthFlow::refreshAccessToken(QString refreshToken, QString clientId)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    co_return co_await requestToken(form);
}

QCoro::Task<OAuthToken> OAuthFlow::requestToken(QUrlQuery form)
{
    const QUrl url = tokenEndpoint();
    const bool fixture = url.scheme() == QLatin1StringView("http") && url.host() == QLatin1StringView("127.0.0.1");
    if (!fixture && url != QUrl(QStringLiteral("https://accounts.spotify.com/api/token"))) {
        throw SpotifyError(SpotifyError::Kind::OAuth, QStringLiteral("Invalid Spotify token endpoint."));
    }
    QNetworkRequest request(url);
    privateRequest(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/x-www-form-urlencoded"));
    request.setTransferTimeout(15'000);
    QByteArray encoded;
    for (const auto &[key, value] : form.queryItems(QUrl::FullyDecoded)) {
        if (!encoded.isEmpty()) encoded += '&';
        encoded += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }

    const auto generation = m_generation;
    auto *pending = boundReply(m_network->post(request, encoded), maxTokenReply, 20'000);
    const QPointer<QNetworkReply> reply(pending);
    const QPointer<OAuthFlow> owner(this);
    m_replies.insert(pending);
    connect(pending, &QObject::destroyed, this, [this, pending] { m_replies.remove(pending); });
    const auto retire = qScopeGuard([owner, reply] {
        if (!reply) return;
        if (owner) owner->m_replies.remove(reply.data());
        reply->deleteLater();
    });
    co_await qCoro(pending).waitForFinished();
    if (!owner) throw Cancelled();
    if (!reply || generation != m_generation) {
        throw SpotifyError(SpotifyError::Kind::OAuth, QStringLiteral("Spotify authorization was cancelled."));
    }
    if (replyLimit(*reply, maxTokenReply) != ReplyLimit::None) {
        throw SpotifyError(SpotifyError::Kind::OAuth, QStringLiteral("Spotify token exchange exceeded the response limit."));
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject object = QJsonDocument::fromJson(reply->read(maxTokenReply + 1)).object();
    if (status < 200 || status >= 300 || reply->error() != QNetworkReply::NoError) {
        const bool revoked = status == 400 && object.value(QStringLiteral("error")).toString() == QLatin1StringView("invalid_grant");
        throw SpotifyError(revoked ? SpotifyError::Kind::LoginRevoked : SpotifyError::Kind::OAuth,
                           status == 0 ? QStringLiteral("Could not reach Spotify for authorization.")
                                       : QStringLiteral("Spotify token exchange failed (HTTP %1).").arg(status));
    }

    OAuthToken token;
    token.accessToken = object.value(QStringLiteral("access_token")).toString();
    token.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
    const int expires = object.value(QStringLiteral("expires_in")).toInt();
    token.expiration = QDateTime::currentDateTimeUtc().addSecs(expires);
    if (token.accessToken.isEmpty() || expires <= 0 || token.accessToken.contains(u'\r') || token.accessToken.contains(u'\n')) {
        throw SpotifyError(SpotifyError::Kind::OAuth,
                           QStringLiteral("Spotify token exchange failed: the response had no access token."));
    }
    co_return token;
}

} // namespace swiftify
