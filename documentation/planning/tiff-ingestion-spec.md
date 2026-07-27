# TIFF Ingestion — Rendered-Image Input (Edit in DFEE / Lightroom round-trip)

## Goal
Let DFEE process a rendered **TIFF** (exported from Lightroom's "Edit in DFEE" round-trip,
or dragged in) exactly as it processes a decoded RAW — feeding the same pipeline with no
downstream changes. Watertight decode + colour handling is the whole task.

## The seam
All sources converge on `fill_decoded_image_from_float_rgb(decoded, pixels, w, h, ch)`,
which expects **linear-light, sRGB-primaries, [0,1] float** RGB (what LibRaw produces with
`output_color=sRGB, gamm=(1,1)`). TIFF ingestion = decode a TIFF into that exact space and
call the same seam. Nothing downstream (analysis, film stages) changes.

## Decode (`decode_tiff_image_from_file`, native, OpenCV/libtiff)
1. `cv::imread(IMREAD_UNCHANGED)`; empty/failed → clear `TIFF_DECODE_FAILED` error.
2. Depth: 8-bit (÷255), 16-bit (÷65535), 32-bit float (as-is); else error.
3. Channels: 1 → replicate to RGB; 3 → BGR→RGB; 4 → drop alpha (BGRA→RGB). Else error.
4. **Linearize** per declared `color_space` transfer curve:
   - sRGB (piecewise EOTF, default), Adobe RGB (γ 2.19921875), ProPhoto (γ 1.8 + linear toe).
5. **Primaries → linear sRGB** via a 3×3 matrix computed once from published matrices
   (sRGB = identity; Adobe RGB→XYZ→sRGB; ProPhoto→XYZ(D50)→Bradford→XYZ(D65)→sRGB). Clamp [0,1].
6. Draft mode → downscale (mirrors RAW `half_size`).
7. Metadata: defaults (dimensions from the image; ISO/ camera generic) so "Auto ISO" grain
   and diagnostics still work; best-effort EXIF later.
8. Reuse `fill_decoded_image_from_float_rgb` → identical DecodedRawImage as a RAW.

## Colour space
Declared via a `color_space` hint on the decode/select request (default `srgb`). The "Edit
in DFEE" export preset standardises on **16-bit sRGB** (as Dehancer does), so the default is
exact; Adobe RGB / ProPhoto supported for manually-dragged files. Unknown → treated as sRGB.

## Dispatch & plumbing
- `decode_raw_image_from_file` detects `.tif/.tiff` (before the LibRaw guard, so TIFF works
  without LibRaw) and routes to the TIFF decoder. All session callers (decode_raw,
  select_file, raw_preview, read_raw_metadata) flow through it unchanged.
- Request gains `color_space` (bridge_types); server passes it; `/api/files` lists tif/tiff.

## Tests
- Synthetic 16-bit sRGB TIFF (mid-gray 0.5 encoded) → decode → linear ≈ 0.214 (sRGB EOTF).
- 8-bit, single-channel (grayscale→RGB), and 4-channel (alpha dropped) decode without error.
- Corrupt/non-image path → error status, no crash.
