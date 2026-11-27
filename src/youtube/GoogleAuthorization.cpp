// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/GoogleAuthorization.h"
#include "network/BoundedReply.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>

#include <atomic>
#include <limits>
#include <utility>

namespace swiftify::youtube {

namespace {

const QUrl tokenEndpoint(QStringLiteral("https://oauth2.googleapis.com/token"));
const QUrl revokeEndpoint(QStringLiteral("https://oauth2.googleapis.com/revoke"));
constexpr qint64 maxOAuthReply = 64 * 1024;
constexpr int oauthRequestMs = 20'000;

class GoogleNetwork : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        return boundReply(QNetworkAccessManager::createRequest(operation, request, data),
                          maxOAuthReply, oauthRequestMs);
    }
};

const QByteArray readOnlyScope("https://www.googleapis.com/auth/youtube.readonly");

std::atomic<QLoggingCategory::CategoryFilter> previousLogFilter = nullptr;

void filterAuthorizationLogs(QLoggingCategory *category)
{
    if (const auto filter = previousLogFilter.load()) filter(category);
    if (!QByteArrayView(category->categoryName()).startsWith("qt.networkauth.")) return;

    // Qt can log callback URLs and provider error bodies, even at warning level.
    for (const auto type : {QtDebugMsg, QtInfoMsg, QtWarningMsg, QtCriticalMsg}) {
        category->setEnabled(type, false);
    }
}

void protectAuthorizationLogs()
{
    static const bool installed = [] {
        previousLogFilter.store(QLoggingCategory::installFilter(filterAuthorizationLogs));
        return true;
    }();
    Q_UNUSED(installed);
}

} // namespace

GoogleAuthorization::GoogleAuthorization(QString clientId, QString clientSecret, bool offlineAccess,
                                         QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network ? network : new GoogleNetwork(this))
    , m_offlineAccess(offlineAccess)
    , m_clientId(clientId.trimmed())
    , m_clientSecret(clientSecret.trimmed())
{
    protectAuthorizationLogs();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        invalidate(tr("The Google authorization request timed out. Sign in again."));
    });
    m_expiration.setSingleShot(true);
    connect(&m_expiration, &QTimer::timeout, this, [this] {
        if (accessToken().isEmpty() && m_flow && m_flow->refreshToken().isEmpty()) {
            invalidate(tr("Google authorization expired. Sign in again."));
        }
    });
}

GoogleAuthorization::~GoogleAuthorization()
{
    clearFlow();
}

bool GoogleAuthorization::configured() const
{
    return !m_clientId.isEmpty() && !m_clientSecret.isEmpty();
}

QByteArray GoogleAuthorization::accessToken() const
{
    if (!m_granted || !m_flow || m_flow->expirationAt() <= QDateTime::currentDateTimeUtc()) return {};
    return m_flow->token().toUtf8();
}

void GoogleAuthorization::signIn()
{
    if (!configured() || m_busy || m_granted) return;
    clearFlow();

    auto *flow = new QOAuth2AuthorizationCodeFlow(m_network, this);
    auto *handler = new QOAuthHttpServerReplyHandler(flow);
    m_flow = flow;
    m_handler = handler;
    handler->close();
    if (!handler->listen(QHostAddress::LocalHost, 0)) {
        invalidate(tr("Could not start the loopback listener for Google sign-in."));
        return;
    }

    handler->setCallbackHost(QStringLiteral("127.0.0.1"));
    handler->setCallbackPath(QStringLiteral("/oauth2/callback"));
    handler->setCallbackText(tr("Return to Swiftify to check the sign-in result."));
    flow->setReplyHandler(handler);
    flow->setAuthorizationUrl(QUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth")));
    flow->setTokenUrl(tokenEndpoint);
    flow->setClientIdentifier(m_clientId);
    flow->setClientIdentifierSharedKey(m_clientSecret);
    flow->setRequestedScopeTokens({readOnlyScope});
    flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    flow->setAutoRefresh(false);
    if (m_offlineAccess) {
        flow->setModifyParametersFunction([](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *params) {
            if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
                params->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
            }
        });
    }
    flow->setNetworkRequestModifier(this, [](QNetworkRequest &request, QAbstractOAuth::Stage) {
        request.setTransferTimeout(oauthRequestMs);
        privateRequest(request);
    });

    connect(flow, &QAbstractOAuth::authorizeWithBrowser, this, [this, flow](const QUrl &url) {
        if (m_flow == flow && !QDesktopServices::openUrl(url)) {
            invalidate(tr("Could not open your browser for Google sign-in."));
        }
    });
    connect(flow, &QAbstractOAuth::statusChanged, this, [this, flow](QAbstractOAuth::Status status) {
        if (m_flow == flow && status == QAbstractOAuth::Status::RefreshingToken) {
            m_busy = true;
            m_timeout.start(oauthRequestMs);
            emit changed();
        }
    });
    connect(flow, &QAbstractOAuth::granted, this, [this, flow, handler] {
        if (m_flow != flow) return;
        if (!flow->grantedScopeTokens().contains(readOnlyScope)
            || !flow->expirationAt().isValid() || flow->token().isEmpty()) {
            invalidate(tr("Google did not grant access to the YouTube library."));
            return;
        }
        handler->close();
        m_timeout.stop();
        const auto expiresIn = QDateTime::currentDateTimeUtc().msecsTo(flow->expirationAt());
        if (expiresIn <= 0) {
            invalidate(tr("Google returned an expired authorization."));
            return;
        }
        if (!m_offlineAccess) flow->setRefreshToken({});
        m_expiration.start(static_cast<int>(qMin(expiresIn, qint64(std::numeric_limits<int>::max()))));
        m_granted = true;
        m_busy = false;
        m_waitingForGrant = false;
        m_error.clear();
        emit changed();
    });
    connect(flow, &QAbstractOAuth::requestFailed, this, [this, flow](QAbstractOAuth::Error) {
        if (m_flow == flow) invalidate(tr("Google sign-in or token refresh failed. Sign in again."));
    });
    connect(flow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this, flow](const QString &error, const QString &, const QUrl &) {
        if (m_flow == flow && error == QLatin1StringView("access_denied")) {
            invalidate(tr("Google sign-in was declined."));
        }
    });

    m_error.clear();
    m_busy = true;
    m_waitingForGrant = true;
    m_timeout.start(180'000);
    emit changed();
    flow->grant();
}

void GoogleAuthorization::cancelSignIn()
{
    if (!m_waitingForGrant) return;
    clearFlow();
    m_error.clear();
    emit changed();
}

void GoogleAuthorization::disconnectAccount()
{
    if (!m_flow || !m_granted) return;
    const QString refreshToken = m_flow->refreshToken();
    const QString token = refreshToken.isEmpty() ? m_flow->token() : refreshToken;
    clearFlow();
    m_error.clear();

    m_busy = true;
    emit changed();
    QNetworkRequest request(revokeEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    privateRequest(request);
    request.setTransferTimeout(oauthRequestMs);
    auto *reply = boundReply(m_network->post(request, "token=" + QUrl::toPercentEncoding(token)),
                             maxOAuthReply, oauthRequestMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            m_error = tr("Signed out locally. Google did not confirm revocation. Check your Google Account connections.");
        } else {
            m_error = tr("Signed out and revoked this app’s Google access.");
        }
        reply->deleteLater();
        m_busy = false;
        emit changed();
    });
}

bool GoogleAuthorization::refreshIfExpired()
{
    if (!m_granted || !m_flow || !accessToken().isEmpty() || m_flow->refreshToken().isEmpty()) return false;
    if (!m_busy) m_flow->refreshTokens();
    return m_busy;
}

void GoogleAuthorization::clearFlow()
{
    m_timeout.stop();
    m_expiration.stop();
    if (m_handler) m_handler->close();
    if (auto *flow = std::exchange(m_flow, nullptr)) {
        flow->setAutoRefresh(false);
        // This manager is dedicated to authorization. Detach the flow before
        // aborting because Qt may emit requestFailed synchronously.
        for (auto *reply : m_network->findChildren<QNetworkReply *>()) {
            if (reply->url() == tokenEndpoint && !reply->isFinished()) reply->abort();
        }
        flow->deleteLater();
    }
    m_handler = nullptr;
    m_granted = false;
    m_waitingForGrant = false;
    m_busy = false;
}

void GoogleAuthorization::invalidate(QString message)
{
    clearFlow();
    m_error = std::move(message);
    emit changed();
}

} // namespace swiftify::youtube
