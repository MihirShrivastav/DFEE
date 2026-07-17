# Material Finish — Grain, Halation, Bloom (Film Lab v1 — Slice 6) Design

Status: approved-in-principle (grain prototype visually validated)
Date: 2026-07-14
Pipeline version: `filmic_v3` (new behaviour); `parity_v1` / `filmic_v2` stay reproducible.
Architecture source of truth:
[film-lab-framework-architecture.md](../../../documentation/architecture/film-lab-framework-architecture.md)

## Motivation & evidence

The optical/texture stage is the weakest part of the look:

- **Grain** reads as sparse "gaussian-blurred oil blotches," not film grain. Root
  cause found in `renderer.cpp` grain application: (1) grain is an **additive**
  layer in gamma space at tiny amplitude (`strength × 0.038`) — nearly
  invisible; (2) it is multiplied by a **detail-based `grain_receptivity_mask`**
  (high in flat areas, low where there is image detail), so grain
  **concentrates in smooth regions and is suppressed on texture** — the source
  of the uneven, blotchy, pasted-on feel; (3) preview (1024px) and export
  (6024px) generate **different** grain, so the UI never shows the export grain.
  Diagnostic exports confirmed grain is nearly absent at full res and mushy in
  the preview JPEG.
- A Python prototype (Soft-Light blend + uniform fine grain + real amplitude)
  reproduced the reference Kodachrome feel — tight, consistent grain that
  interacts with the tones — and was visually approved.
- **Halation** is gated to `trigger: specular_only` at low strength, so it is
  "virtually non-existent." It needs a real, visible Threshold + Strength model.
- **Bloom** (multiscale highlight diffusion) is acceptable; it just needs a
  first-class control.

Reference: real Kodachrome 64 scan (fine, uniform, interacting grain). Research:
Dehancer film-grain model (physical, density-dependent, size auto-scaled to
image dimensions, grain built into the image not overlaid) and halation
(threshold → resolution-scaled blur → red-orange → screen).

## Principles (carried from the framework)

- `filmic_v3` only; `parity_v1` / `filmic_v2` byte-identical. Deterministic.
- Parameter-model: controls default `100` = the stock's calibrated amount,
  `0..200`, forward = more; server-clamped. (Threshold uses its own 0..100 scale.)
- Effect-oriented, plausibly-real, one-click reset, mono-aware where relevant.
- Preview must represent export: grain scaled to a consistent physical size, and
  preview encoded well enough to show it.

## 1. Grain — rebuild (the core of this slice)

**Blend as Soft Light, not additive.** Build a neutral grain layer
`b = 0.5 + noise·amp` and composite it over the (display/gamma-domain) image with
a Soft-Light blend (pegtop: `out = (1−2b)·a² + 2b·a`). This makes grain
**interact** with the tones: it auto-tapers toward black and white and is
strongest in the mids — exactly like film and the Photoshop soft-light technique
— instead of a flat overlay. Grain is applied per channel; a monochrome (shared)
noise field with a small per-channel decorrelation gives the colour-grain
character (chroma controlled by the stock/grain family).

**Uniform across the frame.** Remove the detail-based `grain_receptivity_mask`
suppression. Grain is emulsion-uniform; keep only a gentle **density/tone weight**
(a touch more presence in mids/upper-mids per the emulsion model) — but the
Soft-Light blend already provides most of the tonal taper. This kills the
blotchiness.

**Real amplitude.** Raise the base amplitude so `strength`/"High" is clearly
visible (the prototype used `amp ≈ 0.10–0.13` in the soft-light layer). Map the
`grain_strength` control onto this.

**Fine, resolution-scaled structure.** Generate a fine noise field (near
per-pixel) and set the grain **cell size** from `grain_size` scaled to a physical
reference of the render width, so the grain's *physical* size is consistent
between preview and export (not pixel-locked). `grain_roughness` shapes the
particle contrast/hardness (post-sizing curve, not a sharpen). Retire the sparse-
impulse + large-kernel clump generator that produced the blotches; mild
band-limited clumping only.

**Preview fidelity.** Ensure the preview shows representative grain: scale grain
to the same physical size as export, and raise the **preview JPEG quality** for
`filmic_v3` so fine grain is not destroyed by 8×8 DCT blocking. (Preview
resolution stays as-is; physical-size scaling makes 1024px grain proportional.)

**Controls:** `grain_strength`, `grain_size`, `grain_roughness` (surface the
existing request fields as first-class Material Finish sliders). Stock `grain.*`
YAML sets the per-stock defaults (already present). Determinism preserved via the
existing stable seed + cached noise field.

## 2. Halation — make it real (Threshold + Strength)

Rebuild as a visible, controllable glow:
1. **Threshold** the luminance to isolate the bright sources (`halation_threshold`).
2. **Blur** the thresholded highlights with a **resolution-scaled** radius (from
   the stock geometry, scaled by render width).
3. **Tint** the blurred glow **red-orange** (stock warm-core / red-fringe).
4. **Screen/add** it back, gated to receiver regions.

**Controls:** `halation_strength` (0..200, 100 = stock default; 0 = off) and
`halation_threshold` (0..100; lower = more of the highlights bloom). Supersedes
the Auto/Off/Low/High enum for `filmic_v3` (the enum stays for parity/v2). Stock
`halation.*` YAML provides defaults + geometry (already present).

## 3. Bloom — surface

Keep the existing multiscale highlight diffusion; expose a `bloom` intensity
control (the request field already exists) as a first-class Material Finish
slider. Optional light tuning only.

## Contract & components

- New/changed request fields (`filmic_v3`): `halation_strength` (0..200, def 100),
  `halation_threshold` (0..100, def stock). `grain_strength` / `grain_size` /
  `grain_roughness` and `bloom` already exist — surface them. Full stack: solver
  (resolve stock defaults × control), renderer (grain rebuild + halation rebuild),
  session (already invokes filmic grain/halation under `is_filmic`), pybind,
  bridge, server (+ clamp), report JSON, React (Material Finish panel).
- Renderer: rewrite `apply_filmic_grain` (soft-light, uniform, amplitude, sizing);
  rewrite the halation half of `apply_filmic_halation_bloom` (threshold + strength
  + resolution-scaled radius). `parity_v1`/`filmic_v2` paths untouched.
- Preview: raise `filmic_v3` preview JPEG quality (server/native preview encode).

## Testing

- Native: grain determinism (same seed → identical); grain **uniformity** (a
  flat patch and a detailed patch receive comparable grain variance — the anti-
  blotch guard); grain visibility (strength>0 measurably changes a flat patch);
  soft-light taper (grain effect smaller near pure black/white than mid);
  monochrome channel handling; halation threshold/strength (raising strength
  increases glow energy around a synthetic bright source; threshold gates it);
  `parity_v1`/`filmic_v2` unchanged.
- Bridge/route: field round-trip, range/clamp, preview↔export agreement of the
  resolved values.
- Visual acceptance (human): grain on a mid-tone RAW vs the Kodachrome reference
  (fine, uniform, interacting — no blotches; preview ≈ export); halation on a
  night/neon or backlit RAW (visible red-orange glow, threshold controllable);
  bloom. Timing probe vs baseline (grain/halation are the heaviest optical
  stages).

## Delivery order (tasks, for the plan)

1. **Grain rebuild** (soft-light + uniform + amplitude + resolution-scaled size)
   + native tests. *Highest value; validated.*
2. **Preview grain fidelity** (physical-size consistency + `filmic_v3` preview
   JPEG quality).
3. **Halation rebuild** + `halation_strength` / `halation_threshold` schema +
   solver defaults + native tests.
4. **Contract plumbing** for the new halation fields (pybind/bridge/server/report).
5. **Material Finish UI** — Grain (strength/size/roughness), Halation
   (strength/threshold), Bloom (intensity).
6. **Route/bridge tests, docs, visual acceptance, APAM.**

## Deliberate non-goals (this slice)

- No full 3D volumetric grain model (Dehancer-style particles) — the soft-light
  uniform fine-grain model matches the reference at far lower cost; revisit only
  if visual acceptance demands it.
- No new grain YAML fields (the rich `grain.*` family already exists).
- Analog artifacts (gate weave, dust, leaks) remain out of scope.
