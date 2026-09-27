#include "StockCatalog.h"

#include <QDir>
#include <QtTest>

class StockCatalogTest : public QObject {
    Q_OBJECT
private slots:
    // Every engine stock profile must have a catalog entry (a new YAML without one
    // would appear in the Films browser with no group, ISO or description).
    void coversEveryStockProfile()
    {
        const QString root = QStringLiteral(DFEE_REPO_ROOT);
        const auto catalog = loadStockCatalog(root + "/desktop/resources/stocks/catalog.json");
        const QStringList yamls = QDir(root + "/profiles/stocks").entryList({"*.yaml"}, QDir::Files);
        QVERIFY(yamls.size() >= 33);
        for (const QString& f : yamls) {
            const QString id = f.chopped(5);
            QVERIFY2(catalog.contains(id), qPrintable("missing catalog entry: " + id));
            const StockInfo info = catalog.value(id);
            QVERIFY2(QStringList({"negative", "slide", "bw"}).contains(info.group), qPrintable(id));
            QVERIFY2(info.iso > 0, qPrintable(id));
            QVERIFY2(!info.blurb.isEmpty() && info.blurb.size() <= 60, qPrintable(id));
        }
    }

    void missingFileGivesEmptyCatalog()
    {
        QVERIFY(loadStockCatalog("Z:/no/such/catalog.json").isEmpty());
    }

    void groupLabels()
    {
        QCOMPARE(stockGroupLabel("negative"), QString("Color negative"));
        QCOMPARE(stockGroupLabel("slide"), QString("Slide"));
        QCOMPARE(stockGroupLabel("bw"), QString("B&W"));
    }
};

QTEST_GUILESS_MAIN(StockCatalogTest)
#include "stock_catalog_test.moc"
