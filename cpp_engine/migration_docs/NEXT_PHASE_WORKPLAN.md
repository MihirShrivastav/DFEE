# DFEE Native Engine Next-Phase Workplan

This workplan consolidates the remaining high-value migration and performance items after the current M3 parity push. It is intentionally ordered to keep correctness, operator clarity, and interactive editing latency ahead of premature CUDA work.

## Current Position

- CPU-native decode, analysis, solver, preview render, and export render are implemented.
- FastAPI native support is staged and mostly working behind `DFEE_USE_NATIVE_ENGINE=1`.
- The main remaining functional gap is that `/api/select` still depends on the Python ingest/analyze path for diagnostics and compatibility state.
- The main remaining performance gap is native full export, where the full-resolution color-response stage still dominates warm render time.
- CUDA plumbing exists only as status/build scaffolding, not real runtime probing or acceleration.

## Priority Order

1. Complete native FastAPI cutover on the backend contract side.
2. Reduce the remaining export hotspot with benchmark-disciplined CPU work.
3. Finish export/report/encoder cleanup required for a clean native default path.
4. Add real CUDA runtime probing and execution scaffolding.
5. Start post-parity quality redesign only after the CPU baseline is stable.

## Phase A - Native API Cutover

### Goal

Close `BUG-005` and make the native backend the default execution path without breaking route contracts.

### Tasks

- Add a native select/diagnostics response surface that covers the data `server.py` still derives from Python during `/api/select`.
- Move `/api/select` from native warmup plus Python analysis to native-first response generation.
- Keep Python route definitions, validation, logging, and fallback behavior in place while shifting heavy work fully to the native engine.
- Decide and codify `/api/select` session behavior:
  - eager warm draft decode and raw preview during select, or
  - lazy warm on first preview/raw-preview request.
- Promote `DFEE_USE_NATIVE_ENGINE=1` behavior into the default path once the integration suite is stable.
- Keep route-level override flags temporarily for debugging during cutover, then retire them once confidence is high.

### Deliverables

- Native bridge contract for select diagnostics and metadata.
- `server.py` native-first `/api/select`.
- Updated route behavior docs and startup guidance.
- Bug tracker updates for `BUG-005` and `BUG-003`.

### Verification

- `pytest tests/test_server_errors.py -q`
- Native bridge tests for select metadata/diagnostics.
- Manual server smoke on:
  - file list
  - select
  - raw preview
  - preview render
  - export
- Confirm logs clearly show backend=`native` on the full happy path.

## Phase B - Export Hotspot Reduction

### Goal

Push `M3-014` to done by reducing the remaining full-resolution export hotspot tracked in `BUG-028`.

### Tasks

- Add an isolated native micro-benchmark around the color-response and clamp/re-entry path so we can evaluate that stage without full export noise.
- Focus optimization on the confirmed clamp/re-entry branch rather than rewriting the full color stage blindly.
- Keep all experiments parity-safe and benchmark-gated.
- Prefer branch-reduction, math simplification, cache locality, and data-layout wins before broader algorithmic redesign.
- Keep the existing named baseline flow in `cpp_engine/tools/export_benchmark.py` as the acceptance gate for end-to-end wins.

### Deliverables

- Stage-local benchmark or profiling harness for color-response.
- One or more accepted export optimizations with measured warm-cache wins.
- Updated benchmark artifacts and bug tracker evidence.

### Verification

- `cmake --build cpp_engine\\out\\build\\windows-msvc-vcpkg --config Release --target dfee_native dfee_tests`
- `ctest --test-dir cpp_engine\\out\\build\\windows-msvc-vcpkg -C Release --output-on-failure`
- `python cpp_engine\\tools\\export_benchmark.py --baseline-name export_stable --fail-on-regression`
- Optional targeted trace with `DFEE_TRACE_GAMUT_REENTRY=1`

## Phase C - Native Export And Report Cleanup

### Goal

Finish the remaining M4 backend-quality tasks needed for a clean native default path.

### Tasks

- Review current preview/export encoding stack and decide what still needs dedicated native encoders versus what is already sufficiently handled by OpenCV.
- Finish native report JSON writing so report generation no longer depends on Python-side shaping.
- Verify response compatibility for:
  - JPEG preview
  - final JPEG export with quality control
  - 8-bit PNG export
  - 16-bit PNG export
  - 16-bit TIFF export
- Expand the export contract toward photographer-grade controls:
  - JPEG quality
  - export DPI metadata
  - color-space selection policy
  - metadata embed toggle
  - future TIFF compression hooks
- Add engine timing metadata cleanly into report/output artifacts without disturbing current API response shapes.
- Remove temporary debug-only bridges once the route layer no longer needs them.

### Deliverables

- Completed `M4-001` through `M4-005` as actually needed by the chosen encoder stack.
- Clean native report writer.
- Finalized route contract notes in API docs.

### Verification

- Export-format integration tests.
- Report compatibility tests against current JSON shape.
- Manual open/inspection of generated JPEG, PNG, and TIFF outputs.

## Phase D - CUDA Runtime And Execution Scaffolding

### Goal

Close `BUG-006` and make CUDA a real optional execution backend instead of a stub status surface.

### Tasks

- Implement runtime CUDA probing and device enumeration.
- Report backend mode consistently as:
  - `cpu`
  - `cuda_available`
  - `cuda_active`
  - `cuda_fallback`
- Add a GPU memory budget model and tiled scheduling for large renders.
- Define the first CUDA offload set:
  - RGB/OKLab/OKLCH transforms
  - tone curves
  - zone masks
  - bloom/halation blur baseline
  - grain
- Keep full CPU fallback available for every accelerated stage.

### Deliverables

- Real CUDA runtime status in native bridge, CLI, and startup logs.
- First CUDA-capable execution path with safe fallback.
- Tests for no-CUDA and CUDA-present modes.

### Verification

- Native CUDA/no-CUDA tests.
- CPU-vs-CUDA tolerance tests.
- Large-image tiled render smoke test.

## Phase E - Post-Parity Redesign

### Goal

Improve quality and responsiveness further without losing reproducibility.

### Tasks

- Version the effect pipeline so improved results remain explainable.
- Revisit bloom/halation with a better diffusion model once the parity baseline is stable.
- Revisit grain with a more procedural or precomputed-field design.
- Revisit dehaze/local contrast only after dedicated parity/regression coverage exists.

### Deliverables

- `M6-001` through `M6-004`
- Versioned report metadata for improved-effect paths

## Immediate Execution Plan

This is the recommended near-term sequence for the next implementation slices:

1. Phase A: native `/api/select` contract and backend-default cutover.
2. Phase B: isolated color-stage benchmark plus accepted export optimization.
3. Phase C: report/encoder cleanup needed to make native the clean default.
4. Phase D: real CUDA probing and first execution scaffolding.

## Operating Rules

- Do not merge speculative optimizations without a measured win against the named baseline.
- Do not broaden route response shapes while doing backend cutover.
- Keep commits scoped to one meaningful slice each.
- Update `TASKLIST.md`, `BUG_TRACKER.md`, API docs, and APAM whenever a phase meaningfully advances.
- Stop local servers after manual verification.
