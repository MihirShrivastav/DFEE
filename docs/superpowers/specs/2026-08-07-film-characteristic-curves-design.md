# Film Characteristic Curves — Design Spec

**Status:** Approved for planning (2026-08-07)
**Branch:** `film-characteristic-curves`
**Pipeline gate:** new `effect_pipeline_version = "filmic_v4"` (filmic_v3 / filmic_v2 / parity_v1 untouched)

## Problem

Film stocks barely affect tonality today. The film curve (`apply_film_tone_response`) is a
single generalized-logistic S (`s = ch^α/(ch^α + (1-ch)^β)`, `α = 1 + toe·mult`,
`β = 1 + shoulder·mult`) plus a weak midtone gamma, a toe, and highlight/shadow rolloffs.
Two failures:

1. `midtone_contrast` sits at ~1.0 for nearly every stock and is applied as a gamma only
   near mid-grey with a Gaussian weight, so the main "contrast personality" lever is idle.
   The base logistic is gentle (α≈1.3, β≈1.6), so most stocks land in a narrow, similar band.
2. We compute rich scene analysis (`dynamic_range_stops`, `midtone_anchor`,
   `highlight_headroom`, `shadow_depth`, `scene_exposure_key`, clipping ratios) but only use
   it for ±10–15% nudges to toe/shoulder. The analysis does not *drive* tonality.

Result: stocks read as "slight hue shift + cast," not as distinct film contrast. And real
film contrast is not fixed — it changes with **where the scene lands on the film's curve**
(exposure level), which we don't model at all.

## Goal

Replace the ad-hoc logistic with a **scientifically-grounded characteristic (D–logE) curve
per stock**, and **place the scene on that curve using the analysis we already compute**, so
that:
- each stock has a distinct, real contrast personality (gamma / latitude / toe / shoulder);
- contrast is **exposure-dependent** and emergent (under-exposure → toe-crushed; push up →
  open shadows + rolled highlights), driven by `midtone_anchor` + `dynamic_range_stops`;
- the crossover we already model becomes physically unified with tonality (per-dye-layer
  curves).

## Model

### Characteristic curve (per stock)

New `characteristic_curve:` block in the stock YAML — a log-domain **toe → straight-line →
shoulder** transfer with C¹ (smooth) joins (filmic Hable-style piecewise). Parameters,
all sensitometric:

| param | meaning |
|---|---|
| `gamma` | slope of the straight line = **rendered** contrast index of the *combined* scene→positive transfer (the core personality). For negatives this is negative-γ × print-γ (≈1.0 for a normal Portra print); for reversal it is the film's own γ (≈1.6+). Derived from datasheets per stock — NOT the negative-only γ. |
| `latitude_stops` | usable log-E span between toe and shoulder (wide = forgiving, narrow = punchy) |
| `toe_onset`, `toe_hardness` | log-E below mid where shadows bend off the line + abruptness |
| `shoulder_onset`, `shoulder_hardness` | log-E above mid where highlights roll off + abruptness |
| `d_min`, `d_max` | rendered black floor & white ceiling (endpoints) |

`gamma` is literally the mid-slope; toe/shoulder are smooth compressive joins. This subsumes
today's separate highlight-rolloff and shadow-toe into the curve where they physically belong.

### Scene placement (exposure-dependent contrast)

- Build the scene log-E axis from analysis: `logE = log2(L / midtone_anchor)` → scene
  mid-grey at `logE = 0`, mapped to the straight-line center.
- `dynamic_range_stops` sets how far the scene spreads toward toe/shoulder — this is where
  exposure-dependent contrast emerges (wide-DR/pushed-up scene rides toe+shoulder = compressed
  ends; low-DR scene sits on the straight line = punchy).
- **Film Exposure** slides the whole scene ± stops along log-E (over/under-expose the
  emulsion). Plus a small **auto placement-bias** (default expose-to-the-right ≈ +⅔ stop for
  negatives, neutral for reversal — negative film is designed to be over-exposed).

### Color vs. mono

Color film is three dye layers, each with its own characteristic curve; their gamma/toe
differences *are* the crossover. Apply the curve **per channel** with per-layer
`gamma`/`toe`/`shoulder` multipliers (evolution of today's `channel_*_mult`), unifying
tonality and crossover into one physical model. Mono = one curve.

## Pipeline integration

- The characteristic curve **replaces the core of `apply_film_tone_response`** — logistic +
  midtone gamma + toe + highlight-rolloff + shadow-toe collapse into the one physical curve.
  Applied per channel in a log/density working domain, then back to linear.
- Model the **combined scene→positive transfer** per stock (not a separate negative+print
  sim — print is already its own control; two stages is YAGNI).
- **Gating:** `effect_pipeline_version = "filmic_v4"`. filmic_v3 stays intact for A/B of the
  same stock old-vs-new; parity_v1/filmic_v2 untouched. Stocks migrate to v4 individually;
  un-migrated stocks fall back to the v3 logistic so nothing breaks mid-rollout.

### Control re-layering (removes current overlap)

- **Film Contrast** → multiplies the curve's `gamma`.
- **Highlight Rolloff** → drives `shoulder_onset`/hardness.
- **Shadow Lift** (the compressive toe added earlier) → drives the curve's toe / `d_min`
  (one place, not two).
- **Film Exposure** → scene placement shift along log-E.
- **Light panel** (post-film Exposure/Contrast/Highlights/Shadows/Whites/Blacks/Midtones
  parametric curve) → unchanged, stays post-film as the user's editorial tone. Clean split:
  characteristic curve = the emulsion; Light panel = the darkroom edit on top.
- Old YAML tone params (`toe_strength`, `midtone_contrast`, `shoulder_strength`,
  `highlight_rolloff_start`, `black_density_floor`, `toe_length`) → superseded by
  `characteristic_curve:` for migrated stocks.

## Scope / rollout

Prototype the engine + curve on **3 representative stocks**, validate hard, then roll out to
all 33:
- **Velvia 50** — high gamma, short latitude, abrupt toe/shoulder (punchy slide).
- **Portra 400** — low gamma, wide latitude, gentle everything (forgiving negative).
- **Tri-X 400** — B&W, moderate gamma, long straight line.

## Validation

- **Derive params from published data:** Portra 400 / Velvia 50 / Tri-X 400 datasheets
  publish characteristic curves + contrast index/gamma. Pull `gamma`/latitude/toe/shoulder
  from those, not guesses.
- **Harness proof** (`experiments/bespoke_portra800` + env knobs):
  1. Plot each stock's transfer curve from a ramp; confirm mid-slope matches datasheet gamma.
  2. Exposure sweep — same scene at Film Exposure −2 / 0 / +2; confirm toe-crush when under
     and shoulder-roll when over (exposure-dependent contrast).
  3. Parity gate — filmic_v2 / parity_v1 byte-identical (`dfee_tests`).
- Visual A/B: filmic_v3 vs filmic_v4 for each prototype stock on real images.

## Non-goals (this pass)

- No separate negative + print two-stage simulation (combined transfer only).
- No migration of all 33 stocks until the 3 prototypes validate.
- No change to legacy pipelines (parity_v1 / filmic_v2) or to the post-film Light panel.
