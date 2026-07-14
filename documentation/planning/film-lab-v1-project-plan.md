# DFEE Film Lab v1 — Project Plan

Status: active (branch `new-approach`)
Date: 2026-07-14
Pipeline version: `filmic_v3` (new subtractive core; `parity_v1` and `filmic_v2`
remain reproducible)
Architecture source of truth:
[film-lab-framework-architecture.md](../architecture/film-lab-framework-architecture.md)

## 1. Goal

Ship v1 of the scene-referred subtractive film-emulation framework with **every
fundamental pipeline stage implemented for real — no stubs**: authentic
subtractive film-colour formation plus the image-analysis steering that makes
the film response scene-referred, intent-aware, and content-aware. The result:
pick a stock and the full film look renders with nothing touched; every control
sits at a stock-calibrated default and pushes **forward for more film
character**.

## 2. Guiding constraints (from the architecture spec)

- Subtractive, not additive (saturation → density → darker).
- Effect-oriented controls; hue-honest (neighbour-lean crosstalk, neutrals
  protected); plausibly-real ceiling.
- **Stock-calibrated defaults:** every control ships a per-stock default that is
  already a real film effect; reset returns to that default, not a no-op zero;
  "forward = more filmic."
- Adaptive-on with manual overrides + a global Adaptive switch.
- Principled-perceptual colour in OKLab/OKLCh (no full spectral model in v1).
- Native C++ render path; preview and export share the parameter model and stage
  order; deterministic; report/recipe carries every parameter.
- `filmic_v3` is the versioned home for all new look behaviour; older versions
  stay reproducible; the profile loader keeps its strict field contract.

## 3. Cross-cutting conventions (decide once, apply everywhere)

- **Pipeline version:** introduce `filmic_v3`. Preview/export accept it; native
  session/report record it; unsupported combinations reject rather than silently
  mis-render. Establish this in Slice 1 and reuse.
- **Parameter model (`forward = more filmic`):** each control is a normalized
  request value; the **stock YAML supplies the default and the shaping**; the
  renderer applies `stage(stock_params, control_value)`. At the control's
  default the stock's calibrated amount is applied; increasing intensifies,
  decreasing relaxes toward neutral/digital. The request contract carries the
  user value; the report records both the requested value and the resolved
  stock-calibrated amount. (Exact numeric convention — e.g. default encoded per
  stock, UI shows the stock default position — is finalized in Slice 1 and then
  reused verbatim by later slices.)
- **Stock character in YAML:** each new stage adds its fields to the
  `color_character`/new stage groups with **loader + solver + renderer + report +
  fixture + tests** in the same slice, and **family defaults** so unspecified
  stocks still resolve sensibly. Monochrome resolves colour stages to no-ops.
- **Testing posture per slice:** native unit (stage correctness, determinism,
  monochrome no-op, `parity_v1`/`filmic_v2` untouched, stock-default renders the
  intended effect), bridge/route (field round-trip, range validation,
  preview↔export agreement), synthetic fixtures + representative-RAW visual
  acceptance, and a preview/export timing probe vs the named baseline.
- **Execution:** each slice gets its own detailed implementation plan
  (superpowers:writing-plans) and is executed subagent-driven TDD with per-task
  review + a final whole-branch review, as established on the prior work.

## 4. Slices

Each slice below is an independently shippable increment ending in a working,
tested deliverable. "Native behavior", "Stock params", "Control(s)", and
"Acceptance" are the contract for that slice.

### Slice 1 — Subtractive Film Color Density core + `filmic_v3` — DONE (code)
- **Status:** code-complete on `new-approach`. `filmic_v3` pipeline added
  (inherits filmic_v2 optical/grain); `apply_subtractive_density` reduces OKLab L
  ∝ chroma with a low-luma limiter, wired into preview+export gated to
  filmic_v3; `film_color_density` (0..200, 100 = stock default) plumbed full
  stack; `density.*` YAML + family defaults; UI control + pipeline defaulted to
  filmic_v3. Native + bridge + route tests green; end-to-end probe confirms
  filmic_v3 density=0 == filmic_v2 and density>0 differs. Outstanding: human
  visual acceptance + timing probe.
- **Goal:** the backbone of the look — saturated colours gain density and get
  darker (matte, weighty), with a low-luminance limiter protecting shadows.
- **Native behavior:** in OKLab, reduce `L` by an amount ∝ chroma `C`, gated by a
  low-`L` limiter; hue `h` untouched. New `filmic_v3` colour path; `parity_v1`/
  `filmic_v2` unchanged.
- **Stock params (YAML):** `density.strength`, `density.low_luma_limit` (+ family
  defaults). Per-stock calibrated **default control value**.
- **Control:** **Film Color Density** (forward = denser/matte). Ships at each
  stock's calibrated default.
- **Acceptance:** saturated pixels darken proportional to chroma; neutrals and
  deep shadows protected (limiter); hue unchanged; monochrome no-op; determinism;
  stock-default render shows visible, plausibly-real density; `filmic_v3` plumbed
  end to end (bridge/server/report/React); `parity_v1`/`filmic_v2` byte-identical.
- **Depends on:** nothing (foundation). Establishes `filmic_v3` + parameter-model
  convention.

### Slice 2 — Characteristic-curve controls (Highlight Rolloff + Film Contrast)
- **Goal:** the tonal "not-digital" tells: gentle highlight shoulder + stock
  S-curve punch.
- **Native behavior:** on the tone stage (`apply_film_tone_response`), coordinate
  shoulder/rolloff-start (+ highlight desat easing) for Highlight Rolloff and
  toe/midtone/shoulder for Film Contrast.
- **Stock params:** curve fields already exist (`tone_response.*`); add per-stock
  calibrated defaults for the two controls.
- **Controls:** **Highlight Rolloff**, **Film Contrast** (forward = more filmic
  rolloff / more stock-curve punch).
- **Acceptance:** highlights roll off gently vs clip; contrast reshapes the stock
  curve (not a linear digital contrast); no lightness/hue artifacts on neutrals;
  monochrome still tonally affected (these are tonal, not colour); determinism.
- **Depends on:** Slice 1 (version + convention).

### Slice 3 — Neighbour-lean crosstalk + saturation compression (Color Compression) — DONE (code)
- **Status:** code-complete on `new-approach`. `apply_color_compression`
  (filmic_v3) applies a chroma shoulder (compress high chroma) + a bounded,
  chroma-gated neighbour-lean (red→orange, blue→cyan); wired into preview+export;
  `film_color_compression` (0..200, 100 = stock default) plumbed full stack;
  `compression.*` YAML + family defaults; UI **Color Compression** control added
  and the **Palette Range** slider + anchor render pass retired (`palette_range`
  request field kept as an accepted no-op pending Slice 8 removal). Native +
  bridge + route tests green (79 pytest); end-to-end probe confirms compression
  applies and is stronger on velvia. Outstanding: human visual acceptance +
  timing probe.
- **Goal:** re-found the colour stage as the subtractive-cohesion model; retire
  the anchor/zone-tweak approach that caused hue artifacts.
- **Native behavior:** saturated hues lean toward their *neighbour* (bounded,
  saturation-gated, neutrals/skin protected); high chroma rolls down via a
  chroma shoulder (saturation compression). Replaces `palette_range` anchor pass
  and reframes `dye_contamination`.
- **Stock params:** per-hue neighbour-lean amounts, compression shoulder
  (start/strength), + family defaults; per-stock calibrated default control value.
- **Control:** **Color Compression** (forward = more compressed/cohesive filmic
  palette).
- **Acceptance:** a saturated red leans toward orange (never magenta); neutrals/
  skin stable; high chroma compresses smoothly; no hue-snap artifacts; monochrome
  no-op; determinism; retires `palette_range` cleanly.
- **Depends on:** Slice 1.

### Slice 4 — Analysis-steering backbone (scene-referred + region-aware + Adaptive plane)
- **Goal:** make the core scene-referred and intent-aware, and region-aware —
  the differentiator layer.
- **Native behavior:** a steering sub-module between solve and render sets the
  density/compression/rolloff targets from analysis (saturation spread, headroom,
  tonal skew — intent-aware) and applies zone/spatial/skin-neutral masks so
  density & crosstalk protect skin/neutrals and halation binds to real specular
  sources. Global **Adaptive** switch; manual control values override adaptive.
- **Stock params:** per-stock adaptive priors (how strongly analysis may steer;
  skin/neutral protection strength).
- **Control:** **Adaptive** toggle (default on); existing controls gain adaptive
  behaviour under the hood.
- **Acceptance:** identical stock+controls render differently-but-plausibly across
  a bright vs moody scene; low-key/high-key intent preserved; skin/neutrals
  measurably protected; Adaptive off = deterministic non-adaptive film render;
  manual overrides win; determinism per fixed inputs.
- **Depends on:** Slices 1-3 (there must be real stage parameters to steer).

### Slice 5 — Content-aware harmonization (signature; v1 or fast-follow)
- **Goal:** harmonize the palette toward the image's *own* dominant hues and tame
  the neon/out-of-gamut colours actually present.
- **Native behavior:** derive dominant hue clusters (existing dominant-hue-bins +
  palette entropy) and steer Color Compression's cohesion toward them; neon-risk-
  gated taming.
- **Control:** folds into **Color Compression** (its "harmonization" behaviour
  becomes content-aware) under Adaptive.
- **Acceptance:** merge/compression pulls toward the scene's real palette (not
  fixed hues); neutrals preserved; measurable only where the scene has dominant
  clusters; determinism.
- **Depends on:** Slices 3, 4. Enhances a working stage (not a stub), so it may
  land just after v1 without leaving a placeholder.

### Slice 6 — Optical & grain surfacing
- **Goal:** make the already-rendered optical/texture stages first-class controls.
- **Native behavior:** promote halation Auto/Off/Low/High to numeric
  **intensity + size**; surface **Bloom** as its own control; surface **Grain**
  strength/size/roughness. All on `filmic_v3` with stock-calibrated defaults.
- **Stock params:** halation/grain fields exist; add calibrated default control
  values; bloom default.
- **Controls:** **Halation** (intensity+size), **Bloom**, **Grain** (strength/
  size/roughness).
- **Acceptance:** halation binds to real sources (with Slice 4 region-awareness);
  grain density-scaled; defaults render the stock's authored texture; determinism;
  preview↔export agreement.
- **Depends on:** Slice 1 (version); benefits from Slice 4 (source-bound halation).

### Slice 7 — Stock recalibration on the new model
- **Goal:** author real character across the stock library for the new stages.
- **Work:** populate density/crosstalk/compression/curve/optical defaults and
  per-control stock defaults for all active stocks; family defaults for the rest;
  calibrate against references; monochrome zeros.
- **Acceptance:** every active stock loads; each renders a distinct, plausible
  film character at defaults; family fallbacks sensible; loader contract intact.
- **Depends on:** Slices 1-3, 6 (the stages whose fields it authors).

### Slice 8 — Panel consolidation, explainability/seeding, docs, APAM
- **Goal:** finalize the primary panel and the "lab" polish.
- **Work:** assemble the final control panel (effect names, forward=more-filmic,
  stock-default handles, reset-to-stock-default, mono disabling); surface scene
  **diagnostics/explainability** and **smart seeding / stock match**; batch
  consistency hooks; update all docs (`documentation/`, migration docs, README,
  technical architecture) and APAM; retire superseded M7-003/Slice-1 controls
  from the UI.
- **Acceptance:** panel matches the contract; docs/UI/runtime agree; superseded
  controls removed; explainability accurate; recipes reproduce identically.
- **Depends on:** all prior slices.

## 5. Sequencing

```
S1 Density core (+filmic_v3, param-model)  ── foundation
        │
        ├─> S2 Tone-curve controls
        ├─> S3 Crosstalk + Compression ──┐
        │                                 │
        └─> S6 Optical/grain surfacing    │
                                          ▼
                          S4 Analysis-steering backbone
                                          │
                                          ▼
                          S5 Content-aware harmonization
                                          │
                     S7 Stock recalibration (needs S1-3,6)
                                          │
                     S8 Panel consolidation + docs + APAM
```

Recommended execution order: **S1 → S3 → S2 → S6 → S4 → S5 → S7 → S8** (build the
colour core first, then tone + optical surfacing, then the intelligence layer,
then calibrate and consolidate). S5 may slip just past the v1 line if needed.

## 6. Definition of Done

**Per slice:** contract documented + version behaviour stated; native CPU
implementation complete (no stub); preview/export/report/React carry identical
values; unit + bridge + route + visual-acceptance evidence recorded; timing
checked vs baseline; docs + migration task list + APAM updated in the slice.

**v1 overall:** all fundamental stages real (S1-4, S6-8; S5 in or immediately
after); a fresh stock renders a complete, authentic, scene-adapted film look at
default controls; `parity_v1`/`filmic_v2` still reproducible; full contract
matrix (C++→pybind→bridge→FastAPI→report→React) green; representative visual
acceptance across reversal/negative/B&W approved; performance recorded.

## 7. Risks & mitigations

- **Scope (no stubs = large v1):** mitigate by strict slice boundaries, each
  shippable and reviewed; S5 explicitly allowed to fast-follow.
- **Perceptual OKLab approximation not "film enough":** validate each colour
  slice with representative-RAW visual acceptance before calling it done; the
  subtractive density (S1) is the highest-leverage check.
- **Artifact regressions (the magenta class):** hue-honest constraints
  (neighbour-lean, saturation-gated, neutrals protected) are acceptance criteria,
  not afterthoughts; add fixtures that would catch wrong-hue drift.
- **Determinism/perf on large RAWs:** per-slice timing probe vs named baseline;
  keep the colour core a single per-pixel pass.
- **Steering unpredictability (Slice 4):** Adaptive-off must be a deterministic,
  documented non-adaptive render; manual overrides always win.

## 8. Migration / disposition

- Branch: `new-approach` (from the Slice-1 Palette Range work).
- The M7-003 Color Character controls and the Palette Range slice are a **learning
  step, superseded** by the subtractive core: `emulsion_color_density` →
  **Film Color Density** (real subtractive density, S1); `palette_range` →
  **Color Compression** (neighbour-lean + compression, S3); `highlight_color_hold`
  → folded into **Highlight Rolloff** (S2); `shadow_color_retention` → folded into
  density/compression. Their code is not the destination; retire from the UI in
  S8.
- `parity_v1` and `filmic_v2` remain untouched and reproducible throughout.

## 9. Execution model

For each slice: write the detailed implementation plan
(`docs/superpowers/plans/…`) via the writing-plans workflow, then execute
subagent-driven (fresh implementer per task + per-task spec/quality review +
final whole-branch review), then run human visual-acceptance + timing before
marking the slice done. Track progress in the SDD ledger and update
`documentation/` + APAM as the source of truth.
