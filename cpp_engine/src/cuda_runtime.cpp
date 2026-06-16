#include "dfee/cuda_runtime.hpp"

namespace dfee {

CudaStatus query_cuda_status() noexcept {
    CudaStatus status;
#if defined(DFEE_CUDA_COMPILED)
    status.mode = "cuda_available";
    status.compiled = true;
    status.available = true;
    status.active = false;
    status.device_count = 0;
    status.device_name = "CUDA runtime probing pending";
    status.fallback_reason = "CUDA target compiled, but device selection is not wired in milestone 1.";
#else
    status.mode = "cpu";
    status.compiled = false;
    status.available = false;
    status.active = false;
    status.device_count = 0;
    status.fallback_reason = "CMake configured without DFEE_ENABLE_CUDA.";
#endif
    return status;
}

}  // namespace dfee
