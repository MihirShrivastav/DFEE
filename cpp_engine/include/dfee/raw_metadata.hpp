#pragma once

#include "dfee/bridge_types.hpp"

namespace dfee {

[[nodiscard]] NativeRawMetadataResponse read_raw_metadata_from_file(const NativeRawMetadataRequest& request);

}  // namespace dfee
