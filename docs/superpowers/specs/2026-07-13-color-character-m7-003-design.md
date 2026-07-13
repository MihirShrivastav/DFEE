# Color Character (M7-003) — Design

Status: approved for planning
Date: 2026-07-13
Pipeline version: `filmic_v2` only (`parity_v1` stays reproducible)
Source plan: `cpp_engine/migration_docs/FILM_LAB_IMPLEMENTATION_PLAN.md` (M7-003)

## Goal

Give the Film Lab real, stock-relative Color Character controls that decompose
the stock's colour personality — today collapsed behind a single blunt
`film_color` multiplier — into four bounded, scene-agnostic controls. None of
them may be a renamed generic HSL / saturation / exposure adjustment. Each maps
to bounded, profile-aware native behaviour and preserves a one-click neutral.

## Controls

Four controls, each bipolar `−100..+100`, neutral `0` = stock default (no-op).
Internally normalised to `n = value / 100 ∈ [−1, 1]`.

| Control | Modulates (existing primitive) | Positive direction | Guardrails |
| --- | --- | --- | --- |
| **Highlight Color Hold** | `hi_compression` (highlight chroma rolloff) + `highlight_desaturation` (z5) | colour survives into highlights instead of washing to white | high-zone only; never touches lightness; cannot invert sign |
| **Shadow Color Retention** | `sh_compression` (shadow chroma rolloff after toe) | more chroma kept in shadows | shadow-mask gated; **no black lift** (chroma-only); adds no chroma to neutral highlights |
| **Palette Separation** | new hue-anchor attraction sub-pass | hue families separate | chroma-gated (neutrals preserved); hue-wrap stable |
| **Emulsion Color Density** | `chroma_boost` (dye body) + coupling depth | denser dye expression | chroma-only (no tone coupling this slice); distinct from blunt `film_color` |

Key invariant: **at all-zero, effective coefficients equal today's resolved
values**, so a full-neutral render is bit-for-bit identical to the current
output. This is the primary regression guard.

All four are chroma-domain operations → naturally no-op on monochrome stocks,
with an explicit mono guard as belt-and-suspenders.

## Native behaviour

The controls fold into the existing colour-response pipeline. They modulate the
resolved coefficients *before* the per-pixel loop (Highlight Hold, Shadow
Retention, Emulsion Density), plus one new per-pixel sub-pass (Palette
Separation). They must be applied in **all active colour-response code paths**:
`apply_color_response_and_coupling_pipeline` and `apply_color_response` in
`cpp_engine/src/renderer.cpp`.

### Highlight Color Hold
Scale the stock's resolved highlight chroma rolloff and highlight desaturation:

```
effective_hi_comp        = hi_comp        * (1 − hold_gain_hi   * n * sens_hi)
effective_highlight_desat = highlight_desat * (1 − hold_gain_desat * n * sens_hi)
```

Clamp effective values to `≥ 0`. `sens_hi` is the per-stock
`highlight_hold_sensitivity`. Only high-exposure zones are affected (the rolloff
and z5 masks already gate this). Lightness is never modified.

### Shadow Color Retention
Scale the stock's resolved shadow chroma rolloff:

```
effective_sh_comp = sh_comp * (1 − ret_gain * n * sens_sh)
```

Clamp `≥ 0`. Operates only on the chroma path inside the existing shadow mask
(gated by `lch.l`), so it cannot lift blacks and cannot add chroma to neutral
highlights.

### Palette Separation (new sub-pass)
In OKLCh, after coupling, pull each pixel's hue toward its nearest perceptual
anchor, gated by a chroma mask so near-neutral pixels are untouched:

```
g_c   = smoothstep(c_lo, c_hi, chroma)            # neutral-preserving gate
anchor = nearest anchor to h                       # from YAML or default 6
h'    = h + sep_gain * n * sens_sep * anchor_weight * sin(anchor − h) * g_c
```

`sin(anchor − h)` is wrap-stable (no seam at the hue boundary) and vanishes at
the anchors. Positive `n` pulls hues toward anchors (families separate); negative
pushes toward midpoints (families muddy). A mild, bounded chroma differentiation
reinforces separation (chroma nudged up near anchors, down between). Default
anchors: 6 perceptual hues (R Y G C B M); stocks may override.

### Emulsion Color Density
Scale the stock's dye body (chroma boost + coupling depth), not the hue
compressions or biases (which is what distinguishes it from `film_color`):

```
effective_chroma_boost = 1 + (chroma_boost − 1) * (1 + dens_gain * n * sens_dens)
```

Chroma-only this slice — no tone coupling (density/contrast belongs to Process,
M7-004).

## Profile schema (new optional YAML group)

Added to `profiles/stocks/*.yaml` (and B&W stocks). All optional; absent values
inferred from stock family in the solver. The loader (`profile.cpp`) must
register this group because it rejects unknown leaf fields.

```yaml
color_character:
  highlight_hold_sensitivity: 0.0..1.0
  shadow_retention_sensitivity: 0.0..1.0
  emulsion_density_sensitivity: 0.0..1.0
  palette:
    separation_sensitivity: 0.0..1.0
    anchors: [deg, ...]        # optional; default 6 perceptual anchors
    anchor_weights: [w, ...]   # optional; per-anchor pull, default 1.0
```

Family defaults inferred in the solver (modern colour negative fine / high-speed,
consumer negative, colour reversal fine, B&W cubic / tabular), mirroring the
M6-003 grain-field pattern. **B&W families set all sensitivities to 0.**

## Request contract (FE ↔ BE)

Four new fields on preview and export requests:

- `highlight_color_hold`, `shadow_color_retention`, `palette_separation`,
  `emulsion_color_density` — each `−100..+100`.
- **Omitted → 0** (existing callers unchanged).
- `/api/preview` (GET): query params. `/api/export` (POST): request body.
- Out-of-range values are rejected (same discipline as `film_exposure_ev`).
- **`parity_v1` + any non-zero value → explicit rejection** (no ambiguous
  renders; matches the existing unsupported-version discipline). Under
  `parity_v1` the controls are otherwise forced neutral.

## Native plumbing (single slice)

- `SolverControls` (`solver.hpp`): 4 request inputs.
- `FilmResponsePlan` (`solver.hpp`): resolved sensitivities + anchors + computed
  effective coefficients.
- `solver.cpp`: read YAML group, infer family defaults, compute resolved values.
- `renderer.cpp`: apply in both active colour-response paths.
- `profile.cpp`: register the `color_character` group in the loader contract.
- pybind (`dfee_native_module.cpp`) + `dfee_native_bridge.py` + `server.py`:
  parse, validate, forward on preview + export.
- Report JSON: record **both** requested inputs and resolved effective values.

## Deliberate restraints (out of scope this slice)

- No tone coupling on Emulsion Density — stays chroma-only (density/contrast is
  Process, M7-004).
- No per-pixel palette LUT — the anchor model delivers the character at far lower
  cost and complexity.
- No additional colour controls (skin/foliage/sky, B&W spectral filter pack)
  — deferred to later slices per the plan.

## Testing & verification

Native unit tests (`test_core.cpp`):
- **Neutral no-op:** all four at 0 → output bit-identical to pre-change render.
- **Zone locality:** Highlight Hold changes only high-zone chroma; Shadow
  Retention changes only low-zone chroma with **L delta ≈ 0** (no black lift).
- **Neutral-pixel preservation:** Palette Separation leaves a gray ramp
  unchanged; hue-wrap continuity across the 359°/1° seam.
- **Emulsion Density distinctness:** changes chroma body but not hue-compression
  coefficients or biases.
- **Monochrome no-op** on a B&W stock.
- **Determinism:** identical inputs reproduce exactly.
- **`parity_v1` rejection** on non-zero values.

Bridge/route tests:
- `tests/test_native_bridge.py`: dataclass round-trip, resolved report fields.
- Server tests: range validation, omitted-defaults-to-0, preview↔export
  agreement on identical inputs.

Fixtures + visual review:
- Synthetic zone/hue charts for automated locality assertions.
- Representative RAW pass (M7-003F): highlight-heavy, deep-shadow-chroma,
  saturated-palette, neutral/gray-world, and a monochrome stock.

Verification discipline:
- Native Release build + full CTest, targeted bridge/server pytest, and a
  preview/export timing probe against the named baseline to confirm the added
  palette sub-pass does not regress the hot path. No success claims until those
  run green.

## Task breakdown (extends plan M7-003A–F)

1. **A** — schema: YAML group + request fields + report fields; prove all-neutral
   no-op first.
2. **B** — Highlight Color Hold.
3. **C** — Shadow Color Retention.
4. **D** — Palette Separation.
5. **D′** — Emulsion Color Density (added control).
6. **E** — Color Character UI panel: 4 sliders, scene-agnostic labels, concise
   tooltips, one-click neutral reset, mono-aware disabled state.
7. **F** — calibrate family defaults + fixtures + visual acceptance.

Docs updated in the same slice: `FILM_LAB_WORKFLOW.md`,
`FILM_LAB_IMPLEMENTATION_PLAN.md` (mark M7-003 done),
`STOCK_PROFILE_CONTRACT.md` (new group), README / technical architecture, APAM.
