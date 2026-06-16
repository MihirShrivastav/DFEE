import os
import sys
import unittest
from pathlib import Path
import math

import cv2
import numpy as np

from dfee.analyzer import ImageStateAnalyzer
from dfee.bias import CameraBiasEstimator
from dfee.ingest import RawIngestor
from dfee.profile import FilmStockProfile, PrintStockProfile
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
