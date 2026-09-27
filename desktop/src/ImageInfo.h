#pragma once

#include <QVariantMap>

#include "dfee/bridge_types.hpp"

// Capture details for the UI from the engine's decode metadata. Empty or zero
// values are omitted; rendered (TIFF) inputs carry no camera fields because the
// engine does not read their EXIF.
QVariantMap imageInfoFromMetadata(const dfee::NativeRawMetadata& md);
