# Primary Film Look Controls — Design

Status: approved for slice planning
Date: 2026-07-13
Pipeline version: `filmic_v2` only (`parity_v1` stays reproducible)
Supersedes the primary-tier intent of M7-003 (see "M7-003 Disposition")

## Motivation

DFEE has a genuinely deep, film-authentic engine — stock YAMLs encode tone
curves, zonal colour bias, hue-selective compression, cross-channel dye
contamination, halation geometry, and a rich grain model. But the UI hides
almost all of it behind a blunt "Film Color %" and, after M7-003, three
abstract chroma nudges (Highlight Color Hold, Shadow Color Retention, Palette
Separation). Those controls operate at the wrong altitude: they are
second-order derivatives of stock personality, visually subtle and unnameable
to a user. Nobody perceives "shadow chroma retention."

This redesign replaces the primary tier with **effect-oriented controls**: each
slider is named for the visual effect it produces, and behind it the engine
orchestrates a coordinated group of backend parameters. This is the Dehancer
model — a handful of confident, visible sliders, each doing a lot underneath.

## Principles

1. **Effect-named, not backend-named.** The user thinks in produced effect
   ("richer dyes", "highlights that glow"), never in `hi_compression` or
   `dye_contamination`.
2. **One control orchestrates many backend params.** Consolidate subtle
   primitives behind a single perceptual slider.
3. **Plausibly-real ceiling.** Max settings map to a bounded envelope around
   the stock's authored values — extremes look like that film pushed hard,
   never broken. No hyper-stylized headroom.
4. **Neutral = stock default.** Every control at 0 renders the stock's authored
   look (byte-identical to a no-control render), so a fresh recipe is already
   correct. Controls push *within* the realistic envelope.
5. **filmic_v2 only.** `parity_v1` stays bit-for-bit reproducible; any non-zero
   primary control under `parity_v1` is rejected, never silently rendered.
6. **Film controls are not generic digital controls.** Film Contrast shapes the
   stock tone curve, not a linear digital contrast; Film Color Density is dye
   behaviour, not a saturation multiplier. Generic exposure/curves/HSL remain
   separate in Advanced Correction.

## The Primary Panel (7 controls)

All bipolar `−100..+100`, neutral `0` = stock default, unless noted.

### Tone
- **Highlight Rolloff** — how gently highlights compress and glow vs clip to
  white. Coordinates `tone_response.highlight_rolloff_start` +
  `shoulder_strength`, and eases `highlight_desaturation` as rolloff softens so
  colour survives the glow (this is where M7-003 *Highlight Color Hold* goes).
  `−` = compress earlier and glow; `+` = hold longer, more digital before the
  shoulder.
- **Film Contrast** — the stock S-curve punch vs flat/gentle. Coordinates
  `tone_response.toe_strength` + `midtone_contrast` + `shoulder_strength` as one
  paired curve move. `+` = punchier (deeper toe, firmer shoulder); `−` =
  flatter. Explicitly the stock curve, not the Advanced linear "Contrast".

### Color
- **Film Color Density** — richness of the dyes. Coordinates
  `hue_saturation_response.saturation_boost` + `dye_contamination` depth, and
  retains more shadow chroma at higher density (absorbs M7-003 *Shadow Color
  Retention*). `+` = richer, more authentic cross-dye rendering; `−` =
  thin/faded. Replaces the blunt "Film Color %" and M7-003 Emulsion Density.
- **Palette Range** (muscular, bipolar) — the M7-003 anchor engine cranked and
  completed:
  - `−` (merge / ethereal): pull hues toward the nearest dominant anchor **and
    desaturate proportionally to how far they are pulled**. The coupled
    hue-collapse + chroma-loss is what makes similar colours read as merged into
    one, producing the harmonized, dreamy limited-palette look. Substantially
    higher gain ceiling than M7-003's timid 0.35 rad, still plausibly-real.
  - `+` (separate): push hues apart toward anchors + slight chroma gain.
  - Neutral pixels preserved (chroma gate); hue-wrap stable (sin(delta)); per-
    anchor weights honoured.

### Optical / texture (engine already renders these; surface + refine)
- **Halation** — red/orange glow bleeding from bright light sources. Promotes
  the current Auto/Off/Low/High enum to numeric **intensity** + **size** (size
  scales the diffusion radii). filmic_v2 already renders the warm-core / red-
  fringe geometry. Amount-style range (0 = off / stock default).
- **Bloom** — highlight glow/diffusion, its own control (intensity, optionally
  size). Already a numeric post-effect.
- **Grain** — surface the existing rich model as three finer Material Finish
  sliders: **strength**, **size**, **roughness**.

### Deferred (agreed)
- **Color Warmth / Cast** — a stock-character temperature/tint dial. Valuable
  but needs its own brainstorm; not in this redesign.

## Field naming (clean rename — early dev, no back-compat burden)

- New: `highlight_rolloff`, `film_contrast`, `halation_intensity`,
  `halation_size`.
- Renamed: `emulsion_color_density` -> `film_color_density`;
  `palette_separation` -> `palette_range`.
- Kept: `bloom`, `grain_strength`, `grain_size`, `grain_roughness`.
- Retired as controls: `highlight_color_hold` (into `highlight_rolloff`),
  `shadow_color_retention` (into `film_color_density`). Blunt `film_color`
  demoted to a hidden legacy field, not a Film Lab control.

## M7-003 Disposition

M7-003 shipped four controls; this redesign keeps the engine investment but
re-frames the surface:
- **Palette Separation** -> evolved into **Palette Range** (Slice 1). The anchor
  engine, chroma gate, wrap-stability, and per-anchor weights are reused; the
  merge pole + stronger gains are added.
- **Emulsion Color Density** -> broadened into **Film Color Density** (Slice 2).
- **Highlight Color Hold** -> folded into **Highlight Rolloff** (Slice 3).
- **Shadow Color Retention** -> folded into **Film Color Density** (Slice 2).
- The `color_character:` YAML group and per-family sensitivity solver logic
  remain the calibration substrate; sensitivities are renamed/extended per
  control as each slice lands. Monochrome stocks stay all-zero (no-op).

## Delivery Order

Each slice is independently shippable, `filmic_v2`-only, plausibly-real
bounded, neutral = stock default, and follows TDD + subagent review (as M7-003
did). Each slice gets its own implementation plan; we ship and visually review
each control as it lands rather than building one giant plan.

1. **Palette Range** — add the merge pole (hue-collapse + coupled
   desaturation), raise gains, rename `palette_separation` -> `palette_range`.
2. **Film Color Density** — co-drive `dye_contamination` + shadow chroma; rename
   `emulsion_color_density` -> `film_color_density`; retire `film_color` from
   primary and `shadow_color_retention`.
3. **Highlight Rolloff + Film Contrast** — the new tone-stage pair
   (`apply_film_tone_response`); absorbs `highlight_color_hold`. Highest
   new-code risk (a renderer stage M7-003 never touched).
4. **Halation (numeric) + Bloom** — promote halation to intensity+size; surface
   bloom as its own control.
5. **Grain finer controls** — surface strength/size/roughness in Material
   Finish.
6. **Panel consolidation + cleanup** — assemble the final primary panel, delete
   orphaned M7-003 UI, update docs + APAM.

## Constraints carried from the Film Lab contract

- Every new/renamed parameter needs neutral default, valid range, native
  request field, report field, and a versioned behaviour statement.
- Preview and export use the same parameter model and stage ordering.
- The native profile loader continues to reject unconsumed YAML fields; any new
  profile field needs loader/solver/renderer/report/fixture/test in its slice.
- Primary labels are scene-agnostic; tooltips may describe likely effects but
  never assume a subject is present.
- Each control keeps a one-click neutral reset; monochrome stocks disable the
  colour controls.

## Testing posture (per slice)

- Native: neutral no-op (byte-identical), effect-direction, locality and
  envelope boundedness, determinism, monochrome no-op, `parity_v1` rejection.
- Bridge/route: field round-trip, range validation, preview↔export agreement.
- Fixtures + representative-RAW visual acceptance and a preview/export timing
  probe vs the named baseline before each slice is considered done.

## Outstanding from M7-003 (carry forward)

The M7-003F human steps — visual acceptance pass on representative RAWs and a
preview/export timing probe — were never run. Fold them into Slice 1's visual
acceptance since Palette Range changes the same colour pipeline.
