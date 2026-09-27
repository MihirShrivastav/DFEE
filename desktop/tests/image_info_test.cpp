#include "ImageInfo.h"

#include <QtTest>

class ImageInfoTest : public QObject {
    Q_OBJECT
private slots:
    void rawCaptureDetails()
    {
        dfee::NativeRawMetadata md;
        md.camera_make = "Sony";
        md.camera_model = "ILCE-7CR";
        md.lens_model = "FE 35mm F1.8";
        md.iso = 400;
        md.shutter_speed_str = "1/250";
        md.aperture = 2.8;
        md.focal_length = 35.0;
        md.image_width = 9504;
        md.image_height = 6336;
        md.input_kind = "developed_raw";
        md.developer_profile = "Sony ILCE-7CR Adobe Standard + Adobe Color";
        const QVariantMap m = imageInfoFromMetadata(md);
        QCOMPARE(m.value("camera").toString(), QString("Sony ILCE-7CR"));
        QCOMPARE(m.value("lens").toString(), QString("FE 35mm F1.8"));
        QCOMPARE(m.value("iso").toInt(), 400);
        QCOMPARE(m.value("shutter").toString(), QString("1/250"));
        QCOMPARE(m.value("aperture").toString(), QString("f/2.8"));
        QCOMPARE(m.value("focal").toString(), QString("35 mm"));
        QCOMPARE(m.value("width").toInt(), 9504);
        QCOMPARE(m.value("inputKind").toString(), QString("developed_raw"));
    }

    void modelAlreadyNamesMaker()
    {
        dfee::NativeRawMetadata md;
        md.camera_make = "Canon";
        md.camera_model = "Canon EOS R";
        md.input_kind = "developed_raw";
        QCOMPARE(imageInfoFromMetadata(md).value("camera").toString(), QString("Canon EOS R"));
    }

    void wholeApertureHasNoDecimal()
    {
        dfee::NativeRawMetadata md;
        md.aperture = 2.0;
        md.input_kind = "developed_raw";
        QCOMPARE(imageInfoFromMetadata(md).value("aperture").toString(), QString("f/2"));
    }

    void renderedTiffHasNoCameraFields()
    {
        dfee::NativeRawMetadata md;
        md.camera_make = "Rendered";
        md.camera_model = "TIFF";
        md.iso = 0;
        md.image_width = 4000;
        md.image_height = 3000;
        md.input_kind = "rendered";
        const QVariantMap m = imageInfoFromMetadata(md);
        QVERIFY(!m.contains("camera"));
        QVERIFY(!m.contains("iso"));
        QVERIFY(!m.contains("aperture"));
        QCOMPARE(m.value("width").toInt(), 4000);
        QCOMPARE(m.value("inputKind").toString(), QString("rendered"));
    }
};

QTEST_GUILESS_MAIN(ImageInfoTest)
#include "image_info_test.moc"
