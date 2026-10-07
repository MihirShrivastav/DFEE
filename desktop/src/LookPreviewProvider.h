#pragma once
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Serves Films / Looks tiles: "image://look/<key>?e=<epoch>". The query only
// busts QML's cache; the key picks the image. Written on the GUI thread by
// EngineController, read on QML's image threads.
class LookPreviewProvider : public QQuickImageProvider {
public:
    LookPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    void setImage(const QString& key, const QImage& image) {
        QMutexLocker lock(&mutex_);
        images_.insert(key, image);
    }

    void clear() {
        QMutexLocker lock(&mutex_);
        images_.clear();
    }

    QImage requestImage(const QString& id, QSize* size, const QSize&) override {
        const QString key = id.section(QLatin1Char('?'), 0, 0);
        QMutexLocker lock(&mutex_);
        const QImage image = images_.value(key);
        if (size) *size = image.size();
        return image;
    }

private:
    QMutex mutex_;
    QHash<QString, QImage> images_;
};
