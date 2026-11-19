// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/GoogleAuthorization.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>

#include <utility>

namespace swiftify::youtube {

namespace {

const QByteArray readOnlyScope("https://www.googleapis.com/auth/youtube.readonly");

void protectAuthorizationLogs()
{
    // Qt's reply handler can otherwise log rejected callback URLs and provider error bodies.
    static const bool installed = [] {
        QLoggingCategory::setFilterRules(QStringLiteral("qt.networkauth.*=false"));
        return true;
    }();
    Q_UNUSED(installed);
}

} // namespace

GoogleAuthorization::GoogleAuthorization(QNetworkAccessManager &network, QString clientId,
                                         QString clientSecret, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_clientId(clientId.trimmed())
    , m_clientSecret(clientSecret.trimmed())
{
    protectAuthorizationLogs();
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(tr("Google sign-in timed out. Try again."));
    });
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
    if (!configured() || m_busy || !accessToken().isEmpty()) return;
    clearFlow();

    auto *flow = new QOAuth2AuthorizationCodeFlow(&m_network, this);
    auto *handler = new QOAuthHttpServerReplyHandler(flow);
    m_flow = flow;
    m_handler = handler;
    if (!handler->isListening()) {
        fail(tr("Could not start the loopback listener for Google sign-in."));
        return;
    }

    handler->setCallbackHost(QStringLiteral("127.0.0.1"));
    handler->setCallbackPath(QStringLiteral("/oauth2/callback"));
    handler->setCallbackText(tr("Return to Swiftify to check the sign-in result."));
    flow->setReplyHandler(handler);
    flow->setAuthorizationUrl(QUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth")));
    flow->setTokenUrl(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    flow->setClientIdentifier(m_clientId);
    flow->setClientIdentifierSharedKey(m_clientSecret);
    flow->setRequestedScopeTokens({readOnlyScope});
    flow->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    flow->setRefreshLeadTime(std::chrono::seconds(30));
    flow->setAutoRefresh(true);
    flow->setModifyParametersFunction([](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *params) {
        if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
            params->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
            params->insert(QStringLiteral("prompt"), QStringLiteral("consent"));
        }
    });
    flow->setNetworkRequestModifier(this, [](QNetworkRequest &request, QAbstractOAuth::Stage) {
        request.setTransferTimeout(20'000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    });

    connect(flow, &QAbstractOAuth::authorizeWithBrowser, this, [this, flow](const QUrl &url) {
        if (m_flow == flow && !QDesktopServices::openUrl(url)) {
            fail(tr("Could not open your browser for Google sign-in."));
        }
    });
    connect(flow, &QAbstractOAuth::statusChanged, this, [this, flow](QAbstractOAuth::Status status) {
        if (m_flow == flow && status == QAbstractOAuth::Status::RefreshingToken) {
            m_busy = true;
            emit changed();
        }
    });
    connect(flow, &QAbstractOAuth::granted, this, [this, flow, handler] {
        if (m_flow != flow) return;
        if (!flow->grantedScopeTokens().contains(readOnlyScope)
            || !flow->expirationAt().isValid() || flow->token().isEmpty()) {
            fail(tr("Google did not grant access to the YouTube library."));
            return;
        }
        handler->close();
        m_timeout.stop();
        m_granted = true;
        m_busy = false;
        m_waitingForGrant = false;
        m_error.clear();
        emit changed();
    });
    connect(flow, &QAbstractOAuth::requestFailed, this, [this, flow](QAbstractOAuth::Error) {
        if (m_flow == flow) fail(tr("Google sign-in or token refresh failed. Sign in again."));
    });
    connect(flow, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this, flow](const QString &error, const QString &, const QUrl &) {
        if (m_flow == flow && error == QLatin1StringView("access_denied")) {
            fail(tr("Google sign-in was declined."));
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
    if (!m_flow || !m_granted || m_busy) return;
    const QString refreshToken = m_flow->refreshToken();
    const QString token = refreshToken.isEmpty() ? m_flow->token() : refreshToken;
    clearFlow();
    m_error.clear();

    m_busy = true;
    emit changed();
    QNetworkRequest request(QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20'000);
    auto *reply = m_network.post(request, "token=" + QUrl::toPercentEncoding(token));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() != QNetworkReply::NoError
            || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            m_error = tr("Signed out locally. To revoke this app's access, use your Google Account settings.");
        }
        reply->deleteLater();
        m_busy = false;
        emit changed();
    });
}

void GoogleAuthorization::clearFlow()
{
    m_timeout.stop();
    if (m_handler) m_handler->close();
    if (auto *flow = std::exchange(m_flow, nullptr)) {
        flow->setAutoRefresh(false);
        flow->deleteLater();
    }
    m_handler = nullptr;
    m_granted = false;
    m_waitingForGrant = false;
    m_busy = false;
}

void GoogleAuthorization::fail(QString message)
{
    clearFlow();
    m_error = std::move(message);
    emit changed();
}

} // namespace swiftify::youtube
