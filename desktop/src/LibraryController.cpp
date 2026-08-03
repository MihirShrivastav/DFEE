#include "LibraryController.h"

#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

namespace {
// Supported input extensions for the library (RAW + rendered TIFF).
const QStringList kNameFilters = {
    "*.tif", "*.tiff", "*.arw", "*.nef", "*.cr2", "*.cr3",
    "*.raf", "*.rw2", "*.dng", "*.orf", "*.pef", "*.srw"
};
}  // namespace

LibraryController::LibraryController(QObject* parent) : QObject(parent) {
    loadPinned();
    rebuildFolders();
    if (!pinned_.isEmpty()) {
        selectFolder(pinned_.constFirst());
    }
}

void LibraryController::loadPinned() {
    QSettings settings;
    pinned_ = settings.value(QStringLiteral("library/pinnedFolders")).toStringList();
    // Drop folders that no longer exist.
    QStringList kept;
    for (const QString& p : pinned_) {
        if (QFileInfo(p).isDir()) {
            kept << p;
        }
    }
    pinned_ = kept;
}

void LibraryController::savePinned() {
    QSettings settings;
    settings.setValue(QStringLiteral("library/pinnedFolders"), pinned_);
}

void LibraryController::rebuildFolders() {
    folders_.clear();
    for (const QString& p : pinned_) {
        QVariantMap row;
        row["path"] = p;
        row["name"] = QDir(p).dirName().isEmpty() ? p : QDir(p).dirName();
        folders_.append(row);
    }
    emit foldersChanged();
}

void LibraryController::addFolder(const QUrl& url) {
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        return;
    }
    if (!pinned_.contains(path)) {
        pinned_.prepend(path);
        savePinned();
        rebuildFolders();
    }
    selectFolder(path);
}

void LibraryController::removeFolder(const QString& path) {
    if (pinned_.removeAll(path) > 0) {
        savePinned();
        rebuildFolders();
        if (currentFolder_ == path) {
            if (!pinned_.isEmpty()) {
                selectFolder(pinned_.constFirst());
            } else {
                currentFolder_.clear();
                files_.clear();
                emit currentFolderChanged();
                emit filesChanged();
            }
        }
    }
}

void LibraryController::selectFolder(const QString& path) {
    if (!QFileInfo(path).isDir()) {
        return;
    }
    currentFolder_ = path;
    emit currentFolderChanged();
    listFiles(path);
}

void LibraryController::listFiles(const QString& folder) {
    files_.clear();
    QDir dir(folder);
    const QFileInfoList entries = dir.entryInfoList(
        kNameFilters, QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo& info : entries) {
        QVariantMap row;
        row["path"] = info.absoluteFilePath();
        row["name"] = info.fileName();
        files_.append(row);
    }
    emit filesChanged();
}
