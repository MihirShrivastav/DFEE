#include "EditStore.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

class EditStoreTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripsRecordAndHistory()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        QVERIFY(store.isOpen());
        EditRecord in;
        in.stock = "portra_400";
        in.controls = {{"film_contrast", 150.0}, {"flip_h", true}};
        in.history = {{"Import", "", "none", {}},
                      {"Film stock: Kodak Portra 400", "stock", "portra_400", {}},
                      {"Film contrast", "film_contrast", "portra_400", {{"film_contrast", 150.0}}}};
        in.historyIndex = 2;
        QVERIFY(store.save("E:/shoot/A.ARW", in, true));
        const auto out = store.load("E:/shoot/A.ARW");
        QVERIFY(out.has_value());
        QCOMPARE(out->stock, QString("portra_400"));
        QCOMPARE(out->controls.value("film_contrast").toDouble(), 150.0);
        QCOMPARE(out->controls.value("flip_h").toBool(), true);
        QCOMPARE(out->history.size(), 3);
        QCOMPARE(out->history.at(1).coalesceKey, QString("stock"));
        QCOMPARE(out->history.at(2).controls.value("film_contrast").toDouble(), 150.0);
        QCOMPARE(out->historyIndex, 2);
        QCOMPARE(store.recordCount(), 1);
    }

    void missingPhotoLoadsNothing()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        QVERIFY(!store.load("E:/never/opened.nef").has_value());
    }

    void pathKeyNormalises()
    {
        QCOMPARE(EditStore::pathKey("E:\\Shoot\\A.ARW"), EditStore::pathKey("e:/shoot/a.arw"));
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "velvia_50"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
        QVERIFY(store.save("E:\\Shoot\\A.ARW", r, true));
        const auto out = store.load("e:/shoot/a.arw");
        QVERIFY(out.has_value());
        QCOMPARE(out->stock, QString("velvia_50"));
    }

    void uneditedPhotoIsNotInserted()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "none"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/B.ARW", r, false));
        QCOMPARE(store.recordCount(), 0);
    }

    void resetPhotoUpdatesExistingRow()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord edited; edited.stock = "tri_x_400"; edited.history = {{"Import", "", "none", {}}}; edited.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/C.ARW", edited, true));
        EditRecord reset; reset.stock = "none"; reset.history = edited.history; reset.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/C.ARW", reset, false));
        QCOMPARE(store.load("E:/shoot/C.ARW")->stock, QString("none"));
        QCOMPARE(store.recordCount(), 1);
    }

    void historyIsCappedKeepingBaseline()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "gold_200";
        for (int i = 0; i < 80; ++i) r.history.append({QString("step %1").arg(i), "", "gold_200", {}});
        r.historyIndex = 79;
        QVERIFY(store.save("E:/shoot/D.ARW", r, true));
        const auto out = store.load("E:/shoot/D.ARW");
        QCOMPARE(out->history.size(), EditStore::kMaxHistory);
        QCOMPARE(out->history.first().label, QString("step 0"));
        QCOMPARE(out->history.last().label, QString("step 79"));
        QCOMPARE(out->historyIndex, EditStore::kMaxHistory - 1);
    }

    void unopenableStoreIsHarmless()
    {
        QTemporaryDir dir;
        // A directory where the database file should be: SQLite cannot open it.
        QDir(dir.path()).mkdir("catalog.sqlite");
        EditStore store(dir.filePath("catalog.sqlite"));
        QVERIFY(!store.isOpen());
        EditRecord r; r.stock = "portra_160";
        QVERIFY(!store.save("E:/shoot/E.ARW", r, true));
        QVERIFY(!store.load("E:/shoot/E.ARW").has_value());
        QCOMPARE(store.recordCount(), 0);
    }

    void survivesReopen()
    {
        QTemporaryDir dir;
        {
            EditStore store(dir.filePath("catalog.sqlite"));
            EditRecord r; r.stock = "ektar_100"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
            QVERIFY(store.save("E:/shoot/F.ARW", r, true));
        }
        EditStore again(dir.filePath("catalog.sqlite"));
        QCOMPARE(again.load("E:/shoot/F.ARW")->stock, QString("ektar_100"));
    }

    void reportsEditedPhotos()
    {
        QTemporaryDir dir;
        EditStore store(dir.filePath("catalog.sqlite"));
        EditRecord r; r.stock = "portra_400"; r.history = {{"Import", "", "none", {}}}; r.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/A.ARW", r, true));
        QVERIFY(store.isEdited("e:\\shoot\\a.arw"));
        QVERIFY(!store.isEdited("E:/shoot/B.ARW"));
        EditRecord clean; clean.stock = "none"; clean.history = {{"Import", "", "none", {}}}; clean.historyIndex = 0;
        QVERIFY(store.save("E:/shoot/A.ARW", clean, false));   // edits undone: row stays, not edited
        QVERIFY(!store.isEdited("E:/shoot/A.ARW"));
    }
};

QTEST_GUILESS_MAIN(EditStoreTest)
#include "edit_store_test.moc"
