#include "ExportPrefs.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {
const QString kDefaultTemplate = QStringLiteral("{name}_{film}");
const QStringList kFormats{QStringLiteral("jpeg"), QStringLiteral("png8"), QStringLiteral("png16"), QStringLiteral("tiff")};
const QStringList kCollisions{QStringLiteral("number"), QStringLiteral("replace"), QStringLiteral("skip")};

QString normalized(const QString& path)
{
    if (path.trimmed().isEmpty()) return {};
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (p.size() > 3 && p.endsWith(QLatin1Char('/'))) p.chop(1);
    return p;
}

bool samePath(const QString& a, const QString& b)
{
    return normalized(a).compare(normalized(b), Qt::CaseInsensitive) == 0;
}

int indexOfPath(const QStringList& list, const QString& path)
{
    for (int i = 0; i < list.size(); ++i)
        if (samePath(list.at(i), path)) return i;
    return -1;
}
}  // namespace

ExportPrefs::ExportPrefs(const QString& iniPath, QObject* parent)
    : QObject(parent),
      settings_(iniPath.isEmpty() ? std::make_unique<QSettings>()
                                  : std::make_unique<QSettings>(iniPath, QSettings::IniFormat)),
      defaultFolder_(defaultFolderPath())
{
    QSettings& s = *settings_;
    s.beginGroup(QStringLiteral("export"));
    folder_ = normalized(s.value(QStringLiteral("folder")).toString());
    nextToOriginal_ = s.value(QStringLiteral("nextToOriginal"), false).toBool();
    nameTemplate_ = s.value(QStringLiteral("nameTemplate"), kDefaultTemplate).toString();
    const QString collision = s.value(QStringLiteral("collision")).toString();
    collision_ = kCollisions.contains(collision) ? collision : kCollisions.first();
    const QString format = s.value(QStringLiteral("format")).toString();
    format_ = kFormats.contains(format) ? format : kFormats.first();
    jpegQuality_ = std::clamp(s.value(QStringLiteral("jpegQuality"), 92).toInt(), 1, 100);
    dpi_ = std::clamp(s.value(QStringLiteral("dpi"), 300).toInt(), 1, 65535);
    for (const QString& f : s.value(QStringLiteral("favorites")).toStringList()) favorites_ << normalized(f);
    for (const QString& r : s.value(QStringLiteral("recents")).toStringList())
        if (QFileInfo(r).isDir()) recents_ << normalized(r);     // drop folders that are gone
    sequence_ = std::max(1, s.value(QStringLiteral("sequence"), 1).toInt());
    s.endGroup();
}

ExportPrefs::~ExportPrefs() = default;

QString ExportPrefs::defaultFolderPath()
{
    const QString overridePath = qEnvironmentVariable("DFEE_EXPORT_DIR");
    if (!overridePath.isEmpty()) return normalized(overridePath);
    return normalized(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) +
                      QStringLiteral("/Film Lab Exports"));
}

void ExportPrefs::save()
{
    QSettings& s = *settings_;
    s.beginGroup(QStringLiteral("export"));
    s.setValue(QStringLiteral("folder"), folder_);
    s.setValue(QStringLiteral("nextToOriginal"), nextToOriginal_);
    s.setValue(QStringLiteral("nameTemplate"), nameTemplate_);
    s.setValue(QStringLiteral("collision"), collision_);
    s.setValue(QStringLiteral("format"), format_);
    s.setValue(QStringLiteral("jpegQuality"), jpegQuality_);
    s.setValue(QStringLiteral("dpi"), dpi_);
    s.setValue(QStringLiteral("favorites"), favorites_);
    s.setValue(QStringLiteral("recents"), recents_);
    s.setValue(QStringLiteral("sequence"), sequence_);
    s.endGroup();
    s.sync();
    emit changed();
}

void ExportPrefs::setNameTemplate(const QString& pattern)
{
    if (nameTemplate_ == pattern) return;
    nameTemplate_ = pattern;   // kept verbatim: clearing the field must not snap it back
    save();
}

QString ExportPrefs::effectiveNameTemplate() const
{
    return nameTemplate_.trimmed().isEmpty() ? kDefaultTemplate : nameTemplate_;
}

void ExportPrefs::setCollision(const QString& rule)
{
    const QString value = kCollisions.contains(rule) ? rule : kCollisions.first();
    if (collision_ == value) return;
    collision_ = value;
    save();
}

void ExportPrefs::setFormat(const QString& format)
{
    const QString value = kFormats.contains(format) ? format : kFormats.first();
    if (format_ == value) return;
    format_ = value;
    save();
}

void ExportPrefs::setJpegQuality(int quality)
{
    const int value = std::clamp(quality, 1, 100);
    if (jpegQuality_ == value) return;
    jpegQuality_ = value;
    save();
}

void ExportPrefs::setDpi(int dpi)
{
    const int value = std::clamp(dpi, 1, 65535);
    if (dpi_ == value) return;
    dpi_ = value;
    save();
}

void ExportPrefs::useFolder(const QString& path)
{
    const QString p = normalized(path);
    if (p.isEmpty()) return;
    folder_ = samePath(p, defaultFolder_) ? QString() : p;
    nextToOriginal_ = false;
    save();
}

void ExportPrefs::useFolderUrl(const QUrl& url)
{
    useFolder(url.toLocalFile());
}

void ExportPrefs::useDefaultFolder()
{
    folder_.clear();
    nextToOriginal_ = false;
    save();
}

void ExportPrefs::useNextToOriginal()
{
    nextToOriginal_ = true;
    save();
}

void ExportPrefs::toggleFavorite(const QString& path)
{
    const QString p = normalized(path);
    if (p.isEmpty()) return;
    const int at = indexOfPath(favorites_, p);
    if (at >= 0) favorites_.removeAt(at);
    else favorites_ << p;
    save();
}

bool ExportPrefs::isFavorite(const QString& path) const
{
    return indexOfPath(favorites_, path) >= 0;
}

QString ExportPrefs::targetFolder(const QString& sourcePath) const
{
    return nextToOriginal_ ? normalized(QFileInfo(sourcePath).absolutePath()) : folderPath();
}

void ExportPrefs::noteExported(const QString& outputPath)
{
    const QString dir = normalized(QFileInfo(outputPath).absolutePath());
    const int at = indexOfPath(recents_, dir);
    if (at >= 0) recents_.removeAt(at);
    recents_.prepend(dir);
    while (recents_.size() > kMaxRecents) recents_.removeLast();
    ++sequence_;
    save();
}
