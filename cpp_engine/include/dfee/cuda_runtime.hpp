#pragma once

#include <string>

namespace dfee {

struct CudaStatus {
    bool compiled = false;
    bool available = false;
    bool active = false;
    int device_count = 0;
    std::string device_name;
    std::string mode = "cpu";
    std::string fallback_reason = "CUDA target not compiled";
};

[[nodiscard]] CudaStatus query_cuda_status() noexcept;

}  // namespace dfee
