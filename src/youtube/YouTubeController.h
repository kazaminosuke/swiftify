// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "youtube/GoogleAuthorization.h"
#include "youtube/YouTubeApi.h"

#include <QNetworkAccessManager>
#include <QStandardItemModel>
#include <QtQml/qqmlregistration.h>

namespace swiftify::youtube {

class YouTubeController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Swiftify creates the YouTube client")

public:
    enum View { Search, Likes, Playlists, PlaylistContents };
    Q_ENUM(View)

    Q_PROPERTY(View view READ view NOTIFY pageChanged FINAL)
    Q_PROPERTY(QString playlistTitle READ playlistTitle NOTIFY pageChanged FINAL)
    Q_PROPERTY(QAbstractItemModel *results READ results CONSTANT FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY pageChanged FINAL)
    Q_PROPERTY(bool searched READ searched NOTIFY pageChanged FINAL)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY pageChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY pageChanged FINAL)
    Q_PROPERTY(bool oauthConfigured READ oauthConfigured CONSTANT FINAL)
    Q_PROPERTY(bool canSearch READ canSearch NOTIFY accountChanged FINAL)
    Q_PROPERTY(QString channelName READ channelName NOTIFY accountChanged FINAL)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY accountChanged FINAL)
    Q_PROPERTY(bool accountBusy READ accountBusy NOTIFY accountChanged FINAL)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY accountChanged FINAL)
    Q_PROPERTY(QString accountError READ accountError NOTIFY accountChanged FINAL)

    explicit YouTubeController(QObject *parent = nullptr);

    View view() const { return m_view; }
    QString playlistTitle() const { return m_playlistTitle; }
    QAbstractItemModel *results() { return &m_results; }
    bool loading() const { return m_reply != nullptr; }
    bool searched() const { return m_searched; }
    bool hasMore() const { return !m_nextPage.isEmpty(); }
    QString error() const { return m_error; }
    bool oauthConfigured() const { return m_authorization.configured(); }
    bool canSearch() const { return !m_channelName.isEmpty() || !m_apiKey.isEmpty(); }
    QString channelName() const { return m_channelName; }
    bool signedIn() const { return !m_authorization.accessToken().isEmpty(); }
    bool accountBusy() const { return m_authorization.busy(); }
    bool signingIn() const { return m_authorization.signingIn(); }
    QString accountError() const { return m_channelError.isEmpty() ? m_authorization.error() : m_channelError; }

    Q_INVOKABLE void showSearch();
    Q_INVOKABLE void search(const QString &term);
    Q_INVOKABLE void showLikes();
    Q_INVOKABLE void showPlaylists();
    Q_INVOKABLE void showPlaylist(const QString &id, const QString &title);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void play(const QString &id);
    Q_INVOKABLE void openPlaylist();
    Q_INVOKABLE void openSource();
    Q_INVOKABLE void openYouTubeTerms();
    Q_INVOKABLE void openGooglePrivacy();
    Q_INVOKABLE void openGoogleConnections();
    Q_INVOKABLE void signIn() { m_authorization.signIn(); }
    Q_INVOKABLE void retryChannel();
    Q_INVOKABLE void cancelSignIn() { m_authorization.cancelSignIn(); }
    Q_INVOKABLE void signOut() { m_authorization.disconnectAccount(); }

signals:
    void pageChanged();
    void accountChanged();

private:
    void reset(View view);
    void fetch(bool more);
    void loadChannel();
    void open(const QUrl &url);
    Collection collection() const;

    QNetworkAccessManager m_network;
    GoogleAuthorization m_authorization;
    Api m_api;
    const QByteArray m_apiKey;
    QStandardItemModel m_results;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_channelReply = nullptr;
    bool m_connected = false;
    QString m_channelName;
    QString m_channelError;
    View m_view = Search;
    QString m_value;
    QString m_playlistTitle;
    QString m_nextPage;
    QString m_error;
    bool m_searched = false;
    bool m_retryMore = false;
};

} // namespace swiftify::youtube
