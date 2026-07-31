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
    Q_PROPERTY(double filmExposure READ filmExposure WRITE setFilmExposure NOTIFY paramsChanged)
    Q_PROPERTY(double shadowLift READ shadowLift WRITE setShadowLift NOTIFY paramsChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    // provider must be non-null; it must outlive EngineController (the
    // QQmlApplicationEngine that takes ownership is destroyed after main()
    // returns, which is after the controller's dtor).
    explicit EngineController(PreviewImageProvider* provider,
                              QObject* parent = nullptr);
    ~EngineController() override;

    QStringList stockNames() const { return stockNames_; }
    QString stock() const { return stockId_; }
    void setStock(const QString& id);

    double filmExposure() const { return filmExposure_; }
    void setFilmExposure(double v);
    double shadowLift() const { return shadowLift_; }
    void setShadowLift(double v);

    Q_INVOKABLE QString stockIdAt(int i) const {
        return (i >= 0 && i < stockIds_.size()) ? stockIds_.at(i) : QString("none");
    }

    Q_INVOKABLE void openFile(const QUrl& url);

    bool hasImage() const { return hasImage_; }
    int previewRevision() const { return previewRevision_; }
    QString status() const { return status_; }

    // Called by RenderWorker (via QueuedConnection) to update GUI-thread state.
    Q_INVOKABLE void onPreviewReady();
    Q_INVOKABLE void onRenderFailed(const QString& msg);
    Q_INVOKABLE void onWorkerBusyChanged(bool busy);

signals:
    void stocksChanged();
    void stockChanged();
    void paramsChanged();
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
    // dirty_ = a deferred op is pending while the worker is busy.
    // dirtyIsOpen_ = the pending op is an openAndRender (not just a re-render).
    // pendingFile_ = the file for the pending open (latches the latest openFile call).
    bool workerBusy_ = false;
    bool dirty_ = false;
    bool dirtyIsOpen_ = false;
    QString pendingFile_;
};
