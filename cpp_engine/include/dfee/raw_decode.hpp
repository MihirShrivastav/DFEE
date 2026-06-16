#pragma once

#include "dfee/bridge_types.hpp"

namespace dfee {

[[nodiscard]] NativeRawDecodeResponse decode_raw_from_file(const NativeRawDecodeRequest& request);

}  // namespace dfee
