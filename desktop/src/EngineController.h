#pragma once

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QImage>
#include <QHash>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QVector>
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
    Q_PROPERTY(bool hasBefore READ hasBefore NOTIFY beforeChanged)
    Q_PROPERTY(int beforeRevision READ beforeRevision NOTIFY beforeChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // 256-bin per-channel histogram of the current preview (raw counts).
    Q_PROPERTY(QVariantList histogramR READ histogramR NOTIFY histogramChanged)
    Q_PROPERTY(QVariantList histogramG READ histogramG NOTIFY histogramChanged)
    Q_PROPERTY(QVariantList histogramB READ histogramB NOTIFY histogramChanged)
    Q_PROPERTY(QVariantList vectorscope READ vectorscope NOTIFY histogramChanged)
    // Edit history (newest-first list of {label}), the current step's row in that
    // list, and undo/redo availability. In-memory, reseeded per opened image.
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(int historyIndex READ historyIndex NOTIFY historyChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // Saved presets on disk (Documents/Film Lab/Presets) as {id, name, group}.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    // User group names (first-level subdirectories), sorted.
    Q_PROPERTY(QStringList presetGroups READ presetGroups NOTIFY presetsChanged)

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
    // Reset every edit — film stock, exposure placement and all develop
    // controls — back to the just-opened baseline for the current image.
    Q_INVOKABLE void resetAllEdits();
    // Restore a named editing group from the same defaults used by Reset All.
    Q_INVOKABLE void resetControlGroup(const QString& group);
    // Geometry (Phase 1). setCrop takes a normalized rect on the
    // flipped/rotated/straightened image; rotateQuadrant advances the 90-degree
    // orientation; resetGeometry clears crop/straighten/rotate/flip only.
    Q_INVOKABLE void setCrop(double x, double y, double w, double h);
    Q_INVOKABLE void rotateQuadrant(int steps);
    Q_INVOKABLE void resetGeometry();
    void beginLightroomRoundTrip(const QString& tiffPath);

    bool hasImage() const { return hasImage_; }
    bool hasBefore() const { return hasBefore_; }
    int beforeRevision() const { return beforeRevision_; }
    int previewRevision() const { return previewRevision_; }
    QString status() const { return status_; }
    QVariantList histogramR() const { return histogramR_; }
    QVariantList histogramG() const { return histogramG_; }
    QVariantList histogramB() const { return histogramB_; }
    QVariantList vectorscope() const { return vectorscope_; }

    QVariantList history() const;
    int historyIndex() const { return history_.isEmpty() ? -1 : (int(history_.size()) - 1 - historyIndex_); }
    bool canUndo() const { return historyIndex_ > 0; }
    bool canRedo() const { return historyIndex_ >= 0 && historyIndex_ < int(history_.size()) - 1; }
    QVariantList presets() const { return presets_; }
    QStringList presetGroups() const { return presetGroups_; }

    // Edit history navigation. jumpToHistory takes a display row (0 = newest).
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void jumpToHistory(int displayRow);

    // Presets. A recipe is stock + look controls (geometry excluded). savePreset
    // writes the current recipe into an optional group (subdirectory); a preset id
    // is its path relative to the presets root without extension ("group/name" or
    // just "name"). applyPreset / deletePreset act on that id. Returns false on
    // I/O failure. createGroup makes an (empty) group; presetExists tests a target.
    Q_INVOKABLE bool savePreset(const QString& name, const QString& group = QString());
    Q_INVOKABLE void applyPreset(const QString& id);
    Q_INVOKABLE bool deletePreset(const QString& id);
    Q_INVOKABLE bool createGroup(const QString& name);
    Q_INVOKABLE bool presetExists(const QString& name, const QString& group) const;
    Q_INVOKABLE void refreshPresets();
    // Management. editPreset renames and/or moves a preset (newGroup "" = ungrouped),
    // rewriting the file's name/group and tidying an emptied source group.
    // renameGroup / deleteGroup act on a group directory (deleteGroup removes its
    // presets too). All return false on failure (e.g. a name collision).
    Q_INVOKABLE bool editPreset(const QString& id, const QString& newName, const QString& newGroup);
    Q_INVOKABLE bool renameGroup(const QString& oldName, const QString& newName);
    Q_INVOKABLE bool deleteGroup(const QString& name);

    // Called by RenderWorker (via QueuedConnection) to update GUI-thread state.
    Q_INVOKABLE void onPreviewReady();
    Q_INVOKABLE void onBeforeReady(bool ok);
    Q_INVOKABLE void onHistogram(
        const QVariantList& r, const QVariantList& g, const QVariantList& b, const QVariantList& scope);
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
    void beforeChanged();
    void statusChanged();
    void histogramChanged();
    void historyChanged();
    void presetsChanged();

private:
    // Factory for the default film-control values — the single source of truth
    // shared by the constructor and resetAllEdits().
    static QVariantMap defaultFilmControls();
    void loadStocks();
    void scheduleRender();
    void dispatchScheduledRender();
    // Pick the sensible default exposure placement for the just-opened file:
    // already-developed inputs (TIFF / Lightroom round-trip) default to "as shot"
    // (they're exposed already); RAWs default to "auto balanced".
    void applyDefaultPlacement();
    [[nodiscard]] dfee::NativePreviewRenderRequest buildPreviewRequest() const;
    [[nodiscard]] dfee::NativeExportRequest buildExportRequest() const;
    bool updateNumericFilmControl(const QString& key, double value);

    // ── Edit history ────────────────────────────────────────────────────
    struct HistoryEntry {
        QString label;         // human-readable ("Contrast +8")
        QString coalesceKey;   // consecutive edits with the same non-empty key merge
        QString stock;         // full snapshot: stock + all controls
        QVariantMap controls;
    };
    // Record the current {stock, controls} as a new step (or merge into the last
    // step when coalesceKey matches). No-op before an image is open.
    void recordHistory(const QString& label, const QString& coalesceKey = QString());
    // Replace history with a single baseline step for a freshly opened image.
    void seedHistory(const QString& label);
    // Restore the snapshot at an internal index (0 = baseline) without recording.
    void restoreHistory(int internalIndex);
    static QString friendlyLabel(const QString& key);   // control key -> display name
    static bool isGeometryKey(const QString& key);

    // ── Presets ─────────────────────────────────────────────────────────
    QString presetsDir() const;                          // ensures the dir exists
    QVariantMap captureRecipe() const;                   // {stock, controls (look-only)}
    void applyRecipe(const QVariantMap& recipe, const QString& label);
    void tidyGroupDir(const QString& group);             // remove a now-empty group folder

    QVector<HistoryEntry> history_;
    int historyIndex_ = -1;      // current step within history_ (internal, 0 = oldest)
    bool pendingSeed_ = false;   // seed a baseline step on the next preview-ready
    QVariantList presets_;
    QStringList presetGroups_;

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
    bool hasBefore_ = false;
    int beforeRevision_ = 0;
    int previewRevision_ = 0;
    QString status_;
    QVariantList histogramR_;
    QVariantList histogramG_;
    QVariantList histogramB_;
    QVariantList vectorscope_;

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
    // Slider controls can emit many intermediate values per second. Retain only
    // the latest immutable request after a short quiet period so the native
    // worker spends its time rendering useful previews rather than stale ones.
    QTimer previewDebounceTimer_;
};
