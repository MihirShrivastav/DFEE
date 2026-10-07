#include "ExportNaming.h"
#include "ExportPrefs.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void touch(const QString& path)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
}
}

class ExportTest : public QObject {
    Q_OBJECT
private:
    QTemporaryDir exportsRoot;
private slots:
    void initTestCase()
    {
        QVERIFY(exportsRoot.isValid());
        qputenv("DFEE_EXPORT_DIR", exportsRoot.filePath("default").toUtf8());
    }

    void expandsTokens()
    {
        ExportNaming::Tokens t;
        t.name = "DSC0421";
        t.film = "Kodak Portra 400";
        t.date = "2026-05-03";
        t.camera = "Sony ILCE-7CR";
        t.sequence = 7;
        QCOMPARE(ExportNaming::expand("{name}_{film}", t), QString("DSC0421_Kodak Portra 400"));
        QCOMPARE(ExportNaming::expand("{date} {name}-{seq}", t), QString("2026-05-03 DSC0421-0007"));
        QCOMPARE(ExportNaming::expand("{camera}", t), QString("Sony ILCE-7CR"));
        QCOMPARE(ExportNaming::expand("Roll 3 {name}", t), QString("Roll 3 DSC0421"));
    }

    void emptyTemplatePartsLeaveNoStraySeparators()
    {
        ExportNaming::Tokens t;
        t.name = "DSC0421";
        QCOMPARE(ExportNaming::expand("{name}_{film}", t), QString("DSC0421"));
        QCOMPARE(ExportNaming::expand("{name}_{camera}_{film}", t), QString("DSC0421"));
        QCOMPARE(ExportNaming::expand("{film} - {name}", t), QString("DSC0421"));
        QCOMPARE(ExportNaming::expand("{camera}", t), QString("export"));
        QCOMPARE(ExportNaming::expand("", t), QString("export"));
    }

    void sanitizeMakesWindowsSafeNames()
    {
        QCOMPARE(ExportNaming::sanitize("a/b\\c:d*e?f\"g<h>i|j"), QString("a-b-c-d-e-f-g-h-i-j"));
        QCOMPARE(ExportNaming::sanitize("name. . "), QString("name"));
        QCOMPARE(ExportNaming::sanitize("con"), QString("con_"));
        QCOMPARE(ExportNaming::sanitize("LPT1"), QString("LPT1_"));
        QCOMPARE(ExportNaming::sanitize(QString(250, 'x')).size(), 180);
        QCOMPARE(ExportNaming::sanitize("   "), QString("export"));
    }

    void extensions()
    {
        QCOMPARE(ExportNaming::extensionFor("jpeg"), QString(".jpg"));
        QCOMPARE(ExportNaming::extensionFor("png8"), QString(".png"));
        QCOMPARE(ExportNaming::extensionFor("png16"), QString(".png"));
        QCOMPARE(ExportNaming::extensionFor("tiff"), QString(".tif"));
    }

    void collisionRules()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        using ExportNaming::Collision;
        auto free = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::AddNumber);
        QCOMPARE(free.fileName, QString("a.jpg"));
        QVERIFY(!free.exists);
        QVERIFY(!free.skip);

        touch(dir.filePath("a.jpg"));
        auto numbered = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::AddNumber);
        QCOMPARE(numbered.fileName, QString("a-2.jpg"));
        QCOMPARE(numbered.path, dir.filePath("a-2.jpg"));
        QVERIFY(numbered.exists);
        touch(dir.filePath("a-2.jpg"));
        QCOMPARE(ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::AddNumber).fileName,
                 QString("a-3.jpg"));

        auto replace = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::Replace);
        QCOMPARE(replace.fileName, QString("a.jpg"));
        QVERIFY(replace.exists);
        QVERIFY(!replace.skip);

        auto skip = ExportNaming::resolveTarget(dir.path(), "a", ".jpg", Collision::Skip);
        QVERIFY(skip.exists);
        QVERIFY(skip.skip);

        QCOMPARE(ExportNaming::collisionFromString("replace"), Collision::Replace);
        QCOMPARE(ExportNaming::collisionFromString("skip"), Collision::Skip);
        QCOMPARE(ExportNaming::collisionFromString("number"), Collision::AddNumber);
        QCOMPARE(ExportNaming::collisionFromString("bogus"), Collision::AddNumber);
    }

    void prefsDefaults()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        QCOMPARE(p.defaultFolder(), QDir::fromNativeSeparators(exportsRoot.filePath("default")));
        QCOMPARE(p.folderPath(), p.defaultFolder());
        QVERIFY(!p.nextToOriginal());
        QCOMPARE(p.nameTemplate(), QString("{name}_{film}"));
        QCOMPARE(p.collision(), QString("number"));
        QCOMPARE(p.format(), QString("jpeg"));
        QCOMPARE(p.jpegQuality(), 92);
        QCOMPARE(p.dpi(), 300);
        QCOMPARE(p.sequence(), 1);
        QVERIFY(p.favorites().isEmpty());
    }

    void prefsSurviveRestart()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath("ui.ini");
        const QString shoot = QDir::fromNativeSeparators(dir.filePath("shoot"));
        QDir().mkpath(shoot);
        {
            ExportPrefs p(ini);
            p.setNameTemplate("{date}_{name}");
            p.setCollision("skip");
            p.setFormat("tiff");
            p.setJpegQuality(80);
            p.setDpi(600);
            p.useFolder(shoot);
            p.toggleFavorite(shoot);
            p.noteExported(shoot + "/a.tif");
        }
        ExportPrefs q(ini);
        QCOMPARE(q.nameTemplate(), QString("{date}_{name}"));
        QCOMPARE(q.collision(), QString("skip"));
        QCOMPARE(q.format(), QString("tiff"));
        QCOMPARE(q.jpegQuality(), 80);
        QCOMPARE(q.dpi(), 600);
        QCOMPARE(q.folderPath(), shoot);
        QVERIFY(q.isFavorite(shoot));
        QCOMPARE(q.recents(), QStringList{shoot});
        QCOMPARE(q.sequence(), 2);
    }

    void invalidValuesFallBack()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.setFormat("gif");
        QCOMPARE(p.format(), QString("jpeg"));
        p.setCollision("maybe");
        QCOMPARE(p.collision(), QString("number"));
        p.setJpegQuality(500);
        QCOMPARE(p.jpegQuality(), 100);
        p.setDpi(0);
        QCOMPARE(p.dpi(), 1);
    }

    void blankTemplateIsKeptButResolvesToDefault()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.setNameTemplate("");
        QCOMPARE(p.nameTemplate(), QString(""));          // the field is not snapped back while typing
        QCOMPARE(p.effectiveNameTemplate(), QString("{name}_{film}"));
    }

    void nextToOriginalAndFolders()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.useNextToOriginal();
        QVERIFY(p.nextToOriginal());
        QCOMPARE(p.targetFolder("E:/new_raws/DSC0421.ARW"), QString("E:/new_raws"));
        const QString elsewhere = QDir::fromNativeSeparators(dir.filePath("x"));
        p.useFolder(QDir::toNativeSeparators(elsewhere));
        QVERIFY(!p.nextToOriginal());
        QCOMPARE(p.folderPath(), elsewhere);
        QCOMPARE(p.targetFolder("E:/new_raws/DSC0421.ARW"), elsewhere);
        p.useDefaultFolder();
        QCOMPARE(p.folderPath(), p.defaultFolder());
    }

    void favoritesToggle()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        p.toggleFavorite("D:/Prints");
        QVERIFY(p.isFavorite("d:\\prints\\"));               // case and separators don't matter
        p.toggleFavorite("d:/prints");
        QVERIFY(p.favorites().isEmpty());
    }

    void recentsAreMostRecentFirstCappedAndDeduplicated()
    {
        QTemporaryDir dir;
        ExportPrefs p(dir.filePath("ui.ini"));
        QStringList folders;
        for (int i = 0; i < 8; ++i) {
            folders << QDir::fromNativeSeparators(dir.filePath(QString("f%1").arg(i)));
            QDir().mkpath(folders.last());
            p.noteExported(folders.last() + "/a.jpg");
        }
        QCOMPARE(p.recents().size(), ExportPrefs::kMaxRecents);
        QCOMPARE(p.recents().first(), folders[7]);
        p.noteExported(folders[5].toUpper() + "/b.jpg");
        QCOMPARE(p.recents().size(), ExportPrefs::kMaxRecents);
        QVERIFY(p.recents().first().compare(folders[5], Qt::CaseInsensitive) == 0);
        QCOMPARE(p.sequence(), 10);
    }

    void recentsDropMissingFoldersOnLoad()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath("ui.ini");
        const QString gone = QDir::fromNativeSeparators(dir.filePath("gone"));
        QDir().mkpath(gone);
        {
            ExportPrefs p(ini);
            p.noteExported(gone + "/a.jpg");
        }
        QDir(gone).removeRecursively();
        ExportPrefs q(ini);
        QVERIFY(q.recents().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ExportTest)
#include "export_test.moc"
