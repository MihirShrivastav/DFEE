#pragma once

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QVariant>
#include <QVariantMap>
#include <QVariantList>
#include <QVector>
#include <memory>

#include "dfee/bridge_types.hpp"
#include "EditStore.h"

namespace dfee { class EngineSession; }
class PreviewImageProvider;
class LookPreviewProvider;
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
    Q_PROPERTY(QString exportFormat READ exportFormat WRITE setExportFormat NOTIFY exportSettingsChanged)
    Q_PROPERTY(int jpegQuality READ jpegQuality WRITE setJpegQuality NOTIFY exportSettingsChanged)
    Q_PROPERTY(int exportDpi READ exportDpi WRITE setExportDpi NOTIFY exportSettingsChanged)
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(bool lightroomRoundTrip READ lightroomRoundTrip NOTIFY lightroomRoundTripChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY hasImageChanged)
    // The photo the user asked for most recently (an open still queued behind a busy
    // worker counts), as a local path with forward slashes. Drives filmstrip
    // highlighting and previous/next navigation.
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    // Camera / capture details of the open photo (camera, lens, iso, shutter, aperture,
    // focal, width, height, inputKind, developerProfile); empty until decoded.
    Q_PROPERTY(QVariantMap imageInfo READ imageInfo NOTIFY imageInfoChanged)
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
    // Per-group "has edits" flags for the v2 inspector's section dots:
    // {film, exposure, tone, color, grain_light, print, fine_tune, + v1 group names} -> bool.
    Q_PROPERTY(QVariantMap editedGroups READ editedGroups NOTIFY filmControlsChanged)
    // Films tiles: stock id -> epoch of its ready tile image (image://look/<id>?e=<epoch>);
    // lookEpoch changes with every adjustment; pending = queued + in flight.
    Q_PROPERTY(QVariantMap lookTiles READ lookTiles NOTIFY lookTilesChanged)
    Q_PROPERTY(int lookEpoch READ lookEpoch NOTIFY lookTilesChanged)
    Q_PROPERTY(int lookTilesPending READ lookTilesPending NOTIFY lookTilesChanged)
    // A film shown on the canvas while hovering its tile: changes only the preview
    // request — no history, no catalog write — until it is applied with setStock.
    Q_PROPERTY(QString peekStock READ peekStock NOTIFY peekChanged)

public:
    // provider must be non-null; it must outlive EngineController (the
    // QQmlApplicationEngine that takes ownership is destroyed after main()
    // returns, which is after the controller's dtor).
    explicit EngineController(PreviewImageProvider* provider,
                              EditStore* store,
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
    QVariantMap editedGroups() const;
    // Set one color-grading zone's hue (degrees) and saturation (0..100) as a single
    // history step; zone is shadow, midtone, highlight or global.
    Q_INVOKABLE void setGradeColor(const QString& zone, double hue, double sat);
    // Geometry (Phase 1). setCrop takes a normalized rect on the
    // flipped/rotated/straightened image; rotateQuadrant advances the 90-degree
    // orientation; resetGeometry clears crop/straighten/rotate/flip only.
    Q_INVOKABLE void setCrop(double x, double y, double w, double h);
    Q_INVOKABLE void rotateQuadrant(int steps);
    Q_INVOKABLE void resetGeometry();
    void beginLightroomRoundTrip(const QString& tiffPath);

    bool hasImage() const { return hasImage_; }
    QString currentFile() const { return dirtyIsOpen_ ? pendingFile_ : currentFile_; }
    QVariantMap imageInfo() const { return imageInfo_; }
    // Persist the current photo's edits now (photo switch, export, quit).
    Q_INVOKABLE void flushEdits();
    Q_INVOKABLE void onImageInfo(const QVariantMap& info);
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
    QVariantMap lookTiles() const;
    int lookEpoch() const { return static_cast<int>(lookEpoch_); }
    int lookTilesPending() const { return int(tileQueue_.size()) + (tileInFlight_.isEmpty() ? 0 : 1); }
    void setLookProvider(LookPreviewProvider* provider) { lookProvider_ = provider; }
    QString peekStock() const { return peekStock_; }
    Q_INVOKABLE void beginPeek(const QString& stockId);
    Q_INVOKABLE void endPeek();
    // The tiles the tray shows right now (visible group, in order). Empty stops tiles.
    Q_INVOKABLE void requestLookTiles(const QStringList& stockIds);
    Q_INVOKABLE void onLookProxyReady(const QString& stockId, qulonglong epoch, const QImage& image);
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
    // The file could not be opened at all: drop it (and the previous image) so edits
    // cannot re-trigger it, and show the reason.
    Q_INVOKABLE void onOpenFailed(const QString& msg);
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
    void currentFileChanged();
    void imageInfoChanged();
    void previewChanged();
    void beforeChanged();
    void statusChanged();
    void histogramChanged();
    void historyChanged();
    void presetsChanged();
    void lookTilesChanged();
    void peekChanged();

private:
    // Factory for the default film-control values — the single source of truth
    // shared by the constructor and resetAllEdits().
    static QVariantMap defaultFilmControls();
    void loadStocks();
    void scheduleRender();
    void dispatchScheduledRender();
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
    // Per-photo memory. loadEditsFor replaces the whole edit state (stock, controls,
    // history) with the photo's stored record, or with defaults if it has none.
    void loadEditsFor(const QString& file);
    void saveEditsFor(const QString& file);
    void markEditsDirty();
    bool isEdited() const;
    static QVariantMap sparseControls(const QVariantMap& controls);
    static QVariantMap mergeOnDefaults(const QVariantMap& sparse);
    static bool sameControls(const QVariantMap& a, const QVariantMap& b);
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
    EditStore* store_ = nullptr;          // not owned; null disables memory
    QTimer saveTimer_;                    // debounced store write after an edit
    QString grainRequestFile_;            // photo an Auto-grain request was made for
    QVariantMap imageInfo_;

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
    void bumpLookEpoch();            // adjustments changed: tiles stale, re-queue
    void resetLookTiles();           // a new photo: drop every tile
    void rebuildTileQueue();
    void pumpLookTiles();            // dispatch one tile when the worker is idle
    LookPreviewProvider* lookProvider_ = nullptr;
    quint64 lookEpoch_ = 1;
    QHash<QString, quint64> tileEpochs_;   // stock -> epoch of its ready image
    QSet<QString> failedTiles_;            // failed this epoch: don't retry
    QStringList wantedTiles_;
    QStringList tileQueue_;
    QString tileInFlight_;
    QString peekStock_;
};
