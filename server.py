import os
import glob
import io
import logging
import sys
from contextlib import asynccontextmanager
from functools import lru_cache
from pathlib import Path
import cv2
import numpy as np
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from PIL import Image

from dfee.ingest import RawIngestor
from dfee.analyzer import ImageStateAnalyzer
from dfee.bias import CameraBiasEstimator
from dfee.solver import RenderPlanSolver
from dfee.renderer import FilmRenderer
from dfee.profile import FilmStockProfile, PrintStockProfile
from dfee.report import RenderReporter

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger("dfee.server")


def _candidate_native_build_dirs() -> list[Path]:
    base_dir = Path(__file__).resolve().parent
    return [
        base_dir / "cpp_engine" / "out" / "build" / "windows-msvc-vcpkg" / "Release",
        base_dir / "cpp_engine" / "out" / "build" / "windows-msvc" / "Release",
        base_dir / "cpp_engine" / "out" / "build" / "windows-msvc-vcpkg" / "Debug",
        base_dir / "cpp_engine" / "out" / "build" / "windows-msvc" / "Debug",
    ]


def _configure_native_python_paths() -> Path | None:
    repo_root = Path(__file__).resolve().parent
    for build_dir in _candidate_native_build_dirs():
        if not (build_dir / "dfee_native.pyd").exists():
            continue

        if str(repo_root) not in sys.path:
            sys.path.insert(0, str(repo_root))
        if str(build_dir) not in sys.path:
            sys.path.insert(0, str(build_dir))

        if os.name == "nt" and hasattr(os, "add_dll_directory"):
            os.add_dll_directory(str(build_dir))
            vcpkg_bin = build_dir.parent / "vcpkg_installed" / "x64-windows" / "bin"
            vcpkg_debug_bin = build_dir.parent / "vcpkg_installed" / "x64-windows" / "debug" / "bin"
            if vcpkg_bin.exists():
                os.add_dll_directory(str(vcpkg_bin))
            if vcpkg_debug_bin.exists():
                os.add_dll_directory(str(vcpkg_debug_bin))
        return build_dir
    return None


NATIVE_BUILD_DIR = _configure_native_python_paths()


@asynccontextmanager
async def _app_lifespan(_: FastAPI):
    _log_native_engine_startup_status()
    yield


app = FastAPI(title="Deterministic Film Emulation Engine (DFEE) Server", lifespan=_app_lifespan)

# Allow CORS for development
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Directories
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DIR = os.path.join(BASE_DIR, "raw_files")
STOCKS_DIR = os.path.join(BASE_DIR, "profiles", "stocks")
PRINT_STOCKS_DIR = os.path.join(BASE_DIR, "profiles", "print_stocks")

# Ensure directories exist
os.makedirs(RAW_DIR, exist_ok=True)

# Active session cache to keep slider adjustments real-time
class ActiveSession:
    def __init__(self):
        self.filename = None
        # Draft-res linear data (ingested once on /api/select)
        self.rgb_linear = None
        self.Y = None
        self.clipping_masks = None
        self.clipping_ratios = None
        self.metadata = None

        # Preview-res linear data (for fast renders)
        self.preview_rgb_linear = None
        self.preview_Y = None
        self.preview_clipping_masks = None

        # Cached preview-res analysis features
        self.feature_dict = None
        self.masks = None
        self.cached_diagnostics = None  # Cached so re-selecting same file is instant

        # Cached FULL-res linear data (ingested once on first export)
        self.fullres_rgb_linear = None
        self.fullres_Y = None
        self.fullres_clipping_masks = None
        self.fullres_clipping_ratios = None
        self.fullres_metadata = None

        # Cached FULL-res analysis (computed once on first export, reused after)
        self.fullres_feature_dict = None
        self.fullres_masks = None

        # Raw before preview image (JPEG bytes)
        self.raw_preview_bytes = None

session = ActiveSession()


def _env_flag(name: str, default: bool = False) -> bool:
    raw = os.getenv(name)
    if raw is None:
        return default
    return raw.strip().lower() in {"1", "true", "yes", "on"}


def _env_flag_override(name: str) -> bool | None:
    raw = os.getenv(name)
    if raw is None:
        return None
    return raw.strip().lower() in {"1", "true", "yes", "on"}


def _native_engine_enabled() -> bool:
    return _env_flag("DFEE_USE_NATIVE_ENGINE", default=False)


def _route_native_enabled(override_name: str) -> bool:
    override = _env_flag_override(override_name)
    if override is not None:
        return override
    return _native_engine_enabled()


def _native_profiles_enabled() -> bool:
    return _route_native_enabled("DFEE_USE_NATIVE_PROFILES")


def _native_raw_image_enabled() -> bool:
    return _route_native_enabled("DFEE_USE_NATIVE_RAW_IMAGE")


def _native_preview_enabled() -> bool:
    return _route_native_enabled("DFEE_USE_NATIVE_PREVIEW")


def _native_export_enabled() -> bool:
    return _route_native_enabled("DFEE_USE_NATIVE_EXPORT")


def _native_select_enabled() -> bool:
    return _route_native_enabled("DFEE_USE_NATIVE_SELECT")


@lru_cache(maxsize=1)
def _get_native_bridge_module():
    import dfee_native_bridge

    return dfee_native_bridge


@lru_cache(maxsize=1)
def _get_native_engine_session():
    native_bridge = _get_native_bridge_module()
    return native_bridge.create_session(BASE_DIR)

def _log_native_engine_startup_status():
    try:
        native_session = _get_native_engine_session()
        profiles = native_session.list_profiles()
        engine = profiles.engine
        cuda = engine.cuda_status
        logger.info(
            "Native engine startup status: build_dir=%s version=%s libraw_enabled=%s cuda_mode=%s cuda_compiled=%s cuda_available=%s cuda_active=%s device_count=%s device_name=%s fallback_reason=%s",
            str(NATIVE_BUILD_DIR) if NATIVE_BUILD_DIR is not None else "not_found",
            engine.engine_version,
            engine.libraw_enabled,
            cuda.mode,
            cuda.compiled,
            cuda.available,
            cuda.active,
            cuda.device_count,
            cuda.device_name or "n/a",
            cuda.fallback_reason or "none",
        )
    except Exception as exc:
        logger.warning(
            "Native engine startup probe unavailable: build_dir=%s error=%s",
            str(NATIVE_BUILD_DIR) if NATIVE_BUILD_DIR is not None else "not_found",
            exc,
        )

def _load_stock_profile(stock_id):
    return FilmStockProfile(os.path.join(STOCKS_DIR, f"{stock_id}.yaml"))

def _load_print_stock_profile(print_stock_id):
    if not print_stock_id or print_stock_id == "none":
        return None
    return PrintStockProfile(os.path.join(PRINT_STOCKS_DIR, f"{print_stock_id}.yaml"))

def linear_to_srgb(linear):
    """
    Applies the standard sRGB transfer function to convert linear RGB [0, 1] to sRGB.
    """
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(
        linear <= 0.0031308,
        12.92 * linear,
        1.055 * (linear ** (1.0 / 2.4)) - 0.055
    ).astype(np.float32)

# Helper: Downsample image to preview size (max 1024px on long edge)
def downsample_to_preview(img, max_edge=1024):
    h, w = img.shape[:2]
    if max(h, w) <= max_edge:
        return img
    scale = max_edge / max(h, w)
    new_w = int(w * scale)
    new_h = int(h * scale)
    return cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_AREA)


def _list_profiles_python():
    stocks_files = glob.glob(os.path.join(STOCKS_DIR, "*.yaml"))
    stocks = [{"id": "none", "name": "No emulation (RAW)", "type": "passthrough"}]
    for f in sorted(stocks_files):
        name = os.path.splitext(os.path.basename(f))[0]
        try:
            p = FilmStockProfile(f)
            stocks.append({
                "id": name,
                "name": p.stock_name,
                "type": p.stock_type
            })
        except Exception as exc:
            logger.warning("Skipping invalid stock profile %s: %s", f, exc)

    print_stocks_files = glob.glob(os.path.join(PRINT_STOCKS_DIR, "*.yaml"))
    print_stocks = [{"id": "none", "name": "No print finish"}]
    for f in sorted(print_stocks_files):
        try:
            p = PrintStockProfile(f)
            print_stocks.append({
                "id": p.print_stock_id,
                "name": p.print_stock_name,
            })
        except Exception as exc:
            logger.warning("Skipping invalid print stock profile %s: %s", f, exc)

    return {"stocks": stocks, "print_stocks": print_stocks}


def _list_profiles_native():
    native_session = _get_native_engine_session()
    profiles = native_session.list_profiles()

    stocks = [{"id": "none", "name": "No emulation (RAW)", "type": "passthrough"}]
    stocks.extend(
        {
            "id": stock.stock_id,
            "name": stock.stock_name,
            "type": stock.stock_type,
        }
        for stock in profiles.stocks
    )

    print_stocks = [{"id": "none", "name": "No print finish"}]
    print_stocks.extend(
        {
            "id": print_stock.print_stock_id,
            "name": print_stock.print_stock_name,
        }
        for print_stock in profiles.print_stocks
    )

    return {
        "stocks": stocks,
        "print_stocks": print_stocks,
        "engine": {
            "engine_version": profiles.engine.engine_version,
            "libraw_enabled": profiles.engine.libraw_enabled,
            "cuda_mode": profiles.engine.cuda_status.mode,
        },
    }


def _get_native_raw_preview(filename: str, *, max_edge: int = 1024) -> tuple[bytes, str]:
    native_session = _get_native_engine_session()
    native_session.select_file(filename)
    native_session.decode_raw(filename, draft_mode=True)
    preview = native_session.raw_preview(filename, max_edge=max_edge)
    return preview.jpeg_bytes, preview.content_type


def _get_native_rendered_preview(request_payload: dict) -> tuple[bytes, str]:
    native_bridge = _get_native_bridge_module()
    native_session = _get_native_engine_session()
    native_session.select_file(request_payload["filename"])
    request = native_bridge.NativePreviewRenderRequest(**request_payload)
    preview = native_session.render_preview(request)
    return preview.jpeg_bytes, preview.content_type


def _run_native_export(request_payload: dict) -> dict:
    native_bridge = _get_native_bridge_module()
    native_session = _get_native_engine_session()
    native_session.select_file(request_payload["filename"])
    request = native_bridge.NativeExportRequest(**request_payload)
    exported = native_session.export_image(request)
    return {
        "status": exported.status,
        "output_path": str(exported.output_path),
        "report_path": str(exported.report_path) if exported.report_path is not None else None,
        "format": exported.format_label,
    }


def _warm_native_select(filename: str, *, max_edge: int = 1024) -> None:
    native_session = _get_native_engine_session()
    native_session.select_file(filename)
    native_session.decode_raw(filename, draft_mode=True)
    native_session.raw_preview(filename, max_edge=max_edge)

# Pydantic schemas
class SelectRequest(BaseModel):
    filename: str

class PreviewRequest(BaseModel):
    filename: str
    stock: str
    exposure: float = 0.0    # EV stops
    highlights: float = 0.0  # -100 to +100
    shadows: float = 0.0     # -100 to +100
    blacks: float = 0.0      # -100 to +100
    whites: float = 0.0      # -100 to +100
    midtones: float = 0.0    # -100 to +100 (zone-masked gamma bend)
    contrast: float = 0.0    # -100 to +100
    temp: float = 0.0        # -100 to +100
    tint: float = 0.0        # -100 to +100
    saturation: float = 0.0  # -100 to +100
    vibrance: float = 0.0    # -100 to +100
    # Curves: JSON-encoded list of [x, y] control points, e.g. [[0,0],[0.5,0.5],[1,1]]
    curves: str = "[[0,0],[1,1]]"
    # HSL (8 hue ranges × 3 params) — all in [-100, +100]
    hsl_red_h: float = 0.0;  hsl_red_s: float = 0.0;  hsl_red_l: float = 0.0
    hsl_orange_h: float = 0.0; hsl_orange_s: float = 0.0; hsl_orange_l: float = 0.0
    hsl_yellow_h: float = 0.0; hsl_yellow_s: float = 0.0; hsl_yellow_l: float = 0.0
    hsl_green_h: float = 0.0;  hsl_green_s: float = 0.0;  hsl_green_l: float = 0.0
    hsl_aqua_h: float = 0.0;   hsl_aqua_s: float = 0.0;   hsl_aqua_l: float = 0.0
    hsl_blue_h: float = 0.0;   hsl_blue_s: float = 0.0;   hsl_blue_l: float = 0.0
    hsl_purple_h: float = 0.0; hsl_purple_s: float = 0.0; hsl_purple_l: float = 0.0
    hsl_magenta_h: float = 0.0; hsl_magenta_s: float = 0.0; hsl_magenta_l: float = 0.0
    # Detail
    clarity: float = 0.0    # -100 to +100
    texture: float = 0.0    # -100 to +100
    dehaze: float = 0.0     # -100 to +100
    sharpness: float = 0.0
    sharpness_mask: float = 0.5
    # Film effects
    bloom: float = 0.0      # 0 to 100
    adaptation: float = 1.0
    grain: str = "Auto"
    grain_strength: float = -1.0
    grain_size: float = -1.0
    grain_roughness: float = -1.0
    halation: str = "Auto"
    film_color: float = 100.0   # 0-200, scales film color personality
    print_stock: str = "none"   # print stock id or "none"
    print_strength: float = 1.0  # 0.0-2.0
    print_c: float = 0.0         # -100 to +100
    print_m: float = 0.0         # -100 to +100
    print_y: float = 0.0         # -100 to +100
    print_contrast: float = 0.0  # -100 to +100
    print_black_point: float = 0.0 # -100 to +100
    export_format: str = "tiff"   # "tiff" | "png16" | "png8"


def _apply_pre_film_sliders(rgb_input, masks, exposure, highlights, shadows,
                            blacks, whites, midtones, contrast, temp, tint, plan):
    """
    Applies all user slider adjustments to linear RGB *before* the film emulsion.
    This mirrors real film: the light that hits the emulsion is what you shaped
    with exposure compensation, WB, etc.
    """
    from dfee.color_spaces import rgb_to_oklab, oklab_to_rgb

    # 1. Exposure - linear multiplication (must stay pre-gamma)
    plan["pre_film_normalization"]["exposure_compensation_stops"] += exposure

    # Retrieve solver-solved compensations
    pre_film = plan["pre_film_normalization"]
    contrast_val = contrast + pre_film.get("contrast_compensation", 0.0)
    highlights_val = highlights + pre_film.get("highlights_compensation", 0.0)
    shadows_val = shadows + pre_film.get("shadows_compensation", 0.0)
    whites_val = whites + pre_film.get("whites_compensation", 0.0)
    blacks_val = blacks + pre_film.get("blacks_compensation", 0.0)
    midtones_val = midtones + pre_film.get("midtones_compensation", 0.0)

    # Convert to gamma-2.2 perceptual space for all tonal ops
    rgb_safe = np.clip(rgb_input, 0.0, None)
    rgb_g = np.power(rgb_safe, 1.0 / 2.2).astype(np.float32)

    # Luminance in gamma space - used for all weight functions
    Y = (0.2126 * rgb_g[:, :, 0]
       + 0.7152 * rgb_g[:, :, 1]
       + 0.0722 * rgb_g[:, :, 2])

    # 2. Contrast - tanh S-curve centred at 0.5 in gamma space
    # Positive = steeper (more contrast), negative = flatter.
    # tanh naturally prevents clipping and keeps 0 and 1 anchored.
    if contrast_val != 0.0:
        k = np.clip(contrast_val / 100.0, -1.0, 1.0) * 2.5
        if abs(k) > 0.02:
            denom = float(np.tanh(k * 0.5))
            if abs(denom) > 1e-6:
                rgb_g = np.clip(
                    0.5 + np.tanh(k * (rgb_g - 0.5)) / (2.0 * denom),
                    0.0, 1.0
                ).astype(np.float32)
                Y = (0.2126 * rgb_g[:, :, 0]
                   + 0.7152 * rgb_g[:, :, 1]
                   + 0.0722 * rgb_g[:, :, 2])

    # 3. Highlights - additive boost/crush above mid-brightness
    # Weight peaks at Y=1 (pure white), fades to 0 at Y=0.3
    if highlights_val != 0.0:
        amount = np.clip(highlights_val / 100.0, -1.0, 1.0) * 0.45
        w = np.clip((Y - 0.30) / 0.70, 0.0, 1.0) ** 1.5
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 4. Shadows - additive lift/crush in the lower tonal range
    # Weight peaks at Y=0 (pure black), fades to 0 at Y=0.65
    if shadows_val != 0.0:
        amount = np.clip(shadows_val / 100.0, -1.0, 1.0) * 0.50
        w = np.clip((0.65 - Y) / 0.65, 0.0, 1.0) ** 1.5
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 5. Whites - tighten/expand the very top of the range (Y > 0.60)
    if whites_val != 0.0:
        amount = np.clip(whites_val / 100.0, -1.0, 1.0) * 0.30
        w = np.clip((Y - 0.60) / 0.40, 0.0, 1.0) ** 2.0
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 6. Blacks - crush/lift strictly the darkest zone (Y < 0.35)
    if blacks_val != 0.0:
        amount = np.clip(blacks_val / 100.0, -1.0, 1.0) * 0.25
        w = np.clip((0.35 - Y) / 0.35, 0.0, 1.0) ** 2.0
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 7. Midtones - zone-masked gamma bend, bell centred at Y=0.5
    if midtones_val != 0.0:
        gamma = float(np.clip(1.0 / (1.0 + midtones_val * 0.01), 0.2, 5.0))
        w = np.clip(1.0 - (2.0 * Y - 1.0) ** 2, 0.0, 1.0)
        rgb_bent = np.clip(rgb_g, 1e-8, 1.0) ** gamma
        rgb_g = np.clip(
            rgb_g * (1.0 - w[:, :, np.newaxis]) + rgb_bent * w[:, :, np.newaxis],
            0.0, 1.0
        ).astype(np.float32)

    # Convert back to linear light for the film pipeline
    rgb_input = np.power(rgb_g, 2.2).astype(np.float32)

    # 8. White Balance - Temp + Tint in OKLab (pre-film)
    if temp != 0.0 or tint != 0.0:
        oklab = rgb_to_oklab(rgb_input)
        oklab[:, :, 2] += temp * 0.0008   # b axis: warm/yellow
        oklab[:, :, 1] += tint * 0.0008   # a axis: magenta/green
        rgb_input = oklab_to_rgb(oklab)

    return rgb_input

@app.get("/api/files")
def list_files():
    logger.info("Listing RAW files from %s", RAW_DIR)
    # Scan raw files folder for .ARW
    files = glob.glob(os.path.join(RAW_DIR, "*.[aA][rR][wW]"))
    results = []
    for f in files:
        stat = os.stat(f)
        results.append({
            "filename": os.path.basename(f),
            "size_mb": round(stat.st_size / (1024 * 1024), 2),
            "modified": stat.st_mtime
        })
    return results

@app.get("/api/profiles")
def list_profiles():
    use_native = _native_profiles_enabled()
    logger.info("Listing available stock and print profiles (native=%s)", use_native)

    if use_native:
        try:
            native_payload = _list_profiles_native()
            engine = native_payload.pop("engine")
            logger.info(
                "Listed profiles via native engine version=%s cuda_mode=%s libraw_enabled=%s",
                engine["engine_version"],
                engine["cuda_mode"],
                engine["libraw_enabled"],
            )
            return native_payload
        except Exception:
            logger.exception("Native profile listing failed, falling back to Python profile loader")
            return _list_profiles_python()
    else:
        return _list_profiles_python()

    logger.info("Listing available stock and print profiles")
    # List stocks — prepend a 'none' passthrough option
    stocks_files = glob.glob(os.path.join(STOCKS_DIR, "*.yaml"))
    stocks = [{"id": "none", "name": "No emulation (RAW)", "type": "passthrough"}]
    for f in sorted(stocks_files):
        name = os.path.splitext(os.path.basename(f))[0]
        try:
            p = FilmStockProfile(f)
            stocks.append({
                "id": name,
                "name": p.stock_name,   # use the human-readable name from YAML
                "type": p.stock_type
            })
        except Exception as exc:
            logger.warning("Skipping invalid stock profile %s: %s", f, exc)
            

    # List print stocks
    print_stocks_files = glob.glob(os.path.join(PRINT_STOCKS_DIR, "*.yaml"))
    print_stocks = [{"id": "none", "name": "No print finish"}]
    for f in sorted(print_stocks_files):
        try:
            p = PrintStockProfile(f)
            print_stocks.append({
                "id": p.print_stock_id,
                "name": p.print_stock_name,
            })
        except Exception as exc:
            logger.warning("Skipping invalid print stock profile %s: %s", f, exc)

    return {"stocks": stocks, "print_stocks": print_stocks}


def _apply_post_film_color(rendered, saturation, vibrance):
    """
    Post-film colour density adjustments applied to the fully rendered linear image.

    Saturation  — global OKLCH chroma scale. Positive = richer colours everywhere,
                  negative = desaturate. Simple and predictable.

    Vibrance    — smart saturation. Boosts muted/dull colours more than vivid ones,
                  and protects skin-tone hues from over-saturation. Mimics the way
                  film dye layers have a natural saturation ceiling.

                  Formula per pixel:
                    C_ref   = 0.22 (typical max chroma for natural scenes in OKLCH)
                    dullness = 1 - clip(C / C_ref, 0, 1)   [1 for grey, 0 for vivid]
                    skin_w   = cos(H - 0.55)^4              [1 at warm skin hue, 0 elsewhere]
                    vib_mult = 1 + factor * dullness * (1 - 0.6 * skin_w)
    """
    from dfee.color_spaces import rgb_to_oklab, oklab_to_rgb, oklab_to_oklch, oklch_to_oklab

    if saturation == 0 and vibrance == 0:
        return rendered

    oklab = rgb_to_oklab(rendered)
    lch   = oklab_to_oklch(oklab)

    C = lch[:, :, 1].copy()
    H = lch[:, :, 2]

    # ── Saturation (global uniform chroma scale) ──────────────────────────────
    if saturation != 0:
        sat_factor = saturation / 100.0          # -1 … +1
        # Map linearly: +100 → ×2.0, -100 → ×0.0
        C = C * (1.0 + sat_factor)

    # ── Vibrance (chroma-weighted, skin-protected) ────────────────────────────
    if vibrance != 0:
        vib_factor = vibrance / 100.0            # -1 … +1

        # How "dull" is this pixel? Near 0 C = very dull (gets max boost),
        # near C_ref = already vivid (gets no boost).
        C_ref    = 0.22                          # natural scene chroma reference
        dullness = 1.0 - np.clip(C / C_ref, 0.0, 1.0)

        # Skin-tone protection: warm orange-pink hue in OKLCH ≈ 0.3–0.8 rad.
        # We reduce vibrance for those hues so skin stays natural.
        skin_hue_center = 0.55
        skin_w = np.clip(np.cos(H - skin_hue_center), 0.0, 1.0) ** 4
        protection = 1.0 - skin_w * 0.6         # 0.4 at peak skin hue, 1.0 elsewhere

        vib_mult = 1.0 + vib_factor * dullness * protection
        C = C * np.clip(vib_mult, 0.01, 4.0)

    lch_new = lch.copy()
    lch_new[:, :, 1] = np.clip(C, 0.0, None)
    oklab_new = oklch_to_oklab(lch_new)
    oklab_new[:, :, 0] = oklab[:, :, 0]
    return oklab_to_rgb(oklab_new)


def _apply_post_film_effects(rendered, curves_json, hsl_dict,
                             clarity, texture, dehaze, bloom):
    """
    Orchestrates all post-film creative effects in the correct order.
    All effects are no-ops when at default values so performance is unaffected.
    """
    import json
    from dfee.post_effects import (apply_curves, apply_hsl, apply_clarity,
                                   apply_texture, apply_dehaze, apply_bloom)

    # 1. Curves
    try:
        pts = json.loads(curves_json) if isinstance(curves_json, str) else curves_json
        if pts and not (len(pts) == 2 and pts[0] == [0, 0] and pts[1] == [1, 1]):
            rendered = apply_curves(rendered, pts)
            rendered = np.clip(rendered, 0.0, 1.0)
    except Exception:
        pass

    # 2. HSL
    if hsl_dict and any(float(v) != 0 for v in hsl_dict.values()):
        rendered = apply_hsl(rendered, hsl_dict)
        rendered = np.clip(rendered, 0.0, 1.0)

    # 3. Clarity
    if clarity != 0:
        rendered = apply_clarity(rendered, clarity)
        rendered = np.clip(rendered, 0.0, 1.0)

    # 4. Texture
    if texture != 0:
        rendered = apply_texture(rendered, texture)
        rendered = np.clip(rendered, 0.0, 1.0)

    # 5. Dehaze
    if dehaze != 0:
        rendered = apply_dehaze(rendered, dehaze)
        rendered = np.clip(rendered, 0.0, 1.0)

    # 6. Bloom
    if bloom > 0:
        rendered = apply_bloom(rendered, bloom)
        rendered = np.clip(rendered, 0.0, 1.0)

    return rendered


@app.post("/api/select")
def select_file(req: SelectRequest):
    use_native = _native_select_enabled()
    logger.info("Selecting RAW file %s (native=%s)", req.filename, use_native)
    filepath = os.path.join(RAW_DIR, req.filename)
    if not os.path.exists(filepath):
        logger.warning("Select failed, file not found: %s", req.filename)
        raise HTTPException(status_code=404, detail="File not found")
        
    try:
        # --- Analysis cache: skip expensive reprocessing if same file is already loaded ---
        if session.filename == req.filename and session.cached_diagnostics is not None:
            logger.info("Returning cached analysis for %s", req.filename)
            return {
                "status": "loaded",
                "metadata": {k: str(v) for k, v in session.metadata.items()},
                "diagnostics": session.cached_diagnostics
            }

        if use_native:
            try:
                _warm_native_select(req.filename, max_edge=1024)
                logger.info("Warmed native select state for %s", req.filename)
            except Exception:
                logger.exception("Native select warmup failed for %s, continuing with Python analysis path", req.filename)

        # Ingest in draft mode (half_size) for quick loading
        ingestor = RawIngestor(filepath)
        rgb_linear, Y, clipping_masks, clipping_ratios, metadata = ingestor.ingest(draft_mode=True)
        
        # Scale to max 1024px for real-time preview responsiveness
        h, w = Y.shape
        max_edge = 1024
        if max(h, w) > max_edge:
            scale = max_edge / max(h, w)
            preview_rgb = cv2.resize(rgb_linear, (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
            preview_Y = cv2.resize(Y, (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
            preview_clip = {
                "R": cv2.resize(clipping_masks["R"].astype(np.uint8), (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST) > 0,
                "G": cv2.resize(clipping_masks["G"].astype(np.uint8), (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST) > 0,
                "B": cv2.resize(clipping_masks["B"].astype(np.uint8), (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST) > 0
            }
        else:
            preview_rgb = rgb_linear.copy()
            preview_Y = Y.copy()
            preview_clip = clipping_masks
            
        # Analyze using downscaled preview canvas
        analyzer = ImageStateAnalyzer()
        feature_dict, masks = analyzer.analyze(preview_rgb, preview_Y, preview_clip, clipping_ratios)
        feature_dict["raw_metadata"] = metadata
        
        bias_estimator = CameraBiasEstimator()
        bias_info = bias_estimator.estimate_bias(preview_rgb, preview_Y, preview_clip, masks["luminance_zone_masks"])
        feature_dict["camera_input_bias"] = bias_info
        
        # Save to Active Session
        session.filename = req.filename
        session.rgb_linear = rgb_linear
        session.Y = Y
        session.clipping_masks = clipping_masks
        session.clipping_ratios = clipping_ratios
        session.metadata = metadata
        session.preview_rgb_linear = preview_rgb
        session.preview_Y = preview_Y
        session.preview_clipping_masks = preview_clip
        session.feature_dict = feature_dict
        session.masks = masks

        # Reset full-res cache on new file selection
        session.fullres_rgb_linear = None
        session.fullres_Y = None
        session.fullres_clipping_masks = None
        session.fullres_clipping_ratios = None
        session.fullres_metadata = None
        session.fullres_feature_dict = None
        session.fullres_masks = None
        
        # Build and cache raw preview JPEG
        raw_display = linear_to_srgb(preview_rgb)
        raw_display_uint8 = (raw_display * 255.0).astype(np.uint8)
        _, raw_jpeg_bytes = cv2.imencode('.jpg', cv2.cvtColor(raw_display_uint8, cv2.COLOR_RGB2BGR))
        session.raw_preview_bytes = raw_jpeg_bytes.tobytes()
        
        # Build and cache diagnostics dict
        tonal = feature_dict["tonal_distribution"]
        color = feature_dict["hue_saturation_state"]
        spatial = feature_dict["spatial_frequency"]
        diagnostics = {
            "tonal_skew": tonal["tonal_skew"],
            "dynamic_range_stops": round(tonal["dynamic_range_stops"], 2),
            "midtone_anchor": round(tonal["midtone_anchor"], 3),
            "highlight_headroom": round(tonal["highlight_headroom"], 3),
            "shadow_depth": round(tonal["shadow_depth"], 3),
            "neon_risk": round(color["neon_risk"], 3),
            "dominant_hues": color["dominant_hue_bins"],
            "palette_entropy": round(color["hue_entropy"], 2),
            "specular_ratio": round(spatial["specular_point_ratio"], 4),
            "neutral_confidence": round(bias_info["neutral_confidence"], 2)
        }
        session.cached_diagnostics = diagnostics
        
        return {
            "status": "loaded",
            "metadata": {k: str(v) for k, v in metadata.items()},
            "diagnostics": diagnostics
        }
    except Exception as e:
        logger.exception("Failed to ingest RAW file %s", req.filename)
        raise HTTPException(status_code=500, detail=f"Failed to ingest RAW file: {str(e)}")

@app.get("/api/raw-image")
def get_raw_image():
    use_native = _native_raw_image_enabled()
    logger.info("Serving cached RAW preview for %s (native=%s)", session.filename, use_native)
    if not session.filename and not session.raw_preview_bytes:
        logger.warning("RAW preview requested with no active session")
        raise HTTPException(status_code=400, detail="No active session loaded")

    if use_native and session.filename:
        try:
            jpeg_bytes, content_type = _get_native_raw_preview(session.filename, max_edge=1024)
            logger.info("Served RAW preview via native engine for %s", session.filename)
            return StreamingResponse(io.BytesIO(jpeg_bytes), media_type=content_type)
        except Exception:
            logger.exception("Native RAW preview failed for %s, falling back to Python cache", session.filename)

    if not session.raw_preview_bytes:
        logger.warning("RAW preview cache unavailable for active session %s", session.filename)
        raise HTTPException(status_code=400, detail="No active session loaded")
    return StreamingResponse(io.BytesIO(session.raw_preview_bytes), media_type="image/jpeg")

@app.get("/api/preview")
def get_preview(
    filename: str,
    stock: str,
    exposure: float = 0.0,
    highlights: float = 0.0,
    shadows: float = 0.0,
    blacks: float = 0.0,
    whites: float = 0.0,
    midtones: float = 0.0,
    contrast: float = 0.0,
    temp: float = 0.0,
    tint: float = 0.0,
    saturation: float = 0.0,
    vibrance: float = 0.0,
    curves: str = "[[0,0],[1,1]]",
    hsl_red_h: float = 0.0,    hsl_red_s: float = 0.0,    hsl_red_l: float = 0.0,
    hsl_orange_h: float = 0.0, hsl_orange_s: float = 0.0, hsl_orange_l: float = 0.0,
    hsl_yellow_h: float = 0.0, hsl_yellow_s: float = 0.0, hsl_yellow_l: float = 0.0,
    hsl_green_h: float = 0.0,  hsl_green_s: float = 0.0,  hsl_green_l: float = 0.0,
    hsl_aqua_h: float = 0.0,   hsl_aqua_s: float = 0.0,   hsl_aqua_l: float = 0.0,
    hsl_blue_h: float = 0.0,   hsl_blue_s: float = 0.0,   hsl_blue_l: float = 0.0,
    hsl_purple_h: float = 0.0, hsl_purple_s: float = 0.0, hsl_purple_l: float = 0.0,
    hsl_magenta_h: float = 0.0,hsl_magenta_s: float = 0.0,hsl_magenta_l: float = 0.0,
    clarity: float = 0.0,
    texture: float = 0.0,
    dehaze: float = 0.0,
    sharpness: float = 0.0,
    sharpness_mask: float = 0.5,
    bloom: float = 0.0,
    adaptation: float = 1.0,
    grain: str = "Auto",
    grain_strength: float = -1.0,
    grain_size: float = -1.0,
    grain_roughness: float = -1.0,
    halation: str = "Auto",
    film_color: float = 100.0,
    print_stock: str = "none",
    print_strength: float = 1.0,
    print_c: float = 0.0,
    print_m: float = 0.0,
    print_y: float = 0.0,
    print_contrast: float = 0.0,
    print_black_point: float = 0.0
):
    use_native = _native_preview_enabled()
    logger.info("Rendering preview for file=%s stock=%s print_stock=%s (native=%s)", filename, stock, print_stock, use_native)
    if session.filename != filename:
        logger.warning("Preview session mismatch: active=%s requested=%s", session.filename, filename)
        raise HTTPException(status_code=400, detail=f"Session mismatch: server has '{session.filename}', requested '{filename}'. Select the file first.")

    # Passthrough: no stock selected — return the cached raw preview directly
    if stock == "none":
        if not session.raw_preview_bytes:
            logger.warning("RAW passthrough preview requested without active session")
            raise HTTPException(status_code=400, detail="No active session")
        return StreamingResponse(io.BytesIO(session.raw_preview_bytes), media_type="image/jpeg")

    if use_native:
        native_payload = {
            "filename": filename,
            "stock": stock,
            "exposure": exposure,
            "highlights": highlights,
            "shadows": shadows,
            "blacks": blacks,
            "whites": whites,
            "midtones": midtones,
            "contrast": contrast,
            "temp": temp,
            "tint": tint,
            "saturation": saturation,
            "vibrance": vibrance,
            "curves": curves,
            "hsl_red_h": hsl_red_h,
            "hsl_red_s": hsl_red_s,
            "hsl_red_l": hsl_red_l,
            "hsl_orange_h": hsl_orange_h,
            "hsl_orange_s": hsl_orange_s,
            "hsl_orange_l": hsl_orange_l,
            "hsl_yellow_h": hsl_yellow_h,
            "hsl_yellow_s": hsl_yellow_s,
            "hsl_yellow_l": hsl_yellow_l,
            "hsl_green_h": hsl_green_h,
            "hsl_green_s": hsl_green_s,
            "hsl_green_l": hsl_green_l,
            "hsl_aqua_h": hsl_aqua_h,
            "hsl_aqua_s": hsl_aqua_s,
            "hsl_aqua_l": hsl_aqua_l,
            "hsl_blue_h": hsl_blue_h,
            "hsl_blue_s": hsl_blue_s,
            "hsl_blue_l": hsl_blue_l,
            "hsl_purple_h": hsl_purple_h,
            "hsl_purple_s": hsl_purple_s,
            "hsl_purple_l": hsl_purple_l,
            "hsl_magenta_h": hsl_magenta_h,
            "hsl_magenta_s": hsl_magenta_s,
            "hsl_magenta_l": hsl_magenta_l,
            "clarity": clarity,
            "texture": texture,
            "dehaze": dehaze,
            "sharpness": sharpness,
            "sharpness_mask": sharpness_mask,
            "bloom": bloom,
            "adaptation": adaptation,
            "grain": grain,
            "grain_strength": grain_strength,
            "grain_size": grain_size,
            "grain_roughness": grain_roughness,
            "halation": halation,
            "film_color": film_color,
            "print_stock": print_stock,
            "print_strength": print_strength,
            "print_c": print_c,
            "print_m": print_m,
            "print_y": print_y,
            "print_contrast": print_contrast,
            "print_black_point": print_black_point,
        }
        try:
            jpeg_bytes, content_type = _get_native_rendered_preview(native_payload)
            logger.info("Served rendered preview via native engine for %s", filename)
            return StreamingResponse(io.BytesIO(jpeg_bytes), media_type=content_type)
        except Exception:
            logger.exception("Native preview render failed for %s, falling back to Python pipeline", filename)

    try:
        stock_profile = _load_stock_profile(stock)

        solver = RenderPlanSolver()
        user_overrides = {
            "adaptation_strength": adaptation,
            "exposure_intent": "Preserve",
            "grain_amount": grain,
            "grain_strength": grain_strength,
            "grain_size": grain_size,
            "grain_roughness": grain_roughness,
            "halation_amount": halation,
            "sharpness": sharpness,
            "sharpness_mask": sharpness_mask,
            "film_color": film_color,
            "print_stock": _load_print_stock_profile(print_stock),
            "print_strength": print_strength,
            "print_c": print_c,
            "print_m": print_m,
            "print_y": print_y,
            "print_contrast": print_contrast,
            "print_black_point": print_black_point,
        }
        render_plan = solver.solve(session.feature_dict, stock_profile, user_overrides)

        # Apply all pre-film slider adjustments (corrected pipeline order)
        rgb_input = session.preview_rgb_linear.copy()
        rgb_input = _apply_pre_film_sliders(
            rgb_input, session.masks,
            exposure, highlights, shadows,
            blacks, whites, midtones, contrast, temp, tint, render_plan
        )

        renderer = FilmRenderer()
        rendered = renderer.render(rgb_input, session.masks, render_plan)
        rendered = np.clip(rendered, 0.0, 1.0)

        # Post-film saturation / vibrance
        rendered = _apply_post_film_color(rendered, saturation, vibrance)
        rendered = np.clip(rendered, 0.0, 1.0)

        # Post-film creative effects (curves, HSL, clarity, texture, dehaze, bloom)
        hsl_dict = {
            'red_h': hsl_red_h, 'red_s': hsl_red_s, 'red_l': hsl_red_l,
            'orange_h': hsl_orange_h, 'orange_s': hsl_orange_s, 'orange_l': hsl_orange_l,
            'yellow_h': hsl_yellow_h, 'yellow_s': hsl_yellow_s, 'yellow_l': hsl_yellow_l,
            'green_h': hsl_green_h, 'green_s': hsl_green_s, 'green_l': hsl_green_l,
            'aqua_h': hsl_aqua_h, 'aqua_s': hsl_aqua_s, 'aqua_l': hsl_aqua_l,
            'blue_h': hsl_blue_h, 'blue_s': hsl_blue_s, 'blue_l': hsl_blue_l,
            'purple_h': hsl_purple_h, 'purple_s': hsl_purple_s, 'purple_l': hsl_purple_l,
            'magenta_h': hsl_magenta_h, 'magenta_s': hsl_magenta_s, 'magenta_l': hsl_magenta_l,
        }
        rendered = _apply_post_film_effects(rendered, curves, hsl_dict,
                                            clarity, texture, dehaze, bloom)
        rendered = np.clip(rendered, 0.0, 1.0)

        # Gamma-encode to sRGB before JPEG — the renderer outputs linear light;
        # JPEG viewers expect gamma-encoded values. Without this, everything
        # appears drastically darker (linear 0.18 → 46/255 instead of 118/255).
        rendered_display = linear_to_srgb(rendered)
        rendered_uint8   = (rendered_display * 255.0).astype(np.uint8)
        _, jpeg_bytes    = cv2.imencode('.jpg', cv2.cvtColor(rendered_uint8, cv2.COLOR_RGB2BGR))

        return StreamingResponse(io.BytesIO(jpeg_bytes.tobytes()), media_type="image/jpeg")
    except Exception as e:
        logger.exception("Failed to render preview for file=%s stock=%s", filename, stock)
        raise HTTPException(status_code=500, detail=f"Failed to render preview: {str(e)}")

@app.post("/api/export")
def export_file(req: PreviewRequest):
    logger.info(
        "Export requested for file=%s stock=%s print_stock=%s format=%s (native=%s)",
        req.filename, req.stock, req.print_stock, req.export_format, _native_export_enabled()
    )
    filepath = os.path.join(RAW_DIR, req.filename)
    if not os.path.exists(filepath):
        logger.warning("Export failed, file not found: %s", req.filename)
        raise HTTPException(status_code=404, detail="File not found")

    if _native_export_enabled():
        try:
            native_result = _run_native_export(req.model_dump())
            logger.info("Export completed via native engine for %s", req.filename)
            return native_result
        except Exception:
            logger.exception("Native export failed for %s, falling back to Python pipeline", req.filename)

    try:
        stock_profile = _load_stock_profile(req.stock) if req.stock != "none" else None
        print_stock_profile = _load_print_stock_profile(req.print_stock)

        basename    = os.path.splitext(req.filename)[0]
        report_path = os.path.join(RAW_DIR, f"{basename}_{req.stock}_report.json")
        # output_path/filename set in the format block below

        # ── Use cached full-res data if available (avoids re-reading RAW from disk) ──
        if session.filename == req.filename and session.fullres_rgb_linear is not None:
            logger.info("Using cached full-resolution RGB for %s", req.filename)
            rgb_linear      = session.fullres_rgb_linear
            Y               = session.fullres_Y
            clipping_masks  = session.fullres_clipping_masks
            clipping_ratios = session.fullres_clipping_ratios
            metadata        = session.fullres_metadata
        else:
            logger.info("Full-resolution cache miss for %s, ingesting from disk", req.filename)
            ingestor = RawIngestor(filepath)
            rgb_linear, Y, clipping_masks, clipping_ratios, metadata = ingestor.ingest(draft_mode=False)
            if session.filename == req.filename:
                session.fullres_rgb_linear      = rgb_linear
                session.fullres_Y               = Y
                session.fullres_clipping_masks  = clipping_masks
                session.fullres_clipping_ratios = clipping_ratios
                session.fullres_metadata        = metadata

        # ── Full-res analysis — cached after first export ────────────────────────
        if (session.filename == req.filename
                and session.fullres_feature_dict is not None
                and session.fullres_masks is not None):
            logger.info("Using cached full-resolution analysis for %s", req.filename)
            feature_dict = session.fullres_feature_dict
            masks        = session.fullres_masks
        else:
            logger.info("Running full-resolution analysis for %s", req.filename)
            analyzer = ImageStateAnalyzer()
            feature_dict, masks = analyzer.analyze(rgb_linear, Y, clipping_masks, clipping_ratios)
            bias_estimator = CameraBiasEstimator()
            bias_info = bias_estimator.estimate_bias(rgb_linear, Y, clipping_masks, masks["luminance_zone_masks"])
            feature_dict["camera_input_bias"] = bias_info
            feature_dict["raw_metadata"]      = metadata
            # Cache for subsequent exports of the same file
            if session.filename == req.filename:
                session.fullres_feature_dict = feature_dict
                session.fullres_masks        = masks

        render_plan = None
        rendered = np.clip(rgb_linear.copy(), 0.0, 1.0)

        if stock_profile is not None:
            solver = RenderPlanSolver()
            user_overrides = {
                "adaptation_strength": req.adaptation,
                "exposure_intent": "Preserve",
                "grain_amount": req.grain,
                "grain_strength": req.grain_strength,
                "grain_size": req.grain_size,
                "grain_roughness": req.grain_roughness,
                "halation_amount": req.halation,
                "sharpness": req.sharpness,
                "sharpness_mask": req.sharpness_mask,
                "film_color": req.film_color,
                "print_stock": print_stock_profile,
                "print_strength": req.print_strength,
                "print_c": req.print_c,
                "print_m": req.print_m,
                "print_y": req.print_y,
                "print_contrast": req.print_contrast,
                "print_black_point": req.print_black_point,
            }
            render_plan = solver.solve(feature_dict, stock_profile, user_overrides)

            logger.info("Rendering full-resolution emulation for %s", req.filename)
            rgb_input = _apply_pre_film_sliders(
                rgb_linear.copy(), masks,
                req.exposure, req.highlights, req.shadows,
                req.blacks, req.whites, req.midtones,
                req.contrast, req.temp, req.tint, render_plan
            )

            renderer = FilmRenderer()
            rendered  = renderer.render(rgb_input, masks, render_plan)
            rendered  = np.clip(rendered, 0.0, 1.0)

            rendered = _apply_post_film_color(rendered, req.saturation, req.vibrance)
            rendered = np.clip(rendered, 0.0, 1.0)

            hsl_dict_exp = {
                'red_h': req.hsl_red_h, 'red_s': req.hsl_red_s, 'red_l': req.hsl_red_l,
                'orange_h': req.hsl_orange_h, 'orange_s': req.hsl_orange_s, 'orange_l': req.hsl_orange_l,
                'yellow_h': req.hsl_yellow_h, 'yellow_s': req.hsl_yellow_s, 'yellow_l': req.hsl_yellow_l,
                'green_h': req.hsl_green_h, 'green_s': req.hsl_green_s, 'green_l': req.hsl_green_l,
                'aqua_h': req.hsl_aqua_h, 'aqua_s': req.hsl_aqua_s, 'aqua_l': req.hsl_aqua_l,
                'blue_h': req.hsl_blue_h, 'blue_s': req.hsl_blue_s, 'blue_l': req.hsl_blue_l,
                'purple_h': req.hsl_purple_h, 'purple_s': req.hsl_purple_s, 'purple_l': req.hsl_purple_l,
                'magenta_h': req.hsl_magenta_h, 'magenta_s': req.hsl_magenta_s, 'magenta_l': req.hsl_magenta_l,
            }
            rendered = _apply_post_film_effects(rendered, req.curves, hsl_dict_exp,
                                                req.clarity, req.texture, req.dehaze, req.bloom)
            rendered = np.clip(rendered, 0.0, 1.0)
        else:
            logger.info("Exporting RAW passthrough image for %s", req.filename)
            report_path = None

        # ── Determine output format ────────────────────────────────────────
        fmt = (req.export_format or "tiff").lower().strip()
        if fmt not in ("tiff", "png16", "png8"):
            fmt = "tiff"

        rendered_clipped = np.clip(rendered, 0.0, 1.0)
        srgb_data = linear_to_srgb(rendered_clipped)

        if fmt == "tiff":
            output_filename = f"{basename}_{req.stock}_dfee.tif"
            output_path     = os.path.join(RAW_DIR, output_filename)
            logger.info("Saving 16-bit TIFF to %s", output_path)
            uint16_data = (srgb_data * 65535.0).astype(np.uint16)
            uint16_data = np.ascontiguousarray(uint16_data)
            import tifffile
            tifffile.imwrite(output_path, uint16_data, photometric='rgb', compression=None)
            format_label = "16-bit TIFF"

        elif fmt == "png16":
            output_filename = f"{basename}_{req.stock}_dfee_16.png"
            output_path     = os.path.join(RAW_DIR, output_filename)
            logger.info("Saving 16-bit PNG to %s", output_path)
            uint16_data = (srgb_data * 65535.0).astype(np.uint16)
            uint16_data = np.ascontiguousarray(uint16_data)
            import tifffile
            # tifffile writes 16-bit PNG correctly
            tifffile.imwrite(output_path, uint16_data, photometric='rgb')
            format_label = "16-bit PNG"

        else:  # png8
            output_filename = f"{basename}_{req.stock}_dfee.png"
            output_path     = os.path.join(RAW_DIR, output_filename)
            logger.info("Saving 8-bit PNG to %s", output_path)
            uint8_data = (srgb_data * 255.0).astype(np.uint8)
            img = Image.fromarray(uint8_data, mode="RGB")
            img.save(output_path, format="PNG", optimize=False, compress_level=1)
            format_label = "8-bit PNG"

        if render_plan is not None and report_path is not None:
            reporter = RenderReporter()
            reporter.write_report(
                filepath, output_path, stock_profile, print_stock_profile,
                feature_dict, render_plan, report_path
            )

        return {
            "status": "success",
            "output_path": output_path,
            "report_path": report_path,
            "format": format_label,
        }
    except Exception as e:
        logger.exception("Failed to export image for %s", req.filename)
        raise HTTPException(status_code=500, detail=f"Failed to export image: {str(e)}")

ASSETS_DIR = os.path.join(BASE_DIR, "frontend", "dist", "assets")
if os.path.exists(ASSETS_DIR):
    app.mount("/assets", StaticFiles(directory=ASSETS_DIR), name="assets")

@app.get("/")
def serve_index():
    logger.info("Serving frontend index")
    index_path = os.path.join(BASE_DIR, "frontend", "dist", "index.html")
    if os.path.exists(index_path):
        with open(index_path, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read(), status_code=200)
    return HTMLResponse(
        content="<h3>DFEE API Backend is Running</h3><p>To view the web UI, build the frontend by running <code>npm run build</code> under the <code>frontend/</code> directory.</p>",
        status_code=200
    )

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="127.0.0.1", port=8000)
