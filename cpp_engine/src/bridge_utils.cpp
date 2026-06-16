#include "dfee/bridge_utils.hpp"

#include <iomanip>
#include <sstream>
#include <utility>

namespace dfee {
namespace {

[[nodiscard]] std::string escape_json(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

}  // namespace

ScopedStageTimer::ScopedStageTimer(NativeEngineMetadata& metadata, std::string stage_name)
    : metadata_(metadata),
      stage_name_(std::move(stage_name)),
      start_(std::chrono::steady_clock::now()) {}

ScopedStageTimer::~ScopedStageTimer() {
    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration<double, std::milli>(end - start_);
    metadata_.timings.push_back({
        .stage = stage_name_,
        .milliseconds = duration.count(),
    });
}

std::string serialize_native_error_json(const NativeError& error) {
    std::ostringstream out;
    out << "{"
        << "\"code\":\"" << escape_json(error.code) << "\","
        << "\"user_message\":\"" << escape_json(error.user_message) << "\","
        << "\"detail\":\"" << escape_json(error.detail) << "\""
        << "}";
    return out.str();
}

std::string serialize_native_engine_metadata_json(const NativeEngineMetadata& metadata) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "{"
        << "\"engine_version\":\"" << escape_json(metadata.engine_version) << "\","
        << "\"cuda_status\":{"
        << "\"mode\":\"" << escape_json(metadata.cuda_status.mode) << "\","
        << "\"compiled\":" << (metadata.cuda_status.compiled ? "true" : "false") << ","
        << "\"available\":" << (metadata.cuda_status.available ? "true" : "false") << ","
        << "\"active\":" << (metadata.cuda_status.active ? "true" : "false") << ","
        << "\"device_count\":" << metadata.cuda_status.device_count << ","
        << "\"device_name\":\"" << escape_json(metadata.cuda_status.device_name) << "\","
        << "\"fallback_reason\":\"" << escape_json(metadata.cuda_status.fallback_reason) << "\""
        << "},"
        << "\"timings\":[";

    for (size_t i = 0; i < metadata.timings.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << "{"
            << "\"stage\":\"" << escape_json(metadata.timings[i].stage) << "\","
            << "\"milliseconds\":" << metadata.timings[i].milliseconds
            << "}";
    }

    out << "]"
        << "}";
    return out.str();
}

}  // namespace dfee
