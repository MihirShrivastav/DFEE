# Subtractive Density Core + filmic_v3 (Film Lab v1 — Slice 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Introduce the `filmic_v3` pipeline and the subtractive **Film Color Density** core — saturated colours gain density and get darker (matte, weighty), hue preserved, with a low-luminance limiter — established as the foundation and parameter-model convention for the whole v1 framework.

**Architecture:** Add `filmic_v3` as a new pipeline version that inherits `filmic_v2`'s bloom/halation/grain and adds a new per-pixel **subtractive density** renderer stage (OKLab `L` reduced ∝ chroma `C`, hue untouched, shadow-protecting limiter). The stage is applied only under `filmic_v3`, gated in the `session.cpp` render orchestration after the colour-response stage. `film_color_density` is a stock-defaulted control ("forward = more filmic"). `parity_v1` and `filmic_v2` remain byte-identical.

**Tech Stack:** C++20 (dfee_core, pybind), Python (FastAPI `server.py`, `dfee_native_bridge.py`, pytest), React (`frontend/src/App.jsx`), yaml-cpp, OpenCV, CTest.

## Global Constraints

- New behaviour lives under `effect_pipeline_version = filmic_v3`. `parity_v1` and `filmic_v2` renders are byte-identical to before (primary regression guard). `filmic_v3` inherits `filmic_v2`'s halation/bloom/grain.
- **Parameter-model convention (established here, reused by all later slices):** each film control is a float defaulting to `100.0`, range `[0, 200]`. `100` = apply the stock's authored amount fully (the calibrated default effect — "film on by default"); `< 100` relaxes toward neutral; `> 100` intensifies ("forward = more filmic"); reset returns to `100`. Omitted request field → `100`. The renderer applies `effective = stock_param * (control / 100)`, bounded plausibly-real.
- Subtractive density: reduce OKLab `L` proportional to normalized chroma `C`, hue `h` never changed, `C` preserved; a low-luminance limiter protects deep shadows; neutrals (C≈0) unchanged; monochrome stocks are a no-op.
- Applied only in the live `session.cpp` render orchestration (preview + export) gated by `filmic_v3`; deterministic.
- Every new parameter carries: native request field, FilmResponsePlan field, solver default (with stock-family fallback), report field, loader contract (new YAML fields registered), and tests — in this slice.
- Native C++ is the render path; preview and export carry identical values; the profile loader rejects unconsumed YAML.

---

## File Structure

- `cpp_engine/src/session.cpp` — `filmic_v3` constant + `is_subtractive_effect_pipeline` + widen `is_filmic_effect_pipeline`/`validate`; wire the density stage into preview + export orchestration.
- `cpp_engine/include/dfee/solver.hpp` — `SolverControls.film_color_density`; `FilmResponsePlan` density fields.
- `cpp_engine/include/dfee/bridge_types.hpp` — `NativePreviewRenderRequest.film_color_density`.
- `cpp_engine/include/dfee/renderer.hpp` + `cpp_engine/src/renderer.cpp` — `apply_subtractive_density`.
- `cpp_engine/src/solver.cpp` — read `density.*` YAML + family defaults; populate FilmResponsePlan; copy control.
- `cpp_engine/src/profile.cpp` — register `density.strength`, `density.low_luma_limit`.
- `cpp_engine/bindings/python/dfee_native_module.cpp` — parse `film_color_density`.
- `dfee_native_bridge.py` — dataclass field.
- `server.py` — allowlist `filmic_v3`; preview param + `ExportRequest` field + native dict.
- `frontend/src/App.jsx` — set pipeline to `filmic_v3`; add Film Color Density control.
- `profiles/stocks/*.yaml` — `density:` group on representative stocks.
- `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`, `tests/test_server_errors.py`.
- Docs: `documentation/`, `cpp_engine/migration_docs/*`, `README.md`, `docs/technical_architecture.md`.

**Canonical names:** request/field `film_color_density` (float, default `100.0`); `FilmResponsePlan.film_color_density`, `.density_strength`, `.density_low_luma_limit`; YAML `density.strength`, `density.low_luma_limit`; renderer `FilmRenderer::apply_subtractive_density(const Image&, const FilmResponsePlan&)`; version `filmic_v3`; helper `is_subtractive_effect_pipeline`.

**Build/test (Windows, repo root d:/Codebases/DFEE):**
- Native build: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_native dfee_tests`
- Native tests: `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure`
- Python: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`
- Frontend: `cd frontend; npm run build; npm run lint`

---

## Task 1: `filmic_v3` version plumbing

**Files:** `cpp_engine/src/session.cpp`, `server.py`, `cpp_engine/tests/test_core.cpp`, `tests/test_server_errors.py`

**Interfaces:**
- Produces: `filmic_v3` accepted end to end; `is_subtractive_effect_pipeline(version)` (true only for `filmic_v3`); `is_filmic_effect_pipeline` true for `filmic_v2` and `filmic_v3`.

- [ ] **Step 1: Write the failing native test** in `test_core.cpp`:

```cpp
void test_filmic_v3_version_is_supported_and_subtractive() {
    if (dfee::EngineSession::is_effect_pipeline_supported("filmic_v3") != true) {
        throw std::runtime_error("filmic_v3 must be a supported effect_pipeline_version");
    }
    if (dfee::EngineSession::is_effect_pipeline_supported("bogus_v9") != false) {
        throw std::runtime_error("unknown versions must be rejected");
    }
}
```

If the session does not already expose a static `is_effect_pipeline_supported`, expose a thin one that wraps the existing `validate_effect_pipeline_version` (returns `true` when it yields `std::nullopt`). Register the test in `main()`.

- [ ] **Step 2: Run to verify it fails**

Run the native build + `ctest ... -R filmic_v3`. Expected: FAIL (filmic_v3 not yet accepted / helper missing).

- [ ] **Step 3: Add the version constants + helpers** in `session.cpp` anonymous namespace (after line 45):

```cpp
constexpr const char* kSubtractiveEffectPipelineVersion = "filmic_v3";
```

Update `is_filmic_effect_pipeline` to include v3, and add the subtractive helper:

```cpp
[[nodiscard]] bool is_filmic_effect_pipeline(const std::string& value) {
    const std::string v = normalized_effect_pipeline_version(value);
    return v == kFilmicEffectPipelineVersion || v == kSubtractiveEffectPipelineVersion;
}

[[nodiscard]] bool is_subtractive_effect_pipeline(const std::string& value) {
    return normalized_effect_pipeline_version(value) == kSubtractiveEffectPipelineVersion;
}
```

Update `validate_effect_pipeline_version` to accept the third value and fix the detail string:

```cpp
    if (normalized == kDefaultEffectPipelineVersion || normalized == kFilmicEffectPipelineVersion ||
        normalized == kSubtractiveEffectPipelineVersion) {
        return std::nullopt;
    }
    ...
        .detail = "Supported effect_pipeline_version values: parity_v1, filmic_v2, filmic_v3. Requested: " + normalized,
```

Expose the static wrapper used by the test (e.g. in the `EngineSession` public surface) returning `!validate_effect_pipeline_version(value).has_value()`.

- [ ] **Step 4: Widen the `server.py` allowlists.** Replace both `{"parity_v1", "filmic_v2"}` sets (the export validator near line 552 and the preview handler near line 1180) with `{"parity_v1", "filmic_v2", "filmic_v3"}`, and update the preview error path text if it enumerates versions.

- [ ] **Step 5: Write the failing route test** in `tests/test_server_errors.py`: a preview request with `effect_pipeline_version=filmic_v3` is NOT rejected for the version reason (it may fail later for lack of a real RAW, but must not return the "Unsupported effect_pipeline_version" 400). Assert the 400-unsupported-version path is not taken for `filmic_v3` while `bogus_v9` still is.

- [ ] **Step 6: Run native + python version tests** — Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add cpp_engine/src/session.cpp server.py cpp_engine/tests/test_core.cpp tests/test_server_errors.py
git commit -m "Add filmic_v3 pipeline version (subtractive) plumbing"
```

---

## Task 2: Density schema + contract (no behaviour yet)

Add `film_color_density` and the density plan fields across the stack; solver resolves them; nothing renders differently yet.

**Files:** `solver.hpp`, `bridge_types.hpp`, `session.cpp` (controls map + report), `solver.cpp`, `dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py`, `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`

**Interfaces:**
- Produces: `film_color_density` (float, default 100.0) on every layer; `FilmResponsePlan.film_color_density`, `.density_strength`, `.density_low_luma_limit`; report emits them.

- [ ] **Step 1: Write the failing solver test** in `test_core.cpp`: load `portra_400.yaml`, solve with default controls, assert `plan.film_response.film_color_density == 100.0F` and `plan.film_response.density_strength > 0.0F` (family default when YAML absent); load `tri_x_400.yaml` (monochrome) and assert `density_strength == 0.0F`.

- [ ] **Step 2: Run to verify it fails** (fields don't exist).

- [ ] **Step 3: Add `SolverControls.film_color_density`** in `solver.hpp` after `film_color`:

```cpp
    float film_color_density = 100.0F;
```

- [ ] **Step 4: Add `FilmResponsePlan` fields** in `solver.hpp` (near the other resolved colour fields):

```cpp
    float film_color_density = 100.0F;   // control value (0..200), 100 = stock default
    float density_strength = 0.0F;       // stock authored subtractive-density strength
    float density_low_luma_limit = 0.10F;// OKLab L below which density is suppressed
```

- [ ] **Step 5: Add request field** in `bridge_types.hpp` `NativePreviewRenderRequest` after `film_color`:

```cpp
    float film_color_density = 100.0F;
```

- [ ] **Step 6: Map control → plan** in `session.cpp` at both `SolverControls` build sites (after `controls.film_color = request.film_color;`):

```cpp
    controls.film_color_density = request.film_color_density;
```

- [ ] **Step 7: Add the solver family-defaults helper + population** in `solver.cpp`. Add a helper mirroring `color_character_defaults`:

```cpp
struct DensityDefaults { float strength; float low_luma_limit; };
[[nodiscard]] DensityDefaults density_defaults(StockType type) {
    switch (type) {
        case StockType::ColorReversal: return {0.85F, 0.12F};
        case StockType::ColorNegative: return {0.60F, 0.10F};
        case StockType::Monochrome:    return {0.0F, 0.10F};
    }
    return {0.55F, 0.10F};
}
```

After the `plan.film_response = {...}` initializer, populate:

```cpp
    const DensityDefaults dens = density_defaults(stock_profile.stock_type);
    plan.film_response.film_color_density = controls.film_color_density;
    plan.film_response.density_strength = get_numeric(
        stock_profile.numeric_values, "density.strength", dens.strength);
    plan.film_response.density_low_luma_limit = get_numeric(
        stock_profile.numeric_values, "density.low_luma_limit", dens.low_luma_limit);
```

- [ ] **Step 8: Serialize into report JSON** in `session.cpp` near the `film_color` line:

```cpp
        << "\"film_color_density\": " << json_number(render_plan.film_response.film_color_density) << ",\n"
        << "\"density_strength\": " << json_number(render_plan.film_response.density_strength) << ",\n"
        << "\"density_low_luma_limit\": " << json_number(render_plan.film_response.density_low_luma_limit) << ",\n"
```

Match the surrounding comma/newline convention.

- [ ] **Step 9: Parse in pybind** in `dfee_native_module.cpp` `preview_request_from_dict` after `film_color`:

```cpp
    request.film_color_density = dict_float(dict, "film_color_density", 100.0F);
```

- [ ] **Step 10: Bridge dataclass** in `dfee_native_bridge.py` after `film_color`:

```python
    film_color_density: float = 100.0
```

- [ ] **Step 11: server.py** — add `film_color_density: float = 100.0` as a preview query param and an `ExportRequest`/`PreviewRequest` field, and add `"film_color_density": film_color_density` (preview) / `"film_color_density": req.film_color_density` (export) to the native request dicts, mirroring how `film_color` is threaded.

- [ ] **Step 12: Write the bridge round-trip test** in `tests/test_native_bridge.py`: `NativePreviewRenderRequest(...).film_color_density == 100.0` default; `NativeExportRequest(..., film_color_density=140.0).film_color_density == 140.0`.

- [ ] **Step 13: Build + run solver + bridge tests** — Expected: PASS.

- [ ] **Step 14: Confirm parity/filmic_v2 unchanged.** Run full `ctest` — all existing tests green (no render path touched yet).

- [ ] **Step 15: Commit**

```bash
git add cpp_engine/include/dfee/solver.hpp cpp_engine/include/dfee/bridge_types.hpp cpp_engine/src/session.cpp cpp_engine/src/solver.cpp cpp_engine/bindings/python/dfee_native_module.cpp dfee_native_bridge.py server.py cpp_engine/tests/test_core.cpp tests/test_native_bridge.py
git commit -m "Add Film Color Density schema + solver defaults (density core)"
```

---

## Task 3: Subtractive density renderer stage

**Files:** `cpp_engine/include/dfee/renderer.hpp`, `cpp_engine/src/renderer.cpp`, `cpp_engine/tests/test_core.cpp`

**Interfaces:**
- Consumes: `FilmResponsePlan.film_color_density`, `.density_strength`, `.density_low_luma_limit`.
- Produces: `Image FilmRenderer::apply_subtractive_density(const Image& rgb_linear, const FilmResponsePlan& response) const;` — per-pixel OKLab `L` reduced ∝ normalized chroma, hue/chroma preserved, shadow-limited.

- [ ] **Step 1: Write the failing native test** in `test_core.cpp` — build a 1-row image with a saturated red, a saturated-but-darker red (shadow), and a neutral gray. With `density_strength=0.6`, `density_low_luma_limit=0.10`, `film_color_density=100`, assert after `apply_subtractive_density`:
  - saturated red's OKLab `L` **decreases** (darker); its hue `h` and chroma `C` are unchanged (within 1e-4);
  - the neutral gray is unchanged (all channels within 1e-5);
  - a deep-shadow pixel below the limiter is (near) unchanged in `L` (limiter protects it);
  - `film_color_density=0` is a byte-identical no-op; `film_color_density=200` darkens the red more than `100`.
Use build-agnostic `throw`. Invoke via the public method on a `FilmRenderer`.

- [ ] **Step 2: Run to verify it fails** (method missing).

- [ ] **Step 3: Declare** in `renderer.hpp` (near `apply_color_response_and_coupling`):

```cpp
    [[nodiscard]] Image apply_subtractive_density(const Image& rgb_linear, const FilmResponsePlan& response) const;
```

- [ ] **Step 4: Implement** in `renderer.cpp`. Add constants in the anonymous namespace:

```cpp
constexpr float kOklabChromaRef = 0.35F;    // chroma normalization reference (OKLCh C)
constexpr float kDensityLumaMax = 0.55F;    // max fractional L reduction at full density
constexpr float kDensityLimitSoft = 0.06F;  // soft width of the low-luma limiter
```

Add the method (free-function-in-namespace + thin public wrapper, matching the file's pattern):

```cpp
Image FilmRenderer::apply_subtractive_density(const Image& rgb_linear, const FilmResponsePlan& response) const {
    if (rgb_linear.channels != 3) {
        throw std::invalid_argument("apply_subtractive_density expects a 3-channel RGB image");
    }
    const float amt = std::max(response.density_strength, 0.0F)
        * std::clamp(response.film_color_density / 100.0F, 0.0F, 2.0F);
    Image out(rgb_linear.width, rgb_linear.height, 3);
    if (amt <= 0.0F) { out.pixels = rgb_linear.pixels; return out; }
    const float lo = response.density_low_luma_limit;
    for (std::size_t i = 0; i < rgb_linear.pixel_count(); ++i) {
        const OklabPixel lab = rgb_to_oklab_pixel(
            rgb_linear.pixels[i * 3 + 0], rgb_linear.pixels[i * 3 + 1], rgb_linear.pixels[i * 3 + 2]);
        const OklchPixel lch = oklab_to_oklch_pixel(lab);
        const float c_norm = std::clamp(lch.c / kOklabChromaRef, 0.0F, 1.0F);
        const float limiter = smoothstep01(lo, lo + kDensityLimitSoft, lch.l); // 0 in deep shadow -> 1 above
        const float reduce = kDensityLumaMax * amt * c_norm * limiter;         // fractional L reduction
        const float l_new = lch.l * (1.0F - reduce);
        const OklabPixel adjusted = oklch_to_oklab_pixel({std::max(l_new, 0.0F), lch.c, lch.h});
        const auto rgb = oklab_to_rgb_pixel(adjusted);
        out.pixels[i * 3 + 0] = rgb[0];
        out.pixels[i * 3 + 1] = rgb[1];
        out.pixels[i * 3 + 2] = rgb[2];
    }
    return out;
}
```

Confirm helper names (`rgb_to_oklab_pixel`, `oklab_to_oklch_pixel`, `oklch_to_oklab_pixel`, `oklab_to_rgb_pixel`, `smoothstep01(lo,hi,x)`) match the file; reuse, don't redefine.

- [ ] **Step 5: Run the density test** — Expected: PASS (darkens saturated, protects neutral + shadow, 0 = no-op, 200 > 100).

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/include/dfee/renderer.hpp cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement subtractive density renderer stage"
```

---

## Task 4: Wire the density stage into the render path (filmic_v3 only)

**Files:** `cpp_engine/src/session.cpp`, `tests/test_native_bridge.py`

**Interfaces:**
- Consumes: `apply_subtractive_density`, `is_subtractive_effect_pipeline`.

- [ ] **Step 1: Wire into preview orchestration.** In `session.cpp`, immediately after the colour-response substage (the `apply_color_response_and_coupling` block that ends around line 2173), add a version-gated density substage:

```cpp
            if (render_plan.stock_type != "monochrome" &&
                is_subtractive_effect_pipeline(request.effect_pipeline_version)) {
                ScopedStageTimer substage(response.engine, "render_preview_film_stage_density");
                rendered = renderer.apply_subtractive_density(rendered, render_plan.film_response);
            }
```

- [ ] **Step 2: Wire into export orchestration.** Find the matching colour-response block in the export render path (the second `apply_color_response_and_coupling` site, ~line 2552) and add the same gated density substage with an export-appropriate timer label.

- [ ] **Step 3: Write the failing bridge test** in `tests/test_native_bridge.py` (only if a test RAW is available in the repo; otherwise assert at the report level): render the same file+stock with `effect_pipeline_version=filmic_v2` vs `filmic_v3` (density 100) and assert the outputs differ; render `filmic_v3` with `film_color_density=0` and assert it equals the `filmic_v2` output for the colour path (density no-op). If no RAW fixture exists, instead assert the report JSON for a `filmic_v3` request carries `density_strength>0` and `film_color_density`, and that a `filmic_v2` request path does not invoke density (document this limitation in the report).

- [ ] **Step 4: Run full ctest + bridge tests** — Expected: PASS; `parity_v1`/`filmic_v2` unchanged; `filmic_v3` applies density.

- [ ] **Step 5: Commit**

```bash
git add cpp_engine/src/session.cpp tests/test_native_bridge.py
git commit -m "Apply subtractive density in filmic_v3 render path (preview + export)"
```

---

## Task 5: Register density YAML + calibrate representative stocks

**Files:** `cpp_engine/src/profile.cpp`, `profiles/stocks/*.yaml`, `cpp_engine/tests/test_core.cpp`

- [ ] **Step 1: Register the fields** in `profile.cpp` `validate_native_film_stock_contract` `kNumericFields`:

```cpp
        "density.strength", "density.low_luma_limit",
```

- [ ] **Step 2: Write the failing loader test** in `test_core.cpp`: load a stock with a `density:` block (write a temp YAML based on an existing stock, add `density: { strength: 0.7, low_luma_limit: 0.12 }`) and assert it loads and `profile.numeric_values` contains `density.strength`. (Mirror the existing temp-fixture pattern.)

- [ ] **Step 3: Run to verify it fails** (unsupported field) then passes after Step 1.

- [ ] **Step 4: Add `density:` to representative stocks.** In `velvia_50.yaml` (reversal, strong), `portra_400.yaml` (negative, moderate), `colorplus_200.yaml` (consumer), add a `density:` block with tuned `strength` + `low_luma_limit`; leave `tri_x_400.yaml` without one (monochrome family default is 0). Confirm all load via full CTest.

- [ ] **Step 5: Commit**

```bash
git add cpp_engine/src/profile.cpp profiles/stocks cpp_engine/tests/test_core.cpp
git commit -m "Register density YAML fields + calibrate representative stocks"
```

---

## Task 6: UI — filmic_v3 + Film Color Density control

**Files:** `frontend/src/App.jsx`. Verify: `cd frontend; npm run build; npm run lint`.

- [ ] **Step 1: Switch the pipeline version.** Set `const EFFECT_PIPELINE_VERSION = 'filmic_v3';` (was `filmic_v2`).

- [ ] **Step 2: Add the default + float parsing.** Add `film_color_density: 100` to `DEFAULT_PARAMS`; add `'film_color_density'` to the `set()` `parseFloat` key list.

- [ ] **Step 3: Send on both routes.** Add `film_color_density: String(params.film_color_density)` to the preview query params and `film_color_density: params.film_color_density` to the export body, alongside `film_color`.

- [ ] **Step 4: Add the slider.** In the Colour panel add a **Film Color Density** slider, `min={0} max={200} step={1}`, default 100, reset-to-100 button (shown when value !== 100), dirty-state class, tooltip: `"How dense and matte the film's colours are — forward for richer, deeper, more film-like colour."` Follow the existing slider markup pattern; disable on monochrome stocks (reuse the existing `isMonochrome` logic).

- [ ] **Step 5: Build + lint** — Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add frontend/src/App.jsx
git commit -m "Add Film Color Density control; default pipeline to filmic_v3 (UI)"
```

---

## Task 7: Route/bridge tests, docs, visual acceptance, APAM

**Files:** `tests/test_server_errors.py`, `tests/test_native_bridge.py`, docs, APAM.

- [ ] **Step 1: Route tests.** In `tests/test_server_errors.py` assert preview + export accept `film_color_density` and that an out-of-range value (e.g. 500) is handled per the server's validation posture (add a range clamp/validation for `[0,200]` in `server.py` if not present, and test it returns 400 or clamps consistently — pick clamp-to-range and test it). Run: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`. Expected: PASS.

- [ ] **Step 2: Docs.** Update `documentation/planning/film-lab-v1-project-plan.md` (mark Slice 1 in-progress→done on merge), `documentation/architecture/film-lab-framework-architecture.md` if any convention was refined, `cpp_engine/migration_docs/STOCK_PROFILE_CONTRACT.md` (new `density.*` fields), `cpp_engine/migration_docs/FILM_LAB_WORKFLOW.md`/`FILM_LAB_IMPLEMENTATION_PLAN.md`, `README.md`, `docs/technical_architecture.md` (filmic_v3 + Film Color Density). Commit.

- [ ] **Step 3: Visual acceptance (human/hardware).** Render representative RAWs under `filmic_v3` at `film_color_density` 0 / 100 / 200 on velvia_50, portra_400, and a monochrome stock (must be a no-op), preview + export. Confirm: saturated colours get denser/darker/matte as density rises; neutrals and deep shadows protected; hue unchanged (no colour drift); preview matches export; `parity_v1`/`filmic_v2` visually unchanged. Record images, stock, version, decision in `cpp_engine/migration_docs/BUG_TRACKER.md`. Flag for the human operator; do not fabricate.

- [ ] **Step 4: Timing probe (human/hardware).** Preview/export timing vs the named baseline with `filmic_v3` + density active; confirm the extra per-pixel pass does not materially regress. Record hardware, version, dimensions.

- [ ] **Step 5: APAM.** Add/refresh an L3 record for the subtractive density core + `filmic_v3` and the parameter-model convention; write an L2 episode.

---

## Self-Review Notes

- **Spec/project-plan coverage:** subtractive density core (Tasks 3-4), `filmic_v3` version (Task 1), stock density params + family defaults + loader (Tasks 2, 5), Film Color Density control with stock-defaulted "forward = more filmic" model (Tasks 2, 6), no-op on monochrome + neutrals + shadow limiter + hue-preservation (Task 3 tests), `parity_v1`/`filmic_v2` byte-identical (Tasks 2, 4), full contract stack (Tasks 1, 2, 6), report fields (Task 2), tests + docs + visual acceptance + APAM (Tasks 5, 7). Parameter-model convention documented in Global Constraints for reuse by later slices.
- **Placeholder scan:** none — density math and plumbing are concrete; Task 7 Steps 3-4 are explicit human/hardware steps.
- **Type consistency:** `film_color_density` (float, default 100.0) identical across `SolverControls`, `FilmResponsePlan`, `NativePreviewRenderRequest`, pybind key, bridge dataclass, server params, report JSON, React, YAML-adjacent (`density.strength`/`density.low_luma_limit`). `apply_subtractive_density(const Image&, const FilmResponsePlan&)` and `is_subtractive_effect_pipeline` used consistently. Constants (`kOklabChromaRef`, `kDensityLumaMax`, `kDensityLimitSoft`) defined once in Task 3.
