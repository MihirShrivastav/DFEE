#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QString>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDateTime>
#include <QColor>
#include <QUrl>

#include "dfee/raw_decode.hpp"

// Serves library thumbnails: "image://thumb/<url-encoded-absolute-path>".
// Generates from the file's embedded preview (via the engine), disk-caches the
// result keyed by path + mtime + size, and returns it. Image elements should set
// asynchronous:true so Qt calls requestImage on its worker threads.
class ThumbnailImageProvider : public QQuickImageProvider {
public:
    ThumbnailImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {
        cacheDir_ = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + QStringLiteral("/thumbs"));
        QDir().mkpath(cacheDir_.absolutePath());
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
        const int maxEdge = requested.width() > 0 ? qMax(requested.width(), requested.height()) : 256;
        const QString path = QUrl::fromPercentEncoding(id.toUtf8());
        const QFileInfo info(path);
        if (!info.exists()) {
            return placeholder(size);
        }

        // Cache key: path + mtime + maxEdge bucket, so edits invalidate it.
        const QString key = path + QLatin1Char('|')
            + QString::number(info.lastModified().toSecsSinceEpoch()) + QLatin1Char('|')
            + QString::number(maxEdge);
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
        const QString cacheFile = cacheDir_.absoluteFilePath(hash + QStringLiteral(".jpg"));

        QImage img;
        if (QFileInfo::exists(cacheFile) && img.load(cacheFile)) {
            if (size) *size = img.size();
            return img;
        }

        const dfee::ThumbnailResponse resp =
            dfee::extract_thumbnail_jpeg(path.toStdString(), maxEdge);
        if (resp.ok && !resp.jpeg_bytes.empty()
            && img.loadFromData(resp.jpeg_bytes.data(),
                                static_cast<int>(resp.jpeg_bytes.size()), "JPG")) {
            img.save(cacheFile, "JPG", 82);
            if (size) *size = img.size();
            return img;
        }
        return placeholder(size);
    }

private:
    static QImage placeholder(QSize* size) {
        QImage img(2, 2, QImage::Format_RGB32);
        img.fill(QColor(26, 26, 28));
        if (size) *size = img.size();
        return img;
    }

    QDir cacheDir_;
};
