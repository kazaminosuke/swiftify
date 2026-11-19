// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/YouTubeController.h"

#include <QDesktopServices>
#include <QNetworkReply>

#include <utility>

namespace swiftify::youtube {

namespace {

enum Role { IdRole = Qt::UserRole + 1, TitleRole, ChannelRole, ArtworkRole };

} // namespace

YouTubeController::YouTubeController(QObject *parent)
    : QObject(parent)
    , m_authorization(m_network, qEnvironmentVariable("SWIFTIFY_GOOGLE_CLIENT_ID"),
                      qEnvironmentVariable("SWIFTIFY_GOOGLE_CLIENT_SECRET"))
    , m_api(m_network)
    , m_apiKey(qgetenv("SWIFTIFY_YOUTUBE_API_KEY").trimmed())
{
    m_results.setItemRoleNames({{IdRole, "itemId"}, {TitleRole, "itemTitle"},
                               {ChannelRole, "channel"}, {ArtworkRole, "artwork"}});
    connect(&m_authorization, &GoogleAuthorization::changed, this, [this] {
        const bool connected = signedIn();
        if (std::exchange(m_connected, connected) != connected) {
            if (connected) {
                loadChannel();
            } else {
                if (auto *reply = std::exchange(m_channelReply, nullptr)) reply->abort();
                m_channelName.clear();
                m_channelError.clear();
                reset(Search);
                emit pageChanged();
            }
        }
        emit accountChanged();
    });
}

void YouTubeController::showSearch()
{
    if (m_view == Search) return;
    reset(Search);
    emit pageChanged();
}

void YouTubeController::search(const QString &term)
{
    const QString query = term.trimmed();
    if (query.isEmpty()) return;
    reset(Search);
    if (!canSearch()) {
        QUrl url(QStringLiteral("https://music.youtube.com/search"));
        QUrlQuery parameters;
        parameters.addQueryItem(QStringLiteral("q"), QString::fromLatin1(QUrl::toPercentEncoding(query)));
        url.setQuery(parameters);
        emit pageChanged();
        open(url);
        return;
    }
    m_value = query;
    fetch(false);
}

void YouTubeController::showLikes()
{
    if (m_channelName.isEmpty()) return;
    reset(Likes);
    fetch(false);
}

void YouTubeController::showPlaylists()
{
    if (m_channelName.isEmpty()) return;
    reset(Playlists);
    fetch(false);
}

void YouTubeController::showPlaylist(const QString &id, const QString &title)
{
    if (m_channelName.isEmpty() || playlistUrl(id).isEmpty()) return;
    reset(PlaylistContents);
    m_value = id;
    m_playlistTitle = title;
    fetch(false);
}

void YouTubeController::loadMore()
{
    if (hasMore()) fetch(true);
}

void YouTubeController::retry()
{
    if (m_searched) fetch(m_retryMore);
}

void YouTubeController::retryChannel()
{
    if (signedIn() && !m_channelError.isEmpty()) loadChannel();
}

void YouTubeController::play(const QString &id)
{
    open(watchUrl(id));
}

void YouTubeController::openPlaylist()
{
    if (m_view == PlaylistContents) open(playlistUrl(m_value));
}

void YouTubeController::openSource()
{
    open(QUrl(QStringLiteral("https://www.youtube.com/")));
}

void YouTubeController::openYouTubeTerms()
{
    open(QUrl(QStringLiteral("https://www.youtube.com/t/terms")));
}

void YouTubeController::openGooglePrivacy()
{
    open(QUrl(QStringLiteral("https://policies.google.com/privacy")));
}

void YouTubeController::openGoogleConnections()
{
    open(QUrl(QStringLiteral("https://security.google.com/settings/security/permissions")));
}

void YouTubeController::reset(View view)
{
    if (QNetworkReply *reply = std::exchange(m_reply, nullptr)) reply->abort();
    m_results.removeRows(0, m_results.rowCount());
    m_view = view;
    m_nextPage.clear();
    m_value.clear();
    m_playlistTitle.clear();
    m_error.clear();
    m_searched = false;
    m_retryMore = false;
}

void YouTubeController::fetch(bool more)
{
    if (m_reply) return;
    const bool publicSearch = m_view == Search && !m_apiKey.isEmpty();
    const QByteArray token = publicSearch ? QByteArray() : m_authorization.accessToken();
    if (token.isEmpty() && (m_view != Search || m_apiKey.isEmpty())) {
        m_error = tr("Sign in to access this collection.");
        emit pageChanged();
        return;
    }

    m_searched = true;
    m_error.clear();
    m_retryMore = more;
    const auto currentCollection = collection();
    const QString requestedPage = more ? m_nextPage : QString();
    QNetworkReply *reply = m_api.fetch(currentCollection, m_value, requestedPage, token, m_apiKey);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentCollection, requestedPage] {
        reply->deleteLater();
        if (m_reply != reply) return;
        m_reply = nullptr;

        const Page result = Api::read(*reply, currentCollection);
        m_error = result.error;
        if (m_error.isEmpty()) {
            m_nextPage = result.nextPage == requestedPage ? QString() : result.nextPage;
            for (const Item &item : result.items) {
                auto *row = new QStandardItem;
                row->setData(item.id, IdRole);
                row->setData(item.title, TitleRole);
                row->setData(item.channel, ChannelRole);
                row->setData(item.artwork, ArtworkRole);
                m_results.appendRow(row);
            }
        }
        emit pageChanged();
    });
    emit pageChanged();
}

void YouTubeController::loadChannel()
{
    if (m_channelReply) return;
    m_channelError.clear();
    auto *reply = m_api.fetch(Collection::Channel, {}, {}, m_authorization.accessToken());
    m_channelReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_channelReply != reply) return;
        m_channelReply = nullptr;
        const Page result = Api::read(*reply, Collection::Channel);
        if (!result.error.isEmpty()) {
            m_channelError = result.error;
        } else if (result.items.isEmpty()) {
            m_channelError = tr("This Google account has no accessible YouTube channel.");
        } else {
            m_channelName = result.items.first().title;
        }
        emit accountChanged();
    });
    emit accountChanged();
}

void YouTubeController::open(const QUrl &url)
{
    if (url.isEmpty()) return;
    if (!QDesktopServices::openUrl(url)) {
        m_error = tr("Could not open your browser.");
        emit pageChanged();
    }
}

Collection YouTubeController::collection() const
{
    switch (m_view) {
    case Search: return Collection::Search;
    case Likes: return Collection::Likes;
    case Playlists: return Collection::Playlists;
    case PlaylistContents: return Collection::PlaylistItems;
    }
    Q_UNREACHABLE_RETURN(Collection::Search);
}

} // namespace swiftify::youtube
