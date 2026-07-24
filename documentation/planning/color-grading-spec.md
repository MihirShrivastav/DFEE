# Color Grading — Perceptual 3-Way + Global Wheels

## Goal
A creative colour-grading stage (Lightroom Color Grading / DaVinci Resolve influenced) that
pushes colour into the shadows / midtones / highlights / global independently, done in a
perceptual space so it stays smooth and hue-honest — plus a one-knob film "crossbalance"
shortcut for the classic teal-shadow / warm-highlight cinematic look.

Neutral by default (all zeros = byte no-op), applied for all pipelines.

## Controls (user)
Per zone — **Shadows, Midtones, Highlights, Global**:
- `hue` (0–360°) + `sat` (0–100) → the colour pushed into that zone
- `lum` (−100…+100) → brightness offset for that zone

Global grade controls:
- `balance` (−100…+100) — shifts the shadow↔highlight midpoint (Resolve-style range).
- `blending` (0…100) — width/softness of the zone overlap.
- `crossbalance` (−100…+100) — film split-tone shortcut: + = teal shadows + warm highlights,
  − = the inverse. Layers on top of the wheels.

## Engine (OKLab, perceptual)
Work in OKLab (`L`, `a`, `b`); `a`=green(−)/red(+), `b`=blue(−)/yellow(+).

1. Per pixel, take `L` (OKLab lightness) and compute smooth **Gaussian zone weights**
   (same approach as the tone controls): shadow @ ~0.2, midtone @ `0.5 + 0.25·balance`,
   highlight @ ~0.8; `sigma` widened by `blending`.
2. Convert each zone's `(hue, sat)` to an OKLab offset: `a_off = (sat/100)·kSat·cos(hue)`,
   `b_off = (sat/100)·kSat·sin(hue)` (kSat bounds the max shift so it can't go garish).
3. Accumulate: `a += Σ w_zone·a_off_zone + a_global`; same for `b`. Because we only move
   `a`/`b`, the colour shift is **luminance-preserving by construction** (OKLab `L`
   untouched by colour) — a pro touch that avoids muddying.
4. **Crossbalance:** `a += (w_hi − w_sh)·(cross/100)·kCrossA`, `b += (w_hi − w_sh)·(cross/100)·kCrossB`
   (highlights → orange, shadows → teal).
5. **Luminance:** `L += Σ w_zone·(lum_zone/100)·kLum` (+ global).
6. Convert back to RGB.

All offsets clamped/bounded (kSat, kCross, kLum small) so grading stays tasteful.

## Placement
A dedicated `apply_color_grading(Image&, ColorGradeParams)` stage on the rendered image,
applied after the film/HSL colour stages (final creative grade). Pure function in a header
(`dfee/color_grading.hpp`) for unit testing; session builds `ColorGradeParams` from the
request and calls it. Neutral params → exact no-op.

## Plumbing (mirror the HSL controls)
Request fields `cg_{shadow,midtone,highlight,global}_{hue,sat,lum}`, `cg_balance`,
`cg_blending`, `cg_crossbalance` → bridge_types, pybind dict, bridge dataclass, server.py
(request model + get_preview params + native dict + export), frontend state + payloads.

## UI
New **Color Grading** panel: four draggable colour wheels (SVG) — Shadows / Midtones /
Highlights / Global — each with a Luminance slider; plus Balance, Blending, and Film
Crossbalance sliders. Wheel: angle = hue, radius = saturation; drag the handle.

## Tests
- Per-zone: a shadow grade shifts shadow pixels toward the target hue, leaves highlights ~unchanged.
- Neutral params = byte no-op.
- Crossbalance +: shadows gain teal (b<0), highlights gain warmth (b>0).
- Colour shift is luminance-preserving (OKLab L ~unchanged by a hue push).
