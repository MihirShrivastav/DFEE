import os
from PIL import Image
import numpy as np

from .profile import FilmStockProfile, ScanPrintProfile
from .ingest import RawIngestor
from .analyzer import ImageStateAnalyzer
from .bias import CameraBiasEstimator
from .solver import RenderPlanSolver
from .renderer import FilmRenderer
from .report import RenderReporter

class DFEEEngine:
    def __init__(self, stock_profile_path, scan_profile_path, 
                 adaptation_strength=1.0, output_color_space="sRGB", 
                 output_bit_depth=16):
        
        self.stock_profile = FilmStockProfile(stock_profile_path)
        self.scan_profile = ScanPrintProfile(scan_profile_path)
        
        self.adaptation_strength = adaptation_strength
        self.output_color_space = output_color_space
        self.output_bit_depth = output_bit_depth
        
        # Initialize modules
        self.analyzer = ImageStateAnalyzer()
        self.bias_estimator = CameraBiasEstimator()
        self.solver = RenderPlanSolver()
        self.renderer = FilmRenderer()
        self.reporter = RenderReporter()

    def render(self, input_path, output_path, report_path=None, draft_mode=False, user_overrides=None):
        """
        Executes the full DFEE pipeline on a single RAW file.
        """
        # Step 1: Ingest RAW file
        ingestor = RawIngestor(input_path)
        rgb_linear, Y, clipping_masks, clipping_ratios, metadata = ingestor.ingest(draft_mode)
        
        # Step 2: Extract tonal and spatial features
        feature_dict, masks = self.analyzer.analyze(rgb_linear, Y, clipping_masks, clipping_ratios)
        feature_dict["raw_metadata"] = metadata
        
        # Step 3: Estimate camera input color bias
        bias_info = self.bias_estimator.estimate_bias(rgb_linear, Y, clipping_masks, masks["luminance_zone_masks"])
        feature_dict["camera_input_bias"] = bias_info
        
        # Step 4: Solve render parameters
        solver_controls = {"adaptation_strength": self.adaptation_strength}
        if user_overrides:
            solver_controls.update(user_overrides)
            
        render_plan = self.solver.solve(
            feature_dict, self.stock_profile, self.scan_profile, solver_controls
        )
        
        # Step 5: Render pixel data
        rgb_rendered = self.renderer.render(rgb_linear, masks, render_plan)
        
        # Step 6: Save output file(s)
        self._save_image(rgb_rendered, output_path)
        
        # Step 7: Write sidecar report if requested
        if report_path:
            self.reporter.write_report(
                input_path, output_path, self.stock_profile, self.scan_profile,
                feature_dict, render_plan, report_path
            )
            
        # Return a result object containing final files and details
        return DFEERenderResult(output_path, report_path, render_plan)

    def _save_image(self, rgb_array, filepath):
        """
        Saves rendered float32 array as either 16-bit TIFF or 8-bit JPEG/PNG,
        applying the standard sRGB transfer function (gamma correction) for display.
        """
        ext = os.path.splitext(filepath)[1].lower()
        
        # Apply standard sRGB transfer function (gamma correction) before saving
        srgb_array = np.clip(rgb_array, 0.0, 1.0)
        srgb_array = np.where(
            srgb_array <= 0.0031308,
            12.92 * srgb_array,
            1.055 * (srgb_array ** (1.0 / 2.4)) - 0.055
        )
        
        if self.output_bit_depth == 16 and ext in ['.tif', '.tiff']:
            # Convert float32 [0.0, 1.0] to uint16 [0, 65535]
            uint16_data = (srgb_array * 65535.0).astype(np.uint16)
            img = Image.fromarray(uint16_data)
            img.save(filepath, format="TIFF")
        else:
            # Fallback/Save as 8-bit JPEG/PNG
            uint8_data = (srgb_array * 255.0).astype(np.uint8)
            img = Image.fromarray(uint8_data)
            if ext in ['.jpg', '.jpeg']:
                img.save(filepath, format="JPEG", quality=95)
            elif ext == '.png':
                img.save(filepath, format="PNG")
            else:
                # Default TIFF
                img.save(filepath)


class DFEERenderResult:
    def __init__(self, output_path, report_path, render_plan):
        self.output_path = output_path
        self.report_path = report_path
        self.render_plan = render_plan

    def __repr__(self):
        return f"<DFEERenderResult output={self.output_path} warnings={self.render_plan['warnings']}>"
