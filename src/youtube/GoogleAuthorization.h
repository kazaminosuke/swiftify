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
    GoogleAuthorization(QNetworkAccessManager &network, QString clientId, QString clientSecret,
                        QObject *parent = nullptr);

    bool configured() const;
    bool busy() const { return m_busy; }
    bool signingIn() const { return m_waitingForGrant; }
    QByteArray accessToken() const;
    QString error() const { return m_error; }

    void signIn();
    void cancelSignIn();
    void disconnectAccount();

signals:
    void changed();

private:
    void clearFlow();
    void fail(QString message);

    QNetworkAccessManager &m_network;
    const QString m_clientId;
    const QString m_clientSecret;
    QOAuth2AuthorizationCodeFlow *m_flow = nullptr;
    QOAuthHttpServerReplyHandler *m_handler = nullptr;
    QTimer m_timeout;
    bool m_busy = false;
    bool m_waitingForGrant = false;
    bool m_granted = false;
    QString m_error;
};

} // namespace swiftify::youtube
