# Palette Range (Film Look Slice 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Evolve the M7-003 Palette Separation control into a muscular, bipolar **Palette Range** control: negative merges/harmonizes hues (the ethereal limited-palette look) with coupled desaturation, positive separates them.

**Architecture:** Rename `palette_separation` -> `palette_range` across the whole contract stack (behaviour-preserving), then replace the hue-only sub-pass in `apply_color_response_and_coupling_pipeline` with a bipolar algorithm: merge (negative) pulls each pixel's hue toward its nearest palette anchor AND desaturates proportionally to the pull (fewer distinct, less saturated hues), while separate (positive) pushes hue away from the anchor toward the midpoint and slightly boosts chroma. Gains raised to a plausibly-real ceiling and calibrated.

**Tech Stack:** C++20 (dfee_core, pybind), Python (FastAPI server.py, dfee_native_bridge.py, pytest), React (frontend/src/App.jsx), yaml-cpp, CTest.

## Global Constraints

- filmic_v2 only; `parity_v1` stays bit-for-bit reproducible; non-zero `palette_range` under `parity_v1` is rejected (inherited from the existing parity guard — it keys off the four control values, so it must include `palette_range`).
- Bipolar `−100..+100`, neutral `0` = stock default (no-op); omitted request field -> 0.
- At `palette_range == 0`, output is byte-identical to the pre-slice render (primary regression guard).
- Plausibly-real ceiling: extremes look like a real film palette pushed hard, never broken; neutral (near-gray) pixels are always preserved via the chroma gate; hue shifts stay wrap-stable via `sin(delta)`.
- Applied ONLY in `apply_color_response_and_coupling_pipeline` (sole live path); monochrome stocks skip the colour stage in `render()` and resolve sensitivity to 0.
- Native profile loader rejects unknown YAML leaves; the renamed sensitivity key must be re-registered.
- Preview and export carry identical values; report JSON records requested input + resolved sensitivity.
- Primary UI label scene-agnostic; one-click neutral reset; colour controls disabled for monochrome stocks.

---

## File Structure

Rename surface (behaviour-preserving, Task 1) — `palette_separation` -> `palette_range`, `palette_separation_sensitivity` -> `palette_range_sensitivity`, YAML key `color_character.palette.separation_sensitivity` -> `color_character.palette.range_sensitivity`:
- `cpp_engine/include/dfee/solver.hpp` (SolverControls, FilmResponsePlan)
- `cpp_engine/include/dfee/bridge_types.hpp` (NativePreviewRenderRequest)
- `cpp_engine/src/session.cpp` (build_solver_controls x2 sites, report JSON, parity guard)
- `cpp_engine/src/solver.cpp` (ColorCharacterDefaults field, family defaults, population, YAML key)
- `cpp_engine/src/profile.cpp` (loader whitelist key)
- `cpp_engine/src/renderer.cpp` (sub-pass + constants)
- `cpp_engine/bindings/python/dfee_native_module.cpp` (dict key)
- `dfee_native_bridge.py` (dataclass field)
- `server.py` (preview param + ExportRequest field + validation loop + native dict)
- `frontend/src/App.jsx` (DEFAULT_PARAMS, set() float list, send-sites, slider)
- `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`, `tests/test_server_errors.py`
- `profiles/stocks/velvia_50.yaml`, `portra_400.yaml`, `colorplus_200.yaml`, `tri_x_400.yaml` (YAML key)
- Docs: `cpp_engine/migration_docs/STOCK_PROFILE_CONTRACT.md`, `FILM_LAB_WORKFLOW.md`, `FILM_LAB_IMPLEMENTATION_PLAN.md`, `README.md`, `docs/technical_architecture.md`

Behaviour change (Task 2): `cpp_engine/src/renderer.cpp` sub-pass + constants, and `cpp_engine/tests/test_core.cpp` palette tests.

Do NOT edit the historical specs/plans (`docs/superpowers/specs/2026-07-13-color-character-m7-003-design.md`, `docs/superpowers/plans/2026-07-13-color-character-m7-003.md`) — they are point-in-time records.

**Build/test commands (Windows, repo root d:/Codebases/DFEE):**
- Native build: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_native dfee_tests`
- Native tests: `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure` (`-R palette` for one case)
- Python: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`
- Frontend: `cd frontend && npm run build && npm run lint`

---

## Task 1: Rename palette_separation -> palette_range (behaviour-preserving)

Pure rename across the stack. No behaviour change; all existing palette tests still pass after their field references are renamed.

**Files:** every file in the rename surface above except the behaviour-only items.

**Interfaces:**
- Produces: canonical field `palette_range` (float, default 0.0) and resolved `palette_range_sensitivity` on every layer; YAML leaf `color_character.palette.range_sensitivity`.
- Consumes: existing `palette_separation` plumbing (M7-003).

- [ ] **Step 1: Rename in C++ headers.** In `solver.hpp` rename `SolverControls::palette_separation` -> `palette_range`, `FilmResponsePlan::palette_separation` -> `palette_range`, `FilmResponsePlan::palette_separation_sensitivity` -> `palette_range_sensitivity`. In `bridge_types.hpp` rename `NativePreviewRenderRequest::palette_separation` -> `palette_range`.

- [ ] **Step 2: Rename in C++ sources.** In `session.cpp` (both `controls.palette_separation = request.palette_separation;` sites, the report-JSON keys `"palette_separation"`/`"palette_separation_sensitivity"`, and the parity-guard condition term), `solver.cpp` (`ColorCharacterDefaults` member, `color_character_defaults` return values, the population line, and the YAML key string `"color_character.palette.separation_sensitivity"` -> `"color_character.palette.range_sensitivity"`), `renderer.cpp` (local `n_sep`/comments — rename to `n_range`; the field reads `response.palette_separation`/`response.palette_separation_sensitivity`), `profile.cpp` (whitelist string `"color_character.palette.separation_sensitivity"` -> `"color_character.palette.range_sensitivity"`), `dfee_native_module.cpp` (`dict_float(dict, "palette_separation")` -> `"palette_range"` and assignment target). Report JSON keys become `"palette_range"` and `"palette_range_sensitivity"`.

- [ ] **Step 3: Rename in Python + React.** `dfee_native_bridge.py` field `palette_separation: float = 0.0` -> `palette_range`. `server.py`: preview query param, `ExportRequest` field, the range-validation loop tuple entry, the parity-guard tuple entry, and the native request dict key — all `palette_separation` -> `palette_range`. `frontend/src/App.jsx`: `DEFAULT_PARAMS`, the `set()` float-parse list entry, both send-sites (preview query + export body), and the slider `key`/label references.

- [ ] **Step 4: Rename in tests.** In `test_core.cpp`, `tests/test_native_bridge.py`, `tests/test_server_errors.py`: rename every `palette_separation` occurrence to `palette_range` and `palette_separation_sensitivity` -> `palette_range_sensitivity`. Do NOT change any assertion values or logic — behaviour is unchanged in this task.

- [ ] **Step 5: Rename YAML keys.** In `profiles/stocks/velvia_50.yaml`, `portra_400.yaml`, `colorplus_200.yaml`, `tri_x_400.yaml`, rename the `palette: { separation_sensitivity: ... }` leaf to `range_sensitivity`.

- [ ] **Step 6: Build native + run full CTest.** Run the native build then `ctest ... --output-on-failure`. Expected: PASS (all renamed palette tests green — behaviour identical). If the loader rejects a stock, a YAML key was missed.

- [ ] **Step 7: Run Python + frontend.** `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q` and `cd frontend && npm run build && npm run lint`. Expected: all PASS.

- [ ] **Step 8: Commit.**

```bash
git add -A
git commit -m "Rename palette_separation -> palette_range across stack (behaviour-preserving)"
```

---

## Task 2: Bipolar Palette Range algorithm (merge + separate)

Replace the hue-only sub-pass with the bipolar merge/separate algorithm.

**Files:**
- Modify: `cpp_engine/src/renderer.cpp` (constants + the `if (n_range != 0.0F)` sub-pass in `apply_color_response_and_coupling_pipeline`)
- Test: `cpp_engine/tests/test_core.cpp`

**Interfaces:**
- Consumes: `FilmResponsePlan.palette_range`, `.palette_range_sensitivity`, `.palette_anchors`, `.palette_anchor_weights`; helpers `smoothstep01(lo,hi,x)`, `nearest_anchor_delta(h,anchors)` (returns `{delta,index}`), `wrap_angle_positive`.
- Produces: hue + chroma modulation. Merge desaturates; separate boosts chroma; both gated by chroma gate `g_c` and anchor weight.

- [ ] **Step 1: Write failing tests** in `test_core.cpp`. Add `test_palette_range_merge_reduces_hue_spread_and_chroma` and `test_palette_range_separate_increases_hue_spread`. Build a fixture with several distinct saturated hues plus one neutral-gray pixel, rendered via `FilmRenderer::apply_color_response_and_coupling` with `palette_range_sensitivity = 1.0F` and default anchors. Assert:

```cpp
// MERGE (palette_range = -100): output hues cluster tighter AND mean chroma drops.
//  - compute circular spread (e.g. 1 - |mean unit vector|) of the saturated pixels' output hues
//    at range=0 (baseline) vs range=-100; merged spread < baseline spread.
//  - mean output chroma of the saturated pixels at range=-100 < at range=0.
//  - the neutral-gray pixel is unchanged (chroma delta < 1e-5, hue effectively unchanged) at range=-100.
// SEPARATE (palette_range = +100): output hue spread > baseline spread.
// NEUTRAL (palette_range = 0): output byte-identical to baseline (reuse the existing no-op guard pattern).
// WRAP STABILITY: two saturated pixels at hue ~359deg and ~1deg near a 0-rad anchor both behave
//   continuously under merge (no sign flip / no jump > 0.5 rad).
```

Use build-agnostic `throw std::runtime_error`. Reuse the hue/chroma read helpers the existing palette tests use.

- [ ] **Step 2: Run to verify they fail.** Build + `ctest ... -R palette`. Expected: FAIL (current code only shifts hue toward anchor with a single gain, does not desaturate on merge nor boost/spread on separate).

- [ ] **Step 3: Replace the palette constants.** In the anonymous namespace of `renderer.cpp`, replace `kPaletteSepGain` (and keep `kPaletteChromaLo`/`kPaletteChromaHi`) with:

```cpp
constexpr float kPaletteMergeHueGain = 0.90F; // max radians pulled toward anchor at full merge
constexpr float kPaletteSepHueGain   = 0.60F; // max radians pushed away from anchor at full separate
constexpr float kPaletteMergeDesat   = 0.55F; // max fractional chroma reduction at full merge
constexpr float kPaletteSepChroma    = 0.25F; // max fractional chroma gain at full separate
constexpr float kPaletteChromaLo     = 0.02F; // neutral-preserving chroma gate lower edge
constexpr float kPaletteChromaHi     = 0.06F; // gate upper edge
```

- [ ] **Step 4: Replace the sub-pass.** Replace the current `if (n_sep != 0.0F) { ... }` block (now `n_range` after Task 1) with the bipolar algorithm. It must modify BOTH `h_new` and `c_new` (c_new is consumed by the final `oklch_to_oklab_pixel({lch.l, std::max(c_new,0.0F), h_new})`):

```cpp
        const float n_range = std::clamp(response.palette_range / 100.0F, -1.0F, 1.0F)
            * response.palette_range_sensitivity;
        if (n_range != 0.0F) {
            const float g_c = smoothstep01(kPaletteChromaLo, kPaletteChromaHi, lch.c);
            const auto [delta, nearest_idx] = nearest_anchor_delta(h_new, palette_anchors);
            const float anchor_weight = palette_weights[nearest_idx];
            if (n_range < 0.0F) {
                // MERGE: pull hue toward nearest anchor; desaturate proportional to the pull.
                const float merge = -n_range;                     // 0..1
                h_new = wrap_angle_positive(
                    h_new + kPaletteMergeHueGain * merge * g_c * anchor_weight * std::sin(delta));
                const float pull_frac = std::abs(std::sin(delta)); // 0 at anchor, 1 at quadrature
                c_new = std::max(
                    c_new * (1.0F - kPaletteMergeDesat * merge * g_c * pull_frac), 0.0F);
            } else {
                // SEPARATE: push hue away from nearest anchor (toward the midpoint); slight chroma gain.
                const float sep = n_range;                        // 0..1
                h_new = wrap_angle_positive(
                    h_new - kPaletteSepHueGain * sep * g_c * anchor_weight * std::sin(delta));
                c_new = c_new * (1.0F + kPaletteSepChroma * sep * g_c);
            }
        }
```

Note: `n_range` was declared before the loop in Task 1's rename; move its declaration inside the loop is unnecessary — keep the per-loop computation local. If Task 1 left `n_range` computed once before the loop (as M7-003 did for `n_sep`), keep that single pre-loop computation and only replace the per-pixel block; do not compute it twice. Ensure the pre-loop `n_range` is still used.

- [ ] **Step 5: Run tests.** Build + `ctest ... -R palette`. Expected: PASS. Then run the all-neutral no-op test (`-R color_character` and the neutral-default case) — Expected: PASS (byte-identical at range=0).

- [ ] **Step 6: Full CTest.** `ctest ... --output-on-failure`. Expected: PASS (existing palette direction/neutral/wrap tests from M7-003 were renamed in Task 1 and may assert the OLD single-gain behaviour — UPDATE any that now conflict with the bipolar semantics so they assert the new merge/separate behaviour, and delete assertions that tested only the retired single-direction shift). Keep neutral-preservation, wrap-stability, and determinism assertions.

- [ ] **Step 7: Commit.**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Implement bipolar Palette Range (merge + separate) algorithm"
```

---

## Task 3: Calibrate to plausibly-real + stock anchors

Tune gains and stock calibration so extremes read as strong-but-real film palettes, and give the merge pole a discoverable signature on a calibrated stock.

**Files:**
- Modify: `cpp_engine/src/renderer.cpp` (gain constants), `cpp_engine/src/solver.cpp` (`palette_range_sensitivity` family defaults if needed), `profiles/stocks/velvia_50.yaml` (anchors)
- Test: `cpp_engine/tests/test_core.cpp` (bounds assertion)

**Interfaces:**
- Consumes: Task 2 algorithm + constants.
- Produces: calibrated constants; optionally fewer/tuned anchors on velvia_50 for a stronger merge signature.

- [ ] **Step 1: Add a boundedness test** in `test_core.cpp`: `test_palette_range_stays_plausible`. Assert that at `palette_range = -100` the mean chroma of saturated pixels drops but does NOT collapse to zero (e.g. remains > 25% of baseline) and no output pixel becomes NaN/negative; at `+100` chroma rises but stays finite and within gamut after the existing clamp. Build-agnostic `throw`.

- [ ] **Step 2: Run to verify current behaviour.** Build + `ctest ... -R palette`. If it fails because the default `kPaletteMergeDesat = 0.55F` over-collapses chroma, that is the signal to tune in Step 3.

- [ ] **Step 3: Tune constants** in `renderer.cpp` so merge is clearly visible but leaves residual chroma (colours merge and mute, not vanish) and separate is punchy but in-gamut. Adjust `kPaletteMergeHueGain`, `kPaletteMergeDesat`, `kPaletteSepHueGain`, `kPaletteSepChroma` until `test_palette_range_stays_plausible` and the Task 2 direction tests all pass. Record the chosen values in the report.

- [ ] **Step 4: Give velvia_50 a stronger merge signature.** In `profiles/stocks/velvia_50.yaml`, optionally reduce the anchor set to 3-4 tuned hues (fewer anchors = stronger "collapse into a few dominant hues" for that stock's ethereal look). Keep `anchor_weights` length matched. Confirm it still loads via full CTest.

- [ ] **Step 5: Full CTest.** `ctest ... --output-on-failure`. Expected: PASS.

- [ ] **Step 6: Commit.**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/src/solver.cpp profiles/stocks/velvia_50.yaml cpp_engine/tests/test_core.cpp
git commit -m "Calibrate Palette Range to plausibly-real range + velvia anchors"
```

---

## Task 4: UI — Palette Range slider

**Files:** Modify `frontend/src/App.jsx`. Verify: `cd frontend && npm run build && npm run lint`.

**Interfaces:**
- Consumes: the `palette_range` field (renamed in Task 1, already sent on both routes).
- Produces: a bipolar Palette Range slider with merge/separate framing.

- [ ] **Step 1: Update the slider.** The Color Character panel already has a `palette_range` slider (renamed in Task 1). Update its label to **"Palette Range"** and tooltip to describe the bipolar effect verbatim: `"Drag left to merge similar colours into a harmonised, dreamy palette; right to separate them into distinct, punchy colours."` Keep `min={-100} max={100} step={1}`, the reset-to-0 button gated on value!=0, dirty-state class, and the mono-disabled behaviour already present.

- [ ] **Step 2: Build + lint.** `cd frontend && npm run build && npm run lint`. Expected: both PASS.

- [ ] **Step 3: Commit.**

```bash
git add frontend/src/App.jsx
git commit -m "Reframe Palette Range UI slider (merge <-> separate)"
```

---

## Task 5: Route/bridge tests, docs, visual acceptance, APAM

**Files:** `tests/test_server_errors.py`, `tests/test_native_bridge.py`, docs, APAM.

- [ ] **Step 1: Route + bridge tests.** In `tests/test_server_errors.py` confirm/adjust the existing parity + range-rejection tests reference `palette_range` and still return HTTP 400 (parity_v1 + non-zero; out-of-range 150). In `tests/test_native_bridge.py` confirm the preview/export field-agreement test references `palette_range`. Run: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`. Expected: PASS. Commit if any change was needed.

- [ ] **Step 2: Docs.** Update `STOCK_PROFILE_CONTRACT.md` (rename `palette.separation_sensitivity` -> `palette.range_sensitivity` and describe the bipolar merge/separate semantics), `FILM_LAB_WORKFLOW.md` and `README.md` and `docs/technical_architecture.md` (Palette Range replaces Palette Separation, bipolar merge/ethereal <-> separate), and `FILM_LAB_IMPLEMENTATION_PLAN.md` (record Slice 1 of the Primary Film Look redesign done). Commit: `git commit -m "Document Palette Range (Film Look Slice 1)"`.

- [ ] **Step 3: Visual acceptance (human/hardware).** Render representative RAWs at `palette_range` −100 / 0 / +100 on a colour-negative and the velvia_50 reversal stock, plus a monochrome stock (must be a no-op), at preview and export. Confirm: merge yields a visibly harmonised/ethereal palette with muted-but-present colour and preserved neutrals; separate yields distinct punchy colour; neutral is unchanged; mono unaffected; preview matches export. Record images, stock, pipeline version, and accept/reject in `cpp_engine/migration_docs/BUG_TRACKER.md` (this also discharges the carried-over M7-003F colour-pipeline visual acceptance). This step needs the running app and real RAWs — flag for the human operator; do not fabricate.

- [ ] **Step 4: Timing probe (human/hardware).** Run the documented preview/export timing probe vs the named baseline with `palette_range = -100` active; confirm the added per-pixel chroma math does not regress the hot path. Record numbers with hardware, pipeline version, source dimensions.

- [ ] **Step 5: APAM.** Update the Color Character L3 record (or add a "Palette Range (Film Look Slice 1)" record) noting the rename, the bipolar merge/separate algorithm, calibrated gains, and that Slices 2-6 of the Primary Film Look redesign remain. Write an L2 episode.

---

## Self-Review Notes

- **Spec coverage:** Palette Range bipolar merge(+desaturation)/separate (Task 2), muscular gains + plausibly-real bounding (Task 3), rename to clean name (Task 1), neutral=stock-default no-op + parity rejection + monochrome no-op (Global Constraints, enforced by inherited guards + Task 2 tests), UI reframe (Task 4), report/route/docs/APAM + carried M7-003F visual acceptance (Task 5). Deferred controls (Slices 2-6) explicitly out of scope.
- **Placeholder scan:** none — algorithm code and rename lists are concrete. Task 5 Steps 3-4 are explicitly human/hardware steps, labelled as such (not code placeholders).
- **Type consistency:** field `palette_range` (float) and `palette_range_sensitivity` (float) used identically across headers, solver, session, pybind, bridge, server, React, report JSON, YAML (`color_character.palette.range_sensitivity`), and tests. Constants `kPaletteMergeHueGain/kPaletteSepHueGain/kPaletteMergeDesat/kPaletteSepChroma/kPaletteChromaLo/kPaletteChromaHi` defined once in Task 2 and only tuned (not renamed) in Task 3. `nearest_anchor_delta` returns `{delta,index}` (existing).
