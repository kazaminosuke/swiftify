// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/YouTubeController.h"

#include <QDesktopServices>
#include <QNetworkReply>

#include <utility>

namespace swiftify::youtube {

namespace {

enum Role { IdRole = Qt::UserRole + 1, TitleRole, ChannelRole, ArtworkRole, DescriptionRole };

} // namespace

YouTubeController::YouTubeController(QObject *parent)
    : QObject(parent)
    , m_authorization(m_network, qEnvironmentVariable("SWIFTIFY_GOOGLE_CLIENT_ID"),
                      qEnvironmentVariable("SWIFTIFY_GOOGLE_CLIENT_SECRET"))
    , m_api(m_network)
    , m_apiKey(qgetenv("SWIFTIFY_YOUTUBE_API_KEY").trimmed())
{
    m_results.setItemRoleNames({{IdRole, "itemId"}, {TitleRole, "itemTitle"},
                               {ChannelRole, "channel"}, {ArtworkRole, "artwork"}, {DescriptionRole, "description"}});
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

void YouTubeController::setSearchKind(SearchKind kind)
{
    if (m_searchKind == kind || kind < MusicVideos || kind > PublicPlaylists) return;
    m_searchKind = kind;
    if (m_view == Search) {
        reset(Search);
        m_searchTerm.clear();
    }
    emit pageChanged();
}

void YouTubeController::search(const QString &term)
{
    const QString query = term.trimmed();
    if (query.isEmpty()) return;
    reset(Search);
    if (!canSearch()) {
        m_error = tr("Configure API access to search YouTube.");
        emit pageChanged();
        return;
    }
    m_searchTerm = m_value = query;
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
    if (!canSearch() || !validCollectionId(id)) return;
    m_playlistOrigin = m_view;
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

void YouTubeController::leavePlaylist()
{
    if (m_view != PlaylistContents) return;
    if (m_playlistOrigin == Playlists) {
        showPlaylists();
    } else if (!m_searchTerm.isEmpty()) {
        const QString priorQuery = m_searchTerm;
        search(priorQuery);
    } else {
        showSearch();
    }
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
    const bool useKey = !m_apiKey.isEmpty() && (m_view == Search
        || (m_view == PlaylistContents && m_playlistOrigin == Search && !signedIn()));
    const QByteArray token = useKey ? QByteArray() : m_authorization.accessToken();
    if (token.isEmpty() && !useKey) {
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
                row->setData(item.description, DescriptionRole);
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
    case Search:
        switch (m_searchKind) {
        case Channels: return Collection::SearchChannels;
        case PublicPlaylists: return Collection::SearchPlaylists;
        case MusicVideos: return Collection::Search;
        }
        break;
    case Likes: return Collection::Likes;
    case Playlists: return Collection::Playlists;
    case PlaylistContents: return Collection::PlaylistItems;
    }
    Q_UNREACHABLE_RETURN(Collection::Search);
}

} // namespace swiftify::youtube
