import os
import glob
import io
import cv2
import numpy as np
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import StreamingResponse
from pydantic import BaseModel
from PIL import Image

from dfee.ingest import RawIngestor
from dfee.analyzer import ImageStateAnalyzer
from dfee.bias import CameraBiasEstimator
from dfee.solver import RenderPlanSolver
from dfee.renderer import FilmRenderer
from dfee.profile import FilmStockProfile, ScanPrintProfile
from dfee.report import RenderReporter

app = FastAPI(title="Deterministic Film Emulation Engine (DFEE) Server")

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
SCANNERS_DIR = os.path.join(BASE_DIR, "profiles", "scanners")

# Ensure directories exist
os.makedirs(RAW_DIR, exist_ok=True)

# Active session cache to keep slider adjustments real-time
class ActiveSession:
    def __init__(self):
        self.filename = None
        # Full-res linear data
        self.rgb_linear = None
        self.Y = None
        self.clipping_masks = None
        self.clipping_ratios = None
        self.metadata = None
        
        # Preview-res linear data (for fast renders)
        self.preview_rgb_linear = None
        self.preview_Y = None
        self.preview_clipping_masks = None
        
        # Cached analysis features
        self.feature_dict = None
        self.masks = None
        self.cached_diagnostics = None  # Cached so re-selecting same file is instant
        
        # Raw before preview image (JPEG bytes)
        self.raw_preview_bytes = None

session = ActiveSession()

# Helper: Downsample image to preview size (max 1024px on long edge)
def downsample_to_preview(img, max_edge=1024):
    h, w = img.shape[:2]
    if max(h, w) <= max_edge:
        return img
    scale = max_edge / max(h, w)
    new_w = int(w * scale)
    new_h = int(h * scale)
    return cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_AREA)

# Pydantic schemas
class SelectRequest(BaseModel):
    filename: str

class PreviewRequest(BaseModel):
    filename: str
    stock: str
    scanner: str
    exposure: float = 0.0    # EV stops
    highlights: float = 0.0  # -100 to +100
    shadows: float = 0.0     # -100 to +100
    blacks: float = 0.0      # -100 to +100
    whites: float = 0.0      # -100 to +100
    midtones: float = 0.0    # -100 to +100 (zone-masked gamma bend)
    contrast: float = 0.0    # -100 to +100
    temp: float = 0.0        # -100 to +100
    tint: float = 0.0        # -100 to +100
    adaptation: float = 1.0  # 0.0 to 1.0
    grain: str = "Auto"
    halation: str = "Auto"


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
    if contrast != 0.0:
        k = np.clip(contrast / 100.0, -1.0, 1.0) * 2.5
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
    if highlights != 0.0:
        amount = np.clip(highlights / 100.0, -1.0, 1.0) * 0.45
        w = np.clip((Y - 0.30) / 0.70, 0.0, 1.0) ** 1.5
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 4. Shadows - additive lift/crush in the lower tonal range
    # Weight peaks at Y=0 (pure black), fades to 0 at Y=0.65
    if shadows != 0.0:
        amount = np.clip(shadows / 100.0, -1.0, 1.0) * 0.50
        w = np.clip((0.65 - Y) / 0.65, 0.0, 1.0) ** 1.5
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 5. Whites - tighten/expand the very top of the range (Y > 0.60)
    if whites != 0.0:
        amount = np.clip(whites / 100.0, -1.0, 1.0) * 0.30
        w = np.clip((Y - 0.60) / 0.40, 0.0, 1.0) ** 2.0
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 6. Blacks - crush/lift strictly the darkest zone (Y < 0.35)
    if blacks != 0.0:
        amount = np.clip(blacks / 100.0, -1.0, 1.0) * 0.25
        w = np.clip((0.35 - Y) / 0.35, 0.0, 1.0) ** 2.0
        rgb_g = np.clip(rgb_g + amount * w[:, :, np.newaxis], 0.0, 1.0)
        Y = (0.2126 * rgb_g[:, :, 0]
           + 0.7152 * rgb_g[:, :, 1]
           + 0.0722 * rgb_g[:, :, 2])

    # 7. Midtones - zone-masked gamma bend, bell centred at Y=0.5
    if midtones != 0.0:
        gamma = float(np.clip(1.0 / (1.0 + midtones * 0.01), 0.2, 5.0))
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
    # List stocks — prepend a 'none' passthrough option
    stocks_files = glob.glob(os.path.join(STOCKS_DIR, "*.yaml"))
    stocks = [{"id": "none", "name": "No emulation (RAW)", "type": "passthrough"}]
    for f in stocks_files:
        name = os.path.splitext(os.path.basename(f))[0]
        try:
            p = FilmStockProfile(f)
            stocks.append({
                "id": name,
                "name": name.replace("_", " ").title(),
                "type": p.stock_type
            })
        except:
            pass
            
    # List scanners — prepend a 'none' passthrough option
    scanners_files = glob.glob(os.path.join(SCANNERS_DIR, "*.yaml"))
    scanners = [{"id": "none", "name": "No scan finish"}]
    for f in scanners_files:
        name = os.path.splitext(os.path.basename(f))[0]
        try:
            scanners.append({
                "id": name,
                "name": name.replace("_", " ").title()
            })
        except:
            pass
            
    return {"stocks": stocks, "scanners": scanners}

@app.post("/api/select")
def select_file(req: SelectRequest):
    filepath = os.path.join(RAW_DIR, req.filename)
    if not os.path.exists(filepath):
        raise HTTPException(status_code=404, detail="File not found")
        
    try:
        # --- Analysis cache: skip expensive reprocessing if same file is already loaded ---
        if session.filename == req.filename and session.cached_diagnostics is not None:
            print(f"[cache hit] Returning cached analysis for: {req.filename}")
            return {
                "status": "loaded",
                "metadata": {k: str(v) for k, v in session.metadata.items()},
                "diagnostics": session.cached_diagnostics
            }

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
        
        # Build and cache raw preview JPEG
        raw_display = np.clip(preview_rgb ** (1.0 / 2.2), 0.0, 1.0)
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
        import traceback
        traceback.print_exc()
        raise HTTPException(status_code=500, detail=f"Failed to ingest RAW file: {str(e)}")

@app.get("/api/raw-image")
def get_raw_image():
    if not session.raw_preview_bytes:
        raise HTTPException(status_code=400, detail="No active session loaded")
    return StreamingResponse(io.BytesIO(session.raw_preview_bytes), media_type="image/jpeg")

@app.get("/api/preview")
def get_preview(
    filename: str,
    stock: str,
    scanner: str,
    exposure: float = 0.0,
    highlights: float = 0.0,
    shadows: float = 0.0,
    blacks: float = 0.0,
    whites: float = 0.0,
    midtones: float = 0.0,
    contrast: float = 0.0,
    temp: float = 0.0,
    tint: float = 0.0,
    adaptation: float = 1.0,
    grain: str = "Auto",
    halation: str = "Auto"
):
    if session.filename != filename:
        raise HTTPException(status_code=400, detail=f"Session mismatch: server has '{session.filename}', requested '{filename}'. Select the file first.")

    # Passthrough: no stock selected — return the cached raw preview directly
    if stock == "none":
        if not session.raw_preview_bytes:
            raise HTTPException(status_code=400, detail="No active session")
        return StreamingResponse(io.BytesIO(session.raw_preview_bytes), media_type="image/jpeg")

    # Scanner passthrough
    if scanner == "none":
        scan_path = os.path.join(SCANNERS_DIR, "frontier_soft.yaml")
    else:
        scan_path = os.path.join(SCANNERS_DIR, f"{scanner}.yaml")

    try:
        stock_path   = os.path.join(STOCKS_DIR, f"{stock}.yaml")
        stock_profile = FilmStockProfile(stock_path)
        scan_profile  = ScanPrintProfile(scan_path)

        solver = RenderPlanSolver()
        user_overrides = {
            "adaptation_strength": adaptation,
            "exposure_intent": "Preserve",
            "grain_amount": grain,
            "halation_amount": halation
        }
        plan = solver.solve(session.feature_dict, stock_profile, scan_profile, user_overrides)

        # Apply all pre-film slider adjustments (corrected pipeline order)
        rgb_input = session.preview_rgb_linear.copy()
        rgb_input = _apply_pre_film_sliders(
            rgb_input, session.masks,
            exposure, highlights, shadows,
            blacks, whites, midtones, contrast, temp, tint, plan
        )

        renderer = FilmRenderer()
        rendered  = renderer.render(rgb_input, session.masks, plan)
        rendered  = np.clip(rendered, 0.0, 1.0)

        rendered_uint8 = (rendered * 255.0).astype(np.uint8)
        _, jpeg_bytes  = cv2.imencode('.jpg', cv2.cvtColor(rendered_uint8, cv2.COLOR_RGB2BGR))

        return StreamingResponse(io.BytesIO(jpeg_bytes.tobytes()), media_type="image/jpeg")
    except Exception as e:
        import traceback
        traceback.print_exc()
        raise HTTPException(status_code=500, detail=f"Failed to render preview: {str(e)}")

@app.post("/api/export")
def export_file(req: PreviewRequest):
    filepath = os.path.join(RAW_DIR, req.filename)
    if not os.path.exists(filepath):
        raise HTTPException(status_code=404, detail="File not found")

    try:
        stock_path = os.path.join(STOCKS_DIR, f"{req.stock}.yaml")
        if req.scanner == "none":
            scan_path = os.path.join(SCANNERS_DIR, "frontier_soft.yaml")
        else:
            scan_path = os.path.join(SCANNERS_DIR, f"{req.scanner}.yaml")

        stock_profile = FilmStockProfile(stock_path)
        scan_profile  = ScanPrintProfile(scan_path)

        basename        = os.path.splitext(req.filename)[0]
        output_filename = f"{basename}_{req.stock}_dfee.tif"
        output_path     = os.path.join(RAW_DIR, output_filename)
        report_path     = os.path.join(RAW_DIR, f"{basename}_{req.stock}_report.json")

        # Full-res ingest
        print("Ingesting full-res RAW for export...")
        ingestor = RawIngestor(filepath)
        rgb_linear, Y, clipping_masks, clipping_ratios, metadata = ingestor.ingest(draft_mode=False)

        print("Analyzing full-res canvas...")
        analyzer = ImageStateAnalyzer()
        feature_dict, masks = analyzer.analyze(rgb_linear, Y, clipping_masks, clipping_ratios)

        bias_estimator = CameraBiasEstimator()
        bias_info = bias_estimator.estimate_bias(rgb_linear, Y, clipping_masks, masks["luminance_zone_masks"])
        feature_dict["camera_input_bias"] = bias_info
        feature_dict["raw_metadata"]      = metadata

        solver = RenderPlanSolver()
        user_overrides = {
            "adaptation_strength": req.adaptation,
            "exposure_intent": "Preserve",
            "grain_amount": req.grain,
            "halation_amount": req.halation
        }
        plan = solver.solve(feature_dict, stock_profile, scan_profile, user_overrides)

        # Apply all pre-film slider adjustments (same corrected pipeline as preview)
        print("Rendering full resolution image...")
        rgb_input = rgb_linear.copy()
        rgb_input = _apply_pre_film_sliders(
            rgb_input, masks,
            req.exposure, req.highlights, req.shadows,
            req.blacks, req.whites, req.midtones,
            req.contrast, req.temp, req.tint, plan
        )

        renderer = FilmRenderer()
        rendered  = renderer.render(rgb_input, masks, plan)
        rendered  = np.clip(rendered, 0.0, 1.0)

        # Save 16-bit TIFF
        print(f"Saving output TIFF to: {output_path}")
        uint16_data = (rendered * 65535.0).astype(np.uint16)
        img = Image.fromarray(uint16_data)
        img.save(output_path, format="TIFF")

        reporter = RenderReporter()
        reporter.write_report(
            filepath, output_path, stock_profile, scan_profile,
            feature_dict, plan, report_path
        )

        return {
            "status": "success",
            "output_path": output_path,
            "report_path": report_path
        }
    except Exception as e:
        import traceback
        traceback.print_exc()
        raise HTTPException(status_code=500, detail=f"Failed to export image: {str(e)}")

# Mount static React frontend when compiled
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse

ASSETS_DIR = os.path.join(BASE_DIR, "frontend", "dist", "assets")
if os.path.exists(ASSETS_DIR):
    app.mount("/assets", StaticFiles(directory=ASSETS_DIR), name="assets")

@app.get("/")
def serve_index():
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
