# DFEE Film Lab — Framework Architecture

Status: draft for review (new-approach branch)
Date: 2026-07-14
Scope: the overall structure of the film-emulation framework — what we are
building, how it works, its components, how each stock gets its character, the
defaults, and the user controls. This is the master design that the per-slice
specs and plans will implement.

---

## 1. Vision & Positioning

DFEE is a **scene-referred film-emulation lab**, not a LUT box.

Two pillars, and we need both:

1. **An authentic subtractive film-colour core.** The film "look" — colour,
   cast, density, compression, rolloff — is not a bag of separate effects. It
   is the emergent behaviour of a **subtractive dye model** (a camera negative
   plus a print). We build that model as a coherent per-pixel colour-formation
   transform, so authenticity and freedom-from-artifacts come by construction.

2. **Novel image analysis that steers the core.** A DCTL or 3D LUT is
   scene-blind: it maps a colour identically whether the frame is a moody night
   interior or a blown-out beach. Because we *measure the image* (tone, colour,
   palette, headroom, cast, spatial structure), our film response can be
   **scene-referred, intent-aware, and content-aware**. This is control and
   creativity a static system structurally cannot offer, and it is the reason
   the analysis exists.

The product thesis: **authentic film physics + an intelligent lab that develops
each image on its own terms.** Neither pillar alone is enough — a pure model is
"a nicer LUT"; pure analysis without a principled colour core is heuristic
sand (our current engine, which throws artifacts like a red door drifting to
magenta).

## 2. Design Principles

- **Subtractive, not additive.** As a colour gains saturation it gains
  **density → it gets darker**, never brighter. This is the matte, weighty,
  magazine-print quality. It is the backbone, not a garnish.
- **Effect-oriented controls.** Every user control is named for the visual
  effect it produces; behind it we orchestrate model parameters. The user never
  sees `dye_contamination` or `hi_compression`.
- **Hue-honest.** Colour crosstalk leans a saturated hue only toward its
  *neighbour* (red→orange, neon-blue→cyan), smoothly, and only when saturated;
  low-saturation colours, skin, and neutrals stay stable. No hue-snapping to far
  anchors. This is both physically correct and artifact-free.
- **Plausibly-real ceiling.** Control extremes map to a bounded envelope around
  the stock's authored physics — "that film pushed hard," never broken. No
  hyper-stylized headroom (that is a later, explicit stylistic mode if ever).
- **Neutral = stock default.** Every control at its neutral value renders the
  stock's authored look; a fresh recipe already looks right. Controls push
  within the realistic envelope, with one-click reset.
- **Analysis steers, it does not gate authenticity.** The subtractive core is
  correct on its own; analysis parameterizes it per image. Turning analysis off
  must still yield an authentic (if non-adaptive) film render.
- **Deterministic & reproducible.** Same inputs → same output. Recipes are the
  durable artifact. Preview and export use one parameter model and stage order.
- **Versioned rendering.** New look-changing behaviour lands under a named
  pipeline version; older versions stay reproducible. Native C++ engine is the
  render path.
- **Explainable.** Because we analyze, we can tell the user *what the scene is*
  and *why a control behaves as it does*, and offer smart starting points.

## 3. The Film Model (the subtractive colour core)

We emulate the physical chain: **scene light → camera negative → print →
viewer.** The core produces, as one coherent model, the behaviours the research
identifies as the source of the film look.

### 3.1 Characteristic curve (tone / density formation)
Log-density vs log-exposure with a **toe** (shadows) and **shoulder**
(highlights), and **asymmetric latitude** (much more highlight headroom than
shadow). Gentle rolloff instead of hard clipping — the primary "not-digital"
tell. Drives Highlight Rolloff and Film Contrast controls.

### 3.2 Subtractive colour formation (the crux)
Three dye layers (yellow/magenta/cyan) whose behaviour yields three coupled
effects — implemented together, not as independent knobs:

- **Density (saturation → luminance reduction).** More saturated colours render
  denser and therefore darker, with a **low-luminance limiter** so shadows are
  not crushed. This is the matte, dense, print-like colour. *This is the single
  biggest missing piece in the current engine.*
- **Neighbour-lean crosstalk.** Saturated hues lean toward their neighbour
  (red→orange, neon-blue→cyan, green→grounded foliage) smoothly and only when
  saturated; neutrals/skin protected. Produces the cohesive, "colours belong
  together" palette.
- **Saturation / gamut compression.** Film cannot hold extreme saturation, so
  the top of the saturation range rolls down — the "reduced colour dynamic
  range." A shoulder on chroma, not a flat desaturate.

Working space: **OKLab/OKLCh** (perceptual, hue-stable) is the pragmatic home —
density = lower `L` ∝ `C` (hue `h` preserved), crosstalk = bounded `h` lean by
saturation, compression = `C` shoulder. A fuller spectral/Beer's-law dye model
is explicitly out of scope for v1 (heavy, and perceptual approximation in OKLab
gets the look without the cost). Decision to confirm: **principled-perceptual in
OKLab, not full spectral.**

### 3.3 Print stage
A second characteristic curve (the print stock) adding highlight separation,
its own contrast, and the stock **colour cast** (plus CMY colour-head controls).
Negative sets latitude and base colour; print sets contrast and final cast.

### 3.4 Optical & texture
- **Halation:** red-weighted bloom from light penetrating the emulsion and
  back-reflecting into the deepest (red) layer; intensity-dependent; fires on
  genuine specular sources.
- **Bloom:** softer highlight diffusion/glow, separate from halation.
- **Grain:** density-scaled, per-channel, peaks in mids/shadows, lives within
  density rather than overlaid.

### 3.5 Process (later)
Push/pull development (contrast + tint), bleach bypass (silver retention:
contrast up, desaturation, silvery shadows). Deferred to a Process milestone.

## 4. The Analysis-Steering Layer (the differentiator)

Image analysis parameterizes the core per image and per region. Default posture:
**adaptive-on with manual overrides** — the film auto-develops each image, and
any control the user touches overrides the adaptive value. (Open decision: a
"manual/LUT-like" toggle for people who want a predictable static base. Lean:
adaptive-on is the product; expose a global "Adaptive" switch for the manual
camp.)

Four capabilities, drawing on analysis we largely already compute (tonal zones,
dominant hue bins, palette entropy, hue/chroma stats, highlight headroom, neon
risk, camera cast, spatial/edge/texture masks):

- **A. Scene-referred targets.** The core's density pivot, compression amount,
  and rolloff adapt to the scene's saturation spread and headroom, and are
  **intent-aware** (respect low-key vs high-key so a moody shot is not lifted
  nor an airy one crushed). Auto-metering, but for colour and tone, always
  inside the stock's plausible physics.
- **B. Content-aware harmonization (signature).** Harmonize the palette toward
  the image's *own* dominant hue clusters (from dominant hue bins), and tame
  specifically the neon/out-of-gamut colours that are *actually present* (neon
  risk). The film "finds" and reinforces the scene's real palette — impossible
  for a scene-blind transform.
- **C. Region-aware authenticity.** Use zone/spatial/skin-neutral masks so
  density and crosstalk **protect skin and neutrals**, halation fires on genuine
  specular sources, and grain scales with local density/texture. Film physics,
  guided to land where it should.
- **D. Explainability & seeding.** Surface scene diagnostics, explain why a
  control behaves as it does, suggest a starting recipe or stock match, and keep
  a look consistent across a set of varied exposures ("develop the roll").

## 5. Pipeline & Components (end-to-end)

Stage order (native C++ engine; preview and export identical):

1. **Prepare** — RAW decode, neutral WB, basic hygiene. (`RawIngestor` / native
   decode.)
2. **Analyze** — tonal, colour, palette, spatial, cast features.
   (`ImageStateAnalyzer` + camera bias.)
3. **Solve** — build the render plan: resolve stock parameters, then let the
   **analysis-steering layer** set scene-referred/region-aware targets and
   optional content-aware harmonization. (`RenderPlanSolver` + a new steering
   sub-module.)
4. **Negative formation** — characteristic curve + subtractive colour core
   (density, neighbour-lean crosstalk, saturation compression). (Renderer:
   re-founded colour stage.)
5. **Print stage** — print curve + cast + CMY. (Renderer print finish.)
6. **Optical** — halation, bloom. 7. **Grain.** 8. **Output** — encode/export.

Component map (evolution, not rewrite): keep `RawIngestor`, `ImageStateAnalyzer`,
camera bias, `RenderPlanSolver`, print stocks, halation, grain, report/recipe.
**Re-found the renderer colour stage** on the subtractive core (replacing the
zone-gated OKLab tweak stack). Add a **steering sub-module** between solve and
render that maps analysis → core parameters.

## 6. How each stock gets its character

A stock is defined by the parameters of the subtractive model — authored in YAML,
inferred by family when absent, overridable per stock. Character lives in:

- **Characteristic curve:** toe strength/length, midtone contrast, shoulder,
  highlight rolloff start, latitude asymmetry, black density floor, per-channel
  curve differences.
- **Subtractive colour:** density strength (saturation→luminance coupling) and
  its low-luminance limiter; per-hue **neighbour-lean** amounts (how far red
  leans to orange, blue to cyan, etc.); saturation-compression shoulder
  (where/how hard chroma rolls off); base colour balance.
- **Print pairing & cast:** default print stock, print contrast, colour cast /
  CMY balance.
- **Optical:** halation strength/geometry/warmth/trigger; grain family (PGI,
  clumping, grit, layer correlation, tonal response).
- **Adaptive priors:** how strongly this stock lets analysis steer it (e.g. a
  punchy reversal vs a forgiving negative), and skin/neutral protection strength.
- **Monochrome:** panchromatic weights; colour controls resolve to no-ops.

Authoring model: **stock family defaults** (modern colour negative fine/high-
speed, consumer negative, colour reversal, B&W cubic/tabular) provide sensible
values; per-stock YAML overrides tune the personality; calibration references
real scans / known looks. The native loader keeps its strict contract (reject
unknown/unconsumed fields; every field consumed by loader→solver→renderer→report
→tests). Neutral defaults mean the stock's authored values render its true look
with all user controls at neutral.

## 7. User Controls (the primary panel)

Each control is an exposed, effect-named parameter of the core, bipolar
`−100..+100` (neutral 0 = stock default) unless noted, plausibly-real bounded,
with one-click reset and monochrome-aware disabling of colour controls.

**Exposure** (before the film): Scene Placement (Auto Balanced / As Shot), Film
Exposure (EV).

**Tone:** Highlight Rolloff (shoulder glow vs clip), Film Contrast (stock
S-curve punch — not the generic digital contrast).

**Colour (core parameters):**
- **Film Color Density** — the subtractive density (saturation→darker, matte,
  weighty), with its low-luminance limiter. The headline film-colour control.
- **Colour Compression / Palette** — saturation-range compression + the amount
  of content-aware harmonization (leaning the scene toward its own palette). The
  corrected successor to "Palette Range" (no hue-snap artifacts).
- **Colour Cast** — the stock's temperature/tint personality. (Own future
  brainstorm; deferred but named here.)

**Optical / texture:** Halation (intensity + size), Bloom (own control), Grain
(strength + size + roughness).

**Adaptive:** a global **Adaptive** switch (default on) — film auto-develops to
the scene; manual control touches override the adaptive value.

**Process (later):** push/pull, bleach bypass. **Print (later):** dedicated
print-stage group (stock, CMY, density, contrast, black point).

**Advanced Correction:** generic exposure/curves/HSL/detail remain available but
secondary; never the default path to a film look and never silently driving a
film control.

## 8. Defaults & the "just works" experience

Pick a stock → it renders its authentic look immediately (neutral controls +
adaptive-on develops the image sensibly). The user nudges effect sliders within
a plausibly-real envelope; every control has a one-click neutral reset; smart
seeding can propose a starting recipe or stock match. The goal: great by
default, deep on demand.

## 9. Versioning, determinism, contract

The subtractive core is a new named pipeline version (e.g. `filmic_v3`);
`parity_v1` and `filmic_v2` remain reproducible. Native C++ is the render path;
preview/export share the parameter model and stage order; every parameter has a
neutral default, valid range, native request field, report field, and versioned
behaviour statement; the profile loader rejects unconsumed YAML.

## 10. Migration from the current engine

- **Keep:** RAW ingest, analysis, camera bias, solver, print stocks, halation,
  grain, recipes/report, tone_response fields.
- **Re-found:** the renderer colour stage → subtractive core. `dye_contamination`
  is reframed as the neighbour-lean crosstalk model. The M7-003/Slice-1 colour
  controls (`highlight_color_hold`, `shadow_color_retention`,
  `emulsion_color_density`, `palette_range`) are superseded by the core's
  parameters and their effect-oriented controls; their code is a learning step,
  not the destination.
- **Add:** the analysis-steering sub-module; the Film Color Density (subtractive)
  stage; the Adaptive control plane.

## 11. Open Decisions (confirm during review)

1. **Adaptive default:** adaptive-on with manual overrides + a global Adaptive
   switch (recommended) vs manual-first.
2. **Colour model fidelity:** principled-perceptual in OKLab (recommended for
   v1) vs a fuller spectral/Beer's-law dye model (heavier, later if ever).
3. **v1 scope:** how much of the analysis-steering (A/C as backbone, B as
   signature) lands in v1 vs fast-follow.
4. **Control taxonomy final names** for the colour-core controls (Film Color
   Density; Colour Compression/Palette).

## 12. Delivery Roadmap (re-sliced)

Each slice: filmic_v3, plausibly-real, neutral=stock-default, TDD +
subagent review, visual acceptance on representative RAWs.

1. **Subtractive Density core** — saturation→luminance density with low-luma
   limiter, in OKLab; the backbone and the biggest piece of the look. Stock
   density parameter + family defaults.
2. **Characteristic-curve controls** — Highlight Rolloff + Film Contrast on the
   tone stage.
3. **Neighbour-lean crosstalk + saturation compression** — re-found the colour
   stage; retire the anchor/zone-tweak approach; Colour Compression control.
4. **Analysis-steering v1** — scene-referred + intent-aware targets (A) and
   region-aware protection (C); the Adaptive control plane.
5. **Content-aware harmonization (B)** — the signature palette feature.
6. **Optical & grain surfacing** — Halation numeric + Bloom + Grain trio as
   first-class controls.
7. **Stock recalibration** — author character across the stock library on the
   new model; family defaults.
8. **Panel consolidation, explainability/seeding (D), docs, APAM.**

Colour Cast, Process (push/pull, bleach bypass), and the dedicated Print group
are explicit post-v1 milestones.
