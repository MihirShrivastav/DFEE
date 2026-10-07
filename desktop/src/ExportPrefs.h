#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

#include <memory>

class QSettings;

// Export destination, naming and format choices, remembered between launches in
// QSettings group "export" (the DFEE_UI_SETTINGS ini in tests). Paths are kept with
// '/' separators and compared case-insensitively (Windows).
class ExportPrefs : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(QString folderPath READ folderPath NOTIFY changed)
    Q_PROPERTY(bool nextToOriginal READ nextToOriginal NOTIFY changed)
    Q_PROPERTY(QString defaultFolder READ defaultFolder CONSTANT)
    Q_PROPERTY(QString nameTemplate READ nameTemplate WRITE setNameTemplate NOTIFY changed)
    Q_PROPERTY(QString collision READ collision WRITE setCollision NOTIFY changed)
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY changed)
    Q_PROPERTY(int jpegQuality READ jpegQuality WRITE setJpegQuality NOTIFY changed)
    Q_PROPERTY(int dpi READ dpi WRITE setDpi NOTIFY changed)
    Q_PROPERTY(QStringList favorites READ favorites NOTIFY changed)
    Q_PROPERTY(QStringList recents READ recents NOTIFY changed)
    Q_PROPERTY(int sequence READ sequence NOTIFY changed)
public:
    static constexpr int kMaxRecents = 6;
    // iniPath empty: the app's QSettings; otherwise that ini file.
    explicit ExportPrefs(const QString& iniPath = {}, QObject* parent = nullptr);
    ~ExportPrefs() override;

    static QString defaultFolderPath();

    QString folder() const { return folder_; }
    QString folderPath() const { return folder_.isEmpty() ? defaultFolder_ : folder_; }
    bool nextToOriginal() const { return nextToOriginal_; }
    QString defaultFolder() const { return defaultFolder_; }
    QString nameTemplate() const { return nameTemplate_; }
    void setNameTemplate(const QString& pattern);
    QString effectiveNameTemplate() const;
    QString collision() const { return collision_; }
    void setCollision(const QString& rule);
    QString format() const { return format_; }
    void setFormat(const QString& format);
    int jpegQuality() const { return jpegQuality_; }
    void setJpegQuality(int quality);
    int dpi() const { return dpi_; }
    void setDpi(int dpi);
    QStringList favorites() const { return favorites_; }
    QStringList recents() const { return recents_; }
    int sequence() const { return sequence_; }

    Q_INVOKABLE void useFolder(const QString& path);
    Q_INVOKABLE void useFolderUrl(const QUrl& url);
    Q_INVOKABLE void useDefaultFolder();
    Q_INVOKABLE void useNextToOriginal();
    Q_INVOKABLE void toggleFavorite(const QString& path);
    Q_INVOKABLE bool isFavorite(const QString& path) const;

    // Where an export of sourcePath goes.
    QString targetFolder(const QString& sourcePath) const;
    // After a successful export: its folder becomes the most recent, {seq} advances.
    void noteExported(const QString& outputPath);

signals:
    void changed();

private:
    void save();
    std::unique_ptr<QSettings> settings_;
    QString defaultFolder_;
    QString folder_;
    bool nextToOriginal_ = false;
    QString nameTemplate_;
    QString collision_;
    QString format_;
    int jpegQuality_ = 92;
    int dpi_ = 300;
    QStringList favorites_;
    QStringList recents_;
    int sequence_ = 1;
};
