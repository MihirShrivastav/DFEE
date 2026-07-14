# Color Compression (Film Lab v1 — Slice 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the subtractive **Color Compression** control (`filmic_v3`): compress the top of the saturation range (reduced colour dynamic range) and lean saturated hues gently toward their film-characteristic neighbours (red→orange, blue→cyan), hue-honest and neutral-preserving — and retire the old `palette_range` anchor pass that caused hue-shift artifacts.

**Architecture:** A new per-pixel OKLab renderer stage `apply_color_compression` does two coupled things: (1) a chroma *shoulder* that soft-compresses chroma above a threshold toward a ceiling (neutrals untouched), and (2) a bounded, chroma-gated hue lean toward neighbour hues using the codebase's existing red-orange / blue-cyan hue weighting. Applied only under `filmic_v3`, in the session render orchestration after the density stage. The `palette_range` anchor sub-pass is removed. Follows the Slice-1 parameter-model convention: `film_color_compression` defaults to `100` (stock's calibrated amount), forward = more compressed/filmic.

**Tech Stack:** C++20 (dfee_core, pybind), Python (FastAPI `server.py`, `dfee_native_bridge.py`, pytest), React (`frontend/src/App.jsx`), yaml-cpp, OpenCV, CTest.

## Global Constraints

- Behaviour lives under `effect_pipeline_version = filmic_v3`; `parity_v1` and `filmic_v2` stay byte-identical.
- Parameter-model convention (from Slice 1): `film_color_compression` is a float default `100.0`, range `[0,200]`; `100` = the stock's authored amount; `<100` relaxes, `>100` intensifies ("forward = more filmic"); omitted → `100`; server-clamped to `[0,200]`. Renderer applies `effective = stock_param * (control/100)`, bounded.
- **Hue-honest:** hue leans only toward a *neighbour*, saturation-gated, bounded, and zero for near-neutral pixels/skin — never a large hue rotation (no magenta-class artifacts). Saturation compression only reduces high chroma; it never desaturates neutrals or shifts hue.
- Applied only in the live `session.cpp` render orchestration (preview + export) gated by `filmic_v3`; monochrome stocks are a no-op; deterministic.
- `palette_range` is retired: its renderer sub-pass and UI control are removed. The request field remains an accepted no-op for contract stability until the Slice 8 consolidation (documented).
- Every new parameter carries native request field, FilmResponsePlan field, solver default (family fallback), report field, loader contract, and tests — in this slice.

---

## File Structure

- `cpp_engine/include/dfee/solver.hpp` — `SolverControls.film_color_compression`; `FilmResponsePlan` compression fields.
- `cpp_engine/include/dfee/bridge_types.hpp` — `NativePreviewRenderRequest.film_color_compression`.
- `cpp_engine/include/dfee/renderer.hpp` + `cpp_engine/src/renderer.cpp` — `apply_color_compression`; remove palette sub-pass + its constants/anchor resolution.
- `cpp_engine/src/solver.cpp` — read `compression.*` YAML + family defaults; populate plan; copy control.
- `cpp_engine/src/profile.cpp` — register `compression.strength`, `compression.threshold`, `compression.crosstalk`.
- `cpp_engine/src/session.cpp` — map control; report JSON; wire the stage into preview + export.
- `cpp_engine/bindings/python/dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py` — plumb + clamp.
- `frontend/src/App.jsx` — add Color Compression control; remove the Palette Range slider.
- `profiles/stocks/*.yaml` — `compression:` group on representative stocks.
- `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`, `tests/test_server_errors.py`.
- Docs: `documentation/`, `cpp_engine/migration_docs/STOCK_PROFILE_CONTRACT.md`, `README.md`, `docs/technical_architecture.md`.

**Canonical names:** request/field `film_color_compression` (float, default `100.0`); `FilmResponsePlan.film_color_compression`, `.compression_strength`, `.compression_threshold`, `.compression_crosstalk`; YAML `compression.strength` / `compression.threshold` / `compression.crosstalk`; renderer `FilmRenderer::apply_color_compression(const Image&, const FilmResponsePlan&)`.

**Build/test (Windows, repo root d:/Codebases/DFEE):**
- Native build: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_tests` (add `dfee_native` when the `.pyd` is needed; rebuild it after ANY dfee_core change or bridge/server tests use a stale loader).
- Native tests: `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure`
- Python: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`
- Frontend: `cd frontend; npm run build; npm run lint`

---

## Task 1: Color Compression schema + contract

Mirror the Slice-1 density plumbing for `film_color_compression`.

**Files:** `solver.hpp`, `bridge_types.hpp`, `session.cpp` (controls map ×2 + report), `solver.cpp`, `dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py`, `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`.

**Interfaces:**
- Produces: `film_color_compression` (float, default 100.0) on every layer; `FilmResponsePlan.film_color_compression`, `.compression_strength`, `.compression_threshold`, `.compression_crosstalk`; report emits them.

- [ ] **Step 1: Write the failing solver test** in `test_core.cpp` (`test_solver_compression_defaults`): load `portra_400.yaml`, solve default controls, assert `plan.film_response.film_color_compression == 100.0F` and `plan.film_response.compression_strength > 0.0F`; load `tri_x_400.yaml` and assert `compression_strength == 0.0F`. Register in `main()`.

- [ ] **Step 2: Run to verify it fails** (fields missing).

- [ ] **Step 3: Add `SolverControls.film_color_compression`** in `solver.hpp` after `film_color_density`:

```cpp
    float film_color_compression = 100.0F;
```

- [ ] **Step 4: Add `FilmResponsePlan` fields** in `solver.hpp` after the density fields:

```cpp
    float film_color_compression = 100.0F;  // control value (0..200), 100 = stock default
    float compression_strength = 0.0F;      // chroma-shoulder amount
    float compression_threshold = 0.45F;    // normalized chroma where the shoulder starts
    float compression_crosstalk = 0.0F;     // neighbour-lean amount
```

- [ ] **Step 5: Add request field** in `bridge_types.hpp` `NativePreviewRenderRequest` after `film_color_density`:

```cpp
    float film_color_compression = 100.0F;
```

- [ ] **Step 6: Map control → plan** in `session.cpp` at both `SolverControls` build sites (after `controls.film_color_density = request.film_color_density;`):

```cpp
    controls.film_color_compression = request.film_color_compression;
```

- [ ] **Step 7: Solver defaults + population** in `solver.cpp`. Add a helper after `density_defaults`:

```cpp
struct CompressionDefaults { float strength; float threshold; float crosstalk; };
[[nodiscard]] CompressionDefaults compression_defaults(const StockType type) {
    switch (type) {
        case StockType::ColorReversal: return {0.70F, 0.45F, 0.35F};
        case StockType::ColorNegative: return {0.55F, 0.45F, 0.30F};
        case StockType::Monochrome:    return {0.0F, 0.45F, 0.0F};
    }
    return {0.50F, 0.45F, 0.25F};
}
```

After the density population block, add:

```cpp
    const CompressionDefaults comp = compression_defaults(stock_profile.stock_type);
    plan.film_response.film_color_compression = controls.film_color_compression;
    plan.film_response.compression_strength = get_numeric(
        stock_profile.numeric_values, "compression.strength", comp.strength);
    plan.film_response.compression_threshold = get_numeric(
        stock_profile.numeric_values, "compression.threshold", comp.threshold);
    plan.film_response.compression_crosstalk = get_numeric(
        stock_profile.numeric_values, "compression.crosstalk", comp.crosstalk);
```

- [ ] **Step 8: Report JSON** in `session.cpp` after the density report lines:

```cpp
        << "\"film_color_compression\": " << json_number(render_plan.film_response.film_color_compression) << ",\n"
        << "\"compression_strength\": " << json_number(render_plan.film_response.compression_strength) << ",\n"
        << "\"compression_threshold\": " << json_number(render_plan.film_response.compression_threshold) << ",\n"
        << "\"compression_crosstalk\": " << json_number(render_plan.film_response.compression_crosstalk) << ",\n"
```

Insert these BEFORE the current last field of the `film_response` JSON object and ensure exactly one field remains without a trailing comma (match the existing pattern — the density fields were added the same way).

- [ ] **Step 9: pybind** in `dfee_native_module.cpp` after `film_color_density`:

```cpp
    request.film_color_compression = dict_float(dict, "film_color_compression", 100.0F);
```

- [ ] **Step 10: bridge dataclass** in `dfee_native_bridge.py` after `film_color_density`:

```python
    film_color_compression: float = 100.0
```

- [ ] **Step 11: server.py** — add `film_color_compression: float = 100.0` as a preview query param and a `PreviewRequest`/`ExportRequest` field; add `"film_color_compression": max(0.0, min(200.0, film_color_compression))` to the preview native dict(s) and `max(0.0, min(200.0, req.film_color_compression))` to the export native dict, mirroring `film_color_density`.

- [ ] **Step 12: Bridge round-trip test** in `tests/test_native_bridge.py`: default `film_color_compression == 100.0`; `NativeExportRequest(..., film_color_compression=130.0).film_color_compression == 130.0`.

- [ ] **Step 13: Build + tests.** Build `dfee_tests`, run full CTest (solver test passes; parity/filmic_v2 unchanged). Rebuild `dfee_native` and run `python -m pytest tests/test_native_bridge.py -q -k film_color_compression`.

- [ ] **Step 14: Commit**

```bash
git add -A
git commit -m "Add Film Color Compression schema + solver defaults"
```

---

## Task 2: Saturation-compression renderer stage (chroma shoulder)

**Files:** `cpp_engine/include/dfee/renderer.hpp`, `cpp_engine/src/renderer.cpp`, `cpp_engine/tests/test_core.cpp`.

**Interfaces:**
- Produces: `Image FilmRenderer::apply_color_compression(const Image& rgb_linear, const FilmResponsePlan& response) const;` — this task implements the chroma-shoulder half (crosstalk added in Task 3).

- [ ] **Step 1: Write the failing test** (`test_color_compression_compresses_high_chroma_preserves_neutral`): build a 1-row image with a high-chroma pixel, a mid-chroma pixel, and a neutral gray. With `compression_strength=0.6`, `compression_threshold=0.45`, `compression_crosstalk=0.0`, `film_color_compression=100`, assert:
  - the high-chroma pixel's output chroma **decreases** (compressed);
  - the mid-chroma pixel below threshold is ~unchanged (within 2e-3);
  - the neutral gray is unchanged (within 1e-4), hue of the high-chroma pixel unchanged (within 3e-3, crosstalk off);
  - `film_color_compression=0` is a byte-identical no-op; `=200` compresses the high-chroma pixel more than `100`.
Build-agnostic `throw`. (Chroma read-back may clamp in sRGB; assert direction/inequalities, not exact values, matching the Slice-1 density test style.)

- [ ] **Step 2: Run to verify it fails** (method missing).

- [ ] **Step 3: Declare** in `renderer.hpp` after `apply_subtractive_density`:

```cpp
    [[nodiscard]] Image apply_color_compression(
        const Image& rgb_linear,
        const FilmResponsePlan& response) const;
```

- [ ] **Step 4: Add constants** in the anonymous namespace of `renderer.cpp` near the density constants:

```cpp
// filmic_v3 colour compression.
constexpr float kCompressK        = 3.0F;  // chroma-shoulder hardness scale
constexpr float kCompressCrossLo  = 0.05F; // neutral gate lower edge (OKLCh C) for crosstalk
constexpr float kCompressCrossHi  = 0.10F; // neutral gate upper edge
constexpr float kCompressLeanGain = 0.20F; // max radians of neighbour lean at full crosstalk
```

- [ ] **Step 5: Implement the stage** (shoulder only for now) in `renderer.cpp`:

```cpp
Image FilmRenderer::apply_color_compression(
    const Image& rgb_linear,
    const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_color_compression expects a 3-channel RGB image");
    }
    const float ctl = std::clamp(response.film_color_compression / 100.0F, 0.0F, 2.0F);
    const float eff_strength = std::max(response.compression_strength, 0.0F) * ctl;
    const float t0 = std::clamp(response.compression_threshold, 0.0F, 1.0F);
    Image out(rgb_linear.width, rgb_linear.height, 3);
    if (eff_strength <= 0.0F) {
        out.pixels = rgb_linear.pixels;
        return out;
    }
    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        const OklabPixel lab = rgb_to_oklab_pixel(
            rgb_linear.pixels[i * 3 + 0], rgb_linear.pixels[i * 3 + 1], rgb_linear.pixels[i * 3 + 2]);
        const OklchPixel lch = oklab_to_oklch_pixel(lab);
        float cn = lch.c / kOklabChromaRef;               // normalized chroma
        if (cn > t0) {                                     // soft shoulder above threshold
            const float excess = cn - t0;
            const float k = kCompressK * eff_strength;
            cn = t0 + excess / (1.0F + k * excess);
        }
        const float c_new = std::max(cn * kOklabChromaRef, 0.0F);
        const OklabPixel adj = oklch_to_oklab_pixel({lch.l, c_new, lch.h});
        const auto rgb = oklab_to_rgb_pixel(adj);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    }
    return out;
}
```

- [ ] **Step 6: Run the test** — Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add cpp_engine/include/dfee/renderer.hpp cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement colour-compression chroma shoulder"
```

---

## Task 3: Neighbour-lean crosstalk (same stage)

Add the bounded, chroma-gated hue lean using the codebase's existing red-orange / blue-cyan hue weighting (so we do not guess anchor radians).

**Files:** `cpp_engine/src/renderer.cpp`, `cpp_engine/tests/test_core.cpp`.

- [ ] **Step 1: Write the failing test** (`test_color_compression_leans_neighbours_preserves_neutral`): with `compression_crosstalk=1.0`, `film_color_compression=100`, `compression_strength=0` (isolate crosstalk):
  - a saturated RED pixel's output hue shifts a **small bounded amount toward orange** and does NOT approach magenta (assert `|delta_h| < kCompressLeanGain + eps` and that it moves in the orange direction — validate the sign empirically and encode it);
  - a saturated BLUE pixel leans toward cyan (opposite region), bounded;
  - a **neutral/near-neutral** pixel's hue is unchanged (chroma gate; within 1e-3);
  - `compression_crosstalk=0` is a no-op on hue.
Build-agnostic `throw`. Because the exact sign of "toward orange" depends on the OKLCh hue convention, in Step 3 the implementer determines the sign by rendering a known red pixel and choosing the sign that decreases the hue distance to orange, then encodes it as a constant; the test asserts that chosen direction.

- [ ] **Step 2: Run to verify it fails** (crosstalk not applied).

- [ ] **Step 3: Add the crosstalk to the loop** in `apply_color_compression`, before building `c_new`/`adj`. Reuse the existing hue-weight convention (`cos(h - 0.6)` for red-orange, `cos(h - 4.0)` for blue-cyan, matching `apply_color_response_and_coupling_pipeline`):

```cpp
    const float eff_cross = std::max(response.compression_crosstalk, 0.0F) * ctl;
    // ... inside the loop, after computing lch:
        float h_new = lch.h;
        if (eff_cross > 0.0F) {
            const float g_c = smoothstep01(kCompressCrossLo, kCompressCrossHi, lch.c);
            const float red_cos = clampf(std::cos(lch.h - 0.6F), 0.0F, 1.0F);
            const float blue_cos = clampf(std::cos(lch.h - 4.0F), 0.0F, 1.0F);
            const float w_red = red_cos * red_cos;
            const float w_blue = blue_cos * blue_cos;
            // kLeanRedSign / kLeanBlueSign chosen so red leans toward orange and blue toward cyan.
            const float lean = kLeanRedSign * w_red + kLeanBlueSign * w_blue;
            h_new = wrap_angle_positive(lch.h + kCompressLeanGain * eff_cross * g_c * lean);
        }
```

Add `constexpr float kLeanRedSign` and `kLeanBlueSign` (magnitude 1.0, sign determined in Step 3 by the orange/cyan-direction check) to the anonymous namespace. Use `h_new` instead of `lch.h` when building the final `oklch_to_oklab_pixel({lch.l, c_new, h_new})`.

- [ ] **Step 4: Run the test** — Expected: PASS (red→orange bounded, blue→cyan bounded, neutrals unchanged, crosstalk=0 no-op).

- [ ] **Step 5: Confirm neutral no-op** — with both `eff_strength==0` and `eff_cross==0`, the stage returns the input copy (guard already handles strength; add a combined early-out if both are zero). Run full CTest.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Add neighbour-lean crosstalk to colour compression"
```

---

## Task 4: Wire Color Compression into the filmic_v3 render path

**Files:** `cpp_engine/src/session.cpp`, `tests/test_native_bridge.py`.

- [ ] **Step 1: Wire into preview orchestration.** In `session.cpp`, immediately after the density substage in the preview render path, add:

```cpp
            if (render_plan.stock_type != "monochrome" &&
                is_subtractive_effect_pipeline(request.effect_pipeline_version)) {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_compression");
                rendered = renderer.apply_color_compression(rendered, render_plan.film_response);
            }
```

- [ ] **Step 2: Wire into export orchestration.** Add the same gated substage right after the export density substage, with an export-appropriate timer label.

- [ ] **Step 3: Bridge test** in `tests/test_native_bridge.py` (RAW-guarded, mirroring `test_filmic_v3_subtractive_density_changes_render`): render `filmic_v3` with `film_color_compression=0` vs `100` vs `200`; assert `0` == baseline colour path (no compression) and `100`/`200` differ and differ from each other.

- [ ] **Step 4: Build both targets + run full CTest + bridge/server pytest** — Expected: PASS; `parity_v1`/`filmic_v2` unchanged.

- [ ] **Step 5: Commit**

```bash
git add cpp_engine/src/session.cpp tests/test_native_bridge.py
git commit -m "Apply Color Compression in filmic_v3 render path (preview + export)"
```

---

## Task 5: Retire the palette_range anchor pass

Remove the anchor sub-pass and its now-unused helpers/constants from the renderer; keep the `palette_range` request field as an accepted no-op (documented) until Slice 8.

**Files:** `cpp_engine/src/renderer.cpp`, `cpp_engine/tests/test_core.cpp`.

- [ ] **Step 1: Remove the palette sub-pass** in `apply_color_response_and_coupling_pipeline` (the `if (n_range != 0.0F) { ... }` block, currently ~lines 1028–1052) so the final hue used is `h_new` from the highlight-hue-convergence step only. Remove the pre-loop resolution of `n_range`, `palette_anchors`, and `palette_weights` if they become unused, and remove the now-unused palette constants (`kPaletteMergeHueGain`, `kPaletteSepHueGain`, `kPaletteMergeDesat`, `kPaletteSepChroma`, `kPaletteChromaLo`, `kPaletteChromaHi`) and the `nearest_anchor_delta` helper / `NearestAnchorResult` struct if no other caller remains (grep first).

- [ ] **Step 2: Delete or update the palette_range tests** in `test_core.cpp` (`test_palette_range_*`, `test_palette_anchor_weights_gate_hue_shift`): remove them (the behaviour is retired). Keep the all-neutral colour-pipeline determinism coverage.

- [ ] **Step 3: Build + full CTest** — Expected: PASS. Confirm the colour pipeline still renders (a `palette_range` value now has no effect).

- [ ] **Step 4: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Retire palette_range anchor pass (superseded by Color Compression)"
```

---

## Task 6: UI — Color Compression control; remove Palette Range slider

**Files:** `frontend/src/App.jsx`. Verify: `cd frontend; npm run build; npm run lint`.

- [ ] **Step 1: Add default + float parse.** Add `film_color_compression: 100` to `DEFAULT_PARAMS`; add `'film_color_compression'` to the `set()` `parseFloat` key list.

- [ ] **Step 2: Send on both routes.** Add `film_color_compression: String(params.film_color_compression)` to the preview query params and `film_color_compression: params.film_color_compression` to the export body, alongside `film_color_density`.

- [ ] **Step 3: Remove the Palette Range slider** from the `colorCharSliders` array (the `{ key: 'palette_range', ... }` entry).

- [ ] **Step 4: Add the Color Compression slider** next to Film Color Density (same 0–200 / default-100 / reset-to-100 markup used for `film_color_density`), `key: 'film_color_compression'`, label **"Color Compression"**, tooltip: `"How much the palette is compressed into cohesive, film-like colour — forward for a more harmonised, less digital look."` Disable on monochrome stocks.

- [ ] **Step 5: Build + lint** — Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add frontend/src/App.jsx
git commit -m "Add Color Compression control; remove Palette Range slider (UI)"
```

---

## Task 7: Register YAML, calibrate, route tests, docs, visual acceptance, APAM

**Files:** `cpp_engine/src/profile.cpp`, `profiles/stocks/*.yaml`, `tests/test_server_errors.py`, docs, APAM.

- [ ] **Step 1: Register YAML fields** in `profile.cpp` `kNumericFields`:

```cpp
        "compression.strength", "compression.threshold", "compression.crosstalk",
```

- [ ] **Step 2: Add `compression:` to representative stocks.** In `velvia_50.yaml` (reversal, strong: strength 0.75, threshold 0.42, crosstalk 0.40), `portra_400.yaml` (negative, moderate: 0.55/0.45/0.30), `colorplus_200.yaml` (consumer: 0.50/0.45/0.25); leave `tri_x_400.yaml` (monochrome default 0). Confirm all load via full CTest.

- [ ] **Step 3: Route test** in `tests/test_server_errors.py`: preview accepts `film_color_compression` and clamps out-of-range (e.g. 500 → forwarded 200.0), mirroring the density clamp test.

- [ ] **Step 4: Build both targets + run all tests** — native CTest + `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`. Expected: PASS.

- [ ] **Step 5: Docs.** Update `STOCK_PROFILE_CONTRACT.md` (new `compression.*` group), `documentation/planning/film-lab-v1-project-plan.md` (mark Slice 3 done), `documentation/architecture/film-lab-framework-architecture.md` if a convention was refined, `README.md` + `docs/technical_architecture.md` (Color Compression; palette_range retired). Note `palette_range` is a deprecated no-op pending Slice 8 removal. Commit.

- [ ] **Step 6: Visual acceptance (human/hardware).** Render representative RAWs under `filmic_v3` at `film_color_compression` 0 / 100 / 200 on velvia_50 and portra_400 (and a monochrome no-op): confirm the palette becomes more cohesive/harmonised and high saturation rolls down, reds lean gently to orange / blues to cyan (no magenta), neutrals/skin stable, preview matches export. Render before/after comparison strips (as done for density) for review. Record decision in `cpp_engine/migration_docs/BUG_TRACKER.md`. Flag for the human operator.

- [ ] **Step 7: Timing probe (human/hardware).** Preview/export timing vs baseline with compression active.

- [ ] **Step 8: APAM.** L3 record + episode for Color Compression + palette_range retirement.

---

## Self-Review Notes

- **Spec coverage:** saturation compression (Task 2), neighbour-lean crosstalk hue-honest + neutral-protected (Task 3), Color Compression control with stock-defaulted forward=more-filmic model (Tasks 1, 6), filmic_v3-only + wiring + parity untouched (Tasks 1, 4), retire palette_range (Task 5), stock YAML + defaults + loader (Tasks 1, 7), report fields (Task 1), route/bridge tests (Tasks 1, 4, 7), docs + visual acceptance + APAM (Task 7). Parameter-model reused from Slice 1.
- **Placeholder scan:** none — algorithm and plumbing are concrete. Task 3's lean SIGN is resolved empirically in-task (documented method), not left as a placeholder. Task 7 Steps 6-7 are explicit human/hardware steps.
- **Type consistency:** `film_color_compression` (float, default 100.0) + `compression_strength`/`compression_threshold`/`compression_crosstalk` used identically across `SolverControls`, `FilmResponsePlan`, request DTO, pybind, bridge, server, report, React, YAML. `apply_color_compression(const Image&, const FilmResponsePlan&)` consistent; constants (`kCompressK`, `kCompressCrossLo/Hi`, `kCompressLeanGain`, `kLeanRedSign`, `kLeanBlueSign`) defined once in Tasks 2–3.
