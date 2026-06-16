#pragma once

#include <chrono>
#include <string>

#include "dfee/bridge_types.hpp"

namespace dfee {

class ScopedStageTimer {
public:
    ScopedStageTimer(NativeEngineMetadata& metadata, std::string stage_name);
    ~ScopedStageTimer();

    ScopedStageTimer(const ScopedStageTimer&) = delete;
    ScopedStageTimer& operator=(const ScopedStageTimer&) = delete;

private:
    NativeEngineMetadata& metadata_;
    std::string stage_name_;
    std::chrono::steady_clock::time_point start_;
};

[[nodiscard]] std::string serialize_native_error_json(const NativeError& error);
[[nodiscard]] std::string serialize_native_engine_metadata_json(const NativeEngineMetadata& metadata);
[[nodiscard]] std::string serialize_native_raw_metadata_json(const NativeRawMetadata& metadata);
[[nodiscard]] std::string serialize_native_raw_decode_summary_json(const NativeRawDecodeSummary& summary);

}  // namespace dfee
