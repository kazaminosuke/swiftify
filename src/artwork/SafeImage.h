// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QImageReader>

namespace swiftify {

inline constexpr qint64 maxImageFileBytes = 32 * 1024 * 1024;

inline QImage readSafeImage(QIODevice &device, int maxSide)
{
    QImageReader reader(&device);
    reader.setAutoDetectImageFormat(true);
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    const auto format = reader.format();
    if (format != "jpeg" && format != "png" && format != "webp"
        && format != "gif" && format != "bmp" && format != "avif") return {};
    const QSize size = reader.size();
    constexpr qint64 maxPixels = 32 * 1024 * 1024;
    if (size.isEmpty() || qint64(size.width()) * size.height() > maxPixels) return {};
    if (qMax(size.width(), size.height()) > maxSide) {
        reader.setScaledSize(size.scaled(maxSide, maxSide, Qt::KeepAspectRatio));
    }
    const QImage image = reader.read();
    if (qint64(image.width()) * image.height() > maxPixels) return {};
    return image;
}

} // namespace swiftify
