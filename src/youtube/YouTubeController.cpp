// SPDX-License-Identifier: GPL-3.0-or-later
#include "youtube/YouTubeController.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QNetworkReply>
#include <QTimeZone>

#include <utility>

namespace swiftify::youtube {

namespace {

qint64 quotaResetMs()
{
    const QTimeZone pacific("America/Los_Angeles");
    const auto now = QDateTime::currentDateTimeUtc().toTimeZone(pacific);
    const QDateTime midnight(now.date().addDays(1), QTime(0, 0), pacific);
    return midnight.isValid() ? qMax(qint64(1000), now.msecsTo(midnight)) : 86'400'000;
}

enum Role { IdRole = Qt::UserRole + 1, TitleRole, ChannelRole, ArtworkRole, DescriptionRole };

} // namespace

YouTubeController::YouTubeController(QObject *parent, QNetworkAccessManager *catalogueNetwork,
                                     QNetworkAccessManager *authorizationNetwork)
    : QObject(parent)
    , m_authorization(qEnvironmentVariable("SWIFTIFY_GOOGLE_CLIENT_ID"),
                      qEnvironmentVariable("SWIFTIFY_GOOGLE_CLIENT_SECRET"),
                      qEnvironmentVariableIntValue("SWIFTIFY_GOOGLE_OFFLINE_ACCESS") == 1,
                      authorizationNetwork)
    , m_api(catalogueNetwork ? *catalogueNetwork : m_network)
    , m_apiKey(qgetenv("SWIFTIFY_YOUTUBE_API_KEY").trimmed())
{
    m_results.setItemRoleNames({{IdRole, "itemId"}, {TitleRole, "itemTitle"},
                               {ChannelRole, "channel"}, {ArtworkRole, "artwork"}, {DescriptionRole, "description"}});
    connect(&m_authorization, &GoogleAuthorization::changed, this, [this] {
        const bool connected = signedIn();
        if (std::exchange(m_connected, connected) != connected) {
            m_accountBlock.clear();
            m_channelError.clear();
            if (connected) {
                loadChannel();
            } else {
                stopChannel();
                m_channelName.clear();
                if (credential() == Credential::Account) {
                    reset(Search);
                    emit pageChanged();
                }
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
    if (query.isEmpty() || query.size() > 512) return;
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
    if (!validCollectionId(id) || (m_view != Search && m_view != Playlists)
        || (m_view == Search && !canSearch()) || (m_view == Playlists && !signedIn())) return;
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
    if (m_searched && !m_error.isEmpty()) fetch(m_retryMore);
}

void YouTubeController::signIn()
{
    m_channelError = blocked(Credential::Account);
    if (m_channelError.isEmpty()) m_authorization.signIn();
    emit accountChanged();
}

void YouTubeController::signOut()
{
    stopChannel();
    reset(Search);
    m_searchTerm.clear();
    m_channelError.clear();
    emit catalogueStopped();
    m_authorization.disconnectAccount();
    emit pageChanged();
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
    stopPage();
    m_results.removeRows(0, m_results.rowCount());
    m_view = view;
    m_nextPage.clear();
    m_seenPages.clear();
    m_pages = 0;
    m_value.clear();
    m_playlistTitle.clear();
    m_error.clear();
    m_searched = false;
    m_retryMore = false;
}

YouTubeController::Credential YouTubeController::credential() const
{
    return m_view == Search || (m_view == PlaylistContents && m_playlistOrigin == Search)
        ? Credential::Public : Credential::Account;
}

QString YouTubeController::blocked(Credential credential) const
{
    if (!m_pauseUntil.hasExpired()) return m_pauseReason;
    return credential == Credential::Public ? m_publicBlock : m_accountBlock;
}

QByteArray YouTubeController::accountToken(QString &error)
{
    const QByteArray token = m_authorization.accessToken();
    if (!token.isEmpty()) return token;
    error = m_authorization.refreshIfExpired()
        ? tr("Google is refreshing authorization. Retry when it finishes.")
        : tr("Sign in to access this collection.");
    return {};
}

void YouTubeController::stopPage()
{
    if (auto *reply = std::exchange(m_reply, nullptr)) reply->abort();
}

void YouTubeController::stopChannel()
{
    if (auto *reply = std::exchange(m_channelReply, nullptr)) reply->abort();
}

void YouTubeController::enforceFailure(const Page &page, Credential used)
{
    switch (page.failure) {
    case ApiFailure::Quota:
    case ApiFailure::Throttled: {
        const qint64 delay = page.failure == ApiFailure::Quota
            ? qMax(page.retryAfter, quotaResetMs())
            : (page.retryAfter < 0 ? 60'000 : qMax(page.retryAfter, qint64(1000)));
        m_pauseUntil.setRemainingTime(delay, Qt::PreciseTimer);
        m_pauseReason = page.error;
        if (m_reply) m_error = page.error;
        if (m_channelReply) m_channelError = page.error;
        stopPage();
        stopChannel();
        emit catalogueStopped();
        if (m_authorization.busy() && (signedIn() || signingIn())) {
            m_authorization.invalidate(page.error);
        }
        break;
    }
    case ApiFailure::Unauthorized:
    case ApiFailure::Forbidden:
        if (used == Credential::Public) {
            m_publicBlock = page.error;
            break;
        }
        stopChannel();
        if (credential() == Credential::Account) {
            stopPage();
            m_results.removeRows(0, m_results.rowCount());
            m_nextPage.clear();
            emit catalogueStopped();
        }
        m_channelName.clear();
        if (page.failure == ApiFailure::Unauthorized || m_authorization.busy()) {
            m_authorization.invalidate(tr("Google authorization expired or was revoked. Sign in again."));
        } else {
            m_accountBlock = page.error;
            m_channelError = page.error;
        }
        break;
    default:
        break;
    }
    emit accountChanged();
}

void YouTubeController::fetch(bool more)
{
    if (m_reply || (more && !hasMore())) return;
    const auto used = credential();
    m_searched = true;
    m_retryMore = more;
    m_error = blocked(used);
    if (!m_error.isEmpty()) {
        emit pageChanged();
        return;
    }
    const QByteArray token = used == Credential::Account ? accountToken(m_error) : QByteArray();
    if (used == Credential::Account ? token.isEmpty() : m_apiKey.isEmpty()) {
        if (m_error.isEmpty()) m_error = tr("Configure API access to search YouTube.");
        emit pageChanged();
        return;
    }

    const auto currentCollection = collection();
    const QString requestedPage = more ? m_nextPage : QString();
    auto *reply = m_api.fetch(currentCollection, m_value, requestedPage, token,
                             used == Credential::Public ? m_apiKey : QByteArray());
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentCollection, requestedPage, used] {
        reply->deleteLater();
        if (m_reply != reply) return;
        m_reply = nullptr;

        const Page result = Api::read(*reply, currentCollection);
        m_error = result.error;
        if (m_error.isEmpty()) {
            ++m_pages;
            m_seenPages.insert(requestedPage);
            m_nextPage = m_seenPages.contains(result.nextPage) ? QString() : result.nextPage;
            for (const Item &item : result.items) {
                auto *row = new QStandardItem;
                row->setData(item.id, IdRole);
                row->setData(item.title, TitleRole);
                row->setData(item.channel, ChannelRole);
                row->setData(item.artwork, ArtworkRole);
                row->setData(item.description, DescriptionRole);
                m_results.appendRow(row);
            }
        } else {
            enforceFailure(result, used);
        }
        emit pageChanged();
    });
    emit pageChanged();
}

void YouTubeController::loadChannel()
{
    if (m_channelReply || !signedIn()) return;
    m_channelError = blocked(Credential::Account);
    if (!m_channelError.isEmpty()) {
        emit accountChanged();
        return;
    }
    const auto token = accountToken(m_channelError);
    if (token.isEmpty()) {
        emit accountChanged();
        return;
    }
    auto *reply = m_api.fetch(Collection::Channel, {}, {}, token);
    m_channelReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_channelReply != reply) return;
        m_channelReply = nullptr;
        const Page result = Api::read(*reply, Collection::Channel);
        if (!result.error.isEmpty()) {
            m_channelError = result.error;
            enforceFailure(result, Credential::Account);
        } else if (result.items.isEmpty()) {
            m_channelError = tr("This Google account has no accessible YouTube channel.");
        } else {
            m_channelName = result.items.first().title;
        }
        emit pageChanged();
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
