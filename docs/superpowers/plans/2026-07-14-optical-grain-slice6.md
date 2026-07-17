# Material Finish — Grain / Halation / Bloom (Film Lab v1 — Slice 6) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild film grain as a Soft-Light, spatially-uniform, resolution-scaled fine-grain effect (fixing the blotchy/invisible grain), make halation a real visible Threshold+Strength effect, and surface Grain/Halation/Bloom as first-class Material Finish controls.

**Architecture:** Rewrite `FilmRenderer::apply_filmic_grain` to generate a fine resolution-scaled noise field and composite it via **Soft Light** (interacts with tones), **removing the spatial `grain_receptivity` suppression** (the blotch cause) while keeping the smooth per-luminance tonal weighting. Rework the halation half of `apply_filmic_halation_bloom` to key off a controllable luminance **threshold** and **strength** with a resolution-scaled red-orange glow. Add `halation_strength` / `halation_threshold` controls; surface `grain_strength/size/roughness` and `bloom`. `filmic_v2`/`parity_v1` grain+halation paths (`apply_film_grain`, the non-filmic halation) stay untouched.

**Tech Stack:** C++20 (dfee_core, pybind), OpenCV, Python (server.py, dfee_native_bridge.py, pytest), React, CTest.

## Global Constraints

- `filmic_v3` (and `filmic_v2`, which shares the filmic grain/halation path) get the new behaviour; `parity_v1` and the legacy `apply_film_grain` / non-filmic halation are byte-identical.
- **Grain anti-blotch invariant:** grain is spatially uniform — NO weighting by local image detail/edges/texture. Grain strength is modulated only by the pixel's own luminance (smooth per-tone) via the Soft-Light taper + the stock `grain_*_response` fields. Never reintroduce a neighbourhood/receptivity spatial term.
- Grain determinism preserved (stable seed + cached noise field).
- Halation controls: `halation_strength` (0..200, 100 = stock default, 0 = off; server-clamped), `halation_threshold` (0..100, default 50; lower = more highlights bloom). `filmic_v3` only for the numeric halation controls; the Auto/Off/Low/High enum path stays for compatibility.
- Grain/Bloom controls reuse the existing `grain_strength` / `grain_size` / `grain_roughness` / `bloom` request fields.
- Preview must represent export: grain physical size scales with render width (`scale_factor`); raise the `filmic_v3` preview JPEG quality so fine grain survives.

---

## File Structure

- `cpp_engine/src/renderer.cpp` — rewrite `apply_filmic_grain`; rework halation in `apply_filmic_halation_bloom`; add a `soft_light` helper.
- `cpp_engine/include/dfee/solver.hpp` — `SolverControls.halation_strength`, `.halation_threshold`; `MaterialEffectsPlan.halation_threshold` (+ solver scales `halation_strength`).
- `cpp_engine/include/dfee/bridge_types.hpp` — `NativePreviewRenderRequest.halation_strength`, `.halation_threshold`.
- `cpp_engine/src/solver.cpp` — resolve effective halation strength (stock × control) + threshold; defaults.
- `cpp_engine/src/session.cpp` — map controls; report JSON; raise filmic_v3 preview JPEG quality (or in the native preview encode).
- `cpp_engine/bindings/python/dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py` — plumb + clamp.
- `frontend/src/App.jsx` — Material Finish panel (Grain strength/size/roughness, Halation strength/threshold, Bloom).
- `cpp_engine/tests/test_core.cpp`, `tests/test_native_bridge.py`, `tests/test_server_errors.py`.
- Docs: `documentation/`, `README.md`, `docs/technical_architecture.md`, `STOCK_PROFILE_CONTRACT.md`.

**Canonical names:** `halation_strength`, `halation_threshold` (float; strength default 100.0, threshold default 50.0); `soft_light(a,b)` helper; grain rewrite keeps `apply_filmic_grain(const Image&, const SpatialMasks&, const MaterialEffectsPlan&)`.

**Build/test (Windows, repo root d:/Codebases/DFEE):**
- Native: `cmake --build cpp_engine/out/build/windows-msvc-vcpkg --config Release --target dfee_tests dfee_native`
- Native tests: `ctest --test-dir cpp_engine/out/build/windows-msvc-vcpkg -C Release --output-on-failure`
- Python: `python -m pytest tests/test_native_bridge.py tests/test_server_errors.py -q`
- Frontend: `cd frontend; npm run build; npm run lint`

---

## Task 1: Grain rebuild (Soft-Light, uniform, fine, visible)

**Files:** `cpp_engine/src/renderer.cpp`, `cpp_engine/tests/test_core.cpp`.

**Interfaces:**
- Produces: rewritten `apply_filmic_grain` (same signature); `soft_light(float,float)` helper.

- [ ] **Step 1: Add the Soft-Light helper** in the anonymous namespace of `renderer.cpp` (near the other helpers):

```cpp
// Pegtop soft-light blend: base a, blend layer b, both in [0,1].
[[nodiscard]] inline float soft_light(const float a, const float b) {
    return (1.0F - 2.0F * b) * a * a + 2.0F * b * a;
}

// filmic_v3 grain amplitude (soft-light layer): maps grain_strength to a visible amount.
constexpr float kGrainSoftLightAmp = 0.20F;
```

- [ ] **Step 2: Write the failing native test** `test_filmic_grain_uniform_softlight` in `test_core.cpp`:
  - Build a flat mid-gray image (e.g. 64×64 at linear ~0.22) and a flat gray image with a bright and a dark patch. Provide a `SpatialMasks` whose `grain_receptivity_mask` is DELIBERATELY non-uniform (0 in one half, 1 in the other) and `MaterialEffectsPlan` with `grain_strength = 1.0`, `grain_chroma_strength = 0` (mono), a fixed `grain_seed`.
  - Assert: (a) grain is **visible** — the flat mid-gray output has variance clearly above zero; (b) **spatially uniform** — the grain variance in the receptivity=0 half is within ~20% of the receptivity=1 half (proves grain is NOT gated by the spatial mask — the anti-blotch invariant); (c) **soft-light taper** — grain variance on a near-black patch and a near-white patch is markedly lower than on the mid-gray patch; (d) **determinism** — two calls with the same seed produce byte-identical output.
  Build-agnostic `throw`. Register in `main()`.

- [ ] **Step 3: Run to verify it fails** (current grain is additive, weak, and gated by the receptivity mask → uniformity/visibility assertions fail).

- [ ] **Step 4: Rewrite the noise generation** in `apply_filmic_grain` — replace the `generate_filmic_grain_noise_channel` / sparse-clump block (renderer.cpp ~1821–1888) with a fine, resolution-scaled field:

```cpp
    } else {
        std::mt19937_64 rng(static_cast<std::uint64_t>(grain_seed) ^ 0xD1B54A32D192ED03ULL);
        // Grain cell size: physical, scaled to render width; grain_size drives a small blur.
        const float grain_cell = std::clamp(effects.grain_size, 0.05F, 1.5F) * std::max(scale_factor, 0.35F);
        const float grain_sigma = std::clamp(0.30F + grain_cell * 0.75F, 0.30F, 3.0F);
        const float roughness = std::clamp(effects.grain_roughness, 0.0F, 1.0F);

        auto make_fine = [&](std::mt19937_64& r) {
            cv::Mat m = make_standard_normal_mat(h, w, r);
            if (grain_sigma > 0.35F) {
                cv::GaussianBlur(m, m, cv::Size(0, 0), grain_sigma);
            }
            normalize_zero_mean_unit_variance(m);
            // roughness shapes particle hardness (sign-preserving contrast), not a sharpen.
            if (roughness > 0.0F) {
                const float p = 1.0F / (0.65F + roughness * 0.9F);
                for (int yy = 0; yy < m.rows; ++yy) {
                    float* row = m.ptr<float>(yy);
                    for (int xx = 0; xx < m.cols; ++xx) {
                        const float v = row[xx];
                        row[xx] = std::copysign(std::pow(std::fabs(v), p), v);
                    }
                }
                normalize_zero_mean_unit_variance(m);
            }
            return m;
        };

        cv::Mat mono = make_fine(rng);
        if (is_mono) {
            noise_r = mono; noise_g = mono; noise_b = mono;
        } else {
            const float base_chroma = clampf(effects.grain_chroma_strength * 2.2F, 0.0F, 1.0F);
            const float layer_correlation = std::clamp(effects.grain_layer_correlation, 0.0F, 1.0F);
            const float chroma_mix = base_chroma * std::clamp(0.18F + (1.0F - layer_correlation) * 0.55F, 0.12F, 0.65F);
            cv::Mat ind_r = make_fine(rng);
            cv::Mat ind_b = make_fine(rng);
            noise_g = mono;
            noise_r = (1.0F - chroma_mix) * mono + chroma_mix * ind_r;
            noise_b = (1.0F - chroma_mix) * mono + chroma_mix * ind_b;
            normalize_zero_mean_unit_variance(noise_r);
            normalize_zero_mean_unit_variance(noise_b);
        }
        filmic_grain_noise_cache = GrainNoiseCacheEntry{ .key = cache_key, .noise_r = noise_r, .noise_g = noise_g, .noise_b = noise_b };
    }
```

- [ ] **Step 5: Rewrite the application loop** (renderer.cpp ~1898–1972) to Soft-Light + uniform + real amplitude, keeping the tonal `density_mod` but DROPPING `texture_mod`/`grain_receptivity`:

Replace `strength_base` with the soft-light amplitude and remove the receptivity read:

```cpp
    const float pgi_visibility = std::clamp(effects.grain_target_pgi / 40.0F, 0.55F, 1.55F);
    const float stock_visibility = pgi_visibility * std::clamp(0.82F + effects.grain_midtone_response * 0.18F, 0.65F, 1.25F);
    const float amp_base = std::max(0.0F, effects.grain_strength) * kGrainSoftLightAmp * stock_visibility;
    constexpr std::array<float, 3> kAmpMults{0.94F, 1.00F, 1.08F};
    static const auto kGammaEncodeLut = build_power_lut(1.0F / 2.2F);
    static const auto kGammaDecodeLut = build_power_lut(2.2F);
```

Delete the `const auto& grain_receptivity = ...;`, `texture_masking`, `smooth_mod`, and `texture_mod` lines. Keep the `density_mod` computation (the tonal zone weighting) unchanged. Then the per-pixel blend becomes:

```cpp
            const float exposure_mod = std::max(0.0F, density_mod); // tonal only; no spatial term
            const float gamma_r = sample_unit_lut(kGammaEncodeLut, rgb_linear.pixels[base + 0]);
            const float gamma_g = sample_unit_lut(kGammaEncodeLut, rgb_linear.pixels[base + 1]);
            const float gamma_b = sample_unit_lut(kGammaEncodeLut, rgb_linear.pixels[base + 2]);
            const float bl_r = clamp01(0.5F + noise_r_row[x] * amp_base * kAmpMults[0] * exposure_mod);
            const float bl_g = clamp01(0.5F + noise_g_row[x] * amp_base * kAmpMults[1] * exposure_mod);
            const float bl_b = clamp01(0.5F + noise_b_row[x] * amp_base * kAmpMults[2] * exposure_mod);
            out.pixels[base + 0] = sample_unit_lut(kGammaDecodeLut, clamp01(soft_light(gamma_r, bl_r)));
            out.pixels[base + 1] = sample_unit_lut(kGammaDecodeLut, clamp01(soft_light(gamma_g, bl_g)));
            out.pixels[base + 2] = sample_unit_lut(kGammaDecodeLut, clamp01(soft_light(gamma_b, bl_b)));
```

The `y_gamma` / zone / `density_mod` lines between remain (they are the smooth per-luminance modulation). Note `spatial_masks` is now only used for the dimension check — keep the check.

- [ ] **Step 6: Run the grain test + full CTest** — Expected: PASS (visible, uniform across the receptivity halves, soft-light taper, deterministic). If `generate_filmic_grain_noise_channel` / `make_sparse_master` become unused, leave them (used by the legacy `apply_film_grain`); if truly unused, remove.

- [ ] **Step 7: Commit**

```bash
git add cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Rebuild filmic grain: soft-light blend, spatially uniform, resolution-scaled"
```

---

## Task 2: Preview grain fidelity (JPEG quality)

**Files:** `cpp_engine/src/session.cpp` (native preview JPEG encode).

- [ ] **Step 1: Find the preview JPEG encode** in `session.cpp` (grep `IMWRITE_JPEG_QUALITY` / `imencode` in the render_preview path). Raise the quality for the filmic subtractive pipeline so fine grain survives DCT: use quality 95 when `is_subtractive_effect_pipeline(request.effect_pipeline_version)`, else the existing value.

```cpp
    const int preview_jpeg_quality = is_subtractive_effect_pipeline(request.effect_pipeline_version) ? 95 : <existing>;
    // ... pass preview_jpeg_quality into the imencode params vector
```

- [ ] **Step 2: Build + quick bridge check** — render a filmic_v3 preview with grain High and confirm no error; (visual confirmation happens in Task 6). Full CTest still green.

- [ ] **Step 3: Commit**

```bash
git add cpp_engine/src/session.cpp
git commit -m "Raise filmic_v3 preview JPEG quality so fine grain survives"
```

---

## Task 3: Halation rebuild + schema (Threshold + Strength)

**Files:** `solver.hpp`, `bridge_types.hpp`, `session.cpp`, `solver.cpp`, `renderer.cpp`, `cpp_engine/tests/test_core.cpp`.

- [ ] **Step 1: Add controls** — `SolverControls.halation_strength = 100.0F`, `SolverControls.halation_threshold = 50.0F`; `MaterialEffectsPlan.halation_threshold = 0.58F`; `NativePreviewRenderRequest.halation_strength = 100.0F`, `.halation_threshold = 50.0F`.

- [ ] **Step 2: Map + resolve.** In `session.cpp` both control-build sites: `controls.halation_strength = request.halation_strength; controls.halation_threshold = request.halation_threshold;`. In `solver.cpp` where `MaterialEffectsPlan.halation_strength` is set, scale it by the control under filmic behaviour: `halation_strength_effective = stock_halation_strength * (controls.halation_strength / 100.0F)`, and set `plan.material_effects.halation_threshold = 0.72F - (controls.halation_threshold / 100.0F) * 0.30F` (control 0→0.72 high threshold/less bloom, 100→0.42 low threshold/more bloom; 50→0.57 ≈ current). Populate report fields.

- [ ] **Step 3: Write the failing native test** `test_halation_threshold_and_strength` in `test_core.cpp`: build an image with a small bright disc on a mid-gray field; run `apply_filmic_halation_bloom` with `bloom_strength=0`, `halation_strength=0.6`, `halation_threshold=0.5`, warm_core red-orange. Assert: (a) total added red energy in the ring around the disc is > with `halation_strength=0`; (b) a lower `halation_threshold` (more of the highlights qualify) increases the glow energy vs a higher threshold; (c) the glow is red-orange (added red > added blue around the source). Build-agnostic `throw`.

- [ ] **Step 4: Rework the halation source + apply** in `apply_filmic_halation_bloom` (renderer.cpp ~1564–1628): use `effects.halation_threshold` in the highlight/excess computation instead of the hardcoded `0.66`/`0.58`, and when `halation_strength` is meaningfully above the stock baseline, do not hard-gate to `specular_only` (let the threshold define the source). Concretely: `const float ht = effects.halation_threshold; const float highlight = smoothstep01((y_luma - ht) / 0.20F); const float excess = std::max(0.0F, y_luma - (ht - 0.08F));` and set `halation_source = source * (0.40F + 0.60F * source);` (drop the `specular_only ? halation_mask : source` gate for the filmic path so the threshold controls it). Keep the resolution-scaled radii and the warm_core/red_fringe apply; raise the apply gains so it is visible (e.g. core `0.42F → 0.60F`, fringe `0.18F → 0.28F`).

- [ ] **Step 5: Run the halation test + full CTest** — Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add cpp_engine/include/dfee/solver.hpp cpp_engine/include/dfee/bridge_types.hpp cpp_engine/src/session.cpp cpp_engine/src/solver.cpp cpp_engine/src/renderer.cpp cpp_engine/tests/test_core.cpp
git commit -m "Rebuild halation: threshold + strength controls, visible red-orange glow"
```

---

## Task 4: Contract plumbing (halation fields) + report

**Files:** `dfee_native_module.cpp`, `dfee_native_bridge.py`, `server.py`, `session.cpp` (report), `tests/test_native_bridge.py`.

- [ ] **Step 1: pybind** — `request.halation_strength = dict_float(dict, "halation_strength", 100.0F); request.halation_threshold = dict_float(dict, "halation_threshold", 50.0F);`.
- [ ] **Step 2: bridge dataclass** — `halation_strength: float = 100.0`, `halation_threshold: float = 50.0`.
- [ ] **Step 3: server** — preview query params + `PreviewRequest`/`ExportRequest` fields; native dicts `"halation_strength": max(0.0, min(200.0, halation_strength))`, `"halation_threshold": max(0.0, min(100.0, halation_threshold))` (preview) and `req.`-forms (export).
- [ ] **Step 4: report JSON** — emit `halation_strength` + `halation_threshold` in the material_effects report block.
- [ ] **Step 5: bridge round-trip test** — defaults (100.0 / 50.0) + a non-default round-trip.
- [ ] **Step 6: Build both targets + run bridge/native tests** — Expected: PASS.
- [ ] **Step 7: Commit** `git commit -m "Plumb halation_strength/threshold across the contract stack"`.

---

## Task 5: Material Finish UI

**Files:** `frontend/src/App.jsx`. Verify: `cd frontend; npm run build; npm run lint`.

- [ ] **Step 1: Defaults + parse.** Add `halation_strength: 100`, `halation_threshold: 50` to `DEFAULT_PARAMS`; add both to the `set()` `parseFloat` list. (`grain_strength`/`grain_size`/`grain_roughness`/`bloom` already exist in params.)
- [ ] **Step 2: Send on both routes** — add `halation_strength`, `halation_threshold` to preview query + export body (grain/bloom already sent).
- [ ] **Step 3: Material Finish panel.** In the existing "Material Finish" group, add: **Grain Strength / Grain Size / Grain Roughness** sliders (map to `grain_strength`/`grain_size`/`grain_roughness` — check current ranges; use the existing grain control ranges), **Halation Strength** (0–200, default 100, reset-100) + **Halation Threshold** (0–100, default 50, reset-50), **Bloom** (existing `bloom` range). Reuse the slider markup patterns.
- [ ] **Step 4: Build + lint** — Expected: PASS.
- [ ] **Step 5: Commit** `git commit -m "Add Material Finish controls: Grain, Halation (strength/threshold), Bloom"`.

---

## Task 6: Route tests, docs, visual acceptance, APAM

- [ ] **Step 1: Route tests** — `tests/test_server_errors.py`: preview accepts + clamps `halation_strength` (500→200) and `halation_threshold` (150→100). Run pytest. Expected: PASS.
- [ ] **Step 2: Docs** — `documentation/planning/film-lab-v1-project-plan.md` (mark Slice 6 done), `README.md` + `docs/technical_architecture.md` (grain rebuild + halation controls + preview quality), `STOCK_PROFILE_CONTRACT.md` (halation controls modulate existing `halation.*`; no new fields). Commit.
- [ ] **Step 3: Visual acceptance (human/hardware).** Render a mid-tone RAW at grain 0/High and compare grain to the Kodachrome reference (fine, uniform, interacting, no blotches; preview ≈ export crop). Render a backlit/neon RAW varying `halation_strength` + `halation_threshold` (visible red-orange glow, threshold controls which highlights bloom). Render bloom. Record in `cpp_engine/migration_docs/BUG_TRACKER.md`; produce before/after strips.
- [ ] **Step 4: Timing probe (human/hardware).** Grain/halation are the heaviest optical stages; confirm preview/export timing vs baseline.
- [ ] **Step 5: APAM** — L3 record + episode for the grain rebuild (soft-light/uniform) + halation controls.

---

## Self-Review Notes

- **Spec coverage:** grain soft-light blend + uniform (anti-blotch) + amplitude + resolution-scaled size (Task 1), preview fidelity (Task 2), halation threshold+strength visible red-orange (Task 3), contract plumbing + report (Task 4), Material Finish UI incl. grain trio + bloom (Task 5), route/docs/visual/APAM (Task 6). Non-goals (no volumetric grain, no new grain YAML, no analog artifacts) respected. Anti-blotch invariant enforced by the Task 1 uniformity test.
- **Placeholder scan:** none — grain code is concrete; halation Step 4 gives exact threshold formulas/gains; Task 6 Steps 3-4 are explicit human steps. One lookup left to the implementer (existing preview JPEG quality value / grain control ranges) — bounded and named.
- **Type consistency:** `halation_strength` (default 100.0) / `halation_threshold` (default 50.0) identical across SolverControls, MaterialEffectsPlan (threshold resolved), request DTO, pybind, bridge, server, report, React. `soft_light(float,float)` and `kGrainSoftLightAmp` defined once in Task 1. `apply_filmic_grain` signature unchanged.
