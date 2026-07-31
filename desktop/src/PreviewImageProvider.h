#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QMutex>

class PreviewImageProvider : public QQuickImageProvider {
public:
    PreviewImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    void setImage(const QImage& img) {
        QMutexLocker lock(&mutex_);
        image_ = img;
    }

    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        QMutexLocker lock(&mutex_);
        if (size) *size = image_.size();
        return image_;
    }

private:
    QMutex mutex_;
    QImage image_;
};
