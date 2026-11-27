// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

namespace swiftify::youtube {

class GoogleAuthorization : public QObject {
    Q_OBJECT

public:
    GoogleAuthorization(QString clientId, QString clientSecret, bool offlineAccess = false,
                        QNetworkAccessManager *network = nullptr, QObject *parent = nullptr);
    ~GoogleAuthorization() override;

    bool configured() const;
    bool signedIn() const { return m_granted; }
    bool busy() const { return m_busy; }
    bool signingIn() const { return m_waitingForGrant; }
    QByteArray accessToken() const;
    QString error() const { return m_error; }

    void signIn();
    void cancelSignIn();
    void disconnectAccount();
    bool refreshIfExpired();
    void invalidate(QString message);

signals:
    void changed();

private:
    void clearFlow();
    void fail(QString message);

    QNetworkAccessManager *const m_network;
    const bool m_offlineAccess;
    const QString m_clientId;
    const QString m_clientSecret;
    QOAuth2AuthorizationCodeFlow *m_flow = nullptr;
    QOAuthHttpServerReplyHandler *m_handler = nullptr;
    QTimer m_timeout;
    QTimer m_expiration;
    bool m_busy = false;
    bool m_waitingForGrant = false;
    bool m_granted = false;
    QString m_error;
};

} // namespace swiftify::youtube
