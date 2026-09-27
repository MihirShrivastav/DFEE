#pragma once

#include <QHash>
#include <QString>

// Desktop-side presentation data for film stocks (the engine's profile loader
// rejects unknown YAML fields, so this lives in desktop/resources/stocks/catalog.json).
struct StockInfo {
    QString group;   // negative | slide | bw
    int iso = 0;
    QString blurb;   // one line describing the look
};

// Missing or invalid file -> empty catalog (the UI falls back to the engine type).
QHash<QString, StockInfo> loadStockCatalog(const QString& path);
QString stockGroupLabel(const QString& group);
