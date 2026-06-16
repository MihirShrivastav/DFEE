#pragma once

#include <filesystem>
#include <string>

#include "dfee/bridge_types.hpp"
#include "dfee/cuda_runtime.hpp"
#include "dfee/raw_decode.hpp"
#include "dfee/profile.hpp"
#include "dfee/raw_metadata.hpp"

namespace dfee {

class EngineSession {
public:
    explicit EngineSession(std::filesystem::path project_root);

    [[nodiscard]] const std::filesystem::path& project_root() const noexcept;
    [[nodiscard]] NativeProfilesResponse list_profiles() const;
    [[nodiscard]] NativeSelectResponse select_file(const NativeSelectRequest& request);
    [[nodiscard]] NativeRawMetadataResponse read_raw_metadata(const NativeRawMetadataRequest& request) const;
    [[nodiscard]] NativeRawDecodeResponse decode_raw(const NativeRawDecodeRequest& request) const;
    [[nodiscard]] CudaStatus cuda_status() const noexcept;

private:
    std::filesystem::path project_root_;
    std::filesystem::path raw_dir_;
    std::filesystem::path stocks_dir_;
    std::filesystem::path print_stocks_dir_;
    std::string selected_filename_;
};

}  // namespace dfee
