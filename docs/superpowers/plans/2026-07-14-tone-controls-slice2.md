# Highlight Rolloff + Film Contrast (Film Lab v1 — Slice 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the `filmic_v3` **Highlight Rolloff** and **Film Contrast** tone controls, resolved in the solver from stock defaults × manual controls × a **scene-referred adaptive factor** (flat / high-dynamic-range / log-like files get more filmic contrast + earlier rolloff automatically), with the existing renderer tone stage applying the result unchanged.

**Architecture:** The renderer's `apply_film_tone_response` already reads `toe_strength`, `midtone_density`, `shoulder_strength`, and `highlight_rolloff_start` from `FilmResponsePlan`. Slice 2 modulates those values **in the solver** (which already receives the tonal analysis) when the pipeline is `filmic_v3`: Film Contrast scales toe + midtone punch, Highlight Rolloff pulls the shoulder earlier and firmer, and a bounded adaptive factor derived from `dynamic_range_stops` strengthens both for flat/log-like scenes. A new `adaptive` flag (default on) gates the scene-referred part; manual controls ride on top (`effective ≈ stock × control/100 × adaptive_factor`). `parity_v1` / `filmic_v2` resolve tone exactly as today.

**Tech Stack:** C++20 (dfee_core, pybind), Python (FastAPI `server.py`, `dfee_native_bridge.py`, pytest), React (`frontend/src/App.jsx`), CTest.

## Global Constraints

- Behaviour lives under `effect_pipeline_version = filmic_v3`; `parity_v1` and `filmic_v2` tone resolution is byte-identical to today.
- Parameter-model convention: `highlight_rolloff` and `film_contrast` are floats default `100.0`, range `[0,200]`; `100` = the stock's calibrated tone curve; `<100` relaxes toward flat/digital, `>100` intensifies ("forward = more filmic"); omitted → `100`; server-clamped to `[0,200]`. These controls modulate the stock's EXISTING `tone_response.*` fields — no new per-stock YAML.
- New `adaptive` request flag (bool, default `true`): when on, the solver applies a bounded scene-referred adaptive factor to the tone controls; when off, tone = `stock × control/100` only (no scene steering). This is the first use of the global Adaptive plane (Slice 4 extends it to colour/region).
- Adaptive factor is bounded (≈ ×0.8–×1.4) so tone stays plausibly-real. Monochrome stocks are affected (tone is not colour), which is correct.
- Solver tone modulation is gated by a `subtractive_pipeline` flag set from `effect_pipeline_version == filmic_v3`; the existing renderer tone stage is unchanged.
- Isolation invariant: `filmic_v3` with `adaptive=false`, `highlight_rolloff=100`, `film_contrast=100`, `film_color_density=0`, `film_color_compression=0` equals `filmic_v2`.
- Report JSON records the two control inputs and the resolved adaptive factor.

---

## File Structure

- `cpp_engine/include/dfee/solver.hpp` — `SolverControls`: `highlight_rolloff`, `film_contrast`, `adaptive`, `subtractive_pipeline`; `FilmResponsePlan`: `highlight_rolloff`, `film_contrast`, `tone_adaptive_factor`.
- `cpp_engine/include/dfee/bridge_types.hpp` — `NativePreviewRenderRequest`: `highlight_rolloff`, `film_contrast`, `adaptive`.
- `cpp_engine/src/solver.cpp` — tone steering block (the core of this slice) + populate report fields.
- `cpp_engine/src/session.cpp` — map controls (incl. `subtractive_pipeline` from version, `adaptive` from request) at both build sites; report JSON.
- `cpp_engine/bindings/python/dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py` — plumb + clamp + bool.
- `frontend/src/App.jsx` — Highlight Rolloff + Film Contrast sliders + Adaptive toggle.
- `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`, `tests/test_server_errors.py`.
- Docs: `documentation/`, `README.md`, `docs/technical_architecture.md`, `STOCK_PROFILE_CONTRACT.md` (note: no new stock fields; controls modulate existing tone_response).

**Canonical names:** `highlight_rolloff`, `film_contrast` (float, default 100.0); `adaptive` (bool, default true); `FilmResponsePlan.highlight_rolloff`, `.film_contrast`, `.tone_adaptive_factor`; `SolverControls.subtractive_pipeline` (bool). No renderer function changes.

**Build/test (Windows, repo root d:/Codebases/DFEE):**
- Native: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_tests dfee_native` (rebuild `dfee_native` after dfee_core changes).
- Native tests: `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure`
- Python: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`
- Frontend: `cd frontend; npm run build; npm run lint`

---

## Task 1: Schema + contract (controls + adaptive flag)

**Files:** `solver.hpp`, `bridge_types.hpp`, `session.cpp` (controls map ×2 + report), `dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py`, `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`.

- [ ] **Step 1: Failing bridge test** in `tests/test_native_bridge.py` (`test_tone_controls_defaults_and_round_trip`): `NativePreviewRenderRequest(...)` defaults `highlight_rolloff == 100.0`, `film_contrast == 100.0`, `adaptive is True`; `NativeExportRequest(..., film_contrast=130.0, adaptive=False)` round-trips.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: `SolverControls`** in `solver.hpp` after `film_color_compression`:

```cpp
    float highlight_rolloff = 100.0F;
    float film_contrast = 100.0F;
    bool adaptive = true;              // scene-referred steering on/off
    bool subtractive_pipeline = false; // set true for filmic_v3 (gates v3 tone steering)
```

- [ ] **Step 4: `FilmResponsePlan`** in `solver.hpp` after the compression fields:

```cpp
    float highlight_rolloff = 100.0F;  // control value (0..200), 100 = stock default
    float film_contrast = 100.0F;      // control value (0..200), 100 = stock default
    float tone_adaptive_factor = 1.0F; // resolved scene-referred factor (report/diagnostic)
```

- [ ] **Step 5: `NativePreviewRenderRequest`** in `bridge_types.hpp` after `film_color_compression`:

```cpp
    float highlight_rolloff = 100.0F;
    float film_contrast = 100.0F;
    bool adaptive = true;
```

- [ ] **Step 6: Map controls** in `session.cpp` at BOTH `SolverControls` build sites, after `controls.film_color_compression = request.film_color_compression;`:

```cpp
    controls.highlight_rolloff = request.highlight_rolloff;
    controls.film_contrast = request.film_contrast;
    controls.adaptive = request.adaptive;
    controls.subtractive_pipeline = is_subtractive_effect_pipeline(request.effect_pipeline_version);
```

(`is_subtractive_effect_pipeline` is defined in the `session.cpp` anonymous namespace.)

- [ ] **Step 7: Report JSON** in `session.cpp` after the compression report fields (extend the block, keeping exactly one field without a trailing comma):

```cpp
        << "\"highlight_rolloff\": " << json_number(render_plan.film_response.highlight_rolloff) << ",\n"
        << "\"film_contrast\": " << json_number(render_plan.film_response.film_contrast) << ",\n"
        << "\"tone_adaptive_factor\": " << json_number(render_plan.film_response.tone_adaptive_factor) << ",\n"
```

- [ ] **Step 8: pybind** in `dfee_native_module.cpp` after `film_color_compression` (note `dict_bool` for the flag):

```cpp
    request.highlight_rolloff = dict_float(dict, "highlight_rolloff", 100.0F);
    request.film_contrast = dict_float(dict, "film_contrast", 100.0F);
    request.adaptive = dict_bool(dict, "adaptive", true);
```

(Confirm `dict_bool(PyObject*, const char*, bool)` exists in the file; it is used for other boolean fields. If the signature differs, match it.)

- [ ] **Step 9: bridge dataclass** in `dfee_native_bridge.py` after `film_color_compression`:

```python
    highlight_rolloff: float = 100.0
    film_contrast: float = 100.0
    adaptive: bool = True
```

- [ ] **Step 10: server.py** — add `highlight_rolloff: float = 100.0`, `film_contrast: float = 100.0`, `adaptive: bool = True` as preview query params and `PreviewRequest`/`ExportRequest` fields; add to the native dicts: `"highlight_rolloff": max(0.0, min(200.0, highlight_rolloff))`, `"film_contrast": max(0.0, min(200.0, film_contrast))`, `"adaptive": bool(adaptive)` (preview) and the `req.`-prefixed forms (export).

- [ ] **Step 11: Build + tests.** Build `dfee_tests`, run full CTest. Rebuild `dfee_native`, run the bridge round-trip test.

- [ ] **Step 12: Commit**

```bash
git add -A
git commit -m "Add Highlight Rolloff / Film Contrast / adaptive schema"
```

---

## Task 2: Solver tone steering (the core)

**Files:** `cpp_engine/src/solver.cpp`, `cpp_engine/tests/test_core.cpp`.

**Interfaces:**
- Consumes: `SolverControls.highlight_rolloff/film_contrast/adaptive/subtractive_pipeline`, `SolverInput.tonal_distribution.dynamic_range_stops`.
- Produces: modulated `toe_strength`, `midtone_density`, `shoulder_strength`, `highlight_rolloff_start` in `plan.film_response`; `highlight_rolloff`, `film_contrast`, `tone_adaptive_factor` populated.

- [ ] **Step 1: Write the failing solver test** (`test_solver_tone_steering`): load `portra_400.yaml`. Solve TWO `SolverInput`s that differ only in `dynamic_range_stops` (one normal ≈ 8, one flat/wide ≈ 13), with `controls.subtractive_pipeline=true`, `controls.adaptive=true`, `controls.film_contrast=100`, `controls.highlight_rolloff=100`. Assert:
  - the flat scene resolves a HIGHER `plan.film_response.midtone_density` (and `tone_adaptive_factor > 1`) than the normal scene (adaptive strengthens contrast for flat/log-like);
  - the normal scene (DR ≈ 8) resolves `tone_adaptive_factor` ≈ 1.0 (within 0.02);
  - with `controls.subtractive_pipeline=false` (parity/filmic_v2), the flat scene's `midtone_density` equals the stock value (no steering) and `tone_adaptive_factor == 1.0`;
  - with `subtractive_pipeline=true`, `adaptive=false`, `film_contrast=100`, `highlight_rolloff=100`, tone fields equal the non-steered (stock) values (control-neutral + adaptive-off = stock default).
Build-agnostic `throw`. Register in `main()`.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Add tone-steering constants** near the top of `solver.cpp` (anonymous namespace):

```cpp
constexpr float kToeContrast     = 0.40F; // Film Contrast -> toe deepening
constexpr float kMidContrast     = 0.45F; // Film Contrast -> midtone punch
constexpr float kRolloffStart    = 0.10F; // Highlight Rolloff -> earlier shoulder start
constexpr float kShoulderRolloff = 0.30F; // Highlight Rolloff -> firmer shoulder
```

- [ ] **Step 4: Extract `midtone_density` to a local** so it can be modulated. Before the `plan.film_response = {...}` initializer, add:

```cpp
    float midtone_density = get_numeric(stock_profile.numeric_values, "tone_response.midtone_contrast", 0.0F);
```

and change the initializer line `.midtone_density = get_numeric(..., "tone_response.midtone_contrast", 0.0F),` to `.midtone_density = midtone_density,`.

- [ ] **Step 5: Add the tone-steering block** immediately after the existing DR/headroom adjustment block (after the `if (tonal.highlight_headroom < 0.15F) {...}` block, before `highlight_desaturation` is computed):

```cpp
    float tone_adaptive_factor = 1.0F;
    if (controls.subtractive_pipeline) {
        if (controls.adaptive) {
            // Flat / high-DR / log-like scenes get stronger filmic tone; contrasty scenes less.
            float adapt = 1.0F;
            if (tonal.dynamic_range_stops > 10.0F) {
                adapt += std::min((tonal.dynamic_range_stops - 10.0F) * 0.08F, 0.40F);
            }
            if (tonal.dynamic_range_stops < 6.0F) {
                adapt -= std::min((6.0F - tonal.dynamic_range_stops) * 0.05F, 0.20F);
            }
            tone_adaptive_factor = std::clamp(adapt, 0.80F, 1.40F);
        }
        const float contrast_gain = std::clamp(controls.film_contrast / 100.0F, 0.0F, 2.0F) * tone_adaptive_factor;
        const float rolloff_gain = std::clamp(controls.highlight_rolloff / 100.0F, 0.0F, 2.0F) * tone_adaptive_factor;
        // Film Contrast: deepen toe + punch midtones.
        toe_strength = std::clamp(toe_strength * (1.0F + kToeContrast * (contrast_gain - 1.0F)), 0.0F, 1.5F);
        midtone_density = std::clamp(midtone_density * (1.0F + kMidContrast * (contrast_gain - 1.0F)), 0.0F, 2.0F);
        // Highlight Rolloff: earlier shoulder start + firmer shoulder.
        highlight_rolloff_start = std::clamp(
            highlight_rolloff_start - kRolloffStart * (rolloff_gain - 1.0F), 0.35F, 1.0F);
        shoulder_strength = std::clamp(
            shoulder_strength * (1.0F + kShoulderRolloff * (rolloff_gain - 1.0F)), 0.0F, 0.98F);
    }
```

- [ ] **Step 6: Populate the report/plan fields** after the `plan.film_response = {...}` initializer (near the density/compression population):

```cpp
    plan.film_response.highlight_rolloff = controls.highlight_rolloff;
    plan.film_response.film_contrast = controls.film_contrast;
    plan.film_response.tone_adaptive_factor = tone_adaptive_factor;
```

- [ ] **Step 7: Run the solver test** — Expected: PASS (flat > normal contrast; parity unchanged; control-neutral+adaptive-off = stock).

- [ ] **Step 8: Full CTest** — Expected: PASS; `parity_v1`/`filmic_v2` tone resolution unchanged (subtractive_pipeline defaults false).

- [ ] **Step 9: Commit**

```bash
git add cpp_engine/src/solver.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement analysis-steered Highlight Rolloff + Film Contrast in solver"
```

---

## Task 3: End-to-end verification (bridge) + isolation-invariant fix

**Files:** `tests/test_native_bridge.py`.

- [ ] **Step 1: Add a bridge test** `test_filmic_v3_tone_controls_change_render` (RAW-guarded): render `filmic_v3` on the test RAW with `film_color_density=0, film_color_compression=0` and vary tone: `(film_contrast=100, highlight_rolloff=100, adaptive=False)` vs `(film_contrast=160, ...)` vs `(adaptive=True)`. Assert the contrast change differs, and that `adaptive=True` differs from `adaptive=False` when the scene is not perfectly normal (if the test RAW happens to be DR-normal, assert instead that the manual contrast change differs and note it). Keep assertions on inequality of jpeg bytes.

- [ ] **Step 2: Fix the density/compression isolation tests.** In `test_filmic_v3_subtractive_density_changes_render` and `test_filmic_v3_color_compression_changes_render`, the `filmic_v3 == filmic_v2` comparison must also neutralize tone: pass `adaptive=False, film_contrast=100.0, highlight_rolloff=100.0` in their `_render(...)` helpers so `filmic_v3` with all new stages neutral equals `filmic_v2` regardless of the scene's DR. Update the helper signatures/calls accordingly.

- [ ] **Step 3: Build `dfee_native` + run the bridge tests** — Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add tests/test_native_bridge.py
git commit -m "Verify filmic_v3 tone controls end-to-end; keep v3==v2 isolation with tone neutral"
```

---

## Task 4: UI — Highlight Rolloff + Film Contrast + Adaptive

**Files:** `frontend/src/App.jsx`. Verify: `cd frontend; npm run build; npm run lint`.

- [ ] **Step 1: Defaults + float parse.** Add `highlight_rolloff: 100`, `film_contrast: 100`, `adaptive: true` to `DEFAULT_PARAMS`; add `'highlight_rolloff'` and `'film_contrast'` to the `set()` `parseFloat` key list.

- [ ] **Step 2: Send on both routes.** Add `highlight_rolloff`, `film_contrast`, and `adaptive` to the preview query params (`String(...)` for the two floats, and `adaptive: params.adaptive ? '1' : '0'`) and to the export body (raw values; `adaptive: params.adaptive`).

- [ ] **Step 3: Add a Tone group.** Add a new collapsible "Film Tone" group (or place within the existing Film Lab column near Color Character) containing two 0–200 / default-100 / reset-to-100 sliders — **Highlight Rolloff** (tooltip: `"How gently the brightest areas roll off and glow instead of clipping — forward for softer, more filmic highlights."`) and **Film Contrast** (tooltip: `"The punch of the film's tone curve — forward for a deeper, more contrasty, less flat look."`) — reusing the 0–200 slider markup pattern from Film Color Density/Compression.

- [ ] **Step 4: Add the Adaptive toggle.** Add a checkbox/toggle bound to `params.adaptive` (label "Adaptive", tooltip: `"Let the film read the scene and auto-adjust tone for flat, high-dynamic-range files. Turn off for a fixed, predictable look."`) near the Tone group; `onChange` sets `params.adaptive`.

- [ ] **Step 5: Build + lint** — Expected: both PASS.

- [ ] **Step 6: Commit**

```bash
git add frontend/src/App.jsx
git commit -m "Add Highlight Rolloff + Film Contrast + Adaptive controls (UI)"
```

---

## Task 5: Route tests, docs, visual acceptance, APAM

**Files:** `tests/test_server_errors.py`, docs, APAM.

- [ ] **Step 1: Route tests** in `tests/test_server_errors.py`: preview accepts `highlight_rolloff` / `film_contrast` / `adaptive`; clamps `film_contrast=500 → 200.0` (mirror the density clamp test); `adaptive=0` forwards `False`. Run: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`. Expected: PASS.

- [ ] **Step 2: Docs.** Update `documentation/planning/film-lab-v1-project-plan.md` (mark Slice 2 done, note the Adaptive plane introduced here for tone), `documentation/architecture/film-lab-framework-architecture.md` (Adaptive first lands in Slice 2 for tone), `README.md` + `docs/technical_architecture.md` (Highlight Rolloff / Film Contrast + adaptive tone steering). `STOCK_PROFILE_CONTRACT.md` needs only a note that these controls modulate existing `tone_response.*` (no new fields). Commit.

- [ ] **Step 3: Visual acceptance (human/hardware).** On a flat/high-DR RAW (e.g. `girl_on_street.ARW`) render `filmic_v3` with `adaptive=True` vs `False`, and Film Contrast / Highlight Rolloff at 0/100/200; confirm flat files auto-gain filmic contrast under adaptive, highlights roll off gently vs clip, and the look is authentic (not crushed). Render before/after strips for review. Record in `cpp_engine/migration_docs/BUG_TRACKER.md`.

- [ ] **Step 4: Timing probe (human/hardware).** Solver-side change is cheap; confirm no regression.

- [ ] **Step 5: APAM.** L3 record + episode for analysis-steered tone controls + the Adaptive plane's first use.

---

## Self-Review Notes

- **Spec coverage:** Highlight Rolloff + Film Contrast controls (Tasks 1, 2, 4), scene-referred adaptive factor from DR (Task 2), adaptive-baseline × manual model (Task 2 formula), filmic_v3 gating + parity untouched (Tasks 1, 2), Adaptive flag/plane introduced for tone (Tasks 1, 2, 4), report fields (Tasks 1, 2), isolation invariant maintained (Task 3), route/bridge tests + docs + visual + APAM (Tasks 3, 5). No new stock YAML (controls modulate existing tone_response) — noted in Global Constraints and Task 5.
- **Placeholder scan:** none — solver math is concrete; adaptive gains are named constants (calibratable). Task 5 Steps 3-4 are explicit human/hardware steps.
- **Type consistency:** `highlight_rolloff` / `film_contrast` (float, default 100.0) and `adaptive` (bool, default true) identical across `SolverControls`, `FilmResponsePlan` (control values + `tone_adaptive_factor`), request DTO, pybind, bridge, server, report, React. `subtractive_pipeline` (bool) set only in `session.cpp`. No renderer signature changes. Constants `kToeContrast`/`kMidContrast`/`kRolloffStart`/`kShoulderRolloff` defined once in Task 2.
