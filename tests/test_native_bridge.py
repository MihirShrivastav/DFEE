import os
import sys
import unittest
from pathlib import Path
import math

from dfee.ingest import RawIngestor


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

    def test_select_file(self):
        raw_filename = next(
            entry.name
            for entry in (BASE_DIR / "raw_files").iterdir()
            if entry.is_file() and entry.suffix.lower() == ".arw"
        )
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
        raw_filename = next(
            entry.name
            for entry in (BASE_DIR / "raw_files").iterdir()
            if entry.is_file() and entry.suffix.lower() == ".arw"
        )
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
        raw_filename = next(
            entry.name
            for entry in (BASE_DIR / "raw_files").iterdir()
            if entry.is_file() and entry.suffix.lower() == ".arw"
        )
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


if __name__ == "__main__":
    unittest.main()
