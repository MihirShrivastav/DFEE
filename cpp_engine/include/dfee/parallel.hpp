#pragma once

// CPU parallelism for the per-pixel render stages. Threading only — no CPU-specific
// (SIMD/AVX) instructions — so it is safe on any x86-64 CPU and degrades to serial when
// OpenMP is unavailable or the thread count is 1. All parallel loops in the engine go
// through parallel_for_rows so the pragma, the thread policy, and the serial fallback
// live in exactly one place.

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#if DFEE_HAS_OPENMP
#include <omp.h>
#endif

namespace dfee {

// Resolve the desired thread count. Env DFEE_NATIVE_THREADS:
//   unset / 0 -> auto (all available cores)
//   1         -> serial (used by the bit-identical verification harness)
//   N         -> capped to N
// Clamped to a sane maximum to avoid pathological oversubscription.
[[nodiscard]] inline int configured_thread_count() {
    constexpr int kMaxThreads = 32;
    int hardware = 1;
#if DFEE_HAS_OPENMP
    hardware = std::max(1, omp_get_num_procs());
#endif
    int requested = hardware;
    if (const char* env = std::getenv("DFEE_NATIVE_THREADS")) {
        char* end = nullptr;
        const long value = std::strtol(env, &end, 10);
        if (end != env && value > 0) {
            requested = static_cast<int>(value);
        } else if (end != env && value == 0) {
            requested = hardware;  // explicit auto
        }
    }
    return std::clamp(requested, 1, kMaxThreads);
}

// Apply the thread count to the OpenMP runtime. Call once at session start.
inline void set_thread_count(const int threads) {
#if DFEE_HAS_OPENMP
    omp_set_num_threads(std::max(1, threads));
    omp_set_dynamic(0);  // keep the team size we asked for
#else
    (void)threads;
#endif
}

// Invoke fn(y) for every row y in [0, height). Parallel across rows when OpenMP is
// enabled (and the runtime thread count > 1); serial otherwise. fn MUST be safe to run
// concurrently for distinct y: it may read shared read-only state (LUTs, plan) but must
// only write outputs belonging to row y. Row partitions are disjoint, so per-pixel map
// stages are bit-identical to the serial version.
template <class Fn>
inline void parallel_for_rows(const int height, Fn&& fn) {
#if DFEE_HAS_OPENMP
#pragma omp parallel for schedule(static)
    for (int y = 0; y < height; ++y) {
        fn(y);
    }
#else
    for (int y = 0; y < height; ++y) {
        fn(y);
    }
#endif
}

// Flat variant: invoke fn(i) for every index i in [0, count) — for stages that iterate a
// flat pixel array. Same contract as parallel_for_rows (fn safe for distinct i; writes to
// index i's outputs only). Signed index for OpenMP 2.0 / MSVC.
template <class Fn>
inline void parallel_for_index(const std::ptrdiff_t count, Fn&& fn) {
#if DFEE_HAS_OPENMP
#pragma omp parallel for schedule(static)
    for (std::ptrdiff_t i = 0; i < count; ++i) {
        fn(i);
    }
#else
    for (std::ptrdiff_t i = 0; i < count; ++i) {
        fn(i);
    }
#endif
}

}  // namespace dfee
