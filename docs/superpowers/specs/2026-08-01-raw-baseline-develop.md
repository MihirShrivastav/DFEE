# RAW Baseline Develop + Unified Input Pipeline — Design Spec

**Status:** draft for review
**Date:** 2026-08-01
**Area:** `cpp_engine` — RAW decode (`raw_decode.cpp`) + session render flow (`session.cpp`)

## Goal

One-click, authentic film emulation that works **equally well from RAW and from a
Lightroom TIFF, on any scene**. A processed photo should look as if it were shot on
the chosen stock — robustly across high-key, low-key, high-DR, and saturated scenes.

## Problem

Today there are two quality tiers:
- **TIFF** (already developed by Lightroom) → film applied gently → looks great.
- **RAW** (flat scene-linear, generic sRGB matrix, no baseline tone) → film does all
  the tone/colour work from scratch → muddy, un-film-like, fails on many scenes.

We are implicitly asking the film stage to also be a RAW developer. It isn't one.

## Architecture: two clean stages

```
RAW  → [ 1. Baseline develop (analysis-driven, scene-adaptive) ] ─┐
                                                                   ├─→ [ 2. Film stage (fixed per stock) ] → output
TIFF (already developed) ──────────────────────────────────────── ┘
```

1. **Baseline develop** — make a clean, correctly-exposed, neutral photo (a great
   "camera default"). Scene analysis drives THIS stage (that's its purpose):
   white balance, exposure *placement*, highlight reconstruction, a camera-standard
   tone curve. Robustness across scenes lives here.
2. **Film stage** — the stock's *fixed* character (colour response, tone shoulder,
   grain, halation) applied consistently on the clean baseline. Scene-independent.

The current auto-exposure ("scene placement") and other analysis belong to **stage 1**
(making a good photo), NOT to the film stage (which should be a fixed transform).

## Stage 1 — RAW baseline develop (the new work)

### 1a. Decode (`raw_decode.cpp`)
- Keep as-shot WB (`use_camera_wb=1`).
- **Highlight reconstruction:** set LibRaw `highlight = 2` (blend) instead of clip
  (0), so blown skies/petals reconstruct softly instead of hard-clipping.
- **Phase B (later):** decode into a **wide working gamut** (ProPhoto/`output_color`)
  and convert to sRGB only at final output, so saturated subjects aren't dulled by
  sRGB primaries mid-pipeline. Deferred — bigger change to the colour core.

### 1b. Exposure placement
Reuse the highlight-anchored scene placement already implemented
(2026-08-01-film-auto-exposure.md), but understood as **baseline exposure** — place
the scene into a sensible range for the tone curve below.

### 1c. Baseline tone curve (camera-standard)
Apply a robust, filmic **base tone curve** mapping scene-linear → display-referred,
tuned so the default output has camera-JPEG-like brightness/contrast (what Adobe's
baseline profile does). This gives RAW a pleasing starting image before any stock.
- A single, well-behaved global curve (e.g. ACES-/filmic-style with a soft shoulder),
  parameterised so highlights roll off, midtones sit naturally, shadows keep depth.
- Deterministic and scene-independent in shape (the *placement* in 1b adapts, the
  curve shape does not) → consistent look across a shoot.

Result of stage 1: a clean, display-referred developed image comparable to a
camera-standard / Lightroom-default render.

## Stage 2 — Film stage (unify RAW + TIFF)

Both a RAW (post-baseline) and a TIFF are now "developed input." Route them through
the **same** film application:
- Apply the stock's full **colour** character, **grain**, **halation**.
- Apply the stock's **tone** as a refinement on the developed baseline (the existing
  `rendered_input`/tone-strength behaviour), so we do not double tone-map. Rename the
  user control to a clear **"Film tone strength"** and apply it to RAW and TIFF alike.
- "As shot" vs "Auto balanced" now only affect **stage 1 placement**.

This means: full film colour signature (the authentic look) + a tone that complements
the baseline instead of fighting it. Default strength gives a natural film look;
users can push it.

## What we explicitly do NOT build
Per-camera DCP colour science (HueSat/Look tables). Wide gamut (Phase B) + a good
baseline curve get us the authentic look without reproducing Adobe's profile system.

## Phasing
- **Phase A (this spec, first):** highlight reconstruction + baseline tone curve for
  RAW + unify RAW onto the developed-input film path. Biggest quality win.
- **Phase B (later, if needed):** wide working gamut for last-mile saturated colour.

## Acceptance
Native tests: baseline curve is monotonic, maps mid-grey sensibly, rolls off
highlights (no clip), preserves black depth; RAW and TIFF of the *same scene*
produce close tone/colour after stage 2 (within tolerance). Existing tests +
parity_v1/filmic_v2 byte-identical preserved (gate to filmic_v3).

Visual (human): the four reference frames (dahlias, lighthouse, sea+house, loco)
from **RAW** must look film-authentic and close to the LR-TIFF result of the same
stock; robust across high-key/low-key/high-DR/saturated.

## Risks
Tone-core change; keep it gated to filmic_v3, update tests, validate on the frames.
Double-tone-mapping is the main trap — the baseline curve + gentle film tone must be
balanced so the result isn't over-contrasty. The `rendered_input` mechanism already
exists to attenuate film tone on developed input; reuse it for RAW.
