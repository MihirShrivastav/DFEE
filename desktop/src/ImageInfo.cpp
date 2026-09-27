#include "ImageInfo.h"

#include <QString>

#include <cmath>

QVariantMap imageInfoFromMetadata(const dfee::NativeRawMetadata& md)
{
    QVariantMap m;
    const QString kind = QString::fromStdString(md.input_kind);
    m["inputKind"] = kind;
    if (md.image_width > 0) m["width"] = md.image_width;
    if (md.image_height > 0) m["height"] = md.image_height;
    if (!md.developer_profile.empty()) m["developerProfile"] = QString::fromStdString(md.developer_profile);
    if (kind == QLatin1String("rendered")) return m;

    const QString make = QString::fromStdString(md.camera_make).trimmed();
    const QString model = QString::fromStdString(md.camera_model).trimmed();
    const QString camera = model.startsWith(make, Qt::CaseInsensitive) ? model
                                                                      : (make + ' ' + model).trimmed();
    if (!camera.isEmpty()) m["camera"] = camera;
    if (!md.lens_model.empty()) m["lens"] = QString::fromStdString(md.lens_model);
    if (md.iso > 0) m["iso"] = md.iso;
    if (!md.shutter_speed_str.empty()) m["shutter"] = QString::fromStdString(md.shutter_speed_str);
    if (md.aperture > 0.0) m["aperture"] = QStringLiteral("f/") + QString::number(md.aperture, 'g', 3);
    if (md.focal_length > 0.0) m["focal"] = QString::number(std::lround(md.focal_length)) + QStringLiteral(" mm");
    return m;
}
