#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QUrl>

// Lightweight library: pinned folders (persisted via QSettings) + the supported
// image files in the currently-selected folder, for the left pane grid + bottom
// filmstrip. Thumbnails are served separately by ThumbnailImageProvider.
class LibraryController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(QString currentFolder READ currentFolder NOTIFY currentFolderChanged)
    Q_PROPERTY(QVariantList files READ files NOTIFY filesChanged)

public:
    explicit LibraryController(QObject* parent = nullptr);

    QVariantList folders() const { return folders_; }
    QString currentFolder() const { return currentFolder_; }
    QVariantList files() const { return files_; }

    Q_INVOKABLE void addFolder(const QUrl& url);
    Q_INVOKABLE void removeFolder(const QString& path);
    Q_INVOKABLE void selectFolder(const QString& path);

signals:
    void foldersChanged();
    void currentFolderChanged();
    void filesChanged();

private:
    void loadPinned();
    void savePinned();
    void rebuildFolders();
    void listFiles(const QString& folder);

    QStringList pinned_;        // absolute folder paths
    QVariantList folders_;      // rows: { path, name }
    QString currentFolder_;
    QVariantList files_;        // rows: { path, name }
};
