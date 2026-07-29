# Multi-core Render/Export — Implementation Plan

## Goal
Cut export time (~27 s on a 44 MP frame; ~18 s of it in the full-res film stages) by
running the CPU-bound **per-pixel** stages across all cores with **OpenMP**, following the
stable open reference (darktable/RawTherapee use OpenMP throughout their CPU pixelpipe).

**Hard constraints**
- **Bit-identical output.** Every parallelized stage is a per-pixel *map* (each output pixel
  depends only on its own input pixel + read-only plan/LUTs). Splitting rows across threads
  changes nothing — no seams, no banding, no halos, no rounding drift. This is *not* tiling.
- **No crash on any x86-64 CPU.** Threading uses no special instructions. We keep the
  **SSE2 baseline arch** (mandatory on all x86-64) and do **not** force `/arch:AVX*`. The only
  thing that can illegal-instruction on old CPUs is SIMD compiled for an unsupported ISA — we
  don't do that here.
- **No oversubscription.** OpenCV already threads its own ops (blurs). Our OpenMP loops never
  call OpenCV, and OpenCV calls never run inside an OpenMP region → no N×N thread explosion.
- **Deterministic.** Grain RNG fills and analysis reductions stay serial.
- **Graceful degradation.** Builds and runs correctly with OpenMP absent (serial) and on a
  single core.

## Non-goals (deferred, separate projects)
- SIMD/AVX vectorization (would need runtime dispatch; conservative like darktable's opt-in).
- GPU / OpenCL.
- Tiling (that's a *memory* fix and carries seam risk; unrelated to this speed work).
- Any change to pixel math / the look.

---

## Phase 0 — Build enablement + safety scaffolding

1. **CMake OpenMP (guarded).** `find_package(OpenMP)`, and if found link `OpenMP::OpenMP_CXX`
   to `dfee_core` and define `DFEE_HAS_OPENMP=1` (else `=0`). Build must still succeed without it.
2. **Confirm arch baseline.** Verify no `/arch:AVX*` (MSVC) or `-mavx*` (GCC/Clang) is set;
   keep the default x86-64/SSE2 baseline. Document this as an intentional safety choice.
3. **Central parallel utility** — `cpp_engine/include/dfee/parallel.hpp`:
   ```cpp
   // Runs fn(y) for y in [0,height) across the configured threads; serial if OpenMP off
   // or thread cap == 1. Signed int index (OpenMP 2.0 / MSVC requirement).
   template <class Fn> void parallel_for_rows(int height, Fn&& fn);
   int  configured_thread_count();      // resolves env/cap once
   void set_thread_count(int n);        // applies omp_set_num_threads
   ```
   All parallel loops go through this — one place for the pragma, the thread policy, and the
   serial fallback. Keeps raw `#pragma omp` out of the stages.
4. **Thread policy.** Default = `omp_get_max_threads()` (hardware). Env override
   `DFEE_NATIVE_THREADS` (0/absent = auto; 1 = force serial; N = cap to N). Applied once at
   session construction. A sane upper cap (e.g. 32) to avoid pathological setups.
5. **OpenCV coexistence.** Do not change `cv::setNumThreads` (leave OpenCV's own pool). Rule:
   never wrap an OpenCV call in `parallel_for_rows`; never call OpenCV from inside one.

## Phase 1 — Hottest per-pixel stages (renderer.cpp), measured first
Convert the outer `for (y…)` to `parallel_for_rows`. Ordered by measured cost:
1. `apply_color_response_and_coupling`  (3.6 s)
2. `apply_hue_saturation`               (2.9 s)
3. `apply_color_compression`            (2.5 s)
4. `apply_subtractive_density`
5. `apply_film_tone_response`  (the pixel apply loop; LUT build stays serial — it's tiny)
6. `apply_dye_contamination`, `apply_panchromatic_conversion`, `apply_pre_film_normalization`
Each loop reviewed for shared mutable state (must be none; LUTs/plan are read-only shared).

## Phase 2 — Per-pixel stages in session.cpp
`apply_scene_referred_tone`, `apply_gamma_additive_tone`, `apply_post_film_color`, `apply_hsl`,
`apply_color_grading`, and the **export write** linear→sRGB conversion loop (packs the 8/16-bit
output buffer — a big loop, ~2 s). `apply_clarity/texture/dehaze` per-pixel portions if any.

## Phase 3 — Decode/write loops
- TIFF decode per-pixel linearize loop (`raw_decode.cpp`).
- `fill_decoded_image_from_float_rgb` derived-data loop: contains min/max/clip-count reductions.
  Either keep serial (cheap) or use order-independent `omp reduction` (min/max/int-sum are
  order-independent → still deterministic). Low priority.

## Phase 4 — Grain (careful, but still bit-identical)
- **Keep the noise-field RNG generation serial** (sequential RNG → deterministic field).
- Parallelize only the per-pixel `tanh`/normalize/composite loops that operate on the already-
  generated field. OpenCV convolutions stay on OpenCV's threads.
- Verify: same `grain_seed` → identical output, and serial vs parallel identical.

## Phase 5 — Verification (the safety gate)
1. **Bit-identical harness.** Export the same file twice — `DFEE_NATIVE_THREADS=1` (forced
   serial) vs auto (all cores) — and assert the output files are **byte-hash identical**, for a
   RAW and a TIFF across a few stocks (incl. grain on, halation on, a reversal stock). This is
   the direct proof of "no artifacts / no quality change."
2. **Timings.** Record per-stage ms before/after; confirm the film stages drop ~4–6×.
3. **Regression.** `ctest` (dfee_tests) 100%; native-bridge suite; server suite.
4. Rebuild the Release `.pyd`; commit.

## Risks & mitigations
| Risk | Mitigation |
|---|---|
| Data race (two threads write same pixel) | Row-partitioned writes are disjoint; LUTs/plan read-only. Review each loop. |
| Oversubscription w/ OpenCV | Never nest; OpenMP loops contain no OpenCV calls. |
| Non-determinism | RNG fill + analysis reductions serial (or order-independent reductions only). |
| Old-CPU crash | Threading only; SSE2 baseline; no forced AVX. |
| MSVC OpenMP 2.0 limits | Only `parallel for` with signed int index; nothing needing 3.0+. |
| `vcomp140.dll` at runtime | Ships with the VC++ redist (already required by the build); note in deploy. |
| Build without OpenMP | Guarded macro → `parallel_for_rows` runs serial; identical output. |

## Expected outcome
Film stages ~18 s → ~3–5 s (memory-bandwidth-limited, so ~4–6× not 12×); total export
~27 s → ~8–11 s. Interactive preview also gets snappier. **Output unchanged, byte-for-byte.**

## Rollout order
Phase 0 → Phase 1 (measure the big three, confirm bit-identical) → 2 → 3 → 4 → 5. Land in
one or two commits with the before/after timings and the hash-identical proof in the message.
