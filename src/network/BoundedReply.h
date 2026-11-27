// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QNetworkReply>
#include <QTimer>

#include <algorithm>
#include <optional>

namespace swiftify {

enum class ReplyLimit { None, Size, Time };

// Consumers read the body once, after finished. The read buffer applies
// backpressure before the application observes an oversized reply.
inline QNetworkReply *boundReply(QNetworkReply *reply, qint64 maxBytes, int totalMs)
{
    constexpr auto property = "_swiftify_reply_limit";
    const auto stop = [reply](ReplyLimit limit) {
        if (reply->isFinished()) return;
        reply->setProperty(property, static_cast<int>(limit));
        reply->abort();
    };
    reply->setReadBufferSize(maxBytes + 1);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, maxBytes, stop] {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > maxBytes) {
            stop(ReplyLimit::Size);
        }
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply, maxBytes, stop] {
        if (reply->bytesAvailable() > maxBytes) stop(ReplyLimit::Size);
    });
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    deadline->setTimerType(Qt::PreciseTimer);
    QObject::connect(deadline, &QTimer::timeout, reply, [stop] { stop(ReplyLimit::Time); });
    QObject::connect(reply, &QNetworkReply::finished, deadline, &QTimer::stop);
    deadline->start(totalMs);
    return reply;
}

inline ReplyLimit replyLimit(const QNetworkReply &reply, qint64 maxBytes)
{
    if (reply.bytesAvailable() > maxBytes) return ReplyLimit::Size;
    return static_cast<ReplyLimit>(reply.property("_swiftify_reply_limit").toInt());
}

inline void privateRequest(QNetworkRequest &request)
{
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
}

// Preserve arbitrarily long server delays within the range supported by
// monotonic deadline timers. An overflowing decimal still means "wait".
inline std::optional<qint64> retryAfterMs(QByteArray value, QDateTime now = QDateTime::currentDateTimeUtc())
{
    value = value.trimmed();
    if (value.isEmpty()) return std::nullopt;
    constexpr qint64 maxDelay = 100LL * 365 * 24 * 60 * 60 * 1000;
    if (std::all_of(value.cbegin(), value.cend(), [](char c) { return c >= '0' && c <= '9'; })) {
        bool ok = false;
        const auto seconds = value.toULongLong(&ok);
        if (!ok || seconds > static_cast<quint64>(maxDelay / 1000)) return maxDelay;
        return static_cast<qint64>(seconds) * 1000;
    }
    const auto date = QDateTime::fromString(QString::fromLatin1(value), Qt::RFC2822Date);
    if (!date.isValid()) return std::nullopt;
    return qBound(qint64(0), now.msecsTo(date), maxDelay);
}

} // namespace swiftify
