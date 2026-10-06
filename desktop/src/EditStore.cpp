#include "EditStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QDebug>

namespace {
constexpr int kSchemaVersion = 1;

QByteArray historyToJson(const QVector<StoredHistoryStep>& steps)
{
    QJsonArray arr;
    for (const StoredHistoryStep& s : steps) {
        QJsonObject o;
        o["label"] = s.label;
        o["key"] = s.coalesceKey;
        o["stock"] = s.stock;
        o["controls"] = QJsonObject::fromVariantMap(s.controls);
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

QVector<StoredHistoryStep> historyFromJson(const QByteArray& json)
{
    QVector<StoredHistoryStep> steps;
    for (const QJsonValue& v : QJsonDocument::fromJson(json).array()) {
        const QJsonObject o = v.toObject();
        steps.append({o.value("label").toString(), o.value("key").toString(),
                      o.value("stock").toString(), o.value("controls").toObject().toVariantMap()});
    }
    return steps;
}

// Keep the baseline (step 0) and the newest kMaxHistory-1 steps.
void capHistory(QVector<StoredHistoryStep>& steps, int& index)
{
    const int excess = int(steps.size()) - EditStore::kMaxHistory;
    if (excess <= 0) return;
    steps.erase(steps.begin() + 1, steps.begin() + 1 + excess);
    index = index <= 0 ? 0 : std::max(1, index - excess);
}
}  // namespace

EditStore::EditStore(const QString& databasePath, QObject* parent)
    : QObject(parent)
    , connection_(QStringLiteral("filmlab-catalog-") + QUuid::createUuid().toString(QUuid::Id128))
{
    QDir().mkpath(QFileInfo(databasePath).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_);
    db.setDatabaseName(databasePath);
    if (!db.open()) {
        qWarning() << "EditStore: cannot open" << databasePath << db.lastError().text();
        return;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    const bool created = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS photos ("
        " path_key TEXT PRIMARY KEY, path TEXT NOT NULL, stock TEXT NOT NULL,"
        " controls_json TEXT NOT NULL, history_json TEXT NOT NULL, history_index INTEGER NOT NULL,"
        " edited INTEGER NOT NULL, schema_version INTEGER NOT NULL, updated_at TEXT NOT NULL)"));
    if (!created) {
        qWarning() << "EditStore: cannot create schema" << q.lastError().text();
        return;
    }
    open_ = true;
}

EditStore::~EditStore()
{
    {
        QSqlDatabase db = QSqlDatabase::database(connection_, false);
        if (db.isOpen()) db.close();
    }
    QSqlDatabase::removeDatabase(connection_);
}

QString EditStore::defaultPath()
{
    const QString overridePath = qEnvironmentVariable("DFEE_CATALOG_PATH");
    if (!overridePath.isEmpty()) return overridePath;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/catalog.sqlite");
}

QString EditStore::pathKey(const QString& path)
{
    QString p = path;
    p.replace('\\', '/');
    return QDir::cleanPath(QFileInfo(p).absoluteFilePath()).toLower();
}

std::optional<EditRecord> EditStore::load(const QString& photoPath) const
{
    if (!open_) return std::nullopt;
    QSqlQuery q(QSqlDatabase::database(connection_));
    q.prepare(QStringLiteral(
        "SELECT stock, controls_json, history_json, history_index FROM photos WHERE path_key = ?"));
    q.addBindValue(pathKey(photoPath));
    if (!q.exec() || !q.next()) return std::nullopt;
    EditRecord r;
    r.stock = q.value(0).toString();
    r.controls = QJsonDocument::fromJson(q.value(1).toByteArray()).object().toVariantMap();
    r.history = historyFromJson(q.value(2).toByteArray());
    r.historyIndex = q.value(3).toInt();
    return r;
}

bool EditStore::save(const QString& photoPath, const EditRecord& record, bool edited)
{
    if (!open_) return false;
    QSqlDatabase db = QSqlDatabase::database(connection_);
    const QString key = pathKey(photoPath);
    if (!edited) {
        QSqlQuery exists(db);
        exists.prepare(QStringLiteral("SELECT 1 FROM photos WHERE path_key = ?"));
        exists.addBindValue(key);
        if (exists.exec() && !exists.next()) return true;  // never edited: nothing to remember
    }
    QVector<StoredHistoryStep> history = record.history;
    int index = record.historyIndex;
    capHistory(history, index);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO photos (path_key, path, stock, controls_json, history_json, history_index,"
        " edited, schema_version, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(path_key) DO UPDATE SET path = excluded.path, stock = excluded.stock,"
        " controls_json = excluded.controls_json, history_json = excluded.history_json,"
        " history_index = excluded.history_index, edited = excluded.edited,"
        " schema_version = excluded.schema_version, updated_at = excluded.updated_at"));
    q.addBindValue(key);
    q.addBindValue(photoPath);
    q.addBindValue(record.stock);
    q.addBindValue(QJsonDocument(QJsonObject::fromVariantMap(record.controls)).toJson(QJsonDocument::Compact));
    q.addBindValue(historyToJson(history));
    q.addBindValue(index);
    q.addBindValue(edited ? 1 : 0);
    q.addBindValue(kSchemaVersion);
    q.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "EditStore: save failed" << q.lastError().text();
        return false;
    }
    emit changed();
    return true;
}

int EditStore::recordCount() const
{
    if (!open_) return 0;
    QSqlQuery q(QSqlDatabase::database(connection_));
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM photos")) || !q.next()) return 0;
    return q.value(0).toInt();
}

bool EditStore::isEdited(const QString& photoPath) const
{
    if (!open_) return false;
    QSqlQuery q(QSqlDatabase::database(connection_));
    q.prepare(QStringLiteral("SELECT edited FROM photos WHERE path_key = ?"));
    q.addBindValue(pathKey(photoPath));
    return q.exec() && q.next() && q.value(0).toInt() == 1;
}
