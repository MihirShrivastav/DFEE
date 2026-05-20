import rawpy
import numpy as np
import os


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
            tags = exifread.process_file(f, stop_tag='EXIF LensModel', details=False)

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

    except Exception:
        pass

    return result


class RawIngestor:
    def __init__(self, filepath):
        self.filepath = filepath
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"RAW file not found: {filepath}")

    def ingest(self, draft_mode=False):
        """
        Ingests the RAW file, linearises it, and extracts metadata.

        Returns:
            rgb_linear      – float32 image array, range [0.0, 1.0]
            luminance       – float32 luminance canvas, range [0.0, 1.0]
            clipping_masks  – boolean masks for R, G, B channels
            clipping_ratios – clipping ratios for R, G, B channels
            metadata        – extracted metadata fields (including real EXIF)
        """
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
        iso          = exif.get('iso',          100)
        shutter      = exif.get('shutter_speed', 1 / 125.0)
        aperture     = exif.get('aperture',      4.0)
        focal_length = exif.get('focal_length',  None)

        return {
            'camera_make':             make,
            'camera_model':            model,
            'iso':                     iso,
            'shutter_speed':           shutter,
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
