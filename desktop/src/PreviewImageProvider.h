#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

// Serves two images: the film-processed preview ("frame", default) and the
// neutral "before" preview used by the before/after compare view. The QML source
// id selects which: "image://preview/before?rev=N" vs "image://preview/frame?rev=N".
class PreviewImageProvider : public QQuickImageProvider {
public:
    PreviewImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    void setImage(const QImage& img) {
        QMutexLocker lock(&mutex_);
        image_ = img;
    }

    void setBeforeImage(const QImage& img) {
        QMutexLocker lock(&mutex_);
        before_ = img;
    }

    QImage requestImage(const QString& id, QSize* size, const QSize&) override {
        QMutexLocker lock(&mutex_);
        const QImage& img = id.startsWith(QStringLiteral("before")) ? before_ : image_;
        if (size) *size = img.size();
        return img;
    }

private:
    QMutex mutex_;
    QImage image_;
    QImage before_;
};
