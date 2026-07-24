# Print Engine Deepening — Per-Channel Print Tone Curve

## Problem

The print finish is a shallow finishing grade, not a print-stock emulation. Its colour comes
from **global RGB channel multiplies** (`red_boost` / `blue_suppression` / `green_shift`) —
blunt, non-perceptual, and prone to a flat cast (the yellow/ochre issue). Real positive
print film gets its character from a **steep per-channel characteristic curve**: the R/G/B
layers have slightly different toe/shoulder, so colour lives *in the tone scale* (e.g.
2383's warm highlights + cool-ish shadows come from the blue layer rolling off earlier than
red in the highlights, not from a global tint).

## Design

Add a **per-channel print tone S-curve** as the primary contrast + colour mechanism —
mirroring how the film stocks already do `apply_film_tone_response`.

For each channel `c ∈ {R,G,B}`, applied on the developed image (0..1, perceptual/gamma space):
```
alpha_c = 1 + print_toe      * channel_toe_mult[c]        # shadow density (colour in shadows)
beta_c  = 1 + print_shoulder * channel_shoulder_mult[c]   # highlight rolloff (colour in highlights)
s = ch^alpha_c / (ch^alpha_c + (1 - ch)^beta_c)           # filmic S-curve
```
- Per-channel `toe`/`shoulder` **differences** create the print's tonal colour: e.g. blue
  `channel_shoulder_mult > 1` → blue compresses more in highlights → **warm highlights**;
  red `channel_toe_mult > 1` → red darkens more in shadows → cooler/denser shadows.
- **Default is identity**: `print_toe = print_shoulder = 0` → `alpha = beta = 1` → `s = ch`
  (no-op). So print profiles without the new fields render exactly as before — the existing
  tonal stages (`shadow_lift`, `contrast_boost`, `highlight_rolloff`) still run.

Colour is then completed by the existing **perceptual OKLab per-zone biases**
(shadow/midtone/highlight) for fine mid-tone tuning. The crude `red_boost` /
`blue_suppression` / `green_shift` multiplies are **deprecated** — kept for back-compat but
authored to 0; the per-channel curve replaces them.

## Schema (print profile `tone:` additions)
```
tone:
  print_toe: 0.0            # base shadow contrast for the per-channel curve (0 = off)
  print_shoulder: 0.0       # base highlight rolloff for the per-channel curve (0 = off)
  channel_toe_mult: [r,g,b]      # per-channel toe (colour in shadows), default [1,1,1]
  channel_shoulder_mult: [r,g,b] # per-channel shoulder (colour in highlights), default [1,1,1]
```

## Pipeline order (apply_print_finish)
1. CMY colour head (user) + `shadow_lift` (black point / matte).
2. **Per-channel print S-curve** (new — contrast + tonal colour). Replaces reliance on the
   global tanh contrast when authored.
3. `contrast_boost` (kept, for extra global punch; profiles keep it near 1 once the curve
   carries contrast).
4. Highlight rolloff (kept).
5. Perceptual OKLab per-zone biases (kept — mid colour fine-tuning).
6. Saturation, print grain (kept). `red_boost`/`blue_suppression`/`green_shift` → 0.

## Authoring the 5 prints
Translate each look into the per-channel curve:
- 2383: warm highlights (blue shoulder↑), slightly cool/dense shadows (red toe↑), steep-ish.
- 2393: as 2383 but punchier (higher toe/shoulder, more saturation).
- 2302: vintage — warm highlights + lifted warm shadows, softer.
- fuji_3510: cooler/teal — red shoulder↑ (cyan-ish highlights), gentle.
- editorial_matte: low toe (open blacks) + soft shoulder, near-neutral channels (muted).

## Tests
- Per-channel print curve: blue `channel_shoulder_mult > 1` warms highlights while leaving
  neutral mids ~neutral; identity (toe=shoulder=0) is a byte no-op.
- Existing `test_print_finish` still passes (profiles without curve fields unchanged).
