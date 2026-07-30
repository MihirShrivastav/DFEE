# Bounded-Memory, Faster Export — Strip Engine Spec

## Goal
Bring full-res export memory down from ~4 GB (44 MP) to ~2 image-buffers + small
constants (~1.2 GB @ 44 MP), and make it *faster*, without any visible change to output
(no seams / banding / halos). Match how darktable/Dehancer/Lightroom stay memory-light:
process the full-res image in **strips**, don't materialize whole-image intermediates, and
make grain **coordinate-addressable** instead of a stored field.

## Non-goals
- No pixel-math / look changes. Grain-off output stays byte-identical; grain-on stays
  visually identical (A/B verified).
- Not streaming decode/output — LibRaw decodes a whole RAW (can't strip-decode) and
  `cv::imwrite` takes a whole Mat. Those two full buffers are the accepted floor.
- No GPU, no SIMD/AVX (threading stays OpenMP, SSE2 baseline). GPU/procedural-grain rewrite
  deferred.

## Keep as-is (already correct)
Global analysis on the 2048 px proxy → small zone (7) + spatial (3) masks, solver plan, and
the low-res halation/bloom glow fields. All global decisions stay cheap and once-only.

## The strip engine — three pillars

### Pillar 1 — Inline proxy-mask sampling (removes ~1.8 GB)
Stop calling `resize_zone_masks` / `resize_spatial_masks` to build full-res mask buffers.
Instead sample the small proxy masks by **bilinear-on-read** at each full-res pixel
(`sample_mask(mask_small, x*sx, y*sy)`). Visually identical to the current `cv::resize`
INTER_LINEAR upsample of a smooth mask (per user decision "Option A"); not necessarily
byte-identical, but no seams/banding possible from a smooth mask.

### Pillar 2 — Strip + fuse the per-pixel colour chain (removes intermediates, adds speed)
Today tone → dye → colour-response → density → hue → compression are each a **separate
full-image pass** that allocates a new full buffer and re-reads all of RAM. Restructure into
a **strip loop**: for each horizontal strip, run the per-pixel chain as **one fused kernel**
that reads the decoded strip + inline masks and writes the output strip — no intermediate
full-res buffers, one pass through RAM.
- Bit-identical: same per-pixel math in the same order, just not materialized between stages.
- Faster: ~N fewer whole-image RAM round-trips (memory-bandwidth-bound stages collapse to one).
- Parallelism: strips (or rows within them) run under the existing `parallel_for_*`.

### Pillar 3 — Coordinate-addressable grain (removes ~1.6 GB, makes grain tileable)
Replace the whole-image noise field with a bounded **wrap-around noise texture** (~2 K²,
periodic so its edges join seamlessly), generated once with the existing correlated-noise
math (RNG → convolution → normalize) on that fixed canvas. During the strip composite, sample
it **wrapped** by pixel coordinate (`noise[(y % T)*T + (x % T)]`) with a per-macro-region
offset to hide repetition. Fixed ~48 MB regardless of megapixels; deterministic; seamless.
Grain-on output changes only in that the field now tiles — verified visually identical by A/B.

## Spatial stages (need neighbours — handled without seams)
- **Acutance** (small Gaussian, ~19 px): process per strip with a **halo** of the kernel
  radius; compute on strip+halo, keep the interior. No seam.
- **Halation / bloom**: the expensive part (large-radius blur) already runs on **downscaled**
  data → compute the low-res glow **globally once** (cheap), then composite it per strip by
  upsampling-on-read. No halo needed (glow is global low-res).

## Memory / floor
Peak ≈ decoded (0.53) + output (0.53) + grain texture (0.05) + strip scratch (small) +
low-res globals (small) ≈ **~1.2 GB @ 44 MP**. Scales with ~2 image buffers, not with the
pipeline depth. (Sub-1 GB would need strip I/O, which RAW decode can't do — out of scope.)

## Phases (each measured for peak + verified before the next)
1. **Inline masks** — drop full-res mask materialization; sample proxy masks on read.
2. **Strip + fuse** the per-pixel colour chain (the big memory + speed win).
3. **Coordinate-addressable grain** (wrap-around texture).
4. **Haloed acutance + low-res-glow halation/bloom** composite in the strip loop.

## Verification (the safety gate)
- **Grain-off**: export byte-hash **identical** to pre-change (proves Pillars 1–2 exact).
- **Grain-on**: A/B the output — grain look unchanged, no tile seams (proves Pillar 3 + halos).
- Per-phase peak-working-set measurement (psutil `peak_wset`) showing the drop.
- `ctest` 100%, native-bridge + server suites, on each phase.
- `DFEE_NATIVE_THREADS=1` vs all-cores still identical (parallelism unaffected).

## Risks
| Risk | Mitigation |
|---|---|
| Tile seam (grain / acutance / glow) | Wrap-around grain texture; acutance halo; glow is global low-res. A/B verify. |
| Fusion changes output | Per-pixel same math/order; grain-off byte-hash gate catches any drift. |
| Mask upsample differs | Accepted (Option A): smooth mask, visually identical, no artifact class. |
| Larger images still grow | Decode+output are the floor; documented. GPU/strip-I/O is the future lever. |
