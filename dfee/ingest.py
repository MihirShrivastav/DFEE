import rawpy
import numpy as np
import os


# Extensions treated as rendered (display-referred) inputs rather than camera RAW.
RENDERED_EXTENSIONS = frozenset({".tif", ".tiff"})


def is_rendered_image(filepath):
    return os.path.splitext(filepath)[1].lower() in RENDERED_EXTENSIONS


def _srgb_eotf(v):
    """sRGB EOTF (display -> linear), piecewise. v in [0,1] float array."""
    return np.where(v <= 0.04045, v / 12.92, np.power((v + 0.055) / 1.055, 2.4))


# Linear-primaries -> linear sRGB(D65) matrices (row-major), matching the native decoder.
_XYZ_TO_SRGB = np.array([
    [3.2404542, -1.5371385, -0.4985314],
    [-0.9692660, 1.8760108, 0.0415560],
    [0.0556434, -0.2040259, 1.0572252],
])
_ADOBE_TO_XYZ = np.array([
    [0.5767309, 0.1855540, 0.1881852],
    [0.2973769, 0.6273491, 0.0752741],
    [0.0270343, 0.0706872, 0.9911085],
])
_PROPHOTO_TO_XYZ_D50 = np.array([
    [0.7976749, 0.1351917, 0.0313534],
    [0.2880402, 0.7118741, 0.0000857],
    [0.0000000, 0.0000000, 0.8252100],
])
_BRADFORD_D50_TO_D65 = np.array([
    [0.9555766, -0.0230393, 0.0631636],
    [-0.0282895, 1.0099416, 0.0210077],
    [0.0122982, -0.0204830, 1.3299098],
])


def _primaries_to_srgb(color_space):
    space = (color_space or "srgb").lower().strip()
    if space == "adobe_rgb":
        return _XYZ_TO_SRGB @ _ADOBE_TO_XYZ
    if space == "prophoto":
        return _XYZ_TO_SRGB @ _BRADFORD_D50_TO_D65 @ _PROPHOTO_TO_XYZ_D50
    return None  # sRGB -> identity


def _linearize_transfer(v, color_space):
    space = (color_space or "srgb").lower().strip()
    if space == "adobe_rgb":
        return np.power(np.clip(v, 0.0, None), 2.19921875)
    if space == "prophoto":
        return np.where(v < 0.03125, v / 16.0, np.power(np.clip(v, 0.0, None), 1.8))
    return _srgb_eotf(v)


def _safe_exif_ratio(tag):
    """Convert an exifread IfdTag Ratio to a Python float."""
    try:
        v = tag.values[0]
        if hasattr(v, 'num') and hasattr(v, 'den'):
            return float(v.num) / float(v.den) if v.den != 0 else 0.0
        return float(v)
    except Exception:
        return None


def _read_exif(filepath):
    """
    Read EXIF metadata from a RAW file using exifread.
    Returns a dict with 'iso', 'shutter_speed', 'aperture', 'focal_length',
    'make', 'model'. Any field that cannot be read is set to None.
    """
    try:
        import exifread
    except ImportError:
        return {}

    result = {}
    try:
        with open(filepath, 'rb') as f:
            tags = exifread.process_file(f, details=False)

        # ISO
        for key in ('EXIF ISOSpeedRatings', 'Image ISOSpeedRatings'):
            if key in tags:
                try:
                    result['iso'] = int(str(tags[key]))
                except Exception:
                    pass
                break

        # Shutter speed — stored as a ratio e.g. 1/200
        for key in ('EXIF ExposureTime', 'Image ExposureTime'):
            if key in tags:
                v = _safe_exif_ratio(tags[key])
                if v is not None:
                    result['shutter_speed'] = v
                    result['shutter_speed_str'] = str(tags[key]).strip()
                break

        # Aperture
        for key in ('EXIF FNumber', 'Image FNumber'):
            if key in tags:
                v = _safe_exif_ratio(tags[key])
                if v is not None:
                    result['aperture'] = v
                break

        # Focal length
        for key in ('EXIF FocalLength',):
            if key in tags:
                v = _safe_exif_ratio(tags[key])
                if v is not None:
                    result['focal_length'] = v
                break

        # Camera make / model
        if 'Image Make' in tags:
            result['make'] = str(tags['Image Make']).strip()
        if 'Image Model' in tags:
            result['model'] = str(tags['Image Model']).strip()

        # Lens Model
        for key in ('EXIF LensModel', 'Image LensModel'):
            if key in tags:
                result['lens_model'] = str(tags[key]).strip()
                break

    except Exception:
        pass

    return result


class RawIngestor:
    def __init__(self, filepath, color_space="srgb"):
        self.filepath = filepath
        self.color_space = color_space
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"RAW file not found: {filepath}")

    def ingest(self, draft_mode=False):
        """
        Ingests a RAW (or rendered TIFF) file, linearises it, and extracts metadata.

        Returns:
            rgb_linear      – float32 image array, range [0.0, 1.0]
            luminance       – float32 luminance canvas, range [0.0, 1.0]
            clipping_masks  – boolean masks for R, G, B channels
            clipping_ratios – clipping ratios for R, G, B channels
            metadata        – extracted metadata fields (including real EXIF)
        """
        if is_rendered_image(self.filepath):
            return self._ingest_rendered(draft_mode=draft_mode)

        # ── EXIF read (fast, before rawpy which is slow) ────────────────────
        exif = _read_exif(self.filepath)

        with rawpy.imread(self.filepath) as raw:
            # ── rawpy metadata ───────────────────────────────────────────────
            metadata = self._extract_metadata(raw, exif)

            # ── Linear RGB demosaic ──────────────────────────────────────────
            # gamma=(1,1)     → scene-linear
            # no_auto_bright  → no digital gain compensation
            # use_camera_wb   → as-shot WB multipliers
            # output_color    → sRGB primaries (we work in linear, so no gamma)
            # output_bps=16   → 16-bit integer
            postprocess_opts = {
                'half_size':      draft_mode,
                'no_auto_bright': True,
                'use_camera_wb':  True,
                'output_color':   rawpy.ColorSpace.sRGB,
                'gamma':          (1, 1),
                'output_bps':     16,
            }

            rgb_16bit  = raw.postprocess(**postprocess_opts)
            rgb_linear = rgb_16bit.astype(np.float32) / 65535.0
            rgb_linear = np.clip(rgb_linear, 0.0, 1.0)

            # BT.709 luminance
            luminance = (
                0.2126 * rgb_linear[:, :, 0] +
                0.7152 * rgb_linear[:, :, 1] +
                0.0722 * rgb_linear[:, :, 2]
            ).astype(np.float32)

            # Clipping detection
            clip_threshold = 0.99
            clipping_masks = {
                'R': rgb_linear[:, :, 0] >= clip_threshold,
                'G': rgb_linear[:, :, 1] >= clip_threshold,
                'B': rgb_linear[:, :, 2] >= clip_threshold,
            }
            total_pixels = rgb_linear.shape[0] * rgb_linear.shape[1]
            clipping_ratios = {
                ch: float(np.sum(clipping_masks[ch]) / total_pixels)
                for ch in ('R', 'G', 'B')
            }
            metadata['raw_clipping_ratio'] = max(clipping_ratios.values())

            return rgb_linear, luminance, clipping_masks, clipping_ratios, metadata

    def _ingest_rendered(self, draft_mode=False):
        """Ingest a rendered TIFF (display-referred) into scene-linear sRGB [0,1].

        Mirrors the native TIFF decoder: read via OpenCV, normalise depth,
        coerce channels to RGB, linearise per declared colour space, convert
        primaries to sRGB, and clamp to [0,1] so the rest of the pipeline is
        identical to a decoded RAW.
        """
        import cv2

        raw = cv2.imread(self.filepath, cv2.IMREAD_UNCHANGED | cv2.IMREAD_ANYDEPTH)
        if raw is None:
            raise ValueError(f"Could not decode image: {self.filepath}")

        if raw.dtype == np.uint8:
            arr = raw.astype(np.float32) / 255.0
        elif raw.dtype == np.uint16:
            arr = raw.astype(np.float32) / 65535.0
        elif raw.dtype == np.float32 or raw.dtype == np.float64:
            arr = raw.astype(np.float32)
        else:
            raise ValueError(f"Unsupported image bit depth: {raw.dtype}")

        if arr.ndim == 2:
            arr = np.stack([arr, arr, arr], axis=-1)
        elif arr.shape[2] == 4:
            arr = arr[:, :, :3]
        elif arr.shape[2] != 3:
            raise ValueError(f"Unsupported channel count: {arr.shape[2]}")

        # OpenCV is BGR -> RGB
        arr = arr[:, :, ::-1]
        arr = np.clip(arr, 0.0, 1.0)

        if draft_mode:
            longer = max(arr.shape[0], arr.shape[1])
            max_edge = 2048
            if longer > max_edge:
                scale = max_edge / float(longer)
                new_w = max(1, int(round(arr.shape[1] * scale)))
                new_h = max(1, int(round(arr.shape[0] * scale)))
                arr = cv2.resize(arr, (new_w, new_h), interpolation=cv2.INTER_AREA)

        # Linearise transfer then convert primaries to linear sRGB.
        lin = _linearize_transfer(arr, self.color_space).astype(np.float32)
        matrix = _primaries_to_srgb(self.color_space)
        if matrix is not None:
            lin = lin @ matrix.T.astype(np.float32)
        rgb_linear = np.clip(lin, 0.0, 1.0).astype(np.float32)

        luminance = (
            0.2126 * rgb_linear[:, :, 0] +
            0.7152 * rgb_linear[:, :, 1] +
            0.0722 * rgb_linear[:, :, 2]
        ).astype(np.float32)

        clip_threshold = 0.99
        clipping_masks = {
            'R': rgb_linear[:, :, 0] >= clip_threshold,
            'G': rgb_linear[:, :, 1] >= clip_threshold,
            'B': rgb_linear[:, :, 2] >= clip_threshold,
        }
        total_pixels = rgb_linear.shape[0] * rgb_linear.shape[1]
        clipping_ratios = {
            ch: float(np.sum(clipping_masks[ch]) / total_pixels)
            for ch in ('R', 'G', 'B')
        }

        h, w = rgb_linear.shape[0], rgb_linear.shape[1]
        metadata = {
            'camera_make': 'Rendered',
            'camera_model': 'TIFF',
            'lens_model': '',
            'iso': 100,
            'shutter_speed': 1 / 125.0,
            'shutter_speed_str': '',
            'aperture': 4.0,
            'focal_length': None,
            'white_balance_multipliers': [1.0, 1.0, 1.0, 1.0],
            'black_level': 0,
            'white_level': 65535,
            'image_height': h,
            'image_width': w,
            'raw_height': h,
            'raw_width': w,
            'raw_clipping_ratio': max(clipping_ratios.values()),
        }
        return rgb_linear, luminance, clipping_masks, clipping_ratios, metadata

    def _extract_metadata(self, raw, exif):
        """Merge rawpy + exifread data into a single metadata dict."""
        try:
            wb_multipliers = [float(x) for x in raw.camera_whitebalance]
        except Exception:
            wb_multipliers = [1.0, 1.0, 1.0, 1.0]

        sizes = raw.sizes

        # Determine camera make / model
        make  = exif.get('make',  'Unknown')
        model = exif.get('model', 'Unknown')

        # Fallbacks if EXIF failed
        iso               = exif.get('iso',               100)
        shutter           = exif.get('shutter_speed',     1 / 125.0)
        shutter_speed_str = exif.get('shutter_speed_str', '')
        aperture          = exif.get('aperture',          4.0)
        focal_length      = exif.get('focal_length',      None)
        lens_model        = exif.get('lens_model',        '')

        return {
            'camera_make':             make,
            'camera_model':            model,
            'lens_model':              lens_model,
            'iso':                     iso,
            'shutter_speed':           shutter,
            'shutter_speed_str':       shutter_speed_str,
            'aperture':                aperture,
            'focal_length':            focal_length,
            'white_balance_multipliers': wb_multipliers[:4],
            'black_level': (
                int(raw.black_level_per_channel[0])
                if raw.black_level_per_channel is not None
                else int(raw.black_level)
            ),
            'white_level':  int(raw.white_level),
            'image_height': sizes.height,
            'image_width':  sizes.width,
            'raw_height':   sizes.raw_height,
            'raw_width':    sizes.raw_width,
        }
