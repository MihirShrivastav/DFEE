# Film-Sensible Auto Exposure ("Scene Placement") — Design Spec

**Status:** draft for review
**Date:** 2026-08-01
**Area:** `cpp_engine` tone core — `RenderPlanSolver` (auto exposure / pre-film normalization)

## Problem

"Auto balanced" currently meters the scene's robust midtone to 18% grey:

```
exposure = log2(0.18 / midtone_anchor)      // solver.cpp solve_neutral + solve() stock path
```

capped by a single p98 ceiling at 0.82 linear. This is an **averaging meter**: it is
blind to scene key, so it brightens normal/low-key scenes until diffuse highlights
(skies, water) punch through the film shoulder and clip. Combined with the
with-stock auto compensations (shadow-lift + highlight-recovery + contrast), it
compresses the tonal range — producing a flat, **HDR-like** result, the opposite of
film. Observed on real RAWs: sea+house frame blows the sky; loco blows a bit; only
already-dark scenes (lighthouse) survive.

Film does the opposite: a fixed characteristic curve (toe → straight line →
shoulder) where highlights **roll off gracefully on the shoulder** instead of
clipping, and negative stock has wide over-exposure latitude ("expose for the
shadows"). The auto system must **place** the scene into that latitude and let the
curve **shape** it — not re-expose to a target and stretch.

## Principles

1. **Anchor on diffuse highlights, not the midtone.** Place the robust diffuse
   highlight just below the shoulder knee; let the shoulder + Highlight rolloff do
   the compression. This is the single most important change.
2. **Key-aware.** A soft midtone term keeps dim scenes from staying crushed, but it
   never overrides the highlight anchor. Low-key stays low-key; high-key stays
   bright.
3. **Asymmetric latitude by stock.** Negative → protect shadows, trust the shoulder
   (higher highlight target). Reversal → protect highlights hard (lower target).
   Mono → middle. Neutral/no-stock → most conservative (no shoulder to save it).
4. **Trust the curve — stop stretching.** In the auto path, drop the DR-expanding
   shadow-lift / highlight-recovery / contrast compensations; keep only genuine
   clipped-channel recovery. The film S-curve provides contrast.
5. **Robust & consistent.** Percentile-based, specular-rejecting, tightly bounded,
   stable across a shoot.

## Available inputs (already computed by the analyzer, reach the solver via `tonal`)

`luma_p05, p25, p50, p95, p98, p99, p995`, `midtone_anchor`, `dynamic_range_stops`,
`highlight_headroom`, `shadow_depth`, `tonal_skew` ("low_key"/"normal"/"high_key"),
`spatial.specular_point_ratio`, `spatial.large_highlight_area_ratio`, and
`stock_type`. No new analyzer work required.

## Algorithm

All math in linear scene-referred luma. Compute a single `exposure_comp` (stops).

### 1. Robust diffuse-highlight level
```
diffuse_hl = lerp(luma_p95, luma_p98, clamp(large_highlight_area_ratio*k_area, 0, 1))
```
- p95 is the diffuse-bright level (skies/water). When the bright region is *large*
  and diffuse (big sky), nudge toward p98 so we place the whole sky, not just its
  darker edge. Speculars (top ~1–2%) are intentionally excluded — never anchor on
  p99/p995.
- `k_area ≈ 1.5` (tunable).

### 2. Per-stock highlight target (shoulder knee, linear)
| stock_type | `hl_target` | rationale |
|---|---|---|
| ColorNegative | 0.74 | wide latitude, trust shoulder, expose for shadows |
| Monochrome | 0.72 | middle |
| ColorReversal | 0.64 | narrow latitude, protect highlights hard |
| neutral (no stock) | 0.68 | no shoulder to roll off — stay conservative |

### 3. Highlight-anchored headroom (the ceiling that actually bites)
```
hl_comp = log2(hl_target / max(diffuse_hl, eps))
```
- `diffuse_hl` already bright ⇒ `hl_comp ≤ 0` (no up-push; gentle pull-down allowed).
- Scene dim ⇒ `hl_comp > 0`, but only until diffuse highlights reach the knee.

### 4. Soft key-aware midtone term
```
mid_target = 0.15                         // film-ish, slightly under 18% for richer midtones
mid_comp   = log2(mid_target / max(midtone_anchor, eps))
```

### 5. Combine — highlight-priority, never exceed the ceiling
```
if mid_comp >= 0:                          // scene wants brightening
    exposure_comp = min(mid_comp, max(hl_comp, 0))   // lift toward midtone, capped at HL knee
else:                                       // scene is bright / over
    exposure_comp = max(mid_comp, hl_comp) * k_down  // gentle pull-down, trust over-exposure latitude
```
- `k_down ≈ 0.6` (negative stock tolerates over-exposure; don't slam bright scenes dark).
- This makes the sea/house case: `diffuse_hl` high ⇒ `hl_comp ≤ 0` ⇒ `exposure_comp`
  small/negative ⇒ sky preserved. Lighthouse: `diffuse_hl` low ⇒ lift allowed. Loco:
  bounded by the p95 knee at 0.74 instead of p98 at 0.82.

### 6. Bounds & adaptation
```
exposure_comp = clamp(exposure_comp * adaptation_mult, -1.5, +2.0)
```
(down-latitude for negatives is smaller than up-latitude; keep the asymmetric clamp.)

### 7. Trust the curve (remove stretching from the AUTO path)
In `solve()` stock path, when `exposure_intent == "Auto"`/"Preserve":
- **Drop** the auto `contrast_comp`, `highlights_comp`, `shadows_comp`,
  `midtones_comp` DR-expanding terms from the auto path (they cause the flat look).
- **Keep** `highlight_channel_recovery` (real clipped-channel repair) and the
  colour-cast normalization — those are corrections, not stretching.
- Manual Light-tab sliders are unaffected (they're added later in
  `apply_pre_film_preview_sliders`).

### 8. "As shot" / Preserve
Unchanged in intent: `exposure_comp` from placement is skipped (or heavily
attenuated), trusting the input's exposure. Rendered/TIFF inputs already default to
As shot.

## Parameters (all tunable; live in stock profile `adaptation.*` with these defaults)
`hl_target` (per stock, above), `mid_target=0.15`, `k_area=1.5`, `k_down=0.6`,
clamp `[-1.5, +2.0]`. Exposed as profile numerics so stocks can override.

## Acceptance tests

**Synthetic (native `ctest`):**
- Bright-sky scene (high p95, low midtone): `exposure_comp ≤ 0.15` stops (no blow).
- Dim scene (low p95): `exposure_comp` lifts diffuse_hl to within 0.05 of `hl_target`.
- Over-bright scene (p95 ≫ target): gentle negative comp, not a hard slam.
- Reversal stock: `hl_target` lower ⇒ smaller up-push than negative on same scene.
- Auto path no longer emits large shadow/contrast compensations (assert ~0).
- Endpoints & neutral-grey invariants unchanged (no colour shift from exposure).

**Visual (human, the four reference frames):** loco, lighthouse, sea+house
(portrait + landscape). Sky/water must retain highlight detail with no stock; with
a stock the shoulder should roll highlights, not clip; midtones stay contrasty
(not flat/HDR); look consistent across the set.

## Risks
- Tone core is shared with the Python/React stack and has existing tests — update
  tests, keep `parity_v1`/`filmic_v2` byte-identical (gate changes to filmic_v3 /
  the subtractive pipeline as the current code already does).
- Must not introduce banding/halos; placement is a single global exposure scalar so
  it cannot band. Removing auto shadow-lift reduces (not adds) processing.

## Out of scope (future)
Explicit per-stock H&D curve solve (optimise placement within modelled
toe/straight/shoulder). This spec is the robust heuristic that gets 90% of the way.
