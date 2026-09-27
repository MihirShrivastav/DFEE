#include "StockCatalog.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

QHash<QString, StockInfo> loadStockCatalog(const QString& path)
{
    QHash<QString, StockInfo> catalog;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return catalog;
    const QJsonObject stocks = QJsonDocument::fromJson(f.readAll()).object().value("stocks").toObject();
    for (auto it = stocks.constBegin(); it != stocks.constEnd(); ++it) {
        const QJsonObject o = it.value().toObject();
        catalog.insert(it.key(), StockInfo{o.value("group").toString(), o.value("iso").toInt(),
                                           o.value("blurb").toString()});
    }
    return catalog;
}

QString stockGroupLabel(const QString& group)
{
    if (group == QLatin1String("negative")) return QStringLiteral("Color negative");
    if (group == QLatin1String("slide")) return QStringLiteral("Slide");
    if (group == QLatin1String("bw")) return QStringLiteral("B&W");
    return QString();
}
