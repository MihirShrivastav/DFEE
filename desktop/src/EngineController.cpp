#include "EngineController.h"
#include "RenderWorker.h"
#include "PreviewImageProvider.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QDebug>

#include <algorithm>

#ifndef DFEE_REPO_ROOT
#  define DFEE_REPO_ROOT "."
#endif

EngineController::EngineController(PreviewImageProvider* provider,
                                   QObject* parent)
    : QObject(parent)
    , session_(std::make_unique<dfee::EngineSession>(
          std::filesystem::path(DFEE_REPO_ROOT)))
    , provider_(provider)
{
    filmControls_ = {
        {"exposure_placement", "auto_balanced"},
        {"film_exposure_ev", 0.0},
        {"adaptive", true},
        {"highlight_rolloff", 100.0},
        {"film_contrast", 100.0},
        {"shadow_lift", 0.0},
        {"film_color_density", 100.0},
        {"emulsion_color_density", 0.0},
        {"highlight_color_hold", 0.0},
        {"shadow_color_retention", 0.0},
        {"grain_auto", true},
        {"grain_strength", -1.0},
        {"grain_size", -1.0},
        {"grain_roughness", -1.0},
        {"halation_strength", 100.0},
        {"halation_threshold", 50.0},
        {"bloom", 0.0},
    };
    // list_profiles() is called on the GUI thread BEFORE the worker thread starts,
    // so there is no concurrent access.
    loadStocks();

    // provider_ is already set (constructor arg); worker receives it before the
    // thread starts, so provider_ is never written after workerThread_.start().
    worker_ = new RenderWorker(session_.get(), this, provider_);
    worker_->moveToThread(&workerThread_);
    workerThread_.start();
}

EngineController::~EngineController()
{
    workerThread_.quit();
    workerThread_.wait();
    // worker_ is now safe to delete; it's a child-less QObject, so plain delete.
    delete worker_;
}

void EngineController::loadStocks()
{
    stockNames_.clear();
    stockIds_.clear();
    stockNames_ << "None";
    stockIds_ << "none";
    const dfee::NativeProfilesResponse profiles = session_->list_profiles();
    for (const auto& s : profiles.stocks) {
        stockNames_ << QString::fromStdString(s.stock_name);
        stockIds_ << QString::fromStdString(s.stock_id);
        monochromeStocks_.insert(QString::fromStdString(s.stock_id),
                                 s.stock_type == "monochrome");
    }
    emit stocksChanged();
}

void EngineController::setStock(const QString& id)
{
    if (stockId_ == id) return;
    stockId_ = id;
    emit stockChanged();
    scheduleRender();
}

void EngineController::setFilmExposure(double v)
{
    if (!updateNumericFilmControl("film_exposure_ev", v)) return;
    filmExposure_ = filmControls_.value("film_exposure_ev").toDouble();
    emit paramsChanged();
}

void EngineController::setShadowLift(double v)
{
    if (!updateNumericFilmControl("shadow_lift", v)) return;
    shadowLift_ = filmControls_.value("shadow_lift").toDouble();
    emit paramsChanged();
}

bool EngineController::currentStockMonochrome() const
{
    return monochromeStocks_.value(stockId_, false);
}

void EngineController::setExportFormat(const QString& format)
{
    static const QStringList formats{"png8", "png16", "tiff", "jpeg"};
    const QString canonical = formats.contains(format) ? format : "png8";
    if (exportFormat_ == canonical) return;
    exportFormat_ = canonical;
    emit exportSettingsChanged();
}

void EngineController::setJpegQuality(int quality)
{
    const int bounded = std::clamp(quality, 1, 100);
    if (jpegQuality_ == bounded) return;
    jpegQuality_ = bounded;
    emit exportSettingsChanged();
}

void EngineController::setExportDpi(int dpi)
{
    const int bounded = std::clamp(dpi, 1, 65535);
    if (exportDpi_ == bounded) return;
    exportDpi_ = bounded;
    emit exportSettingsChanged();
}

void EngineController::beginLightroomRoundTrip(const QString& tiffPath)
{
    const QFileInfo source(tiffPath);
    const QString suffix = source.suffix().toLower();
    if (!source.isAbsolute() || !source.isFile() || (suffix != "tif" && suffix != "tiff")) {
        status_ = "Lightroom edit requires an existing TIFF working file.";
        emit statusChanged();
        qWarning() << "DFEE Lightroom round-trip rejected" << tiffPath;
        return;
    }

    lightroomRoundTrip_ = true;
    exportFormat_ = "tiff";
    currentFile_ = source.absoluteFilePath();
    emit exportSettingsChanged();
    emit lightroomRoundTripChanged();
    qInfo() << "DFEE Lightroom round-trip opened" << currentFile_;
    openFile(QUrl::fromLocalFile(currentFile_));
}

bool EngineController::updateNumericFilmControl(const QString& key, double value)
{
    struct Range { double minimum; double maximum; };
    static const QHash<QString, Range> ranges = {
        {"film_exposure_ev", {-3.0, 3.0}},
        {"highlight_rolloff", {0.0, 200.0}},
        {"film_contrast", {0.0, 200.0}},
        {"shadow_lift", {-100.0, 100.0}},
        {"film_color_density", {0.0, 200.0}},
        {"emulsion_color_density", {-100.0, 100.0}},
        {"highlight_color_hold", {-100.0, 100.0}},
        {"shadow_color_retention", {-100.0, 100.0}},
        {"grain_strength", {0.0, 2.0}},
        {"grain_size", {0.1, 2.0}},
        {"grain_roughness", {0.0, 1.0}},
        {"halation_strength", {0.0, 200.0}},
        {"halation_threshold", {0.0, 100.0}},
        {"bloom", {0.0, 100.0}},
    };
    const auto it = ranges.constFind(key);
    if (it == ranges.cend()) {
        qWarning() << "DFEE: unsupported film control" << key;
        return false;
    }
    const double bounded = std::clamp(value, it->minimum, it->maximum);
    if (qFuzzyCompare(filmControls_.value(key).toDouble() + 1.0, bounded + 1.0)) {
        return false;
    }
    filmControls_.insert(key, bounded);
    emit filmControlsChanged();
    scheduleRender();
    return true;
}

void EngineController::setFilmControl(const QString& key, const QVariant& value)
{
    if (key == "adaptive" || key == "grain_auto") {
        const bool enabled = value.toBool();
        if (filmControls_.value(key).toBool() == enabled) return;
        filmControls_.insert(key, enabled);
        emit filmControlsChanged();
        scheduleRender();
        return;
    }
    if (key == "exposure_placement") {
        const QString placement = value.toString() == "as_shot" ? "as_shot" : "auto_balanced";
        if (filmControls_.value(key).toString() == placement) return;
        filmControls_.insert(key, placement);
        emit filmControlsChanged();
        scheduleRender();
        return;
    }
    updateNumericFilmControl(key, value.toDouble());
}

void EngineController::setAutoGrain(bool enabled)
{
    const bool isAuto = filmControls_.value("grain_auto").toBool();
    if (enabled == isAuto || grainResolving_) return;
    if (enabled) {
        filmControls_.insert("grain_auto", true);
        filmControls_.insert("grain_strength", -1.0);
        filmControls_.insert("grain_size", -1.0);
        filmControls_.insert("grain_roughness", -1.0);
        emit filmControlsChanged();
        scheduleRender();
        return;
    }
    if (currentFile_.isEmpty() || stockId_ == "none") {
        status_ = "Select an image and film stock before customizing grain.";
        emit statusChanged();
        return;
    }
    grainResolving_ = true;
    emit grainResolvingChanged();
    const dfee::NativePreviewRenderRequest request = buildPreviewRequest();
    QMetaObject::invokeMethod(worker_, [worker = worker_, request]() {
        worker->resolveAutoGrain(request);
    }, Qt::QueuedConnection);
}

void EngineController::openFile(const QUrl& url)
{
    const QString file = url.toLocalFile();
    status_ = "Loading " + QFileInfo(file).fileName();
    emit statusChanged();

    if (workerBusy_) {
        // Latch the latest file for the pending open; mark it as an open (not
        // a plain re-render) so onWorkerBusyChanged posts openAndRender, which
        // runs select_file + decode_raw before rendering.
        pendingFile_  = file;
        dirty_        = true;
        dirtyIsOpen_  = true;
        // Do NOT update currentFile_ yet — the in-flight operation still owns
        // the session's decoded buffer.  currentFile_ is updated when the
        // deferred open actually fires.
        return;
    }

    currentFile_ = file;
    workerBusy_  = true;
    const dfee::NativePreviewRenderRequest request = buildPreviewRequest();
    QMetaObject::invokeMethod(worker_, [worker = worker_, request]() {
        worker->openAndRender(request);
    }, Qt::QueuedConnection);
}

void EngineController::scheduleRender()
{
    if (currentFile_.isEmpty() || exporting_) return;

    if (workerBusy_) {
        dirty_ = true;
        return;
    }

    workerBusy_ = true;
    const dfee::NativePreviewRenderRequest request = buildPreviewRequest();
    QMetaObject::invokeMethod(worker_, [worker = worker_, request]() {
        worker->render(request);
    }, Qt::QueuedConnection);
}

dfee::NativePreviewRenderRequest EngineController::buildPreviewRequest() const
{
    dfee::NativePreviewRenderRequest request;
    request.filename = currentFile_.toStdString();
    request.stock = stockId_.toStdString();
    request.effect_pipeline_version = "filmic_v3";
    request.exposure_placement = filmControls_.value("exposure_placement").toString().toStdString();
    request.film_exposure_ev = static_cast<float>(filmControls_.value("film_exposure_ev").toDouble());
    request.adaptive = filmControls_.value("adaptive").toBool();
    request.highlight_rolloff = static_cast<float>(filmControls_.value("highlight_rolloff").toDouble());
    request.film_contrast = static_cast<float>(filmControls_.value("film_contrast").toDouble());
    request.shadow_lift = static_cast<float>(filmControls_.value("shadow_lift").toDouble());
    request.film_color_density = static_cast<float>(filmControls_.value("film_color_density").toDouble());
    request.emulsion_color_density = static_cast<float>(filmControls_.value("emulsion_color_density").toDouble());
    request.highlight_color_hold = static_cast<float>(filmControls_.value("highlight_color_hold").toDouble());
    request.shadow_color_retention = static_cast<float>(filmControls_.value("shadow_color_retention").toDouble());
    const bool grainAuto = filmControls_.value("grain_auto").toBool();
    request.grain = grainAuto ? "Auto" : "Custom";
    request.grain_strength = static_cast<float>(filmControls_.value("grain_strength").toDouble());
    request.grain_size = static_cast<float>(filmControls_.value("grain_size").toDouble());
    request.grain_roughness = static_cast<float>(filmControls_.value("grain_roughness").toDouble());
    request.halation = "Auto";
    request.halation_strength = static_cast<float>(filmControls_.value("halation_strength").toDouble());
    request.halation_threshold = static_cast<float>(filmControls_.value("halation_threshold").toDouble());
    request.bloom = static_cast<float>(filmControls_.value("bloom").toDouble());
    return request;
}

dfee::NativeExportRequest EngineController::buildExportRequest() const
{
    dfee::NativeExportRequest request;
    static_cast<dfee::NativePreviewRenderRequest&>(request) = buildPreviewRequest();
    request.export_format = exportFormat_.toStdString();
    request.jpeg_quality = jpegQuality_;
    request.export_dpi = exportDpi_;
    if (lightroomRoundTrip_) {
        request.export_format = "tiff";
        request.output_path = std::filesystem::path(currentFile_.toStdString());
    }
    return request;
}

// --- Callbacks marshalled back to the GUI thread ---

void EngineController::onPreviewReady()
{
    hasImage_ = true;
    emit hasImageChanged();
    previewRevision_++;
    emit previewChanged();
    status_.clear();
    emit statusChanged();
}

void EngineController::exportImage()
{
    if (currentFile_.isEmpty()) return;
    status_ = "Exporting…";
    emit statusChanged();
    exporting_ = true;
    emit exportingChanged();
    status_ = lightroomRoundTrip_
        ? "Rendering and saving the Lightroom working TIFF..."
        : "Exporting full resolution...";
    emit statusChanged();
    const dfee::NativeExportRequest request = buildExportRequest();
    QMetaObject::invokeMethod(worker_, [worker = worker_, request]() {
        worker->exportImage(request);
    }, Qt::QueuedConnection);
}

void EngineController::onExportDone(const QString& msg)
{
    exporting_ = false;
    emit exportingChanged();
    status_ = msg;
    emit statusChanged();
    qDebug() << "DFEE export:" << msg;
}

void EngineController::onAutoGrainResolved(bool ok, double strength, double size,
                                           double roughness, const QString& error)
{
    grainResolving_ = false;
    emit grainResolvingChanged();
    if (!ok) {
        status_ = "Could not resolve Auto grain: " + error;
        emit statusChanged();
        if (qEnvironmentVariableIsSet("DFEE_SELFTEST_GRAIN")) {
            qDebug() << "SELFTEST Auto grain resolution failed:" << error;
        }
        return;
    }
    filmControls_.insert("grain_auto", false);
    filmControls_.insert("grain_strength", strength);
    filmControls_.insert("grain_size", size);
    filmControls_.insert("grain_roughness", roughness);
    emit filmControlsChanged();
    if (qEnvironmentVariableIsSet("DFEE_SELFTEST_GRAIN")) {
        qDebug() << "SELFTEST Auto grain materialized" << strength << size << roughness;
    }
    scheduleRender();
}

void EngineController::onRenderFailed(const QString& msg)
{
    status_ = msg;
    emit statusChanged();
    qDebug() << "DFEE:" << msg;
}

void EngineController::onWorkerBusyChanged(bool busy)
{
    workerBusy_ = busy;
    if (!busy && dirty_) {
        dirty_ = false;

        if (dirtyIsOpen_) {
            // A new file was opened while the worker was busy.  We must run
            // select_file + decode_raw for the new file before rendering — a
            // plain render() would use the OLD decoded buffer.
            dirtyIsOpen_  = false;
            currentFile_  = pendingFile_;
            pendingFile_.clear();

            workerBusy_ = true;
            const dfee::NativePreviewRenderRequest request = buildPreviewRequest();
            QMetaObject::invokeMethod(worker_, [worker = worker_, request]() {
                worker->openAndRender(request);
            }, Qt::QueuedConnection);
        } else {
            // Just a parameter change (stock / exposure / shadow lift); the
            // file is already decoded — a render-only kick is correct.
            scheduleRender();
        }
    }
}
