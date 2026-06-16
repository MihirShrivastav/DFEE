import os
import sys
import unittest
from pathlib import Path
import math

import cv2
import numpy as np

from dfee.analyzer import ImageStateAnalyzer
from dfee.bias import CameraBiasEstimator
from dfee.color_spaces import oklab_to_oklch, rgb_to_oklab
from dfee.ingest import RawIngestor
from dfee.post_effects import apply_clarity, apply_dehaze, apply_texture
from dfee.profile import FilmStockProfile, PrintStockProfile
from dfee.renderer import FilmRenderer
from dfee.solver import RenderPlanSolver


BASE_DIR = Path(__file__).resolve().parent.parent
NATIVE_BUILD_DIR = BASE_DIR / "cpp_engine" / "out" / "build" / "windows-msvc-vcpkg" / "Debug"
if not NATIVE_BUILD_DIR.exists():
    NATIVE_BUILD_DIR = BASE_DIR / "cpp_engine" / "out" / "build" / "windows-msvc" / "Debug"

if NATIVE_BUILD_DIR.exists():
    sys.path.insert(0, str(NATIVE_BUILD_DIR))

import dfee_native_bridge


@unittest.skipUnless(NATIVE_BUILD_DIR.exists(), "native build output not present")
class TestNativeBridge(unittest.TestCase):
    def setUp(self):
        self.session = dfee_native_bridge.create_session(BASE_DIR)
        self._temp_files: list[Path] = []

    def tearDown(self):
        for path in self._temp_files:
            try:
                path.unlink(missing_ok=True)
            except Exception:
                pass

    def _raw_filename(self) -> str:
        return next(
            entry.name
            for entry in (BASE_DIR / "raw_files").iterdir()
            if entry.is_file() and entry.suffix.lower() == ".arw"
        )

    def _create_temp_raw_file(self, name: str, payload: bytes) -> str:
        path = BASE_DIR / "raw_files" / name
        path.write_bytes(payload)
        self._temp_files.append(path)
        return path.name

    @staticmethod
    def _linear_to_srgb(linear: np.ndarray) -> np.ndarray:
        linear = np.clip(linear, 0.0, 1.0)
        return np.where(
            linear <= 0.0031308,
            12.92 * linear,
            1.055 * (linear ** (1.0 / 2.4)) - 0.055,
        ).astype(np.float32)

    def test_engine_status(self):
        version = dfee_native_bridge.engine_version()
        status = dfee_native_bridge.cuda_status()
        self.assertTrue(version)
        self.assertIn(status.mode, {"cpu", "cuda_available", "cuda_active", "cuda_fallback"})

    def test_list_profiles(self):
        profiles = self.session.list_profiles()
        self.assertGreater(len(profiles.stocks), 0)
        self.assertGreater(len(profiles.print_stocks), 0)
        self.assertTrue(profiles.engine.engine_version)
        self.assertIsInstance(profiles.engine.libraw_enabled, bool)
        self.assertGreater(len(profiles.engine.timings), 0)
        self.assertIn("list_profiles_total", profiles.engine.metadata_json)

    def test_list_profiles_skips_invalid_yaml(self):
        invalid_stock = BASE_DIR / "profiles" / "stocks" / "native_invalid_bridge_stock.yaml"
        invalid_print = BASE_DIR / "profiles" / "print_stocks" / "native_invalid_bridge_print.yaml"
        invalid_stock.write_text(
            "stock_id: native_invalid_bridge_stock\n"
            "stock_name: Native Invalid Bridge Stock\n"
            "stock_type: invalid_kind\n",
            encoding="utf-8",
        )
        invalid_print.write_text(
            "print_stock_id: native_invalid_bridge_print\n"
            "print_stock_name: Native Invalid Bridge Print\n"
            "tone: []\n",
            encoding="utf-8",
        )
        self._temp_files.extend([invalid_stock, invalid_print])

        profiles = self.session.list_profiles()
        self.assertNotIn("native_invalid_bridge_stock", {stock.stock_id for stock in profiles.stocks})
        self.assertNotIn("native_invalid_bridge_print", {stock.print_stock_id for stock in profiles.print_stocks})

    def test_python_color_analyzer_reference_fixture(self):
        rgb = np.array(
            [
                [[1.0, 0.0, 0.0], [1.0, 0.5, 0.0], [1.0, 1.0, 0.0], [0.0, 1.0, 0.0]],
                [[0.0, 1.0, 1.0], [0.0, 0.0, 1.0], [1.0, 0.0, 1.0], [0.5, 0.5, 0.5]],
            ],
            dtype=np.float32,
        )
        Y = (0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]).astype(np.float32)
        analyzer = ImageStateAnalyzer()
        tonal = analyzer._analyze_tonal(Y, {"R": 0.0, "G": 0.0, "B": 0.0})
        zone_masks = analyzer._generate_zone_masks(Y, tonal["midtone_anchor"])
        color = analyzer._analyze_color(rgb, zone_masks)

        self.assertGreater(color["mean_chroma"], 0.0)
        self.assertGreaterEqual(color["sat_p95"], color["mean_chroma"])
        self.assertGreater(color["hue_entropy"], 0.0)
        self.assertGreater(color["red_orange_density"], 0.0)
        self.assertGreater(color["cyan_blue_ratio"], 0.0)
        self.assertIn(color["dominant_hue_bins"][0], {
            "Red", "Orange", "Yellow", "Yellow-Green", "Green", "Green-Cyan",
            "Cyan", "Cyan-Blue", "Blue", "Blue-Violet", "Violet", "Magenta",
        })

    def test_python_spatial_analyzer_reference_fixture(self):
        Y = np.full((32, 32), 0.08, dtype=np.float32)
        Y[:, 8:24] += np.linspace(0.0, 0.25, 16, dtype=np.float32)[None, :]
        checker = ((np.indices((16, 16)).sum(axis=0) % 2) * 0.15).astype(np.float32)
        Y[8:24, 8:24] += checker
        Y[16, 16] = 1.0
        Y[16, 17] = 0.94
        Y[17, 16] = 0.92
        Y[17, 17] = 0.88

        analyzer = ImageStateAnalyzer()
        spatial, masks = analyzer._analyze_spatial(Y)

        self.assertGreater(spatial["texture_density"], 0.0)
        self.assertGreaterEqual(spatial["smooth_area_ratio"], 0.0)
        self.assertLessEqual(spatial["smooth_area_ratio"], 1.0)
        self.assertGreater(spatial["edge_density"], 0.0)
        self.assertGreater(spatial["specular_point_ratio"], 0.0)
        self.assertEqual(masks["grain_receptivity_mask"].shape, Y.shape)
        self.assertEqual(masks["halation_source_mask"].shape, Y.shape)
        self.assertEqual(masks["halation_receiver_mask"].shape, Y.shape)
        self.assertGreater(float(masks["halation_source_mask"][16, 16]), 0.0)
        self.assertGreater(float(masks["halation_receiver_mask"][15, 15]), 0.0)

    def test_python_camera_bias_reference_fixture(self):
        rgb = np.full((12, 12, 3), [0.38, 0.40, 0.44], dtype=np.float32)
        rgb[:4, :4, :] = [0.08, 0.11, 0.20]
        rgb[8:, 8:, :] = [0.82, 0.83, 0.86]
        Y = (0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]).astype(np.float32)
        clipping_masks = {
            "R": np.zeros((12, 12), dtype=bool),
            "G": np.zeros((12, 12), dtype=bool),
            "B": np.zeros((12, 12), dtype=bool),
        }

        analyzer = ImageStateAnalyzer()
        tonal = analyzer._analyze_tonal(Y, {"R": 0.0, "G": 0.0, "B": 0.0})
        zone_masks = analyzer._generate_zone_masks(Y, tonal["midtone_anchor"])
        bias = CameraBiasEstimator().estimate_bias(rgb, Y, clipping_masks, zone_masks)

        self.assertGreater(bias["neutral_confidence"], 0.0)
        self.assertGreater(bias["global_cast_lab"][0], 0.0)
        self.assertGreaterEqual(bias["blue_excess_index"], 0.0)
        self.assertLess(bias["warm_cool_bias"], 0.0)
        self.assertLess(bias["shadow_cast_lab"][2], 0.0)

    def test_python_solver_reference_fixture(self):
        stock = FilmStockProfile(str(BASE_DIR / "profiles" / "stocks" / "portra_400.yaml"))
        print_stock = PrintStockProfile(str(BASE_DIR / "profiles" / "print_stocks" / "kodak_2383.yaml"))
        solver = RenderPlanSolver()

        feature_dict = {
            "tonal_distribution": {
                "tonal_skew": "highlight_stressed",
                "dynamic_range_stops": 12.2,
                "midtone_anchor": 0.11,
                "highlight_headroom": 0.08,
                "shadow_depth": 0.01,
                "luma_p95": 0.86,
            },
            "hue_saturation_state": {
                "neon_risk": 0.07,
            },
            "spatial_frequency": {
                "specular_point_ratio": 0.03,
                "large_highlight_area_ratio": 0.15,
            },
            "channel_behavior": {
                "clipping_ratios": {"R": 0.04, "G": 0.01, "B": 0.0},
            },
            "camera_input_bias": {
                "neutral_confidence": 0.2,
                "blue_excess_index": 0.04,
                "green_magenta_bias": 0.02,
                "warm_cool_bias": -0.03,
            },
            "raw_metadata": {
                "iso": 1600,
            },
        }
        controls = {
            "adaptation_strength": 1.0,
            "color_cast_handling": "Auto",
            "grain_amount": "Auto",
            "halation_amount": "High",
            "film_color": 110.0,
            "print_stock": print_stock,
            "print_strength": 0.9,
            "print_c": 0.02,
            "print_m": -0.01,
            "print_y": 0.03,
            "print_contrast": 0.1,
            "print_black_point": -0.02,
        }

        plan = solver.solve(feature_dict, stock, controls)
        self.assertEqual(plan["stock_type"], "color_negative")
        self.assertEqual(plan["input_diagnosis"]["tonal_state"], "highlight_stressed")
        self.assertIn("HIGH_CHANNEL_CLIPPING", plan["warnings"])
        self.assertIn("LOW_NEUTRAL_CONFIDENCE", plan["warnings"])
        self.assertGreater(plan["pre_film_normalization"]["highlight_channel_recovery"], 0.0)
        self.assertGreater(plan["film_response"]["toe_length"], 0.0)
        self.assertGreater(plan["material_effects"]["grain_strength"], 0.0)
        self.assertIsNotNone(plan["print_finish"])

    def test_python_pre_film_reference_fixture(self):
        rgb = np.zeros((4, 4, 3), dtype=np.float32)
        for y in range(4):
            for x in range(4):
                rgb[y, x, 0] = 0.18 + 0.02 * x
                rgb[y, x, 1] = 0.16 + 0.01 * y
                rgb[y, x, 2] = 0.12
        rgb[3, 3] = [0.96, 0.90, 0.82]

        Y = (0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]).astype(np.float32)
        analyzer = ImageStateAnalyzer()
        zone_masks = analyzer._generate_zone_masks(Y, 0.18)
        masks = {"luminance_zone_masks": zone_masks}
        pre_film = {
            "exposure_compensation_stops": 0.5,
            "shadow_blue_normalization": 0.035,
            "green_magenta_stabilization": 0.02,
        }

        normalized = FilmRenderer()._apply_pre_film_normalization(rgb.copy(), masks, pre_film)
        src_mid_luma = float(0.2126 * rgb[1, 1, 0] + 0.7152 * rgb[1, 1, 1] + 0.0722 * rgb[1, 1, 2])
        dst_mid_luma = float(0.2126 * normalized[1, 1, 0] + 0.7152 * normalized[1, 1, 1] + 0.0722 * normalized[1, 1, 2])
        self.assertGreater(dst_mid_luma, src_mid_luma)

        src_high_spread = float(np.max(rgb[3, 3]) - np.min(rgb[3, 3]))
        dst_high_spread = float(np.max(normalized[3, 3]) - np.min(normalized[3, 3]))
        self.assertLess(dst_high_spread, src_high_spread)

    def test_python_panchromatic_reference_fixture(self):
        rgb = np.array(
            [[[0.8, 0.2, 0.1], [0.1, 0.8, 0.2]]],
            dtype=np.float32,
        )
        response = {
            "pan_weight_r": 0.25,
            "pan_weight_g": 0.55,
            "pan_weight_b": 0.20,
        }
        mono = FilmRenderer()._apply_panchromatic_conversion(rgb, response)
        np.testing.assert_allclose(mono[:, :, 0], mono[:, :, 1], atol=1e-6)
        np.testing.assert_allclose(mono[:, :, 1], mono[:, :, 2], atol=1e-6)
        self.assertGreater(float(mono[0, 1, 0]), float(mono[0, 0, 0]))

    def test_python_tone_response_reference_fixture(self):
        rgb = np.array(
            [[[0.02, 0.02, 0.02], [0.18, 0.18, 0.18], [0.55, 0.55, 0.55], [1.35, 1.35, 1.35]]],
            dtype=np.float32,
        )
        response = {
            "toe_strength": 0.46,
            "toe_length": 0.30,
            "midtone_density": 1.08,
            "shoulder_strength": 0.78,
            "black_density_floor": 0.01,
            "channel_toe_mult": [1.0, 1.0, 1.0],
            "channel_shoulder_mult": [1.0, 1.0, 1.0],
            "channel_midtone_mult": [1.0, 1.0, 1.0],
        }
        toned = FilmRenderer()._apply_film_tone_response(rgb, response)
        self.assertGreaterEqual(float(toned[0, 0, 0]), 0.01)
        self.assertGreater(float(toned[0, 1, 0]), float(toned[0, 0, 0]))
        self.assertGreater(float(toned[0, 2, 0]), float(toned[0, 1, 0]))
        self.assertLessEqual(float(toned[0, 3, 0]), 1.0)
        self.assertGreater(float(toned[0, 3, 0]), float(toned[0, 1, 0]))
        self.assertLess(float(toned[0, 3, 0]), float(toned[0, 2, 0]))

    def test_python_color_response_reference_fixture(self):
        stock = FilmStockProfile(str(BASE_DIR / "profiles" / "stocks" / "portra_400.yaml"))
        rgb = np.array(
            [[[0.65, 0.25, 0.18], [0.60, 0.50, 0.18], [0.62, 0.66, 0.92]]],
            dtype=np.float32,
        )
        Y = (0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]).astype(np.float32)
        zone_masks = ImageStateAnalyzer()._generate_zone_masks(Y, 0.18)
        masks = {"luminance_zone_masks": zone_masks}
        response = {
            "chroma_boost": stock.hue_saturation_response["saturation_boost"],
            "red_orange_compression": stock.hue_saturation_response["red_orange_midtone_compression"],
            "blue_cyan_compression": stock.hue_saturation_response["cyan_blue_highlight_compression"],
            "neon_compression": stock.hue_saturation_response["neon_compression"],
            "highlight_desaturation": stock.hue_saturation_response["highlight_desaturation"],
            "shadow_bias_lab": stock.color_response["shadow_bias_lab"],
            "midtone_bias_lab": stock.color_response["midtone_bias_lab"],
            "highlight_bias_lab": stock.color_response["highlight_bias_lab"],
            "film_color": 100.0,
        }
        adjusted = FilmRenderer()._apply_color_response(rgb, masks, response, fc=1.0)
        src_oklab = rgb_to_oklab(rgb)
        dst_oklab = rgb_to_oklab(adjusted)
        self.assertGreater(float(dst_oklab[0, 0, 1]), float(src_oklab[0, 0, 1]))
        self.assertGreater(float(dst_oklab[0, 0, 2]), float(src_oklab[0, 0, 2]))
        self.assertGreater(abs(float(dst_oklab[0, 2, 2] - src_oklab[0, 2, 2])), 1e-4)

    def test_python_luminance_chroma_coupling_reference_fixture(self):
        rgb = np.array(
            [[[0.12, 0.07, 0.03], [0.82, 0.70, 0.42], [0.92, 0.78, 0.70]]],
            dtype=np.float32,
        )
        response = {
            "stock_type": "color_negative",
            "film_color": 100.0,
            "chroma_coupling": {
                "hi_rolloff_start": 0.75,
                "hi_rolloff_rate": 1.5,
                "hi_compression": 0.48,
                "sh_rolloff_start": 0.18,
                "sh_compression": 0.43,
                "hi_hue_conv_rad": 0.30,
                "hi_hue_conv_str": 0.20,
            },
        }
        adjusted = FilmRenderer()._apply_luminance_chroma_coupling(rgb, response, fc=1.0)
        src_lch = oklab_to_oklch(rgb_to_oklab(rgb))
        dst_lch = oklab_to_oklch(rgb_to_oklab(adjusted))
        self.assertLess(float(dst_lch[0, 0, 1]), float(src_lch[0, 0, 1]))
        self.assertLess(float(dst_lch[0, 2, 1]), float(src_lch[0, 2, 1]))
        self.assertGreater(abs(float(dst_lch[0, 2, 2] - src_lch[0, 2, 2])), 1e-4)

    def test_python_acutance_reference_fixture(self):
        rgb = np.zeros((8, 8, 3), dtype=np.float32)
        rgb[:, :4, :] = 0.25
        rgb[:, 4:, :] = 0.65
        rgb[2:6, 2:6, :] += 0.03
        effects = {"edge_softening": 0.12, "sharpness": 0.55, "sharpness_mask": 0.5}

        adjusted = FilmRenderer()._apply_acutance_shaping(rgb.copy(), effects)
        src_l = rgb_to_oklab(rgb)[:, :, 0]
        dst_l = rgb_to_oklab(adjusted)[:, :, 0]

        self.assertGreater(abs(float(dst_l[4, 4] - src_l[4, 4])), 1e-4)
        self.assertGreater(abs(float((dst_l[4, 4] - dst_l[4, 3]) - (src_l[4, 4] - src_l[4, 3]))), 1e-4)

    def test_python_clarity_reference_fixture(self):
        rgb = np.full((32, 32, 3), 0.45, dtype=np.float32)
        rgb[:, 16:, :] += 0.06
        rgb[8:24, 8:24, :] += 0.03

        adjusted = apply_clarity(rgb.copy(), 40)
        src_gamma = np.clip(rgb, 0.0, 1.0) ** (1.0 / 2.2)
        dst_gamma = np.clip(adjusted, 0.0, 1.0) ** (1.0 / 2.2)

        self.assertGreater(float(np.std(dst_gamma[:, :, 0])), float(np.std(src_gamma[:, :, 0])))

    def test_python_texture_reference_fixture(self):
        checker = ((np.indices((32, 32)).sum(axis=0) % 2).astype(np.float32) * 0.04)
        rgb = np.repeat((0.45 + checker)[:, :, None], 3, axis=2).astype(np.float32)

        adjusted = apply_texture(rgb.copy(), 45)
        src_gamma = np.clip(rgb, 0.0, 1.0) ** (1.0 / 2.2)
        dst_gamma = np.clip(adjusted, 0.0, 1.0) ** (1.0 / 2.2)

        self.assertGreater(float(np.std(dst_gamma[:, :, 0])), float(np.std(src_gamma[:, :, 0])))

    def test_python_dehaze_reference_fixture(self):
        rgb = np.zeros((32, 32, 3), dtype=np.float32)
        for y in range(32):
            for x in range(32):
                base = 0.15 + 0.55 * (x / 31.0)
                haze = 0.18
                rgb[y, x, :] = base * (1.0 - haze) + haze * 0.9
        rgb[10:22, 10:22, :] *= np.array([0.55, 0.60, 0.70], dtype=np.float32)

        adjusted = apply_dehaze(rgb.copy(), 45)
        src_gamma = np.clip(rgb, 0.0, 1.0) ** (1.0 / 2.2)
        dst_gamma = np.clip(adjusted, 0.0, 1.0) ** (1.0 / 2.2)
        src_luma = 0.2126 * src_gamma[:, :, 0] + 0.7152 * src_gamma[:, :, 1] + 0.0722 * src_gamma[:, :, 2]
        dst_luma = 0.2126 * dst_gamma[:, :, 0] + 0.7152 * dst_gamma[:, :, 1] + 0.0722 * dst_gamma[:, :, 2]

        self.assertGreater(float(np.std(dst_luma)), float(np.std(src_luma)))
        self.assertLess(float(np.mean(dst_luma[10:22, 10:22])), float(np.mean(src_luma[10:22, 10:22])))

    def test_python_halation_bloom_reference_fixture(self):
        rgb = np.full((64, 64, 3), 0.08, dtype=np.float32)
        rgb[24:40, 24:40, :] = [0.92, 0.88, 0.82]
        rgb[28:36, 28:36, :] = [1.0, 0.98, 0.92]
        Y = (0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]).astype(np.float32)

        zone_masks = {f"Z{i}": np.zeros_like(Y) for i in range(7)}
        zone_masks["Z5"] = np.clip((Y - 0.6) / 0.4, 0.0, 1.0).astype(np.float32)
        masks = {
            "halation_source_mask": np.pad(np.ones((4, 4), dtype=np.float32), ((30, 30), (30, 30))),
            "halation_receiver_mask": np.zeros_like(Y),
            "luminance_zone_masks": zone_masks,
        }
        masks["halation_receiver_mask"][20:44, 20:44] = 1.0 - Y[20:44, 20:44]

        effects = {"halation_strength": 0.35, "bloom_strength": 0.22}
        adjusted = FilmRenderer()._apply_halation_bloom(rgb.copy(), masks, effects)

        mean_abs_delta = float(np.mean(np.abs(adjusted - rgb)))
        max_abs_delta = float(np.max(np.abs(adjusted - rgb)))
        self.assertGreater(mean_abs_delta, 1e-5)
        self.assertGreater(max_abs_delta, 1e-3)
        self.assertGreaterEqual(float(adjusted[30, 30, 0]), float(adjusted[30, 30, 2]))

    def test_python_film_grain_reference_fixture(self):
        rgb = np.zeros((32, 32, 3), dtype=np.float32)
        for y in range(32):
            for x in range(32):
                rgb[y, x, 0] = 0.20 + 0.40 * (x / 31.0)
                rgb[y, x, 1] = 0.18 + 0.45 * (y / 31.0)
                rgb[y, x, 2] = 0.16 + 0.35 * ((x + y) / 62.0)

        masks = {
            "grain_receptivity_mask": np.full((32, 32), 0.85, dtype=np.float32),
        }
        effects = {
            "grain_strength": 0.65,
            "grain_size": 0.55,
            "grain_roughness": 0.45,
            "grain_chroma_strength": 0.12,
        }

        first = FilmRenderer()._apply_film_grain(rgb.copy(), masks, effects)
        second = FilmRenderer()._apply_film_grain(rgb.copy(), masks, effects)

        np.testing.assert_array_equal(first, second)
        self.assertGreater(float(np.mean(np.abs(first - rgb))), 1e-5)
        self.assertGreater(
            float(np.max(np.abs(first[:, :, 0] - first[:, :, 1])) + np.max(np.abs(first[:, :, 1] - first[:, :, 2]))),
            1e-5,
        )

    def test_python_print_finish_reference_fixture(self):
        rgb = np.zeros((8, 8, 3), dtype=np.float32)
        for y in range(8):
            for x in range(8):
                rgb[y, x, 0] = 0.12 + 0.08 * (x / 7.0)
                rgb[y, x, 1] = 0.10 + 0.55 * (y / 7.0)
                rgb[y, x, 2] = 0.18 + 0.70 * ((x + y) / 14.0)
        rgb[7, 7, :] = [0.92, 0.88, 0.84]

        pf = {
            "strength": 0.9,
            "print_c": 2.0,
            "print_m": -1.0,
            "print_y": 3.0,
            "print_contrast": 10.0,
            "print_black_point": -2.0,
            "shadow_lift": 0.03,
            "contrast_boost": 1.15,
            "highlight_rolloff": 0.78,
            "highlight_rolloff_rate": 2.0,
            "shadow_bias_lab": [0.0, 0.8, 1.2],
            "midtone_bias_lab": [0.0, 0.3, 0.2],
            "highlight_bias_lab": [0.0, -0.4, -0.6],
            "red_boost": 0.2,
            "blue_suppression": 0.1,
            "green_shift": 0.05,
            "saturation_scale": 1.08,
            "grain_strength": 0.10,
            "grain_size": 0.35,
        }

        adjusted = FilmRenderer()._apply_print_finish(rgb.copy(), pf)
        src_shadow = float(0.2126 * rgb[0, 0, 0] + 0.7152 * rgb[0, 0, 1] + 0.0722 * rgb[0, 0, 2])
        dst_shadow = float(0.2126 * adjusted[0, 0, 0] + 0.7152 * adjusted[0, 0, 1] + 0.0722 * adjusted[0, 0, 2])
        src_oklab = rgb_to_oklab(rgb)
        dst_oklab = rgb_to_oklab(adjusted)
        src_mid_spread = float(np.max(rgb[4, 4]) - np.min(rgb[4, 4]))
        dst_mid_spread = float(np.max(adjusted[4, 4]) - np.min(adjusted[4, 4]))

        self.assertGreater(dst_shadow, src_shadow)
        self.assertGreater(abs(float(dst_oklab[2, 2, 1] - src_oklab[2, 2, 1])), 1e-4)
        self.assertLess(dst_mid_spread, src_mid_spread)
        self.assertGreater(float(np.mean(np.abs(adjusted - rgb))), 1e-4)
        self.assertTrue(np.all(adjusted >= 0.0))
        self.assertTrue(np.all(adjusted <= 1.0))

    def test_select_file(self):
        raw_filename = self._raw_filename()
        result = self.session.select_file(raw_filename)
        self.assertTrue(result.ok)
        self.assertEqual(result.filename, raw_filename)
        self.assertEqual(result.status, "selected")
        self.assertGreater(len(result.engine.timings), 0)
        self.assertIn("select_file_total", result.engine.metadata_json)

    def test_select_missing_file_raises_structured_error(self):
        with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
            self.session.select_file("definitely_missing_file.ARW")
        self.assertEqual(ctx.exception.code, "RAW_FILE_NOT_FOUND")
        self.assertEqual(ctx.exception.status, "not_found")
        self.assertIn("requested RAW file was not found", ctx.exception.user_message)

    def test_invalid_project_root_raises_bridge_error(self):
        invalid_root = BASE_DIR / "this_project_root_does_not_exist"
        with self.assertRaises(dfee_native_bridge.NativeBridgeError) as ctx:
            dfee_native_bridge.create_session(invalid_root)
        self.assertEqual(ctx.exception.code, "PROJECT_ROOT_NOT_FOUND")
        self.assertIn("project root", ctx.exception.user_message.lower())

    def test_read_raw_metadata(self):
        raw_filename = self._raw_filename()
        if self.session.list_profiles().engine.libraw_enabled:
            metadata = self.session.read_raw_metadata(raw_filename)
            self.assertGreater(metadata.image_width, 0)
            self.assertGreater(metadata.image_height, 0)
            self.assertTrue(metadata.camera_make or metadata.camera_model)
        else:
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.read_raw_metadata(raw_filename)
            self.assertEqual(ctx.exception.code, "LIBRAW_UNAVAILABLE")

    def test_decode_raw(self):
        raw_filename = self._raw_filename()
        if self.session.list_profiles().engine.libraw_enabled:
            summary, metadata = self.session.decode_raw(raw_filename, draft_mode=True)
            rgb, _, _, clipping_ratios, py_metadata = RawIngestor(str((BASE_DIR / "raw_files" / raw_filename))).ingest(draft_mode=True)

            self.assertEqual(summary.image_width, rgb.shape[1])
            self.assertEqual(summary.image_height, rgb.shape[0])
            self.assertEqual(summary.channels, rgb.shape[2])
            self.assertTrue(0.0 <= summary.min_value <= 1.0)
            self.assertTrue(0.0 <= summary.max_value <= 1.0)
            self.assertTrue(math.isclose(summary.clipping_ratio_r, clipping_ratios["R"], rel_tol=0.0, abs_tol=0.02))
            self.assertTrue(math.isclose(summary.clipping_ratio_g, clipping_ratios["G"], rel_tol=0.0, abs_tol=0.02))
            self.assertTrue(math.isclose(summary.clipping_ratio_b, clipping_ratios["B"], rel_tol=0.0, abs_tol=0.02))
            self.assertEqual(metadata.image_width, py_metadata["image_width"])
            self.assertEqual(metadata.image_height, py_metadata["image_height"])
        else:
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.decode_raw(raw_filename, draft_mode=True)
            self.assertEqual(ctx.exception.code, "LIBRAW_UNAVAILABLE")

    def test_cache_state_tracks_preview_and_fullres_ownership(self):
        raw_filename = self._raw_filename()
        self.session.select_file(raw_filename)
        initial_state = self.session.cache_state()
        self.assertEqual(initial_state.selected_filename, raw_filename)
        self.assertFalse(initial_state.draft_decode_cached)
        self.assertFalse(initial_state.preview_cached)
        self.assertFalse(initial_state.full_decode_cached)

        if self.session.list_profiles().engine.libraw_enabled:
            draft_summary, _ = self.session.decode_raw(raw_filename, draft_mode=True)
            after_draft = self.session.cache_state()
            self.assertTrue(after_draft.draft_decode_cached)
            self.assertTrue(after_draft.preview_cached)
            self.assertFalse(after_draft.raw_preview_jpeg_cached)
            self.assertFalse(after_draft.full_decode_cached)
            self.assertEqual(after_draft.draft_width, draft_summary.image_width)
            self.assertEqual(after_draft.draft_height, draft_summary.image_height)
            self.assertLessEqual(after_draft.preview_width, after_draft.draft_width)
            self.assertLessEqual(after_draft.preview_height, after_draft.draft_height)

            self.session.select_file(raw_filename)
            after_reselect = self.session.cache_state()
            self.assertTrue(after_reselect.draft_decode_cached)
            self.assertTrue(after_reselect.preview_cached)

            full_summary, _ = self.session.decode_raw(raw_filename, draft_mode=False)
            after_full = self.session.cache_state()
            self.assertTrue(after_full.full_decode_cached)
            self.assertEqual(after_full.full_width, full_summary.image_width)
            self.assertEqual(after_full.full_height, full_summary.image_height)
        else:
            with self.assertRaises(dfee_native_bridge.NativeOperationError):
                self.session.decode_raw(raw_filename, draft_mode=True)
            after_failed_decode = self.session.cache_state()
            self.assertFalse(after_failed_decode.draft_decode_cached)
            self.assertFalse(after_failed_decode.preview_cached)
            self.assertFalse(after_failed_decode.raw_preview_jpeg_cached)
            self.assertFalse(after_failed_decode.full_decode_cached)

    def test_raw_preview(self):
        raw_filename = self._raw_filename()
        if self.session.list_profiles().engine.libraw_enabled:
            self.session.select_file(raw_filename)
            self.session.decode_raw(raw_filename, draft_mode=True)
            preview = self.session.raw_preview(raw_filename, max_edge=1024)
            self.assertEqual(preview.content_type, "image/jpeg")
            self.assertGreater(len(preview.jpeg_bytes), 0)

            state_after_preview = self.session.cache_state()
            self.assertTrue(state_after_preview.raw_preview_jpeg_cached)
            self.assertGreater(state_after_preview.raw_preview_jpeg_bytes, 0)

            cached_preview = self.session.raw_preview(raw_filename, max_edge=1024)
            self.assertEqual(cached_preview.status, "cached")
            self.assertEqual(cached_preview.jpeg_bytes, preview.jpeg_bytes)

            rgb, _, _, _, _ = RawIngestor(str((BASE_DIR / "raw_files" / raw_filename))).ingest(draft_mode=True)
            max_edge = 1024
            h, w = rgb.shape[:2]
            if max(h, w) > max_edge:
                scale = max_edge / max(h, w)
                expected_rgb = cv2.resize(rgb, (0, 0), fx=scale, fy=scale, interpolation=cv2.INTER_AREA)
            else:
                expected_rgb = rgb.copy()
            expected_srgb = self._linear_to_srgb(expected_rgb)
            expected_u8 = (expected_srgb * 255.0).astype(np.uint8)
            _, expected_jpeg = cv2.imencode(".jpg", cv2.cvtColor(expected_u8, cv2.COLOR_RGB2BGR))

            decoded_bgr = cv2.imdecode(np.frombuffer(preview.jpeg_bytes, dtype=np.uint8), cv2.IMREAD_COLOR)
            self.assertIsNotNone(decoded_bgr)
            decoded_rgb = cv2.cvtColor(decoded_bgr, cv2.COLOR_BGR2RGB)
            expected_decoded_bgr = cv2.imdecode(expected_jpeg, cv2.IMREAD_COLOR)
            self.assertIsNotNone(expected_decoded_bgr)
            expected_decoded_rgb = cv2.cvtColor(expected_decoded_bgr, cv2.COLOR_BGR2RGB)
            self.assertEqual(decoded_rgb.shape[1], expected_u8.shape[1])
            self.assertEqual(decoded_rgb.shape[0], expected_u8.shape[0])
            mean_abs_error = float(np.mean(np.abs(decoded_rgb.astype(np.int16) - expected_decoded_rgb.astype(np.int16))))
            self.assertLess(mean_abs_error, 1.0)
        else:
            self.session.select_file(raw_filename)
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.raw_preview(raw_filename, max_edge=1024)
            self.assertIn(ctx.exception.code, {"LIBRAW_UNAVAILABLE", "OPENCV_UNAVAILABLE", "RAW_PREVIEW_NOT_CACHED"})

    def test_render_preview(self):
        raw_filename = self._raw_filename()
        if self.session.list_profiles().engine.libraw_enabled:
            self.session.select_file(raw_filename)
            preview = self.session.render_preview(
                dfee_native_bridge.NativePreviewRenderRequest(
                    filename=raw_filename,
                    stock="portra_400",
                    print_stock="kodak_2383",
                    exposure=0.15,
                    saturation=8.0,
                    vibrance=10.0,
                    clarity=5.0,
                    texture=4.0,
                    dehaze=3.0,
                    bloom=8.0,
                    film_color=108.0,
                )
            )

            self.assertEqual(preview.content_type, "image/jpeg")
            self.assertGreater(len(preview.jpeg_bytes), 0)
            self.assertTrue(any(t.stage == "render_preview_total" for t in preview.engine.timings))

            decoded_bgr = cv2.imdecode(np.frombuffer(preview.jpeg_bytes, dtype=np.uint8), cv2.IMREAD_COLOR)
            self.assertIsNotNone(decoded_bgr)
            self.assertGreater(decoded_bgr.shape[0], 0)
            self.assertGreater(decoded_bgr.shape[1], 0)
        else:
            self.session.select_file(raw_filename)
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.render_preview(
                    dfee_native_bridge.NativePreviewRenderRequest(
                        filename=raw_filename,
                        stock="portra_400",
                    )
                )
            self.assertIn(ctx.exception.code, {"LIBRAW_UNAVAILABLE", "OPENCV_UNAVAILABLE"})

    def test_unsupported_and_corrupt_raw_failures(self):
        unsupported_filename = self._create_temp_raw_file(
            "native_test_unsupported.arw",
            b"this is not a raw file\n",
        )
        source_raw = BASE_DIR / "raw_files" / self._raw_filename()
        corrupt_filename = self._create_temp_raw_file(
            "native_test_corrupt.arw",
            source_raw.read_bytes()[:4096],
        )

        if self.session.list_profiles().engine.libraw_enabled:
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.read_raw_metadata(unsupported_filename)
            self.assertIn(ctx.exception.code, {"LIBRAW_UNSUPPORTED_RAW", "LIBRAW_OPEN_FAILED"})
            self.assertIn(ctx.exception.status, {"unsupported", "error"})

            self.session.select_file(unsupported_filename)
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.decode_raw(unsupported_filename, draft_mode=True)
            self.assertIn(ctx.exception.code, {"LIBRAW_UNSUPPORTED_RAW", "LIBRAW_OPEN_FAILED"})
            self.assertIn(ctx.exception.status, {"unsupported", "error"})

            unsupported_cache = self.session.cache_state()
            self.assertFalse(unsupported_cache.draft_decode_cached)
            self.assertFalse(unsupported_cache.preview_cached)
            self.assertFalse(unsupported_cache.raw_preview_jpeg_cached)

            self.session.select_file(corrupt_filename)
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.decode_raw(corrupt_filename, draft_mode=True)
            self.assertIn(ctx.exception.code, {"LIBRAW_CORRUPT_RAW", "LIBRAW_OPEN_FAILED"})
            self.assertEqual(ctx.exception.status, "error")

            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.raw_preview(corrupt_filename, max_edge=1024)
            self.assertEqual(ctx.exception.code, "RAW_PREVIEW_NOT_CACHED")

            corrupt_cache = self.session.cache_state()
            self.assertFalse(corrupt_cache.draft_decode_cached)
            self.assertFalse(corrupt_cache.preview_cached)
            self.assertFalse(corrupt_cache.raw_preview_jpeg_cached)
        else:
            self.session.select_file(unsupported_filename)
            with self.assertRaises(dfee_native_bridge.NativeOperationError) as ctx:
                self.session.decode_raw(unsupported_filename, draft_mode=True)
            self.assertEqual(ctx.exception.code, "LIBRAW_UNAVAILABLE")


if __name__ == "__main__":
    unittest.main()
