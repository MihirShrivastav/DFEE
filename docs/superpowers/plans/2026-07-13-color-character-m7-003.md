# Color Character (M7-003) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add four bounded, stock-relative Color Character controls (Highlight Color Hold, Shadow Color Retention, Palette Separation, Emulsion Color Density) that decompose the stock's colour personality currently collapsed behind the single `film_color` multiplier.

**Architecture:** The controls modulate the stock's *already-resolved* colour-response coefficients (highlight/shadow chroma rolloff, highlight desaturation, chroma boost) before the per-pixel loop, plus one new hue-anchor-attraction sub-pass for Palette Separation, all inside the existing native colour pipeline in `renderer.cpp`. New optional per-stock `color_character:` YAML calibrates sensitivities and palette anchors. The full contract stack — C++ structs → pybind → Python bridge → FastAPI preview/export → report JSON → React — carries the four values in one slice.

**Tech Stack:** C++20 (dfee_core, pybind CPython module), Python (FastAPI `server.py`, `dfee_native_bridge.py`, pytest), React (Vite, `frontend/src/App.jsx`), yaml-cpp, OpenCV, CTest.

## Global Constraints

- New behaviour lands in `filmic_v2` only; `parity_v1` stays bit-for-bit reproducible.
- Each control is bipolar `−100..+100`, neutral `0` = stock default (no-op). Omitted request field → `0`.
- At all-neutral (all four = 0), rendered output is bit-for-bit identical to the current build (primary regression guard).
- `parity_v1` + any non-zero Color Character value → explicit rejection (HTTP 400 / native error), never a silent render.
- All four are chroma-domain only; they never modify lightness (no black lift) and are no-ops on monochrome stocks.
- The native profile loader rejects unknown/unconsumed YAML leaf fields — every new YAML field must be registered in the loader contract in this slice.
- A new profile field requires loader, solver, renderer, report, fixture, and test support in the same slice.
- Preview and export use the same parameter model and stage ordering.
- Primary control labels are scene-agnostic; tooltips may describe likely effects but must not assume a subject.
- Controls retain a one-click neutral reset in the UI.
- No success claims until native Release build + CTest + targeted bridge/server pytest run green.

---

## File Structure

- `cpp_engine/include/dfee/solver.hpp` — add 4 request fields to `SolverControls`; add resolved fields to `FilmResponsePlan`.
- `cpp_engine/include/dfee/bridge_types.hpp` — add 4 request fields to `NativePreviewRenderRequest`.
- `cpp_engine/src/session.cpp` — map request → `SolverControls`; serialize inputs into report JSON; parity-version guard.
- `cpp_engine/src/solver.cpp` — read `color_character:` YAML, infer family defaults, populate `FilmResponsePlan`.
- `cpp_engine/src/profile.cpp` — register `color_character:` leaf fields; add variable-length whitelisted arrays.
- `cpp_engine/src/renderer.cpp` — apply modulation + palette sub-pass in both colour-response paths.
- `cpp_engine/bindings/python/dfee_native_module.cpp` — parse 4 request fields.
- `dfee_native_bridge.py` — add 4 dataclass fields.
- `server.py` — preview query params + export Pydantic model, range + parity validation, forward to native dict.
- `frontend/src/App.jsx` — Color Character panel: 4 bipolar sliders; move legacy `film_color` to Advanced.
- `profiles/stocks/*.yaml` — optional `color_character:` group (calibration).
- `cpp_engine/tests/test_core.cpp` — native unit tests.
- `tests/test_native_bridge.py`, `tests/test_server_errors.py` — bridge/route tests.
- Docs: `cpp_engine/migration_docs/FILM_LAB_WORKFLOW.md`, `FILM_LAB_IMPLEMENTATION_PLAN.md`, `STOCK_PROFILE_CONTRACT.md`, `README.md`, `docs/technical_architecture.md`.

**Canonical names (used across all tasks):**
- Request/DTO/JSON fields: `highlight_color_hold`, `shadow_color_retention`, `palette_separation`, `emulsion_color_density` (float, default `0.0`).
- `FilmResponsePlan` resolved fields: `highlight_color_hold`, `shadow_color_retention`, `palette_separation`, `emulsion_color_density` (copied inputs); `highlight_hold_sensitivity`, `shadow_retention_sensitivity`, `emulsion_density_sensitivity`, `palette_separation_sensitivity` (float); `palette_anchors`, `palette_anchor_weights` (`std::vector<float>`).
- Renderer gain constants (calibrated in Task 8): `kHoldGainHi`, `kHoldGainDesat`, `kShadowRetentionGain`, `kEmulsionDensityGain`, `kPaletteSepGain`.

---

## Task 1: Schema + all-neutral no-op (M7-003A)

Add the four fields end to end with zero behavioural change, proving the neutral no-op first.

**Files:**
- Modify: `cpp_engine/include/dfee/solver.hpp` (`SolverControls`, `FilmResponsePlan`)
- Modify: `cpp_engine/include/dfee/bridge_types.hpp` (`NativePreviewRenderRequest`)
- Modify: `cpp_engine/src/session.cpp` (`build_solver_controls`, report JSON, parity guard)
- Modify: `cpp_engine/src/solver.cpp` (copy inputs into `FilmResponsePlan`)
- Modify: `cpp_engine/bindings/python/dfee_native_module.cpp` (`preview_request_from_dict`)
- Modify: `dfee_native_bridge.py` (`NativePreviewRenderRequest`)
- Modify: `server.py` (preview params + export model, validation, native dict)
- Test: `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`

**Interfaces:**
- Produces: 4 request fields default `0.0` on every layer; `FilmResponsePlan` carries the 4 inputs; report JSON emits them.
- Consumes: existing `film_color` plumbing as the structural template.

- [ ] **Step 1: Write the failing native test** in `cpp_engine/tests/test_core.cpp`

```cpp
TEST_CASE("color character request fields default to neutral zero") {
    dfee::NativePreviewRenderRequest request;
    CHECK(request.highlight_color_hold == doctest::Approx(0.0F));
    CHECK(request.shadow_color_retention == doctest::Approx(0.0F));
    CHECK(request.palette_separation == doctest::Approx(0.0F));
    CHECK(request.emulsion_color_density == doctest::Approx(0.0F));

    dfee::SolverControls controls;
    controls.highlight_color_hold = request.highlight_color_hold;
    controls.shadow_color_retention = request.shadow_color_retention;
    controls.palette_separation = request.palette_separation;
    controls.emulsion_color_density = request.emulsion_color_density;
    CHECK(controls.highlight_color_hold == doctest::Approx(0.0F));
}
```

Match the existing assertion framework/style already used in `test_core.cpp`; if it uses a different macro set, mirror that.

- [ ] **Step 2: Run to verify it fails**

Run: build the native tests (`cmake --build` the configured preset, then the test target). Expected: compile error — members do not exist.

- [ ] **Step 3: Add fields to `SolverControls`** in `solver.hpp` after `film_color`:

```cpp
    float film_color = 100.0F;
    float highlight_color_hold = 0.0F;
    float shadow_color_retention = 0.0F;
    float palette_separation = 0.0F;
    float emulsion_color_density = 0.0F;
```

- [ ] **Step 4: Add fields to `FilmResponsePlan`** in `solver.hpp` after `film_color`:

```cpp
    float film_color = 100.0F;
    float highlight_color_hold = 0.0F;
    float shadow_color_retention = 0.0F;
    float palette_separation = 0.0F;
    float emulsion_color_density = 0.0F;
    float highlight_hold_sensitivity = 0.0F;
    float shadow_retention_sensitivity = 0.0F;
    float emulsion_density_sensitivity = 0.0F;
    float palette_separation_sensitivity = 0.0F;
    std::vector<float> palette_anchors;
    std::vector<float> palette_anchor_weights;
```

Ensure `#include <vector>` is present in `solver.hpp`.

- [ ] **Step 5: Add fields to `NativePreviewRenderRequest`** in `bridge_types.hpp` after `film_color`:

```cpp
    float film_color = 100.0F;
    float highlight_color_hold = 0.0F;
    float shadow_color_retention = 0.0F;
    float palette_separation = 0.0F;
    float emulsion_color_density = 0.0F;
```

- [ ] **Step 6: Map request → controls** in `session.cpp` `build_solver_controls`, after `controls.film_color = request.film_color;`:

```cpp
    controls.film_color = request.film_color;
    controls.highlight_color_hold = request.highlight_color_hold;
    controls.shadow_color_retention = request.shadow_color_retention;
    controls.palette_separation = request.palette_separation;
    controls.emulsion_color_density = request.emulsion_color_density;
```

Apply the same four lines at the second controls-build site in `session.cpp` (the export path near `controls.film_color = request.film_color;`).

- [ ] **Step 7: Copy inputs into `FilmResponsePlan`** in `solver.cpp` within the `plan.film_response = { ... }` initializer, after `.film_color = controls.film_color,`:

```cpp
        .film_color = controls.film_color,
        .highlight_color_hold = controls.highlight_color_hold,
        .shadow_color_retention = controls.shadow_color_retention,
        .palette_separation = controls.palette_separation,
        .emulsion_color_density = controls.emulsion_color_density,
```

(Sensitivity/anchor fields are populated in Task 6; they default to neutral until then.)

- [ ] **Step 8: Serialize inputs into report JSON** in `session.cpp` near the `"film_color"` serialization line:

```cpp
        << "\"film_color\": " << json_number(render_plan.film_response.film_color) << ",\n"
        << "\"highlight_color_hold\": " << json_number(render_plan.film_response.highlight_color_hold) << ",\n"
        << "\"shadow_color_retention\": " << json_number(render_plan.film_response.shadow_color_retention) << ",\n"
        << "\"palette_separation\": " << json_number(render_plan.film_response.palette_separation) << ",\n"
        << "\"emulsion_color_density\": " << json_number(render_plan.film_response.emulsion_color_density) << ",\n"
```

Match the exact comma/newline convention of surrounding lines in that report writer.

- [ ] **Step 9: Add parity-version guard** in `session.cpp`. Locate where `effect_pipeline_version` is validated against the allowlist. Immediately after that validation, add:

```cpp
    const bool has_color_character =
        request.highlight_color_hold != 0.0F ||
        request.shadow_color_retention != 0.0F ||
        request.palette_separation != 0.0F ||
        request.emulsion_color_density != 0.0F;
    if (has_color_character && request.effect_pipeline_version == "parity_v1") {
        throw std::invalid_argument(
            "Color Character controls require effect_pipeline_version=filmic_v2");
    }
```

Apply in both the preview and export request entry points (mirror wherever the version allowlist is checked).

- [ ] **Step 10: Parse fields in pybind** in `dfee_native_module.cpp` `preview_request_from_dict`, after `request.film_color = dict_float(dict, "film_color", 100.0F);`:

```cpp
    request.highlight_color_hold = dict_float(dict, "highlight_color_hold");
    request.shadow_color_retention = dict_float(dict, "shadow_color_retention");
    request.palette_separation = dict_float(dict, "palette_separation");
    request.emulsion_color_density = dict_float(dict, "emulsion_color_density");
```

(`export_request_from_dict` reuses `preview_request_from_dict`, so it inherits these automatically.)

- [ ] **Step 11: Add bridge dataclass fields** in `dfee_native_bridge.py` `NativePreviewRenderRequest`, after `film_color: float = 100.0`:

```python
    film_color: float = 100.0
    highlight_color_hold: float = 0.0
    shadow_color_retention: float = 0.0
    palette_separation: float = 0.0
    emulsion_color_density: float = 0.0
```

(`NativeExportRequest` inherits these.)

- [ ] **Step 12: Wire `server.py` preview endpoint.** Add query params (after `film_color: float = 100.0,`):

```python
    film_color: float = 100.0,
    highlight_color_hold: float = 0.0,
    shadow_color_retention: float = 0.0,
    palette_separation: float = 0.0,
    emulsion_color_density: float = 0.0,
```

Add to the native request dict (near `"film_color": film_color,`):

```python
        "film_color": film_color,
        "highlight_color_hold": highlight_color_hold,
        "shadow_color_retention": shadow_color_retention,
        "palette_separation": palette_separation,
        "emulsion_color_density": emulsion_color_density,
```

Add validation (after the `film_exposure_ev` range check in the preview handler):

```python
    for _cc_name, _cc_val in (
        ("highlight_color_hold", highlight_color_hold),
        ("shadow_color_retention", shadow_color_retention),
        ("palette_separation", palette_separation),
        ("emulsion_color_density", emulsion_color_density),
    ):
        if _cc_val < -100.0 or _cc_val > 100.0:
            raise HTTPException(status_code=400, detail=f"{_cc_name} must be between -100 and 100")
    if EFFECT_PIPELINE_VERSION == "parity_v1" and any(
        v != 0.0 for v in (highlight_color_hold, shadow_color_retention, palette_separation, emulsion_color_density)
    ):
        raise HTTPException(status_code=400, detail="Color Character requires filmic_v2")
```

(Use the same version source the preview endpoint already sends; if it forwards a request-provided version, validate against that value instead of the constant.)

- [ ] **Step 13: Wire `server.py` export model.** Add to the `ExportRequest` Pydantic model (after `film_color: float = 100.0`):

```python
    film_color: float = 100.0
    highlight_color_hold: float = 0.0
    shadow_color_retention: float = 0.0
    palette_separation: float = 0.0
    emulsion_color_density: float = 0.0
```

Add the same range + parity validation in the export handler (after its `film_exposure_ev` check, using `req.<field>`), and add the four keys to the export native request dict near `"film_color": req.film_color,`.

- [ ] **Step 14: Write the failing bridge test** in `tests/test_native_bridge.py`:

```python
def test_color_character_fields_default_zero_and_round_trip():
    from dfee_native_bridge import NativePreviewRenderRequest, NativeExportRequest
    req = NativePreviewRenderRequest(filename="x.ARW", stock="none")
    assert req.highlight_color_hold == 0.0
    assert req.shadow_color_retention == 0.0
    assert req.palette_separation == 0.0
    assert req.emulsion_color_density == 0.0
    exp = NativeExportRequest(filename="x.ARW", stock="none", palette_separation=25.0)
    assert exp.palette_separation == 25.0
```

- [ ] **Step 15: Build native + run native no-op test**

Run: build native Release, run the color-character CTest case. Expected: PASS.

- [ ] **Step 16: Run the bridge test**

Run: `python -m pytest tests/test_native_bridge.py::test_color_character_fields_default_zero_and_round_trip -q`
Expected: PASS.

- [ ] **Step 17: Commit**

```bash
git add cpp_engine/include/dfee/solver.hpp cpp_engine/include/dfee/bridge_types.hpp cpp_engine/src/session.cpp cpp_engine/src/solver.cpp cpp_engine/bindings/python/dfee_native_module.cpp dfee_native_bridge.py server.py cpp_engine/tests/test_core.cpp tests/test_native_bridge.py
git commit -m "Add Color Character request/report schema (M7-003A)"
```

---

## Task 2: Loader support for the color_character YAML group

Register the new optional group and support the variable-length `anchors` array.

**Files:**
- Modify: `cpp_engine/src/profile.cpp` (`validate_native_film_stock_contract`, `validate_profile_fields`)
- Test: `cpp_engine/tests/test_core.cpp`

**Interfaces:**
- Consumes: flattened dotted keys under `color_character.*` from `flatten_profile_document`.
- Produces: loader accepts the new numeric leaves + variable-length `color_character.palette.anchors` / `anchor_weights`.

- [ ] **Step 1: Write the failing loader test** in `test_core.cpp` — load a temp YAML stock that includes a `color_character:` block with a 6-element `palette.anchors`, assert it loads without throwing and the flattened values are present. Write the fixture YAML to a temp path under the profiles dir at test runtime and remove it after (mirror the existing temp-fixture pattern used in the RAW failure tests).

```cpp
TEST_CASE("loader accepts optional color_character group with variable-length anchors") {
    // write temp stock yaml with color_character.palette.anchors: [30,90,150,210,270,330]
    // load_film_stock_profile(temp_path) must not throw
    // profile.numeric_values must contain "color_character.highlight_hold_sensitivity"
    // profile.numeric_arrays must contain "color_character.palette.anchors" with size 6
}
```

- [ ] **Step 2: Run to verify it fails**

Expected: throws "unsupported numeric field" / "array field must contain exactly three values".

- [ ] **Step 3: Add a variable-length array whitelist path** in `validate_profile_fields`. Add a parameter `const std::unordered_set<std::string>& variable_array_fields` and change the array-size check to skip the `== 3` rule for keys in that set:

```cpp
    for (const auto& [key, values] : profile.numeric_arrays) {
        const bool is_fixed3 = array_fields.contains(key);
        const bool is_variable = variable_array_fields.contains(key);
        if (!is_fixed3 && !is_variable) {
            throw std::runtime_error(kind + " profile contains an unsupported array field: '" + key + "'.");
        }
        if (is_fixed3 && values.size() != 3U) {
            throw std::runtime_error(kind + " profile array field must contain exactly three values: '" + key + "'.");
        }
        if (values.empty()) {
            throw std::runtime_error(kind + " profile array field must not be empty: '" + key + "'.");
        }
        for (const double value : values) {
            if (!std::isfinite(value)) {
                throw std::runtime_error(kind + " profile contains a non-finite array value: '" + key + "'.");
            }
        }
    }
```

Update the print-stock caller to pass an empty `variable_array_fields` set so its behaviour is unchanged.

- [ ] **Step 4: Register the new numeric fields** in `validate_native_film_stock_contract` `kNumericFields`:

```cpp
        "color_character.highlight_hold_sensitivity",
        "color_character.shadow_retention_sensitivity",
        "color_character.emulsion_density_sensitivity",
        "color_character.palette.separation_sensitivity",
```

- [ ] **Step 5: Register the variable-length arrays.** Add a static set and pass it into `validate_profile_fields`:

```cpp
    static const std::unordered_set<std::string> kVariableArrayFields{
        "color_character.palette.anchors",
        "color_character.palette.anchor_weights",
    };
    validate_profile_fields(profile, kNumericFields, kArrayFields, kStringFields, kVariableArrayFields, "Film stock");
```

- [ ] **Step 6: Run the loader test**

Expected: PASS.

- [ ] **Step 7: Run full CTest** to confirm all 27 existing profiles still load (they have no `color_character:` block yet, which is valid since the group is optional).

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add cpp_engine/src/profile.cpp cpp_engine/tests/test_core.cpp
git commit -m "Register optional color_character YAML group in native loader"
```

---

## Task 3: Highlight Color Hold (M7-003B)

**Files:**
- Modify: `cpp_engine/src/renderer.cpp` (`apply_color_response_and_coupling_pipeline` and `apply_color_response`)
- Test: `cpp_engine/tests/test_core.cpp`

**Interfaces:**
- Consumes: `FilmResponsePlan.highlight_color_hold`, `.highlight_hold_sensitivity`.
- Produces: modulated `hi_comp` and `highlight_desat` used by both colour paths.

- [ ] **Step 1: Write the failing native test.** Render a synthetic highlight-heavy patch through the colour pipeline with a chromatic-highlight stock at `highlight_color_hold = +100`, and assert highlight chroma is higher than at `0`, while a mid/shadow patch is unchanged.

```cpp
TEST_CASE("highlight color hold increases only highlight chroma") {
    // build FilmResponsePlan with hi_compression>0, highlight_desaturation>0, sensitivity=1
    // render bright chromatic pixel + midtone chromatic pixel through the pipeline
    // hold=+100 -> bright pixel chroma strictly greater than hold=0
    // midtone pixel chroma equal (within 1e-4) between hold=0 and hold=+100
}
```

- [ ] **Step 2: Run to verify it fails** — chroma equal because control not applied yet.

- [ ] **Step 3: Implement.** In `renderer.cpp`, add near the top of the anonymous namespace:

```cpp
constexpr float kHoldGainHi = 0.6F;
constexpr float kHoldGainDesat = 0.6F;
```

In `apply_color_response_and_coupling_pipeline`, after `highlight_desat` and `hi_comp` are resolved (and before the per-pixel loop), add:

```cpp
    const float n_hold = std::clamp(response.highlight_color_hold / 100.0F, -1.0F, 1.0F)
        * response.highlight_hold_sensitivity;
    hi_comp = std::max(hi_comp * (1.0F - kHoldGainHi * n_hold), 0.0F);
    const float highlight_desat_effective =
        std::max(highlight_desat * (1.0F - kHoldGainDesat * n_hold), 0.0F);
```

Replace subsequent uses of `highlight_desat` in this function with `highlight_desat_effective`. Apply the identical modulation in `apply_color_response` (the non-coupling path) for its `highlight_desat`/`hi_comp` equivalents.

- [ ] **Step 4: Run the test** — Expected: PASS.

- [ ] **Step 5: Run the all-neutral no-op test** (from Task 1) to confirm `hold=0` is still identical. Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement Highlight Color Hold (M7-003B)"
```

---

## Task 4: Shadow Color Retention (M7-003C)

**Files:**
- Modify: `cpp_engine/src/renderer.cpp`
- Test: `cpp_engine/tests/test_core.cpp`

**Interfaces:**
- Consumes: `FilmResponsePlan.shadow_color_retention`, `.shadow_retention_sensitivity`.
- Produces: modulated `sh_comp`.

- [ ] **Step 1: Write the failing native test** — shadow chromatic pixel: `shadow_color_retention=+100` yields higher shadow chroma than `0`; assert **lightness (`lch.l` / output luma) is unchanged** between the two (no black lift); a highlight pixel's chroma is unchanged.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement.** Add constant:

```cpp
constexpr float kShadowRetentionGain = 0.6F;
```

After `sh_comp` is resolved and before the loop:

```cpp
    const float n_ret = std::clamp(response.shadow_color_retention / 100.0F, -1.0F, 1.0F)
        * response.shadow_retention_sensitivity;
    sh_comp = std::max(sh_comp * (1.0F - kShadowRetentionGain * n_ret), 0.0F);
```

Confirm no code path lets this touch `lch.l` — it only scales the shadow chroma term `c_new = c_hi * (1.0F - sh_mask * sh_comp);`.

- [ ] **Step 4: Run the test** — Expected: PASS (including the L-unchanged assertion).

- [ ] **Step 5: Run all-neutral no-op test** — Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement Shadow Color Retention (M7-003C)"
```

---

## Task 5: Palette Separation (M7-003D)

**Files:**
- Modify: `cpp_engine/src/renderer.cpp`
- Test: `cpp_engine/tests/test_core.cpp`

**Interfaces:**
- Consumes: `FilmResponsePlan.palette_separation`, `.palette_separation_sensitivity`, `.palette_anchors`, `.palette_anchor_weights`.
- Produces: a chroma-gated, wrap-stable hue-anchor attraction applied per pixel in both colour paths.

- [ ] **Step 1: Write the failing native tests:**
  - Neutral-pixel preservation: a low-chroma (gray) pixel is unchanged at `palette_separation=+100`.
  - Separation direction: a saturated pixel offset from an anchor moves *toward* the nearest anchor at `+100` and *away* at `−100`.
  - Wrap stability: two pixels at hue 359° and 1° with an anchor at 0° both move continuously toward 0° (no sign flip / discontinuity).

- [ ] **Step 2: Run to verify they fail.**

- [ ] **Step 3: Add a shared helper** in the anonymous namespace of `renderer.cpp`:

```cpp
constexpr float kPaletteSepGain = 0.35F;   // max radians of hue pull at full strength
constexpr float kPaletteChromaLo = 0.02F;  // OKLCh chroma gate lower edge
constexpr float kPaletteChromaHi = 0.06F;

inline float smoothstep01(float lo, float hi, float x) {
    const float t = std::clamp((x - lo) / std::max(hi - lo, 1.0e-6F), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

// Returns the signed shortest angular delta from h toward the nearest anchor.
inline float nearest_anchor_delta(float h, const std::vector<float>& anchors) {
    float best = 0.0F;
    float best_abs = std::numeric_limits<float>::max();
    for (const float a : anchors) {
        float d = std::fmod((a - h) + std::numbers::pi_v<float>, 2.0F * std::numbers::pi_v<float>)
            - std::numbers::pi_v<float>;
        if (std::abs(d) < best_abs) { best_abs = std::abs(d); best = d; }
    }
    return best;  // == sin-friendly delta; caller uses std::sin(best) for wrap stability
}
```

Provide default anchors when `response.palette_anchors` is empty: six evenly spaced hues (radians) `{0, π/3, 2π/3, π, 4π/3, 5π/3}` and unit weights.

- [ ] **Step 4: Apply the sub-pass** inside the per-pixel loop of `apply_color_response_and_coupling_pipeline`, after the coupling stage computes final `lch.c` and `lch.h` (just before converting back to OKLab at the end):

```cpp
    const float n_sep = std::clamp(response.palette_separation / 100.0F, -1.0F, 1.0F)
        * response.palette_separation_sensitivity;
    if (n_sep != 0.0F) {
        const float g_c = smoothstep01(kPaletteChromaLo, kPaletteChromaHi, lch.c);
        const float delta = nearest_anchor_delta(lch.h, anchors);  // anchors resolved once outside loop
        h_new = wrap_angle_positive(lch.h + kPaletteSepGain * n_sep * g_c * std::sin(delta));
    }
```

Resolve `anchors`/weights once before the loop. Mirror the same block in `apply_color_response`. (Anchor-weighted chroma differentiation is deferred to Task 8 calibration; keep this task hue-only to isolate the behaviour.)

- [ ] **Step 5: Run the palette tests** — Expected: PASS (neutral preserved, direction correct, wrap-stable).

- [ ] **Step 6: Run all-neutral no-op test** — Expected: PASS (guarded by `n_sep != 0.0F`).

- [ ] **Step 7: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement Palette Separation (M7-003D)"
```

---

## Task 6: Emulsion Color Density + solver defaults (M7-003D′)

Implement the density control and wire the solver to read YAML + infer family sensitivities/anchors (needed so Tasks 3–5 have non-zero sensitivities against real stocks).

**Files:**
- Modify: `cpp_engine/src/renderer.cpp` (density modulation of `chroma_boost`)
- Modify: `cpp_engine/src/solver.cpp` (read `color_character:`, infer family defaults, populate `FilmResponsePlan`)
- Test: `cpp_engine/tests/test_core.cpp`

**Interfaces:**
- Consumes: `FilmResponsePlan.emulsion_color_density`, `.emulsion_density_sensitivity`, `chroma_boost`; stock YAML `color_character.*`.
- Produces: family-inferred sensitivities/anchors on `FilmResponsePlan`; density-modulated `chroma_boost`.

- [ ] **Step 1: Write the failing renderer test** — `emulsion_color_density=+100` increases mid-saturation chroma via `chroma_boost`, but leaves the hue-compression coefficients (`red_comp`, etc.) unchanged (distinct from blunt `film_color`).

- [ ] **Step 2: Write the failing solver test** — a color-negative stock with no `color_character:` block resolves non-zero default sensitivities (e.g. `highlight_hold_sensitivity > 0`), and a monochrome stock resolves all sensitivities to `0`.

- [ ] **Step 3: Run to verify both fail.**

- [ ] **Step 4: Implement density modulation** in `renderer.cpp`. Add constant `constexpr float kEmulsionDensityGain = 0.5F;`. Where `chroma_boost` is resolved:

```cpp
    const float n_dens = std::clamp(response.emulsion_color_density / 100.0F, -1.0F, 1.0F)
        * response.emulsion_density_sensitivity;
    const float chroma_boost = (1.0F + (response.chroma_boost - 1.0F) * fc) * (1.0F + kEmulsionDensityGain * n_dens);
```

Apply in both colour paths. Do not alter `red_comp`/`blue_comp`/biases.

- [ ] **Step 5: Add a family-defaults helper** in `solver.cpp`, mirroring `grain_family_defaults`:

```cpp
struct ColorCharacterDefaults {
    float highlight_hold_sensitivity;
    float shadow_retention_sensitivity;
    float emulsion_density_sensitivity;
    float palette_separation_sensitivity;
};

[[nodiscard]] ColorCharacterDefaults color_character_defaults(StockType type) {
    switch (type) {
        case StockType::ColorReversal:   return {0.85F, 0.55F, 0.70F, 0.70F};
        case StockType::ColorNegative:   return {0.70F, 0.65F, 0.60F, 0.55F};
        case StockType::Monochrome:      return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    return {0.60F, 0.60F, 0.55F, 0.50F};
}
```

(These are initial values; Task 8 calibrates.)

- [ ] **Step 6: Populate `FilmResponsePlan`** in `solver.cpp` after the `plan.film_response = {...}` initializer:

```cpp
    const ColorCharacterDefaults cc_defaults = color_character_defaults(stock_profile.stock_type);
    plan.film_response.highlight_hold_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.highlight_hold_sensitivity",
        cc_defaults.highlight_hold_sensitivity);
    plan.film_response.shadow_retention_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.shadow_retention_sensitivity",
        cc_defaults.shadow_retention_sensitivity);
    plan.film_response.emulsion_density_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.emulsion_density_sensitivity",
        cc_defaults.emulsion_density_sensitivity);
    plan.film_response.palette_separation_sensitivity = get_numeric(
        stock_profile.numeric_values, "color_character.palette.separation_sensitivity",
        cc_defaults.palette_separation_sensitivity);
    plan.film_response.palette_anchors = get_numeric_vector(
        stock_profile.numeric_arrays, "color_character.palette.anchors");   // empty -> renderer default
    plan.film_response.palette_anchor_weights = get_numeric_vector(
        stock_profile.numeric_arrays, "color_character.palette.anchor_weights");
```

If a `get_numeric_vector` helper does not exist in `solver.cpp`, add a small one that returns the mapped `std::vector<float>` or an empty vector when the key is absent (mirror `get_array3`).

- [ ] **Step 7: Serialize resolved sensitivities into report JSON** in `session.cpp` (extend the block from Task 1 Step 8 with the four `*_sensitivity` values). This gives reproducibility of the resolved calibration.

- [ ] **Step 8: Run both tests** — Expected: PASS.

- [ ] **Step 9: Run all-neutral no-op + full CTest** — Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/src/solver.cpp cpp_engine/src/session.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement Emulsion Color Density + solver family defaults (M7-003D')"
```

---

## Task 7: Color Character UI (M7-003E)

**Files:**
- Modify: `frontend/src/App.jsx`
- Test: manual (Vite build + lint); no unit-test harness exists for the panel.

**Interfaces:**
- Consumes: the four request fields on preview (query string) and export (JSON body).
- Produces: four bipolar sliders in the Color Character panel; legacy `film_color` relocated to Advanced Correction.

- [ ] **Step 1: Add defaults** to `DEFAULT_PARAMS` in `App.jsx` (near `film_color: 100,`):

```js
    highlight_color_hold: 0,
    shadow_color_retention: 0,
    palette_separation: 0,
    emulsion_color_density: 0,
```

- [ ] **Step 2: Treat the four keys as floats** in the `set()` change handler — add them to the float branch used for `film_exposure_ev` (the `parseFloat` list at line ~457):

```js
      ? (['exposure', 'film_exposure_ev', 'adaptation', 'sharpness', 'sharpness_mask', 'highlight_color_hold', 'shadow_color_retention', 'palette_separation', 'emulsion_color_density'].includes(key) ? parseFloat(e.target.value) : parseInt(e.target.value))
```

- [ ] **Step 3: Send on preview.** In the preview query builder (near `` + `&film_color=${params.film_color}` ``), append:

```js
        + `&highlight_color_hold=${params.highlight_color_hold}`
        + `&shadow_color_retention=${params.shadow_color_retention}`
        + `&palette_separation=${params.palette_separation}`
        + `&emulsion_color_density=${params.emulsion_color_density}`
```

Also add them to the preview `POST` params object (near line ~596/620) and the export params object (near line ~868/893), matching the existing style.

- [ ] **Step 4: Replace the Color Character panel body.** In the `openSections['Color Character']` block, remove the legacy `film_color` slider and add four bipolar sliders (`min=-100 max=100 step=1`), each with a scene-agnostic label, tooltip, dirty-state class, and a reset-to-0 button. Follow the exact markup pattern of the existing Film Exposure slider. Labels and tooltips:

```
Highlight Color Hold — "How much colour survives in the brightest areas before they wash toward white."
Shadow Color Retention — "How much colour is kept in the deep shadows."
Palette Separation — "How distinctly different colours are held apart from one another."
Emulsion Color Density — "Overall strength of the stock's colour dyes."
```

Add a mono-aware disabled state: when the selected stock is monochrome, disable the four sliders (grey them) with a tooltip "Not available for black & white stocks." Use whatever the app already knows about the selected stock's type; if type is not currently surfaced to the client, disable purely on the existing stock metadata if available, otherwise leave enabled (the native path is a safe no-op) and note the follow-up.

- [ ] **Step 5: Relocate legacy `film_color`.** Add the existing `film_color` slider (0–200, label "Film Color (legacy)") into the Advanced Correction section so the contract field remains reachable without occupying the primary panel.

- [ ] **Step 6: Build + lint**

Run: `cd frontend && npm run build && npm run lint`
Expected: both succeed (clean any lint blockers touched).

- [ ] **Step 7: Commit**

```bash
git add frontend/src/App.jsx
git commit -m "Add Color Character UI panel; move legacy film_color to Advanced (M7-003E)"
```

---

## Task 8: Calibration, fixtures, route tests, visual acceptance (M7-003F)

**Files:**
- Modify: `cpp_engine/src/solver.cpp` (`color_character_defaults` calibration), `cpp_engine/src/renderer.cpp` (gain constants), selected `profiles/stocks/*.yaml`
- Test: `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`, `tests/test_server_errors.py`

**Interfaces:**
- Consumes: everything from Tasks 1–7.
- Produces: tuned defaults, synthetic zone/hue fixtures, route-level validation coverage, recorded visual acceptance.

- [ ] **Step 1: Add synthetic zone/hue fixture tests** in `test_core.cpp` — a hue wheel + a 7-zone luminance ramp rendered through the pipeline, asserting locality (highlight hold changes only high zones; shadow retention only low zones; palette separation only chromatic pixels) and determinism (two identical renders byte-identical).

- [ ] **Step 2: Add the parity-rejection route test** in `tests/test_server_errors.py` — a preview/export request with `parity_v1` (or the parity path) and non-zero `palette_separation` returns HTTP 400.

- [ ] **Step 3: Add a preview↔export agreement test** in `tests/test_native_bridge.py` — the same Color Character inputs produce matching resolved report fields on both preview and export.

- [ ] **Step 4: Run all native + python tests** — Expected: PASS.

Run: native Release CTest; `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`

- [ ] **Step 5: Populate `color_character:` in representative stock YAMLs.** For at least one stock per family (reversal, modern negative fine/high-speed, consumer negative, B&W), add an explicit `color_character:` block with tuned sensitivities and (for the reversal example) a custom `palette.anchors`. Confirm each still loads (CTest). Use the family defaults as the starting point and adjust per emulsion character.

- [ ] **Step 6: Visual acceptance pass.** Render representative RAWs through preview and export at each control's extremes: highlight-heavy, deep-shadow-chroma, saturated-palette, neutral/gray-world, and a monochrome stock. Confirm: no black lift from shadow retention, neutrals preserved under palette separation, mono no-op, and preview/export agreement. Record the images reviewed, stock, pipeline version, and accept/reject decision in `cpp_engine/migration_docs/BUG_TRACKER.md` (or the established visual-acceptance log).

- [ ] **Step 7: Timing probe.** Run the documented preview/export timing probe against the named baseline with all four controls active; confirm the palette sub-pass does not materially regress the hot path. Record the numbers with hardware, pipeline version, and source dimensions.

- [ ] **Step 8: Tune gains/defaults** in `renderer.cpp` constants and `color_character_defaults` based on Steps 6–7, re-running CTest after changes.

- [ ] **Step 9: Commit**

```bash
git add cpp_engine/src/solver.cpp cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp tests/test_native_bridge.py tests/test_server_errors.py profiles/stocks cpp_engine/migration_docs/BUG_TRACKER.md
git commit -m "Calibrate Color Character, add fixtures + route tests + visual acceptance (M7-003F)"
```

---

## Task 9: Documentation + APAM

**Files:**
- Modify: `cpp_engine/migration_docs/FILM_LAB_WORKFLOW.md`, `cpp_engine/migration_docs/FILM_LAB_IMPLEMENTATION_PLAN.md`, `cpp_engine/migration_docs/STOCK_PROFILE_CONTRACT.md`, `README.md`, `docs/technical_architecture.md`

- [ ] **Step 1: Document the contract** — add the four controls (range, neutral default, request/report fields, filmic_v2-only behaviour) to `FILM_LAB_WORKFLOW.md`; mark M7-003 done and update the "next slice" pointer to M7-004 Process in `FILM_LAB_IMPLEMENTATION_PLAN.md`; document the `color_character:` YAML group and variable-length-array loader rule in `STOCK_PROFILE_CONTRACT.md`; update README + technical architecture control lists.

- [ ] **Step 2: Verify docs, UI labels, and runtime agree** (control names identical across spec, tooltip copy, and report fields).

- [ ] **Step 3: Commit**

```bash
git add cpp_engine/migration_docs/FILM_LAB_WORKFLOW.md cpp_engine/migration_docs/FILM_LAB_IMPLEMENTATION_PLAN.md cpp_engine/migration_docs/STOCK_PROFILE_CONTRACT.md README.md docs/technical_architecture.md
git commit -m "Document Color Character contract and mark M7-003 done"
```

- [ ] **Step 4: Update APAM** — write an L2 episode and update L1/L3 for the completed M7-003 slice (new controls, contract surfaces, calibration approach, next slice M7-004).

---

## Self-Review Notes

- **Spec coverage:** control mapping (Tasks 3–6), bipolar/neutral convention + all-neutral no-op (Task 1 + reused in 3–6), YAML group + variable-length anchors (Task 2, 6), family defaults incl. B&W zero (Task 6), request contract + parity rejection + range validation (Task 1, 8), report inputs+resolved (Task 1, 6), UI incl. mono-disable and legacy relocation (Task 7), tests incl. no-op/locality/L-unchanged/neutral-preserve/wrap/mono/determinism (Tasks 1,3,4,5,8), preview↔export agreement + timing probe (Task 8), docs+APAM (Task 9). Deliberate restraints (no tone coupling, no palette LUT) honoured.
- **Fallback note:** the legacy Python engine path is not extended; the controls are filmic_v2 native-only and are a documented no-op on the legacy fallback (consistent with the native-first posture). Called out here so it is not mistaken for a gap.
- **Type consistency:** field names identical across `SolverControls`, `FilmResponsePlan`, `NativePreviewRenderRequest`, pybind keys, bridge dataclass, server params, JSON, and React (`highlight_color_hold`, `shadow_color_retention`, `palette_separation`, `emulsion_color_density`). Gain constants named once and reused.
