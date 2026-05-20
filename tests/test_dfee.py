import os
import unittest
import numpy as np

from dfee.color_spaces import rgb_to_oklab, oklab_to_rgb, oklab_to_oklch, oklch_to_oklab
from dfee.profile import FilmStockProfile, ScanPrintProfile
from dfee.analyzer import ImageStateAnalyzer
from dfee.solver import RenderPlanSolver
from dfee.renderer import FilmRenderer
from dfee.engine import DFEEEngine

class TestDFEEColorSpaces(unittest.TestCase):
    def test_rgb_oklab_roundtrip(self):
        # Create a grid of RGB colors
        rgb = np.random.uniform(0.0, 1.0, (10, 10, 3)).astype(np.float32)
        oklab = rgb_to_oklab(rgb)
        rgb_rt = oklab_to_rgb(oklab)
        
        # Verify roundtrip accuracy within small epsilon
        np.testing.assert_allclose(rgb, rgb_rt, atol=1e-5)

    def test_oklab_oklch_roundtrip(self):
        oklab = np.random.uniform(-0.1, 0.1, (10, 10, 3)).astype(np.float32)
        oklab[:, :, 0] = np.random.uniform(0.1, 0.9, (10, 10)) # Lightness L
        
        lch = oklab_to_oklch(oklab)
        oklab_rt = oklch_to_oklab(lch)
        
        np.testing.assert_allclose(oklab, oklab_rt, atol=1e-5)


class TestDFEEProfiles(unittest.TestCase):
    def setUp(self):
        self.base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    def test_load_stock_profiles(self):
        stocks = ["kodachrome_64", "portra_400", "superia_400", "tri_x_400"]
        for stock in stocks:
            path = os.path.join(self.base_dir, "profiles", "stocks", f"{stock}.yaml")
            self.assertTrue(os.path.exists(path), f"Stock profile {stock} path does not exist")
            
            profile = FilmStockProfile(path)
            self.assertEqual(profile.stock_id, stock)
            self.assertIn(profile.stock_type, ["color_negative", "color_reversal", "monochrome"])

    def test_load_scanner_profiles(self):
        scanners = ["frontier_soft", "noritsu_smooth", "darkroom_print"]
        for scanner in scanners:
            path = os.path.join(self.base_dir, "profiles", "scanners", f"{scanner}.yaml")
            self.assertTrue(os.path.exists(path), f"Scanner profile {scanner} path does not exist")
            
            profile = ScanPrintProfile(path)
            self.assertEqual(profile.scanner_id, scanner)


class TestDFEEAnalyzer(unittest.TestCase):
    def test_zone_masks_partition_of_unity(self):
        # Create a gradient luminance canvas
        Y = np.linspace(0.0001, 1.0, 100).astype(np.float32).reshape(10, 10)
        analyzer = ImageStateAnalyzer()
        
        # Test zone masks properties
        zone_masks = analyzer._generate_zone_masks(Y, midtone_anchor=0.18)
        
        # 1. Verify we have 7 zones
        self.assertEqual(len(zone_masks), 7)
        for i in range(7):
            mask = zone_masks[f'Z{i}']
            # 2. Check bounds
            self.assertTrue(np.all(mask >= 0.0))
            self.assertTrue(np.all(mask <= 1.0))
            
        # 3. Check partition of unity (sum of weights at each pixel equals 1.0)
        mask_sum = sum(zone_masks[f'Z{i}'] for i in range(7))
        np.testing.assert_allclose(mask_sum, 1.0, atol=1e-5)


class TestDFEESolverAndRenderer(unittest.TestCase):
    def setUp(self):
        self.base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        stock_path = os.path.join(self.base_dir, "profiles", "stocks", "kodachrome_64.yaml")
        scan_path = os.path.join(self.base_dir, "profiles", "scanners", "frontier_soft.yaml")
        
        self.stock_profile = FilmStockProfile(stock_path)
        self.scan_profile = ScanPrintProfile(scan_path)
        self.analyzer = ImageStateAnalyzer()
        self.solver = RenderPlanSolver()
        self.renderer = FilmRenderer()

    def test_solver_adaptation(self):
        # Mock features
        clipping_ratios = {"R": 0.0, "G": 0.0, "B": 0.0}
        feature_dict = {
            "tonal_distribution": {
                "tonal_skew": "normal",
                "dynamic_range_stops": 9.5,
                "midtone_anchor": 0.18,
                "highlight_headroom": 0.25,
                "shadow_depth": 0.05,
                "contrast_index": 0.35,
                "black_point_actual": 0.01,
                "white_point_actual": 0.95
            },
            "hue_saturation_state": {
                "neon_risk": 0.01,
                "cyan_blue_ratio": 0.1,
                "red_orange_density": 0.2,
                "highlight_desaturation": 0.30,
                "cyan_blue_highlight_compression": 0.25,
                "red_orange_midtone_compression": 0.15
            },
            "spatial_frequency": {
                "specular_point_ratio": 0.01,
                "large_highlight_area_ratio": 0.02,
                "edge_softening": 0.15
            },
            "channel_behavior": {
                "clipping_ratios": clipping_ratios
            },
            "camera_input_bias": {
                "neutral_confidence": 0.8,
                "blue_excess_index": 0.05,
                "green_magenta_bias": 0.02,
                "warm_cool_bias": 0.04
            }
        }
        
        plan = self.solver.solve(feature_dict, self.stock_profile, self.scan_profile)
        
        # Verify plan layout
        self.assertIn("pre_film_normalization", plan)
        self.assertIn("film_response", plan)
        self.assertIn("material_effects", plan)
        self.assertIn("scanner_finish", plan)
        
        # Verify warnings are list
        self.assertIsInstance(plan["warnings"], list)

    def test_renderer_pipeline(self):
        # Create small synthetic linear image
        h, w = 64, 64
        rgb_linear = np.random.uniform(0.0, 1.0, (h, w, 3)).astype(np.float32)
        Y = (0.2126 * rgb_linear[:, :, 0] + 0.7152 * rgb_linear[:, :, 1] + 0.0722 * rgb_linear[:, :, 2])
        
        # Run analyzer to get actual masks
        clipping_masks = {"R": rgb_linear[:, :, 0] > 0.99, "G": rgb_linear[:, :, 1] > 0.99, "B": rgb_linear[:, :, 2] > 0.99}
        clipping_ratios = {"R": 0.0, "G": 0.0, "B": 0.0}
        
        feature_dict, masks = self.analyzer.analyze(rgb_linear, Y, clipping_masks, clipping_ratios)
        
        # Solve
        plan = self.solver.solve(feature_dict, self.stock_profile, self.scan_profile)
        
        # Render
        rendered = self.renderer.render(rgb_linear, masks, plan)
        
        # Check properties of output
        self.assertEqual(rendered.shape, (h, w, 3))
        self.assertTrue(np.all(rendered >= 0.0))
        self.assertTrue(np.all(rendered <= 1.0))
        
        # Check determinism: rendering twice must yield exact same pixels
        rendered_second = self.renderer.render(rgb_linear, masks, plan)
        np.testing.assert_array_equal(rendered, rendered_second)

if __name__ == '__main__':
    unittest.main()
