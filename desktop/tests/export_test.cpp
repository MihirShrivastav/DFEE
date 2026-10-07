#include "ExportNaming.h"

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
private slots:
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
};

QTEST_GUILESS_MAIN(ExportTest)
#include "export_test.moc"
