#include "EngineController.h"
#include "RenderWorker.h"
#include "PreviewImageProvider.h"
#include "LookPreviewProvider.h"
#include "StockCatalog.h"

#include "dfee/session.hpp"
#include "dfee/bridge_types.hpp"

#include <QFileInfo>
#include <QMetaObject>
#include <QDebug>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>

#ifndef DFEE_REPO_ROOT
#  define DFEE_REPO_ROOT "."
#endif

namespace {
// Locate the data root that holds profiles/stocks. In an installed/deployed build the
// profiles are bundled next to the executable; fall back to the source-tree repo root
// (DFEE_REPO_ROOT) for developer builds run from the build tree.
std::filesystem::path resolveProjectRoot() {
    std::error_code ec;
    const std::filesystem::path exeDir(
        QCoreApplication::applicationDirPath().toStdWString());
    if (std::filesystem::exists(exeDir / "profiles" / "stocks", ec)) {
        return exeDir;
    }
    return std::filesystem::path(DFEE_REPO_ROOT);
}

// Compact value for history labels: integers without a decimal, else 2 places.
QString fmtControlValue(double v) {
    const double r = std::round(v);
    if (std::abs(v - r) < 1e-6) return QString::number(static_cast<long long>(r));
    return QString::number(v, 'f', 2);
}

// A single filesystem-safe path component (preset or group name). The display
// name is preserved inside the JSON; only the file/dir name is sanitized.
QString sanitizeComponent(const QString& s) {
    QString safe = s.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    return safe;
}
}  // namespace

EngineController::EngineController(PreviewImageProvider* provider,
                                   EditStore* store,
                                   QObject* parent)
    : QObject(parent)
    , session_(std::make_unique<dfee::EngineSession>(resolveProjectRoot()))
    , provider_(provider)
    , store_(store)
{
    filmControls_ = defaultFilmControls();
    previewDebounceTimer_.setSingleShot(true);
    previewDebounceTimer_.setInterval(90);
    connect(&previewDebounceTimer_, &QTimer::timeout,
            this, &EngineController::dispatchScheduledRender);
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(400);
    connect(&saveTimer_, &QTimer::timeout, this, [this]() { saveEditsFor(currentFile_); });
    // On quit: save the open photo, then stop the worker while everything it touches
    // still exists. main() destroys the QML engine -- which owns the preview image
    // provider the worker writes into -- before this controller, so waiting for an
    // in-flight open/render only in the destructor let it write into freed memory.
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
        flushEdits();
        workerThread_.quit();
        workerThread_.wait();
    });
    // list_profiles() is called on the GUI thread BEFORE the worker thread starts,
    // so there is no concurrent access.
    loadStocks();

    // provider_ is already set (constructor arg); worker receives it before the
    // thread starts, so provider_ is never written after workerThread_.start().
    worker_ = new RenderWorker(session_.get(), this, provider_);
    worker_->moveToThread(&workerThread_);
    workerThread_.start();

    refreshPresets();
    // Every adjustment changes what a tile shows (tiles are "what a click gives").
    connect(this, &EngineController::filmControlsChanged, this, &EngineController::bumpLookEpoch);
}

QVariantMap EngineController::defaultFilmControls()
{
    return QVariantMap{
        // Every input reaches the film path already developed (a Lightroom TIFF, or a
        // RAW developed natively to Lightroom's default render), so it is exposed already.
        {"exposure_placement", "as_shot"},
        {"film_exposure_ev", 0.0},
        {"adaptive", true},
        {"rendered_input", 80.0},
        {"highlight_rolloff", 100.0},
        {"film_contrast", 100.0},
        {"crossover", 100.0},
        {"profile_strength", 100.0},
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
    const QHash<QString, StockInfo> catalog = loadStockCatalog(QStringLiteral(":/stocks/catalog.json"));
    const auto fallbackGroup = [](const std::string& t) -> QString {
        if (t == "color_reversal") return QStringLiteral("slide");
        if (t == "monochrome") return QStringLiteral("bw");
        return QStringLiteral("negative");
    };
    for (const char* cat : {"color_negative", "color_reversal", "monochrome"}) {
        for (const auto& s : profiles.stocks) {
            if (s.stock_type != cat) continue;
            QVariantMap m;
            m["id"] = QString::fromStdString(s.stock_id);
            m["name"] = QString::fromStdString(s.stock_name);
            m["type"] = QString::fromStdString(s.stock_type);
            m["typeLabel"] = typeLabel(s.stock_type);
            const StockInfo info = catalog.value(m["id"].toString());
            const QString group = info.group.isEmpty() ? fallbackGroup(s.stock_type) : info.group;
            m["group"] = group;
            m["groupLabel"] = stockGroupLabel(group);
            m["iso"] = info.iso;
            m["blurb"] = info.blurb;
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
    const int i = stockIds_.indexOf(id);
    const QString name = (i >= 0) ? stockNames_.at(i) : id;
    recordHistory(id == "none" ? QStringLiteral("Film stock: None")
                               : QStringLiteral("Film stock: ") + name, "stock");
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
        {"crossover", {0.0, 200.0}},
        {"profile_strength", {0.0, 200.0}},
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
        {"print_strength", {0.0, 1.0}},
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
    // Grouped controls (color grading / HSL / crop) read cleaner without a raw
    // per-sub-band number; scalar controls carry their value.
    const bool grouped = (key.startsWith(QStringLiteral("cg_")) && key != QStringLiteral("cg_crossbalance"))
                      || key.startsWith(QStringLiteral("hsl_"))
                      || key.startsWith(QStringLiteral("crop_"));
    recordHistory(grouped ? friendlyLabel(key)
                          : friendlyLabel(key) + QStringLiteral(" ") + fmtControlValue(bounded),
                  key);
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
        recordHistory(friendlyLabel(key), key);
        scheduleRender();
        return;
    }
    if (key == "adaptive" || key == "grain_auto" || key == "flip_h" || key == "flip_v") {
        const bool enabled = value.toBool();
        if (filmControls_.value(key).toBool() == enabled) return;
        filmControls_.insert(key, enabled);
        emit filmControlsChanged();
        recordHistory(friendlyLabel(key) + (enabled ? QStringLiteral(" on") : QStringLiteral(" off")), key);
        scheduleRender();
        return;
    }
    if (key == "rotate_quadrant") {
        const int q = ((value.toInt() % 4) + 4) % 4;
        if (filmControls_.value(key).toInt() == q) return;
        filmControls_.insert(key, q);
        emit filmControlsChanged();
        recordHistory(QStringLiteral("Rotate"), key);
        scheduleRender();
        return;
    }
    if (key == "exposure_placement") {
        const QString placement = value.toString() == "as_shot" ? "as_shot" : "auto_balanced";
        if (filmControls_.value(key).toString() == placement) return;
        filmControls_.insert(key, placement);
        emit filmControlsChanged();
        recordHistory(placement == "as_shot" ? QStringLiteral("Exposure: as shot")
                                             : QStringLiteral("Exposure: auto balanced"), key);
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

    if (stockId_ != "none") {
        stockId_ = "none";
        emit stockChanged();
    }
    emit filmControlsChanged();
    emit paramsChanged();
    recordHistory(QStringLiteral("Reset all"));
    scheduleRender();
}

namespace {

// Control groups for section Reset and the edited dots. The v2 inspector's sections
// come first; the v1 names stay until the v1 window is retired.
const QHash<QString, QStringList>& controlGroups()
{
    static const QStringList light = {"exposure", "contrast", "highlights", "shadows",
        "whites", "blacks", "midtones", "texture", "clarity", "dehaze", "sharpness",
        "sharpness_mask"};
    static const QStringList grade = {"cg_shadow_hue", "cg_shadow_sat", "cg_shadow_lum",
        "cg_midtone_hue", "cg_midtone_sat", "cg_midtone_lum", "cg_highlight_hue",
        "cg_highlight_sat", "cg_highlight_lum", "cg_global_hue", "cg_global_sat",
        "cg_global_lum", "cg_balance", "cg_blending"};
    static const QStringList hsl = [] {
        QStringList keys;
        for (const char* band : {"red", "orange", "yellow", "green", "aqua", "blue", "purple", "magenta"})
            for (const char* part : {"h", "s", "l"})
                keys << QStringLiteral("hsl_%1_%2").arg(QLatin1String(band), QLatin1String(part));
        return keys;
    }();
    static const QStringList grainLight = {"grain_auto", "grain_strength", "grain_size",
        "grain_roughness", "halation_strength", "halation_threshold", "bloom"};
    static const QStringList print = {"print_stock", "print_strength", "print_c", "print_m",
        "print_y", "print_contrast", "print_black_point"};
    static const QHash<QString, QStringList> groups = {
        // v2 inspector sections
        {"film", {"profile_strength"}},
        {"exposure", {"exposure_placement", "film_exposure_ev"}},
        {"tone", {"rendered_input", "adaptive", "highlight_rolloff", "film_contrast", "shadow_lift"}},
        {"color", {"film_color_density", "emulsion_color_density", "highlight_color_hold",
                   "shadow_color_retention", "crossover", "cg_crossbalance", "temp", "tint",
                   "vibrance", "saturation"}},
        {"grain_light", grainLight},
        {"print", print},
        {"fine_tune", light + grade + hsl},
        // v1 groups
        {"film_tone", {"rendered_input", "adaptive", "profile_strength", "highlight_rolloff",
                       "film_contrast", "shadow_lift"}},
        {"color_character", {"film_color_density", "emulsion_color_density",
                             "highlight_color_hold", "shadow_color_retention", "crossover",
                             "cg_crossbalance"}},
        {"material", grainLight},
        {"light", light},
        {"color_balance", {"temp", "tint", "vibrance", "saturation"}},
        {"grade", grade},
    };
    return groups;
}

QString groupLabel(const QString& group)
{
    static const QHash<QString, QString> labels = {
        {"film", "Film"}, {"exposure", "Exposure"}, {"tone", "Tone"}, {"color", "Color"},
        {"grain_light", "Grain & light"}, {"print", "Print"}, {"fine_tune", "Fine-tune"},
        {"film_tone", "Film tone"}, {"color_character", "Color character"},
        {"material", "Material finish"}, {"light", "Light"}, {"color_balance", "Color balance"},
        {"grade", "Color grading"},
    };
    return labels.value(group, group);
}

// Controls hold doubles, ints, bools and strings; numbers compare by value.
bool sameControlValue(const QVariant& a, const QVariant& b)
{
    const auto numeric = [](const QVariant& v) {
        const int id = v.metaType().id();
        return id == QMetaType::Double || id == QMetaType::Int || id == QMetaType::LongLong
            || id == QMetaType::UInt || id == QMetaType::Float;
    };
    if (numeric(a) && numeric(b)) return qAbs(a.toDouble() - b.toDouble()) < 1e-6;
    return a == b;
}

} // namespace

void EngineController::resetControlGroup(const QString& group)
{
    const auto& groups = controlGroups();
    const auto it = groups.constFind(group);
    if (it == groups.cend()) return;

    const QVariantMap defaults = defaultFilmControls();
    bool changed = false;
    for (const QString& key : *it) {
        const QVariant value = defaults.value(key);
        if (filmControls_.value(key) != value) {
            filmControls_.insert(key, value);
            changed = true;
        }
    }
    if (!changed) return;
    if (group == QStringLiteral("material") || group == QStringLiteral("grain_light")) {
        grainResolving_ = false;
        emit grainResolvingChanged();
    }
    emit filmControlsChanged();
    recordHistory(QStringLiteral("Reset %1").arg(groupLabel(group)));
    scheduleRender();
}

QVariantMap EngineController::editedGroups() const
{
    const QVariantMap defaults = defaultFilmControls();
    // While Auto grain is on, the amount keys are the engine's, not the user's.
    const bool autoGrain = filmControls_.value("grain_auto").toBool();
    QVariantMap result;
    const auto& groups = controlGroups();
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        bool edited = false;
        for (const QString& key : it.value()) {
            if (autoGrain && key.startsWith(QLatin1String("grain_")) && key != QLatin1String("grain_auto"))
                continue;
            if (!sameControlValue(filmControls_.value(key), defaults.value(key))) {
                edited = true;
                break;
            }
        }
        result.insert(it.key(), edited);
    }
    return result;
}

void EngineController::setGradeColor(const QString& zone, double hue, double sat)
{
    static const QHash<QString, QString> labels = {
        {"shadow", "Shadow tint"}, {"midtone", "Midtone tint"},
        {"highlight", "Highlight tint"}, {"global", "Global tint"}};
    if (!labels.contains(zone)) return;
    const QString hueKey = QStringLiteral("cg_%1_hue").arg(zone);
    const QString satKey = QStringLiteral("cg_%1_sat").arg(zone);
    const double h = std::fmod(std::fmod(hue, 360.0) + 360.0, 360.0);
    const double s = std::clamp(sat, 0.0, 100.0);
    if (sameControlValue(filmControls_.value(hueKey), h) && sameControlValue(filmControls_.value(satKey), s))
        return;
    filmControls_.insert(hueKey, h);
    filmControls_.insert(satKey, s);
    emit filmControlsChanged();
    recordHistory(labels.value(zone), QStringLiteral("grade_") + zone);
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
    recordHistory(QStringLiteral("Crop"), "crop");
    scheduleRender();
}

void EngineController::rotateQuadrant(int steps)
{
    const int q = ((filmControls_.value("rotate_quadrant").toInt() + steps) % 4 + 4) % 4;
    filmControls_.insert("rotate_quadrant", q);
    emit filmControlsChanged();
    recordHistory(QStringLiteral("Rotate"), "rotate_quadrant");
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
    recordHistory(QStringLiteral("Reset crop & rotate"));
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
        recordHistory(QStringLiteral("Grain: auto"), "grain_auto");
        scheduleRender();
        return;
    }
    if (currentFile_.isEmpty() || stockId_ == "none") {
        status_ = "Select an image and film stock before customizing grain.";
        emit statusChanged();
        return;
    }
    grainRequestFile_ = currentFile_;
    grainResolving_ = true;
    emit grainResolvingChanged();
    const dfee::NativePreviewRenderRequest request = buildPreviewRequest();
    QMetaObject::invokeMethod(worker_, [worker = worker_, request]() {
        worker->resolveAutoGrain(request);
    }, Qt::QueuedConnection);
}

void EngineController::openFile(const QUrl& url)
{
    if (exporting_) {
        status_ = "Finish the current export before opening another image.";
        emit statusChanged();
        return;
    }

    const QString file = url.toLocalFile();
    previewDebounceTimer_.stop();
    status_ = "Loading " + QFileInfo(file).fileName();
    emit statusChanged();
    // The new image's history is set up by loadEditsFor when the open actually
    // dispatches (restored, or seeded on its first preview). Setting the seed flag
    // here instead would let a still-running render of the OUTGOING photo re-seed
    // that photo's history just before it is saved.

    if (workerBusy_) {
        // Latch the latest file for the pending open; mark it as an open (not
        // a plain re-render) so onWorkerBusyChanged posts openAndRender, which
        // runs select_file + decode_raw before rendering.
        pendingFile_  = file;
        dirty_        = true;
        dirtyIsOpen_  = true;
        emit currentFileChanged();
        // Do NOT update currentFile_ yet — the in-flight operation still owns
        // the session's decoded buffer.  currentFile_ is updated when the
        // deferred open actually fires.
        return;
    }

    flushEdits();                         // persist the outgoing photo first
    currentFile_ = file;
    emit currentFileChanged();
    imageInfo_.clear();
    emit imageInfoChanged();
    resetLookTiles();
    loadEditsFor(file);                   // before the first preview request
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

    // Coalesce high-frequency slider updates before taking the worker. This
    // keeps the GUI responsive and prevents an early drag value from starting
    // an expensive render that will immediately be superseded.
    previewDebounceTimer_.start();
}

void EngineController::dispatchScheduledRender()
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
    request.effect_pipeline_version = "filmic_v4"; // A/B eval: display-referred film tone (Portra 400 / Tri-X 400 / Kodachrome 64 have curve blocks)
    request.exposure_placement = filmControls_.value("exposure_placement").toString().toStdString();
    request.film_exposure_ev = static_cast<float>(filmControls_.value("film_exposure_ev").toDouble());
    request.adaptive = filmControls_.value("adaptive").toBool();
    request.highlight_rolloff = static_cast<float>(filmControls_.value("highlight_rolloff").toDouble());
    request.film_contrast = static_cast<float>(filmControls_.value("film_contrast").toDouble());
    request.crossover = static_cast<float>(filmControls_.value("crossover").toDouble());
    request.profile_strength = static_cast<float>(filmControls_.value("profile_strength").toDouble());
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

    // Rendered-input handling (every input: TIFFs and natively developed RAWs). We deliberately
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
    if (pendingSeed_) {
        pendingSeed_ = false;
        seedHistory(QStringLiteral("Import"));
    }
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

void EngineController::onHistogram(
    const QVariantList& r, const QVariantList& g, const QVariantList& b, const QVariantList& scope)
{
    histogramR_ = r;
    histogramG_ = g;
    histogramB_ = b;
    vectorscope_ = scope;
    emit histogramChanged();
}

void EngineController::exportImage()
{
    if (currentFile_.isEmpty()) return;
    if (exporting_) return;
    flushEdits();
    // The export snapshot is built below, so a queued preview would only spend
    // memory and CPU on an image the user is about to save at full resolution.
    previewDebounceTimer_.stop();
    dirty_ = false;
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
    if (grainRequestFile_ != currentFile_) return;  // the photo changed while resolving
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
    recordHistory(QStringLiteral("Grain: custom"), "grain_auto");
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

void EngineController::onOpenFailed(const QString& msg)
{
    // A newer open queued behind this one keeps its own file; only clear if the
    // failed file is still the current one.
    if (!dirtyIsOpen_) {
        currentFile_.clear();
        emit currentFileChanged();
        imageInfo_.clear();
        emit imageInfoChanged();
        pendingSeed_ = false;
        if (hasImage_) {
            hasImage_ = false;
            emit hasImageChanged();
        }
        if (hasBefore_) {
            hasBefore_ = false;
            emit beforeChanged();
        }
    }
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
            flushEdits();                 // the outgoing photo's state is still loaded
            currentFile_  = pendingFile_;
            pendingFile_.clear();
            emit currentFileChanged();
            imageInfo_.clear();
            emit imageInfoChanged();
            resetLookTiles();
            loadEditsFor(currentFile_);

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
    } else if (!busy) {
        pumpLookTiles();
    }
}

// ── Films tiles ─────────────────────────────────────────────────────────

namespace { constexpr int kLookTileEdge = 256; }

QVariantMap EngineController::lookTiles() const
{
    QVariantMap tiles;
    for (auto it = tileEpochs_.cbegin(); it != tileEpochs_.cend(); ++it) {
        tiles.insert(it.key(), static_cast<int>(it.value()));
    }
    return tiles;
}

void EngineController::requestLookTiles(const QStringList& stockIds)
{
    wantedTiles_ = stockIds;
    rebuildTileQueue();
    pumpLookTiles();
}

void EngineController::bumpLookEpoch()
{
    ++lookEpoch_;
    failedTiles_.clear();
    rebuildTileQueue();                  // old tiles stay visible until replaced
    // Not now: the edit that bumped the epoch schedules its preview right after this
    // signal returns. Pumping on the next turn sees that preview's debounce and lets
    // it go first.
    QTimer::singleShot(0, this, &EngineController::pumpLookTiles);
}

void EngineController::resetLookTiles()
{
    ++lookEpoch_;
    tileEpochs_.clear();
    failedTiles_.clear();
    if (lookProvider_) lookProvider_->clear();
    rebuildTileQueue();
}

void EngineController::rebuildTileQueue()
{
    tileQueue_.clear();
    for (const QString& id : std::as_const(wantedTiles_)) {
        if (id == tileInFlight_ || failedTiles_.contains(id)) continue;
        if (tileEpochs_.value(id, 0) != lookEpoch_) tileQueue_.append(id);
    }
    emit lookTilesChanged();
}

// Tiles are the lowest priority: only when the worker is idle and nothing else
// waits — a pending open/preview (dirty_), a slider debounce, an export or an
// auto-grain resolve all go first. One tile in flight at a time.
void EngineController::pumpLookTiles()
{
    if (workerBusy_ || dirty_ || exporting_ || grainResolving_ || !hasImage_ || !lookProvider_ ||
        currentFile_.isEmpty() || previewDebounceTimer_.isActive() || tileQueue_.isEmpty()) {
        return;
    }
    const QString stock = tileQueue_.takeFirst();
    tileInFlight_ = stock;
    workerBusy_ = true;
    dfee::NativeLookProxyRequest request;
    request.look = buildPreviewRequest();
    request.look.stock = stock.toStdString();
    request.max_edge = kLookTileEdge;
    const qulonglong epoch = lookEpoch_;
    QMetaObject::invokeMethod(worker_, [worker = worker_, request, stock, epoch]() {
        worker->renderLookProxy(request, stock, epoch);
    }, Qt::QueuedConnection);
    emit lookTilesChanged();
}

void EngineController::onLookProxyReady(const QString& stockId, qulonglong epoch, const QImage& image)
{
    tileInFlight_.clear();
    if (epoch == lookEpoch_) {
        if (image.isNull()) {
            failedTiles_.insert(stockId);
        } else {
            if (lookProvider_) lookProvider_->setImage(stockId, image);
            tileEpochs_.insert(stockId, epoch);
        }
    } else if (wantedTiles_.contains(stockId) && !tileQueue_.contains(stockId)) {
        tileQueue_.append(stockId);      // rendered for an older look: do it again
    }
    emit lookTilesChanged();
}

// ── Edit history ────────────────────────────────────────────────────────

bool EngineController::isGeometryKey(const QString& key)
{
    return key.startsWith(QStringLiteral("crop_"))
        || key == QStringLiteral("straighten_deg")
        || key == QStringLiteral("rotate_quadrant")
        || key == QStringLiteral("flip_h")
        || key == QStringLiteral("flip_v");
}

QString EngineController::friendlyLabel(const QString& key)
{
    // History labels use the inspector's names (desktop/qml/v2/inspector), so a step
    // reads the same as the control that made it.
    static const QHash<QString, QString> names = {
        {"exposure_placement", "Exposure placement"}, {"film_exposure_ev", "Film exposure"},
        {"adaptive", "Adaptive scene tone"}, {"rendered_input", "Preserve rendered tone"},
        {"highlight_rolloff", "Highlight rolloff"}, {"film_contrast", "Film contrast"},
        {"crossover", "Crossover"}, {"profile_strength", "Film strength"},
        {"shadow_lift", "Shadow lift"}, {"film_color_density", "Color density"},
        {"emulsion_color_density", "Color boost"}, {"highlight_color_hold", "Highlight saturation"},
        {"shadow_color_retention", "Shadow saturation"}, {"grain_strength", "Grain strength"},
        {"grain_size", "Grain size"}, {"grain_roughness", "Grain roughness"},
        {"halation_strength", "Halation strength"}, {"halation_threshold", "Halation threshold"},
        {"bloom", "Bloom"}, {"exposure", "Exposure"}, {"contrast", "Contrast"},
        {"highlights", "Highlights"}, {"shadows", "Shadows"}, {"whites", "Whites"},
        {"blacks", "Blacks"}, {"midtones", "Midtones"}, {"temp", "Temperature"},
        {"tint", "Tint"}, {"saturation", "Saturation"}, {"vibrance", "Vibrance"},
        {"texture", "Texture"}, {"clarity", "Clarity"}, {"dehaze", "Dehaze"},
        {"sharpness", "Sharpening"}, {"sharpness_mask", "Sharpening mask"},
        {"print_stock", "Print stock"}, {"print_strength", "Print strength"},
        {"print_c", "Color head: cyan"}, {"print_m", "Color head: magenta"}, {"print_y", "Color head: yellow"},
        {"print_contrast", "Print contrast"}, {"print_black_point", "Paper black"},
        {"straighten_deg", "Straighten"}, {"cg_crossbalance", "Split toning"},
        {"flip_h", "Flip horizontal"}, {"flip_v", "Flip vertical"}, {"grain_auto", "Auto grain"},
    };
    const auto it = names.constFind(key);
    if (it != names.cend()) return it.value();
    if (key.startsWith(QStringLiteral("cg_"))) return QStringLiteral("Color grading");
    if (key.startsWith(QStringLiteral("hsl_"))) return QStringLiteral("Color mixer");
    if (key.startsWith(QStringLiteral("crop_"))) return QStringLiteral("Crop");
    return key;
}

void EngineController::recordHistory(const QString& label, const QString& coalesceKey)
{
    if (currentFile_.isEmpty() || historyIndex_ < 0) return;  // no history before baseline
    // Drop any redo tail — a new edit replaces the abandoned future.
    if (historyIndex_ < int(history_.size()) - 1) {
        history_.erase(history_.begin() + historyIndex_ + 1, history_.end());
    }
    HistoryEntry entry{label, coalesceKey, stockId_, filmControls_};
    if (!coalesceKey.isEmpty() && !history_.isEmpty()
        && history_.last().coalesceKey == coalesceKey) {
        history_.last() = entry;      // merge consecutive same-control edits
    } else {
        history_.append(entry);
        historyIndex_ = int(history_.size()) - 1;
    }
    emit historyChanged();
    markEditsDirty();
}

void EngineController::seedHistory(const QString& label)
{
    history_.clear();
    history_.append(HistoryEntry{label, QString(), stockId_, filmControls_});
    historyIndex_ = 0;
    emit historyChanged();
}

void EngineController::restoreHistory(int internalIndex)
{
    if (internalIndex < 0 || internalIndex >= int(history_.size())) return;
    historyIndex_ = internalIndex;
    const HistoryEntry& e = history_.at(internalIndex);
    filmControls_ = e.controls;
    filmExposure_ = filmControls_.value("film_exposure_ev").toDouble();
    shadowLift_ = filmControls_.value("shadow_lift").toDouble();
    if (stockId_ != e.stock) {
        stockId_ = e.stock;
        emit stockChanged();
    }
    emit filmControlsChanged();
    emit paramsChanged();
    emit historyChanged();
    scheduleRender();
    markEditsDirty();
}

// ── Per-photo memory ────────────────────────────────────────────────────

QVariantMap EngineController::sparseControls(const QVariantMap& controls)
{
    const QVariantMap defaults = defaultFilmControls();
    QVariantMap sparse;
    for (auto it = controls.cbegin(); it != controls.cend(); ++it) {
        if (!defaults.contains(it.key())) continue;
        if (!sameControls({{it.key(), it.value()}}, {{it.key(), defaults.value(it.key())}})) {
            sparse.insert(it.key(), it.value());
        }
    }
    return sparse;
}

QVariantMap EngineController::mergeOnDefaults(const QVariantMap& sparse)
{
    QVariantMap controls = defaultFilmControls();
    for (auto it = sparse.cbegin(); it != sparse.cend(); ++it) {
        if (controls.contains(it.key())) controls.insert(it.key(), it.value());  // drop unknown keys
    }
    return controls;
}

// Value-wise equality that treats 150 and 150.0 as equal (JSON returns doubles).
bool EngineController::sameControls(const QVariantMap& a, const QVariantMap& b)
{
    if (a.size() != b.size()) return false;
    for (auto it = a.cbegin(); it != a.cend(); ++it) {
        const auto other = b.constFind(it.key());
        if (other == b.cend()) return false;
        const QVariant& x = it.value();
        const QVariant& y = other.value();
        bool okX = false, okY = false;
        const double dx = x.toDouble(&okX);
        const double dy = y.toDouble(&okY);
        const bool numeric = okX && okY && x.typeId() != QMetaType::QString && y.typeId() != QMetaType::QString;
        if (numeric ? !qFuzzyCompare(dx + 1.0, dy + 1.0) : x.toString() != y.toString()) return false;
    }
    return true;
}

bool EngineController::isEdited() const
{
    return stockId_ != QStringLiteral("none") || !sameControls(filmControls_, defaultFilmControls());
}

void EngineController::loadEditsFor(const QString& file)
{
    saveTimer_.stop();
    filmControls_ = defaultFilmControls();
    QString stock = QStringLiteral("none");
    history_.clear();
    historyIndex_ = -1;
    pendingSeed_ = true;                  // seed "Import" on first preview unless restored
    if (store_ && !lightroomRoundTrip_) {
        if (const auto rec = store_->load(file)) {
            filmControls_ = mergeOnDefaults(rec->controls);
            stock = stockIds_.contains(rec->stock) ? rec->stock : QStringLiteral("none");
            for (const StoredHistoryStep& s : rec->history) {
                history_.append(HistoryEntry{s.label, s.coalesceKey,
                    stockIds_.contains(s.stock) ? s.stock : QStringLiteral("none"),
                    mergeOnDefaults(s.controls)});
            }
            if (!history_.isEmpty()) {
                historyIndex_ = std::clamp(rec->historyIndex, 0, int(history_.size()) - 1);
                pendingSeed_ = false;
            }
        }
    }
    filmExposure_ = filmControls_.value("film_exposure_ev").toDouble();
    shadowLift_ = filmControls_.value("shadow_lift").toDouble();
    if (stockId_ != stock) {
        stockId_ = stock;
        emit stockChanged();
    }
    if (grainResolving_) {
        grainResolving_ = false;
        emit grainResolvingChanged();
    }
    emit filmControlsChanged();
    emit paramsChanged();
    emit historyChanged();
}

void EngineController::saveEditsFor(const QString& file)
{
    if (!store_ || lightroomRoundTrip_ || file.isEmpty() || history_.isEmpty()) return;
    EditRecord rec;
    rec.stock = stockId_;
    rec.controls = sparseControls(filmControls_);
    for (const HistoryEntry& e : history_) {
        rec.history.append({e.label, e.coalesceKey, e.stock, sparseControls(e.controls)});
    }
    rec.historyIndex = historyIndex_;
    store_->save(file, rec, isEdited());
}

void EngineController::markEditsDirty()
{
    if (store_ && !lightroomRoundTrip_) saveTimer_.start();
}

void EngineController::flushEdits()
{
    saveTimer_.stop();
    saveEditsFor(currentFile_);
}

void EngineController::onImageInfo(const QVariantMap& info)
{
    imageInfo_ = info;
    emit imageInfoChanged();
}

QVariantList EngineController::history() const
{
    QVariantList out;                 // newest-first for the UI
    for (int i = int(history_.size()) - 1; i >= 0; --i) {
        QVariantMap m;
        m["label"] = history_.at(i).label;
        out.append(m);
    }
    return out;
}

void EngineController::undo()      { if (canUndo()) restoreHistory(historyIndex_ - 1); }
void EngineController::redo()      { if (canRedo()) restoreHistory(historyIndex_ + 1); }
void EngineController::jumpToHistory(int displayRow)
{
    restoreHistory(int(history_.size()) - 1 - displayRow);
}

// ── Presets ─────────────────────────────────────────────────────────────

QString EngineController::presetsDir() const
{
    // DFEE_PRESETS_DIR keeps UI tests out of the user's Documents.
    const QString override = qEnvironmentVariable("DFEE_PRESETS_DIR");
    const QString dir = !override.isEmpty()
        ? override
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
              + QStringLiteral("/Film Lab/Presets");
    QDir().mkpath(dir);
    return dir;
}

QVariantMap EngineController::captureRecipe() const
{
    QVariantMap controls;
    for (auto it = filmControls_.cbegin(); it != filmControls_.cend(); ++it) {
        if (!isGeometryKey(it.key())) controls.insert(it.key(), it.value());
    }
    QVariantMap recipe;
    recipe["stock"] = stockId_;
    recipe["controls"] = controls;
    return recipe;
}

void EngineController::applyRecipe(const QVariantMap& recipe, const QString& label)
{
    const QString newStock = recipe.value("stock").toString();
    const QVariantMap controls = recipe.value("controls").toMap();
    for (auto it = controls.cbegin(); it != controls.cend(); ++it) {
        if (isGeometryKey(it.key())) continue;
        if (!filmControls_.contains(it.key())) continue;  // ignore unknown keys
        filmControls_.insert(it.key(), it.value());
    }
    filmExposure_ = filmControls_.value("film_exposure_ev").toDouble();
    shadowLift_ = filmControls_.value("shadow_lift").toDouble();
    if (!newStock.isEmpty() && stockId_ != newStock) {
        stockId_ = newStock;
        emit stockChanged();
    }
    emit filmControlsChanged();
    emit paramsChanged();
    recordHistory(label);
    scheduleRender();
}

void EngineController::refreshPresets()
{
    presets_.clear();
    presetGroups_.clear();
    const QDir root(presetsDir());

    // Read every *.json in a directory into presets_, tagged with its group.
    const auto readDir = [this](const QDir& dir, const QString& group) {
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& f : files) {
            QFile file(dir.filePath(f));
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
            const QString base = QFileInfo(f).completeBaseName();
            QVariantMap m;
            m["id"] = group.isEmpty() ? base : (group + QStringLiteral("/") + base);
            m["name"] = obj.value("name").toString(base);
            m["group"] = group;
            m["stock"] = obj.value("stock").toString();
            presets_.append(m);
        }
    };

    readDir(root, QString());                                   // ungrouped (root)
    const QStringList subdirs =
        root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& d : subdirs) {                          // one level of groups
        presetGroups_.append(d);
        readDir(QDir(root.filePath(d)), d);
    }
    emit presetsChanged();
}

bool EngineController::savePreset(const QString& name, const QString& group)
{
    const QString displayName = name.trimmed();
    const QString safeName = sanitizeComponent(displayName);
    if (safeName.isEmpty() || currentFile_.isEmpty()) return false;

    QDir dir(presetsDir());
    const QString safeGroup = sanitizeComponent(group);
    if (!safeGroup.isEmpty()) {
        dir = QDir(dir.filePath(safeGroup));
        if (!QDir().mkpath(dir.absolutePath())) return false;
    }

    const QVariantMap recipe = captureRecipe();
    QJsonObject obj;
    obj["schemaVersion"] = 1;
    obj["name"] = displayName;
    obj["group"] = safeGroup;
    obj["stock"] = recipe.value("stock").toString();
    obj["controls"] = QJsonObject::fromVariantMap(recipe.value("controls").toMap());
    obj["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QFile file(dir.filePath(safeName + QStringLiteral(".json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    refreshPresets();
    return true;
}

void EngineController::applyPreset(const QString& id)
{
    QFile file(QDir(presetsDir()).filePath(id + QStringLiteral(".json")));
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    QVariantMap recipe;
    recipe["stock"] = obj.value("stock").toString();
    recipe["controls"] = obj.value("controls").toObject().toVariantMap();
    const QString shown = obj.value("name").toString(QFileInfo(id).completeBaseName());
    applyRecipe(recipe, QStringLiteral("Preset: ") + shown);
}

void EngineController::tidyGroupDir(const QString& group)
{
    if (group.isEmpty()) return;
    const QDir root(presetsDir());
    const QDir gdir(root.filePath(group));
    if (gdir.exists() && gdir.entryList({QStringLiteral("*.json")}, QDir::Files).isEmpty()) {
        root.rmdir(group);      // no-op if the folder still holds other files
    }
}

bool EngineController::deletePreset(const QString& id)
{
    const QDir root(presetsDir());
    const bool ok = QFile::remove(root.filePath(id + QStringLiteral(".json")));
    if (ok) {
        const int slash = id.indexOf(QLatin1Char('/'));
        if (slash > 0) tidyGroupDir(id.left(slash));
        refreshPresets();
    }
    return ok;
}

bool EngineController::editPreset(const QString& id, const QString& newName, const QString& newGroup)
{
    const QDir root(presetsDir());
    const QString oldPath = root.filePath(id + QStringLiteral(".json"));
    QFile in(oldPath);
    if (!in.open(QIODevice::ReadOnly)) return false;
    QJsonObject obj = QJsonDocument::fromJson(in.readAll()).object();
    in.close();

    const QString displayName = newName.trimmed();
    const QString safeName = sanitizeComponent(displayName);
    if (safeName.isEmpty()) return false;
    const QString safeGroup = sanitizeComponent(newGroup);

    QDir destDir(root);
    if (!safeGroup.isEmpty()) {
        destDir = QDir(root.filePath(safeGroup));
        if (!QDir().mkpath(destDir.absolutePath())) return false;
    }
    const QString newPath = destDir.filePath(safeName + QStringLiteral(".json"));
    const bool samePath =
        QFileInfo(newPath).absoluteFilePath() == QFileInfo(oldPath).absoluteFilePath();

    obj["name"] = displayName;
    obj["group"] = safeGroup;
    QFile out(newPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    out.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    out.close();

    if (!samePath) {
        QFile::remove(oldPath);
        const int slash = id.indexOf(QLatin1Char('/'));
        if (slash > 0) tidyGroupDir(id.left(slash));   // source group may now be empty
    }
    refreshPresets();
    return true;
}

bool EngineController::renameGroup(const QString& oldName, const QString& newName)
{
    const QString safeNew = sanitizeComponent(newName);
    if (safeNew.isEmpty() || oldName.isEmpty()) return false;
    if (safeNew == oldName) return true;
    QDir root(presetsDir());
    if (QFileInfo::exists(root.filePath(safeNew))) return false;   // don't clobber
    const bool ok = root.rename(oldName, safeNew);
    if (ok) refreshPresets();
    return ok;
}

bool EngineController::deleteGroup(const QString& name)
{
    if (name.isEmpty()) return false;
    QDir gdir(QDir(presetsDir()).filePath(name));
    const bool ok = gdir.removeRecursively();   // group folder + its presets
    if (ok) refreshPresets();
    return ok;
}

bool EngineController::createGroup(const QString& name)
{
    const QString safe = sanitizeComponent(name);
    if (safe.isEmpty()) return false;
    const bool ok = QDir().mkpath(QDir(presetsDir()).filePath(safe));
    if (ok) refreshPresets();
    return ok;
}

bool EngineController::presetExists(const QString& name, const QString& group) const
{
    QDir dir(presetsDir());
    const QString safeGroup = sanitizeComponent(group);
    if (!safeGroup.isEmpty()) dir = QDir(dir.filePath(safeGroup));
    return QFile::exists(dir.filePath(sanitizeComponent(name) + QStringLiteral(".json")));
}
