#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

// One history step as persisted: label, coalescing key, stock and (sparse) controls.
struct StoredHistoryStep {
    QString label;
    QString coalesceKey;
    QString stock;
    QVariantMap controls;
};

// A photo's persisted edit state. `controls` is whatever map the caller saves
// (EngineController stores only non-default keys); numbers return as double.
struct EditRecord {
    QString stock;
    QVariantMap controls;
    QVector<StoredHistoryStep> history;
    int historyIndex = -1;
};

// Per-photo edit memory: a SQLite catalog in the app's data folder, keyed by a
// normalised path. Photo folders are never written to. If the database cannot be
// opened the store stays closed and every call is a harmless no-op.
class EditStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(int recordCount READ recordCount NOTIFY changed)
public:
    static constexpr int kMaxHistory = 50;

    explicit EditStore(const QString& databasePath, QObject* parent = nullptr);
    ~EditStore() override;

    // DFEE_CATALOG_PATH when set (tests), else <AppDataLocation>/catalog.sqlite.
    static QString defaultPath();
    // Absolute, cleaned, forward slashes, lower-case (Windows paths are case-insensitive).
    static QString pathKey(const QString& path);

    bool isOpen() const { return open_; }
    std::optional<EditRecord> load(const QString& photoPath) const;
    // Writes the record. An unedited photo with no existing row is not inserted,
    // so merely viewing photos never grows the catalog.
    bool save(const QString& photoPath, const EditRecord& record, bool edited);
    int recordCount() const;

signals:
    void changed();

private:
    QString connection_;
    bool open_ = false;
};
