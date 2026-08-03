#pragma once

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QUrl>
#include <QImage>
#include <QHash>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <memory>

#include "dfee/bridge_types.hpp"

namespace dfee { class EngineSession; }
class PreviewImageProvider;
class RenderWorker;

class EngineController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList stockNames READ stockNames NOTIFY stocksChanged)
    // Category-grouped stock list for the picker: rows of {id, name, type, typeLabel},
    // ordered None -> color negative -> color reversal -> monochrome.
    Q_PROPERTY(QVariantList stockModel READ stockModel NOTIFY stocksChanged)
    Q_PROPERTY(QStringList printStockNames READ printStockNames NOTIFY stocksChanged)
    Q_PROPERTY(QString stock READ stock WRITE setStock NOTIFY stockChanged)
    Q_PROPERTY(double filmExposure READ filmExposure WRITE setFilmExposure NOTIFY paramsChanged)
    Q_PROPERTY(double shadowLift READ shadowLift WRITE setShadowLift NOTIFY paramsChanged)
    Q_PROPERTY(QVariantMap filmControls READ filmControls NOTIFY filmControlsChanged)
    Q_PROPERTY(bool grainResolving READ grainResolving NOTIFY grainResolvingChanged)
    Q_PROPERTY(bool currentStockMonochrome READ currentStockMonochrome NOTIFY stockChanged)
    Q_PROPERTY(bool renderedInput READ renderedInput NOTIFY previewChanged)
    Q_PROPERTY(QString exportFormat READ exportFormat WRITE setExportFormat NOTIFY exportSettingsChanged)
    Q_PROPERTY(int jpegQuality READ jpegQuality WRITE setJpegQuality NOTIFY exportSettingsChanged)
    Q_PROPERTY(int exportDpi READ exportDpi WRITE setExportDpi NOTIFY exportSettingsChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(bool lightroomRoundTrip READ lightroomRoundTrip NOTIFY lightroomRoundTripChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // 256-bin per-channel histogram of the current preview (raw counts).
    Q_PROPERTY(QVariantList histogramR READ histogramR NOTIFY histogramChanged)
    Q_PROPERTY(QVariantList histogramG READ histogramG NOTIFY histogramChanged)
    Q_PROPERTY(QVariantList histogramB READ histogramB NOTIFY histogramChanged)

public:
    // provider must be non-null; it must outlive EngineController (the
    // QQmlApplicationEngine that takes ownership is destroyed after main()
    // returns, which is after the controller's dtor).
    explicit EngineController(PreviewImageProvider* provider,
                              QObject* parent = nullptr);
    ~EngineController() override;

    QStringList stockNames() const { return stockNames_; }
    QVariantList stockModel() const { return stockModel_; }
    QStringList printStockNames() const { return printStockNames_; }
    QString stock() const { return stockId_; }
    void setStock(const QString& id);

    double filmExposure() const { return filmExposure_; }
    void setFilmExposure(double v);
    double shadowLift() const { return shadowLift_; }
    void setShadowLift(double v);
    QVariantMap filmControls() const { return filmControls_; }
    bool grainResolving() const { return grainResolving_; }
    bool currentStockMonochrome() const;
    // True when the loaded file is an already-rendered input (TIFF, or a Lightroom
    // round-trip working file) — the only case where the "Preserve rendered tone"
    // control does anything. Used to disable it (Lightroom-style) for RAW files.
    bool renderedInput() const {
        if (lightroomRoundTrip_) return true;
        const QString lower = currentFile_.toLower();
        return lower.endsWith(".tif") || lower.endsWith(".tiff");
    }
    QString exportFormat() const { return exportFormat_; }
    void setExportFormat(const QString& format);
    int jpegQuality() const { return jpegQuality_; }
    void setJpegQuality(int quality);
    int exportDpi() const { return exportDpi_; }
    void setExportDpi(int dpi);
    bool exporting() const { return exporting_; }
    bool lightroomRoundTrip() const { return lightroomRoundTrip_; }

    Q_INVOKABLE QString stockIdAt(int i) const {
        return (i >= 0 && i < stockIds_.size()) ? stockIds_.at(i) : QString("none");
    }
    Q_INVOKABLE QString printStockIdAt(int i) const {
        return (i >= 0 && i < printStockIds_.size()) ? printStockIds_.at(i) : QString("none");
    }

    Q_INVOKABLE void openFile(const QUrl& url);
    Q_INVOKABLE void exportImage();
    Q_INVOKABLE void setFilmControl(const QString& key, const QVariant& value);
    Q_INVOKABLE void setAutoGrain(bool enabled);
    void beginLightroomRoundTrip(const QString& tiffPath);

    bool hasImage() const { return hasImage_; }
    int previewRevision() const { return previewRevision_; }
    QString status() const { return status_; }
    QVariantList histogramR() const { return histogramR_; }
    QVariantList histogramG() const { return histogramG_; }
    QVariantList histogramB() const { return histogramB_; }

    // Called by RenderWorker (via QueuedConnection) to update GUI-thread state.
    Q_INVOKABLE void onPreviewReady();
    Q_INVOKABLE void onHistogram(const QVariantList& r, const QVariantList& g, const QVariantList& b);
    Q_INVOKABLE void onRenderFailed(const QString& msg);
    Q_INVOKABLE void onWorkerBusyChanged(bool busy);
    Q_INVOKABLE void onExportDone(const QString& msg);
    Q_INVOKABLE void onAutoGrainResolved(bool ok, double strength, double size,
                                         double roughness, const QString& error);

signals:
    void stocksChanged();
    void stockChanged();
    void paramsChanged();
    void filmControlsChanged();
    void grainResolvingChanged();
    void exportSettingsChanged();
    void exportingChanged();
    void lightroomRoundTripChanged();
    void hasImageChanged();
    void previewChanged();
    void statusChanged();
    void histogramChanged();

private:
    void loadStocks();
    void scheduleRender();
    [[nodiscard]] dfee::NativePreviewRenderRequest buildPreviewRequest() const;
    [[nodiscard]] dfee::NativeExportRequest buildExportRequest() const;
    bool updateNumericFilmControl(const QString& key, double value);

    std::unique_ptr<dfee::EngineSession> session_;
    QStringList stockNames_;
    QStringList stockIds_;
    QVariantList stockModel_;
    QStringList printStockNames_;
    QStringList printStockIds_;
    QHash<QString, bool> monochromeStocks_;
    QString stockId_ = "none";

    PreviewImageProvider* provider_ = nullptr;
    RenderWorker* worker_ = nullptr;
    QThread workerThread_;

    QString currentFile_;
    bool hasImage_ = false;
    int previewRevision_ = 0;
    QString status_;
    QVariantList histogramR_;
    QVariantList histogramG_;
    QVariantList histogramB_;

    // Film parameters — declared now, wired in Task 4.
    double filmExposure_ = 0.0;
    double shadowLift_ = 0.0;
    QVariantMap filmControls_;
    bool grainResolving_ = false;
    QString exportFormat_ = "png8";
    int jpegQuality_ = 92;
    int exportDpi_ = 300;
    bool exporting_ = false;
    bool lightroomRoundTrip_ = false;

    // Coalescing state (read/written only on GUI thread).
    // dirty_ = a deferred op is pending while the worker is busy.
    // dirtyIsOpen_ = the pending op is an openAndRender (not just a re-render).
    // pendingFile_ = the file for the pending open (latches the latest openFile call).
    bool workerBusy_ = false;
    bool dirty_ = false;
    bool dirtyIsOpen_ = false;
    QString pendingFile_;
};
