#include "EngineController.h"
#include "RenderWorker.h"
#include "PreviewImageProvider.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>

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
    filmControls_ = defaultFilmControls();
    // list_profiles() is called on the GUI thread BEFORE the worker thread starts,
    // so there is no concurrent access.
    loadStocks();

    // provider_ is already set (constructor arg); worker receives it before the
    // thread starts, so provider_ is never written after workerThread_.start().
    worker_ = new RenderWorker(session_.get(), this, provider_);
    worker_->moveToThread(&workerThread_);
    workerThread_.start();
}

QVariantMap EngineController::defaultFilmControls()
{
    return QVariantMap{
        {"exposure_placement", "auto_balanced"},
        {"film_exposure_ev", 0.0},
        {"adaptive", true},
        {"rendered_input", 80.0},
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
        // Basic tone/colour (generic grade on top of the film response)
        {"exposure", 0.0},
        {"contrast", 0.0},
        {"highlights", 0.0},
        {"shadows", 0.0},
        {"whites", 0.0},
        {"blacks", 0.0},
        {"midtones", 0.0},
        {"temp", 0.0},
        {"tint", 0.0},
        {"saturation", 0.0},
        {"vibrance", 0.0},
        // Detail & optics
        {"texture", 0.0},
        {"clarity", 0.0},
        {"dehaze", 0.0},
        {"sharpness", 0.0},
        {"sharpness_mask", 0.5},
        // Print finish
        {"print_stock", "none"},
        {"print_strength", 1.0},
        {"print_c", 0.0},
        {"print_m", 0.0},
        {"print_y", 0.0},
        {"print_contrast", 0.0},
        {"print_black_point", 0.0},
        // Geometry (Phase 1) — normalized crop rect, fine straighten, 90-degree
        // quadrant, mirror. Identity defaults = whole frame, no transform.
        {"crop_x", 0.0}, {"crop_y", 0.0}, {"crop_w", 1.0}, {"crop_h", 1.0},
        {"straighten_deg", 0.0},
        {"rotate_quadrant", 0},
        {"flip_h", false},
        {"flip_v", false},
        // Colour grading — 3-way + global (hue 0..360, sat 0..100, lum -100..100)
        {"cg_shadow_hue", 0.0}, {"cg_shadow_sat", 0.0}, {"cg_shadow_lum", 0.0},
        {"cg_midtone_hue", 0.0}, {"cg_midtone_sat", 0.0}, {"cg_midtone_lum", 0.0},
        {"cg_highlight_hue", 0.0}, {"cg_highlight_sat", 0.0}, {"cg_highlight_lum", 0.0},
        {"cg_global_hue", 0.0}, {"cg_global_sat", 0.0}, {"cg_global_lum", 0.0},
        {"cg_balance", 0.0}, {"cg_blending", 0.0}, {"cg_crossbalance", 0.0},
        // HSL — 8 bands x hue/sat/lum (-100..100)
        {"hsl_red_h", 0.0}, {"hsl_red_s", 0.0}, {"hsl_red_l", 0.0},
        {"hsl_orange_h", 0.0}, {"hsl_orange_s", 0.0}, {"hsl_orange_l", 0.0},
        {"hsl_yellow_h", 0.0}, {"hsl_yellow_s", 0.0}, {"hsl_yellow_l", 0.0},
        {"hsl_green_h", 0.0}, {"hsl_green_s", 0.0}, {"hsl_green_l", 0.0},
        {"hsl_aqua_h", 0.0}, {"hsl_aqua_s", 0.0}, {"hsl_aqua_l", 0.0},
        {"hsl_blue_h", 0.0}, {"hsl_blue_s", 0.0}, {"hsl_blue_l", 0.0},
        {"hsl_purple_h", 0.0}, {"hsl_purple_s", 0.0}, {"hsl_purple_l", 0.0},
        {"hsl_magenta_h", 0.0}, {"hsl_magenta_s", 0.0}, {"hsl_magenta_l", 0.0},
    };
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
    printStockNames_.clear();
    printStockIds_.clear();
    printStockNames_ << "None";
    printStockIds_ << "none";
    const dfee::NativeProfilesResponse profiles = session_->list_profiles();
    for (const auto& s : profiles.stocks) {
        stockNames_ << QString::fromStdString(s.stock_name);
        stockIds_ << QString::fromStdString(s.stock_id);
        monochromeStocks_.insert(QString::fromStdString(s.stock_id),
                                 s.stock_type == "monochrome");
    }
    for (const auto& p : profiles.print_stocks) {
        printStockNames_ << QString::fromStdString(p.print_stock_name);
        printStockIds_ << QString::fromStdString(p.print_stock_id);
    }

    // Category-grouped model for the stock picker (None first, then by type).
    stockModel_.clear();
    {
        QVariantMap none;
        none["id"] = "none";
        none["name"] = "None";
        none["type"] = "";
        none["typeLabel"] = "";
        stockModel_.append(none);
    }
    const auto typeLabel = [](const std::string& t) -> QString {
        if (t == "color_negative") return QStringLiteral("Color negative");
        if (t == "color_reversal") return QStringLiteral("Color reversal");
        if (t == "monochrome") return QStringLiteral("Monochrome");
        return QStringLiteral("Other");
    };
    for (const char* cat : {"color_negative", "color_reversal", "monochrome"}) {
        for (const auto& s : profiles.stocks) {
            if (s.stock_type != cat) continue;
            QVariantMap m;
            m["id"] = QString::fromStdString(s.stock_id);
            m["name"] = QString::fromStdString(s.stock_name);
            m["type"] = QString::fromStdString(s.stock_type);
            m["typeLabel"] = typeLabel(s.stock_type);
            stockModel_.append(m);
        }
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
        // Rendered-input handling (TIFF/already-developed files only)
        {"rendered_input", {0.0, 100.0}},
        // Basic tone/colour
        {"exposure", {-3.0, 3.0}},
        {"contrast", {-100.0, 100.0}},
        {"highlights", {-100.0, 100.0}},
        {"shadows", {-100.0, 100.0}},
        {"whites", {-100.0, 100.0}},
        {"blacks", {-100.0, 100.0}},
        {"midtones", {-100.0, 100.0}},
        {"temp", {-100.0, 100.0}},
        {"tint", {-100.0, 100.0}},
        {"saturation", {-100.0, 100.0}},
        {"vibrance", {-100.0, 100.0}},
        // Detail & optics
        {"texture", {-100.0, 100.0}},
        {"clarity", {-100.0, 100.0}},
        {"dehaze", {-100.0, 100.0}},
        {"sharpness", {0.0, 2.0}},
        {"sharpness_mask", {0.0, 1.0}},
        // Print finish
        {"print_strength", {0.0, 2.0}},
        {"print_c", {-100.0, 100.0}},
        {"print_m", {-100.0, 100.0}},
        {"print_y", {-100.0, 100.0}},
        {"print_contrast", {-100.0, 100.0}},
        {"print_black_point", {-100.0, 100.0}},
        // Geometry
        {"straighten_deg", {-45.0, 45.0}},
        // Colour grading
        {"cg_shadow_hue", {0.0, 360.0}}, {"cg_shadow_sat", {0.0, 100.0}}, {"cg_shadow_lum", {-100.0, 100.0}},
        {"cg_midtone_hue", {0.0, 360.0}}, {"cg_midtone_sat", {0.0, 100.0}}, {"cg_midtone_lum", {-100.0, 100.0}},
        {"cg_highlight_hue", {0.0, 360.0}}, {"cg_highlight_sat", {0.0, 100.0}}, {"cg_highlight_lum", {-100.0, 100.0}},
        {"cg_global_hue", {0.0, 360.0}}, {"cg_global_sat", {0.0, 100.0}}, {"cg_global_lum", {-100.0, 100.0}},
        {"cg_balance", {-100.0, 100.0}}, {"cg_blending", {0.0, 100.0}}, {"cg_crossbalance", {-100.0, 100.0}},
        // HSL — 8 bands
        {"hsl_red_h", {-100.0, 100.0}}, {"hsl_red_s", {-100.0, 100.0}}, {"hsl_red_l", {-100.0, 100.0}},
        {"hsl_orange_h", {-100.0, 100.0}}, {"hsl_orange_s", {-100.0, 100.0}}, {"hsl_orange_l", {-100.0, 100.0}},
        {"hsl_yellow_h", {-100.0, 100.0}}, {"hsl_yellow_s", {-100.0, 100.0}}, {"hsl_yellow_l", {-100.0, 100.0}},
        {"hsl_green_h", {-100.0, 100.0}}, {"hsl_green_s", {-100.0, 100.0}}, {"hsl_green_l", {-100.0, 100.0}},
        {"hsl_aqua_h", {-100.0, 100.0}}, {"hsl_aqua_s", {-100.0, 100.0}}, {"hsl_aqua_l", {-100.0, 100.0}},
        {"hsl_blue_h", {-100.0, 100.0}}, {"hsl_blue_s", {-100.0, 100.0}}, {"hsl_blue_l", {-100.0, 100.0}},
        {"hsl_purple_h", {-100.0, 100.0}}, {"hsl_purple_s", {-100.0, 100.0}}, {"hsl_purple_l", {-100.0, 100.0}},
        {"hsl_magenta_h", {-100.0, 100.0}}, {"hsl_magenta_s", {-100.0, 100.0}}, {"hsl_magenta_l", {-100.0, 100.0}},
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
    if (key == "print_stock") {
        const QString id = value.toString();
        if (filmControls_.value(key).toString() == id) return;
        filmControls_.insert(key, id);
        emit filmControlsChanged();
        scheduleRender();
        return;
    }
    if (key == "adaptive" || key == "grain_auto" || key == "flip_h" || key == "flip_v") {
        const bool enabled = value.toBool();
        if (filmControls_.value(key).toBool() == enabled) return;
        filmControls_.insert(key, enabled);
        emit filmControlsChanged();
        scheduleRender();
        return;
    }
    if (key == "rotate_quadrant") {
        const int q = ((value.toInt() % 4) + 4) % 4;
        if (filmControls_.value(key).toInt() == q) return;
        filmControls_.insert(key, q);
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

void EngineController::resetAllEdits()
{
    // A no-op guard so a stray click before any image is loaded does nothing
    // visible, and a fresh reset never fires a pointless render.
    filmControls_ = defaultFilmControls();
    filmExposure_ = filmControls_.value("film_exposure_ev").toDouble();
    shadowLift_ = filmControls_.value("shadow_lift").toDouble();
    // exposure_placement default depends on the loaded file (as_shot for
    // already-developed TIFFs, auto_balanced for RAW); applyDefaultPlacement
    // corrects the map's generic default to the right one for this image.
    applyDefaultPlacement();

    if (stockId_ != "none") {
        stockId_ = "none";
        emit stockChanged();
    }
    emit filmControlsChanged();
    emit paramsChanged();
    scheduleRender();
}

void EngineController::setCrop(double x, double y, double w, double h)
{
    const double cx = std::clamp(x, 0.0, 1.0);
    const double cy = std::clamp(y, 0.0, 1.0);
    const double cw = std::clamp(w, 0.0, 1.0 - cx);
    const double ch = std::clamp(h, 0.0, 1.0 - cy);
    const bool unchanged =
        qFuzzyCompare(filmControls_.value("crop_x").toDouble() + 1.0, cx + 1.0) &&
        qFuzzyCompare(filmControls_.value("crop_y").toDouble() + 1.0, cy + 1.0) &&
        qFuzzyCompare(filmControls_.value("crop_w").toDouble() + 1.0, cw + 1.0) &&
        qFuzzyCompare(filmControls_.value("crop_h").toDouble() + 1.0, ch + 1.0);
    if (unchanged) return;
    filmControls_.insert("crop_x", cx);
    filmControls_.insert("crop_y", cy);
    filmControls_.insert("crop_w", cw);
    filmControls_.insert("crop_h", ch);
    emit filmControlsChanged();
    scheduleRender();
}

void EngineController::rotateQuadrant(int steps)
{
    const int q = ((filmControls_.value("rotate_quadrant").toInt() + steps) % 4 + 4) % 4;
    filmControls_.insert("rotate_quadrant", q);
    emit filmControlsChanged();
    scheduleRender();
}

void EngineController::resetGeometry()
{
    filmControls_.insert("crop_x", 0.0);
    filmControls_.insert("crop_y", 0.0);
    filmControls_.insert("crop_w", 1.0);
    filmControls_.insert("crop_h", 1.0);
    filmControls_.insert("straighten_deg", 0.0);
    filmControls_.insert("rotate_quadrant", 0);
    filmControls_.insert("flip_h", false);
    filmControls_.insert("flip_v", false);
    emit filmControlsChanged();
    scheduleRender();
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
    applyDefaultPlacement();
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

    // Shorthand: pull a numeric film control as float.
    const auto f = [this](const char* key) {
        return static_cast<float>(filmControls_.value(key).toDouble());
    };

    // Rendered-input handling (TIFF/already-developed files only). We deliberately
    // do NOT plumb film_color_compression or palette_range: both are retired in the
    // engine (compression follows film_color_density; the palette_range pass was
    // removed), so their neutral struct defaults are correct. adaptation is a live
    // engine control but intentionally not surfaced, so its 1.0 default stands too.
    request.rendered_input = f("rendered_input");

    // Basic tone/colour
    request.exposure = f("exposure");
    request.contrast = f("contrast");
    request.highlights = f("highlights");
    request.shadows = f("shadows");
    request.whites = f("whites");
    request.blacks = f("blacks");
    request.midtones = f("midtones");
    request.temp = f("temp");
    request.tint = f("tint");
    request.saturation = f("saturation");
    request.vibrance = f("vibrance");

    // Detail & optics
    request.texture = f("texture");
    request.clarity = f("clarity");
    request.dehaze = f("dehaze");
    request.sharpness = f("sharpness");
    request.sharpness_mask = f("sharpness_mask");

    // Print finish
    request.print_stock = filmControls_.value("print_stock").toString().toStdString();
    request.print_strength = f("print_strength");
    request.print_c = f("print_c");
    request.print_m = f("print_m");
    request.print_y = f("print_y");
    request.print_contrast = f("print_contrast");
    request.print_black_point = f("print_black_point");

    // Geometry (Phase 1)
    request.crop_x = f("crop_x");
    request.crop_y = f("crop_y");
    request.crop_w = f("crop_w");
    request.crop_h = f("crop_h");
    request.straighten_deg = f("straighten_deg");
    request.rotate_quadrant = filmControls_.value("rotate_quadrant").toInt();
    request.flip_h = filmControls_.value("flip_h").toBool();
    request.flip_v = filmControls_.value("flip_v").toBool();

    // Colour grading — 3-way + global
    request.cg_shadow_hue = f("cg_shadow_hue");
    request.cg_shadow_sat = f("cg_shadow_sat");
    request.cg_shadow_lum = f("cg_shadow_lum");
    request.cg_midtone_hue = f("cg_midtone_hue");
    request.cg_midtone_sat = f("cg_midtone_sat");
    request.cg_midtone_lum = f("cg_midtone_lum");
    request.cg_highlight_hue = f("cg_highlight_hue");
    request.cg_highlight_sat = f("cg_highlight_sat");
    request.cg_highlight_lum = f("cg_highlight_lum");
    request.cg_global_hue = f("cg_global_hue");
    request.cg_global_sat = f("cg_global_sat");
    request.cg_global_lum = f("cg_global_lum");
    request.cg_balance = f("cg_balance");
    request.cg_blending = f("cg_blending");
    request.cg_crossbalance = f("cg_crossbalance");

    // HSL — 8 bands
    request.hsl_red_h = f("hsl_red_h");     request.hsl_red_s = f("hsl_red_s");         request.hsl_red_l = f("hsl_red_l");
    request.hsl_orange_h = f("hsl_orange_h"); request.hsl_orange_s = f("hsl_orange_s"); request.hsl_orange_l = f("hsl_orange_l");
    request.hsl_yellow_h = f("hsl_yellow_h"); request.hsl_yellow_s = f("hsl_yellow_s"); request.hsl_yellow_l = f("hsl_yellow_l");
    request.hsl_green_h = f("hsl_green_h");   request.hsl_green_s = f("hsl_green_s");     request.hsl_green_l = f("hsl_green_l");
    request.hsl_aqua_h = f("hsl_aqua_h");     request.hsl_aqua_s = f("hsl_aqua_s");       request.hsl_aqua_l = f("hsl_aqua_l");
    request.hsl_blue_h = f("hsl_blue_h");     request.hsl_blue_s = f("hsl_blue_s");       request.hsl_blue_l = f("hsl_blue_l");
    request.hsl_purple_h = f("hsl_purple_h"); request.hsl_purple_s = f("hsl_purple_s");   request.hsl_purple_l = f("hsl_purple_l");
    request.hsl_magenta_h = f("hsl_magenta_h"); request.hsl_magenta_s = f("hsl_magenta_s"); request.hsl_magenta_l = f("hsl_magenta_l");

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

void EngineController::onBeforeReady(bool ok)
{
    hasBefore_ = ok;
    beforeRevision_++;
    emit beforeChanged();
}

void EngineController::onHistogram(const QVariantList& r, const QVariantList& g,
                                  const QVariantList& b)
{
    histogramR_ = r;
    histogramG_ = g;
    histogramB_ = b;
    emit histogramChanged();
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
    const bool savedToLightroom = lightroomRoundTrip_ && msg.startsWith("Exported:");
    status_ = savedToLightroom ? "Saved. Returning to Lightroom..." : msg;
    emit statusChanged();
    qDebug() << "DFEE export:" << msg;
    if (savedToLightroom) {
        // Lightroom owns the external-editor session and refreshes its TIFF
        // after the editor process returns. Close only after the atomic
        // replacement has completed; failed exports intentionally stay open.
        QTimer::singleShot(350, this, []() { QCoreApplication::quit(); });
    }
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

void EngineController::applyDefaultPlacement()
{
    const QString want = renderedInput() ? QStringLiteral("as_shot")
                                         : QStringLiteral("auto_balanced");
    if (filmControls_.value("exposure_placement").toString() != want) {
        filmControls_.insert("exposure_placement", want);
        emit filmControlsChanged();
    }
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
            applyDefaultPlacement();

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
