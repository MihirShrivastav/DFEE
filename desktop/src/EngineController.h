#pragma once

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QImage>
#include <memory>

namespace dfee { class EngineSession; }
class PreviewImageProvider;
class RenderWorker;

class EngineController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList stockNames READ stockNames NOTIFY stocksChanged)
    Q_PROPERTY(QString stock READ stock WRITE setStock NOTIFY stockChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit EngineController(QObject* parent = nullptr);
    ~EngineController() override;

    QStringList stockNames() const { return stockNames_; }
    QString stock() const { return stockId_; }
    void setStock(const QString& id);

    Q_INVOKABLE QString stockIdAt(int i) const {
        return (i >= 0 && i < stockIds_.size()) ? stockIds_.at(i) : QString("none");
    }

    Q_INVOKABLE void openFile(const QUrl& url);

    void setProvider(PreviewImageProvider* p);


    bool hasImage() const { return hasImage_; }
    int previewRevision() const { return previewRevision_; }
    QString status() const { return status_; }

    // Called by RenderWorker (via QueuedConnection) to update GUI-thread state.
    Q_INVOKABLE void onPreviewReady(const QImage& img);
    Q_INVOKABLE void onRenderFailed(const QString& msg);
    Q_INVOKABLE void onWorkerBusyChanged(bool busy);

signals:
    void stocksChanged();
    void stockChanged();
    void hasImageChanged();
    void previewChanged();
    void statusChanged();

private:
    void loadStocks();
    void scheduleRender();

    std::unique_ptr<dfee::EngineSession> session_;
    QStringList stockNames_;
    QStringList stockIds_;
    QString stockId_ = "none";

    PreviewImageProvider* provider_ = nullptr;
    RenderWorker* worker_ = nullptr;
    QThread workerThread_;

    QString currentFile_;
    bool hasImage_ = false;
    int previewRevision_ = 0;
    QString status_;

    // Film parameters — declared now, wired in Task 4.
    double filmExposure_ = 0.0;
    double shadowLift_ = 0.0;

    // Coalescing state (read/written only on GUI thread).
    bool workerBusy_ = false;
    bool dirty_ = false;
};
