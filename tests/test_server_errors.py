import logging
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import numpy as np
from fastapi.testclient import TestClient

import server


BASE_DIR = Path(__file__).resolve().parent.parent


class TestServerRawFailureHandling(unittest.TestCase):
    def setUp(self):
        self.client = TestClient(server.app)
        server.session = server.ActiveSession()
        server._get_native_bridge_module.cache_clear()
        server._get_native_engine_session.cache_clear()
        self._temp_files: list[Path] = []

    def tearDown(self):
        server.session = server.ActiveSession()
        server._get_native_bridge_module.cache_clear()
        server._get_native_engine_session.cache_clear()
        for path in self._temp_files:
            try:
                path.unlink(missing_ok=True)
            except Exception:
                pass

    def _create_temp_raw_file(self, name: str, payload: bytes) -> str:
        path = BASE_DIR / "raw_files" / name
        path.write_bytes(payload)
        self._temp_files.append(path)
        return path.name

    def _raw_filename(self) -> str:
        return next(
            entry.name
            for entry in (BASE_DIR / "raw_files").iterdir()
            if entry.is_file() and entry.suffix.lower() == ".arw"
        )

    @staticmethod
    def _fake_select_ingest():
        rgb = np.full((2, 2, 3), 0.25, dtype=np.float32)
        Y = np.full((2, 2), 0.25, dtype=np.float32)
        clipping_masks = {
            "R": np.zeros((2, 2), dtype=bool),
            "G": np.zeros((2, 2), dtype=bool),
            "B": np.zeros((2, 2), dtype=bool),
        }
        clipping_ratios = {"R": 0.0, "G": 0.0, "B": 0.0}
        metadata = {"image_width": 2, "image_height": 2, "camera_model": "Test Camera"}
        return rgb, Y, clipping_masks, clipping_ratios, metadata

    @staticmethod
    def _fake_engine_info():
        return SimpleNamespace(
            engine_version="0.1.0",
            libraw_enabled=True,
            cuda_status=SimpleNamespace(mode="cpu"),
            timings=[
                SimpleNamespace(stage="render_preview_total", milliseconds=12.5),
                SimpleNamespace(stage="render_preview_film_pipeline", milliseconds=8.25),
            ],
        )

    def test_native_backend_is_enabled_by_default_when_no_override_is_set(self):
        with mock.patch.dict(server.os.environ, {}, clear=True):
            self.assertTrue(server._native_profiles_enabled())
            self.assertTrue(server._native_raw_image_enabled())
            self.assertTrue(server._native_preview_enabled())
            self.assertTrue(server._native_export_enabled())
            self.assertTrue(server._native_select_enabled())

    def test_global_native_flag_can_disable_routes_when_set_false(self):
        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_ENGINE": "0"}, clear=True):
            self.assertFalse(server._native_profiles_enabled())
            self.assertFalse(server._native_raw_image_enabled())
            self.assertFalse(server._native_preview_enabled())
            self.assertFalse(server._native_export_enabled())
            self.assertFalse(server._native_select_enabled())

    def test_route_override_takes_precedence_over_global_native_flag(self):
        with mock.patch.dict(
            server.os.environ,
            {
                "DFEE_USE_NATIVE_ENGINE": "1",
                "DFEE_USE_NATIVE_PREVIEW": "0",
                "DFEE_USE_NATIVE_EXPORT": "false",
                "DFEE_USE_NATIVE_SELECT": "no",
            },
            clear=True,
        ):
            self.assertTrue(server._native_profiles_enabled())
            self.assertTrue(server._native_raw_image_enabled())
            self.assertFalse(server._native_preview_enabled())
            self.assertFalse(server._native_export_enabled())
            self.assertFalse(server._native_select_enabled())

    def test_uvicorn_run_kwargs_disable_access_log_and_custom_log_config(self):
        kwargs = server._uvicorn_run_kwargs()
        self.assertEqual(kwargs["host"], "127.0.0.1")
        self.assertEqual(kwargs["port"], 8000)
        self.assertIsNone(kwargs["log_config"])
        self.assertFalse(kwargs["access_log"])

    def test_color_formatter_formats_without_mutating_record(self):
        formatter = server.ColorFormatter(use_color=False)
        record = logging.LogRecord(
            name="dfee.server",
            level=logging.INFO,
            pathname=__file__,
            lineno=1,
            msg="hello world",
            args=(),
            exc_info=None,
        )
        rendered = formatter.format(record)
        self.assertIn("INFO", rendered)
        self.assertIn("hello world", rendered)
        self.assertEqual(record.levelname, "INFO")

    def test_select_rejects_unsupported_raw_without_crashing(self):
        filename = self._create_temp_raw_file("server_test_unsupported.arw", b"not a real raw file\n")

        response = self.client.post("/api/select", json={"filename": filename})

        self.assertEqual(response.status_code, 500)
        self.assertIn("Failed to ingest RAW file", response.json()["detail"])

        follow_up = self.client.get("/")
        self.assertEqual(follow_up.status_code, 200)

    def test_select_rejects_corrupt_raw_without_crashing(self):
        source_raw = BASE_DIR / "raw_files" / self._raw_filename()
        filename = self._create_temp_raw_file("server_test_corrupt.arw", source_raw.read_bytes()[:4096])

        response = self.client.post("/api/select", json={"filename": filename})

        self.assertEqual(response.status_code, 500)
        self.assertIn("Failed to ingest RAW file", response.json()["detail"])

        follow_up = self.client.get("/")
        self.assertEqual(follow_up.status_code, 200)

    def test_profiles_can_use_native_loader_behind_flag(self):
        native_payload = {
            "stocks": [
                {"id": "none", "name": "No emulation (RAW)", "type": "passthrough"},
                {"id": "portra_400", "name": "Kodak Portra 400", "type": "color_negative"},
            ],
            "print_stocks": [
                {"id": "none", "name": "No print finish"},
                {"id": "kodak_2383", "name": "Kodak Vision 2383"},
            ],
            "engine": {
                "engine_version": "0.1.0",
                "libraw_enabled": True,
                "cuda_mode": "cpu",
            },
        }

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_PROFILES": "1"}, clear=False):
            with mock.patch.object(server, "_list_profiles_native", return_value=native_payload) as native_mock:
                with mock.patch.object(server, "_list_profiles_python") as python_mock:
                    response = self.client.get("/api/profiles")

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {
            "stocks": native_payload["stocks"],
            "print_stocks": native_payload["print_stocks"],
        })
        native_mock.assert_called_once_with()
        python_mock.assert_not_called()

    def test_profiles_fall_back_to_python_loader_when_native_listing_fails(self):
        python_payload = {
            "stocks": [{"id": "none", "name": "No emulation (RAW)", "type": "passthrough"}],
            "print_stocks": [{"id": "none", "name": "No print finish"}],
        }

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_PROFILES": "true"}, clear=False):
            with mock.patch.object(server, "_list_profiles_native", side_effect=RuntimeError("native failed")) as native_mock:
                with mock.patch.object(server, "_list_profiles_python", return_value=python_payload) as python_mock:
                    response = self.client.get("/api/profiles")

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), python_payload)
        native_mock.assert_called_once_with()
        python_mock.assert_called_once_with()

    def test_native_startup_log_reports_engine_capabilities(self):
        fake_cuda = SimpleNamespace(
            mode="cpu",
            compiled=False,
            available=False,
            active=False,
            device_count=0,
            device_name="",
            fallback_reason="CUDA not compiled",
        )
        fake_engine = SimpleNamespace(
            engine_version="0.1.0",
            libraw_enabled=True,
            cuda_status=fake_cuda,
        )
        fake_profiles = SimpleNamespace(engine=fake_engine)
        fake_session = SimpleNamespace(list_profiles=mock.Mock(return_value=fake_profiles))

        with mock.patch.object(server, "_get_native_engine_session", return_value=fake_session):
            with mock.patch.object(server, "NATIVE_BUILD_DIR", Path("D:/Codebases/DFEE/cpp_engine/out/build/windows-msvc-vcpkg/Release")):
                with mock.patch.object(server.logger, "info") as info_log:
                    server._log_native_engine_startup_status()

        fake_session.list_profiles.assert_called_once_with()
        info_log.assert_any_call(
            "Native engine startup status: build_dir=%s version=%s libraw_enabled=%s cuda_mode=%s cuda_compiled=%s cuda_available=%s cuda_active=%s device_count=%s device_name=%s fallback_reason=%s",
            "D:\\Codebases\\DFEE\\cpp_engine\\out\\build\\windows-msvc-vcpkg\\Release",
            "0.1.0",
            True,
            "cpu",
            False,
            False,
            False,
            0,
            "n/a",
            "CUDA not compiled",
        )

    def test_native_startup_log_degrades_to_warning_on_probe_failure(self):
        with mock.patch.object(server, "_get_native_engine_session", side_effect=RuntimeError("bridge missing")):
            with mock.patch.object(server, "NATIVE_BUILD_DIR", None):
                with mock.patch.object(server.logger, "warning") as warning_log:
                    server._log_native_engine_startup_status()

        warning_log.assert_called_once()
        self.assertEqual(warning_log.call_args.args[0], "Native engine startup probe unavailable: build_dir=%s error=%s")
        self.assertEqual(warning_log.call_args.args[1], "not_found")
        self.assertEqual(str(warning_log.call_args.args[2]), "bridge missing")

    def test_raw_image_can_use_native_preview_behind_flag(self):
        native_preview = SimpleNamespace(
            jpeg_bytes=b"native-jpeg-bytes",
            content_type="image/jpeg",
            engine=self._fake_engine_info(),
        )
        fake_session = SimpleNamespace(
            select_file=mock.Mock(),
            decode_raw=mock.Mock(),
            raw_preview=mock.Mock(return_value=native_preview),
        )
        server.session.filename = "example.ARW"
        server.session.raw_preview_bytes = b"python-jpeg-bytes"

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_RAW_IMAGE": "1"}, clear=False):
            with mock.patch.object(server, "_get_native_engine_session", return_value=fake_session):
                response = self.client.get("/api/raw-image")

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.content, b"native-jpeg-bytes")
        self.assertEqual(response.headers["content-type"], "image/jpeg")
        fake_session.select_file.assert_called_once_with("example.ARW")
        fake_session.decode_raw.assert_called_once_with("example.ARW", draft_mode=True)
        fake_session.raw_preview.assert_called_once_with("example.ARW", max_edge=1024)

    def test_raw_image_falls_back_to_python_cache_when_native_preview_fails(self):
        server.session.filename = "example.ARW"
        server.session.raw_preview_bytes = b"python-jpeg-bytes"

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_RAW_IMAGE": "true"}, clear=False):
            with mock.patch.object(server, "_get_native_engine_session", side_effect=RuntimeError("native preview failed")):
                response = self.client.get("/api/raw-image")

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.content, b"python-jpeg-bytes")
        self.assertEqual(response.headers["content-type"], "image/jpeg")

    def test_preview_can_use_native_render_behind_flag(self):
        server.session.filename = "example.ARW"
        server.session.raw_preview_bytes = b"python-raw-preview"

        native_preview = SimpleNamespace(
            jpeg_bytes=b"native-preview-jpeg",
            content_type="image/jpeg",
            engine=self._fake_engine_info(),
        )
        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_PREVIEW": "1"}, clear=False):
            with mock.patch.object(server, "_get_native_rendered_preview", return_value=native_preview) as native_mock:
                response = self.client.get(
                    "/api/preview",
                    params={
                        "filename": "example.ARW",
                        "stock": "portra_400",
                        "print_stock": "kodak_2383",
                        "exposure": 0.15,
                        "saturation": 10.0,
                        "bloom": 5.0,
                    },
                )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.content, native_preview.jpeg_bytes)
        self.assertEqual(response.headers["content-type"], "image/jpeg")
        native_mock.assert_called_once()
        payload = native_mock.call_args.args[0]
        self.assertEqual(payload["filename"], "example.ARW")
        self.assertEqual(payload["stock"], "portra_400")
        self.assertEqual(payload["print_stock"], "kodak_2383")
        self.assertEqual(payload["exposure"], 0.15)
        self.assertEqual(payload["saturation"], 10.0)
        self.assertEqual(payload["bloom"], 5.0)

    def test_preview_falls_back_to_python_pipeline_when_native_render_fails(self):
        server.session.filename = "example.ARW"
        server.session.preview_rgb_linear = mock.Mock(copy=mock.Mock(return_value=np.zeros((2, 2, 3), dtype=np.float32)))
        server.session.masks = {}
        server.session.feature_dict = {}
        server.session.raw_preview_bytes = b"python-raw-preview"

        stock_profile = object()
        render_plan = {
            "pre_film_normalization": {
                "exposure_compensation_stops": 0.0,
                "contrast_compensation": 0.0,
                "highlights_compensation": 0.0,
                "shadows_compensation": 0.0,
                "whites_compensation": 0.0,
                "blacks_compensation": 0.0,
                "midtones_compensation": 0.0,
            }
        }
        rendered_linear = np.full((2, 2, 3), 0.25, dtype=np.float32)
        jpeg_bytes = np.array([1, 2, 3], dtype=np.uint8)

        solver_instance = mock.Mock(solve=mock.Mock(return_value=render_plan))
        renderer_instance = mock.Mock(render=mock.Mock(return_value=rendered_linear))

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_PREVIEW": "true"}, clear=False):
            with mock.patch.object(server, "_get_native_rendered_preview", side_effect=RuntimeError("native preview failed")):
                with mock.patch.object(server, "_load_stock_profile", return_value=stock_profile):
                    with mock.patch.object(server, "RenderPlanSolver", return_value=solver_instance):
                        with mock.patch.object(server, "FilmRenderer", return_value=renderer_instance):
                            with mock.patch.object(server, "_apply_pre_film_sliders", return_value=np.zeros((2, 2, 3), dtype=np.float32)):
                                with mock.patch.object(server, "_apply_post_film_color", return_value=rendered_linear):
                                    with mock.patch.object(server, "_apply_post_film_effects", return_value=rendered_linear):
                                        with mock.patch.object(server, "linear_to_srgb", return_value=rendered_linear):
                                            with mock.patch.object(server.cv2, "cvtColor", return_value=np.zeros((2, 2, 3), dtype=np.uint8)):
                                                with mock.patch.object(server.cv2, "imencode", return_value=(True, jpeg_bytes)):
                                                    response = self.client.get(
                                                        "/api/preview",
                                                        params={"filename": "example.ARW", "stock": "portra_400"},
                                                    )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.content, bytes(jpeg_bytes.tolist()))
        solver_instance.solve.assert_called_once()
        renderer_instance.render.assert_called_once()

    def test_export_can_use_native_path_behind_flag(self):
        native_result = {
            "status": "success",
            "output_path": "D:/Codebases/DFEE/raw_files/example_portra_400_dfee.png",
            "report_path": "D:/Codebases/DFEE/raw_files/example_portra_400_report.json",
            "format": "8-bit PNG",
            "engine": self._fake_engine_info(),
        }

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_EXPORT": "1"}, clear=False):
            with mock.patch.object(server, "_run_native_export", return_value=native_result) as native_mock:
                response = self.client.post(
                    "/api/export",
                    json={
                        "filename": self._raw_filename(),
                        "stock": "portra_400",
                        "print_stock": "kodak_2383",
                        "export_format": "png8",
                        "exposure": 0.1,
                        "saturation": 6.0,
                    },
                )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {
            "status": "success",
            "output_path": "D:/Codebases/DFEE/raw_files/example_portra_400_dfee.png",
            "report_path": "D:/Codebases/DFEE/raw_files/example_portra_400_report.json",
            "format": "8-bit PNG",
        })
        native_mock.assert_called_once()
        payload = native_mock.call_args.args[0]
        self.assertEqual(payload["stock"], "portra_400")
        self.assertEqual(payload["print_stock"], "kodak_2383")
        self.assertEqual(payload["export_format"], "png8")
        self.assertEqual(payload["exposure"], 0.1)
        self.assertEqual(payload["saturation"], 6.0)

    def test_export_can_use_native_png8_path_with_dpi(self):
        native_result = {
            "status": "success",
            "output_path": "D:/Codebases/DFEE/raw_files/example_portra_400_dfee.png",
            "report_path": "D:/Codebases/DFEE/raw_files/example_portra_400_report.json",
            "format": "8-bit PNG",
            "engine": self._fake_engine_info(),
        }

        with mock.patch.object(server, "_run_native_export", return_value=native_result) as native_mock:
            response = self.client.post(
                "/api/export",
                json={
                    "filename": self._raw_filename(),
                    "stock": "portra_400",
                    "print_stock": "kodak_2383",
                    "export_format": "png8",
                    "export_dpi": 240,
                    "embed_metadata": True,
                },
            )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["format"], "8-bit PNG")
        native_mock.assert_called_once()
        payload = native_mock.call_args.args[0]
        self.assertEqual(payload["export_format"], "png8")
        self.assertEqual(payload["export_dpi"], 240)
        self.assertTrue(payload["embed_metadata"])

    def test_export_can_use_native_png16_path_with_dpi(self):
        native_result = {
            "status": "success",
            "output_path": "D:/Codebases/DFEE/raw_files/example_portra_400_dfee_16.png",
            "report_path": "D:/Codebases/DFEE/raw_files/example_portra_400_report.json",
            "format": "16-bit PNG",
            "engine": self._fake_engine_info(),
        }

        with mock.patch.object(server, "_run_native_export", return_value=native_result) as native_mock:
            response = self.client.post(
                "/api/export",
                json={
                    "filename": self._raw_filename(),
                    "stock": "portra_400",
                    "print_stock": "kodak_2383",
                    "export_format": "png16",
                    "export_dpi": 240,
                    "embed_metadata": True,
                },
            )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["format"], "16-bit PNG")
        native_mock.assert_called_once()
        payload = native_mock.call_args.args[0]
        self.assertEqual(payload["export_format"], "png16")
        self.assertEqual(payload["export_dpi"], 240)
        self.assertTrue(payload["embed_metadata"])

    def test_export_can_use_native_tiff_path_with_dpi(self):
        native_result = {
            "status": "success",
            "output_path": "D:/Codebases/DFEE/raw_files/example_portra_400_dfee.tif",
            "report_path": "D:/Codebases/DFEE/raw_files/example_portra_400_report.json",
            "format": "16-bit TIFF",
            "engine": self._fake_engine_info(),
        }

        with mock.patch.object(server, "_run_native_export", return_value=native_result) as native_mock:
            response = self.client.post(
                "/api/export",
                json={
                    "filename": self._raw_filename(),
                    "stock": "portra_400",
                    "print_stock": "kodak_2383",
                    "export_format": "tiff",
                    "export_dpi": 240,
                    "embed_metadata": True,
                },
            )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["format"], "16-bit TIFF")
        native_mock.assert_called_once()
        payload = native_mock.call_args.args[0]
        self.assertEqual(payload["export_format"], "tiff")
        self.assertEqual(payload["export_dpi"], 240)
        self.assertTrue(payload["embed_metadata"])

    def test_export_uses_python_backend_when_requested_options_exceed_native_support(self):
        raw_filename = self._raw_filename()
        server.session.filename = raw_filename
        server.session.fullres_rgb_linear = np.full((2, 2, 3), 0.25, dtype=np.float32)
        server.session.fullres_Y = np.full((2, 2), 0.25, dtype=np.float32)
        server.session.fullres_clipping_masks = {"R": np.zeros((2, 2), dtype=bool), "G": np.zeros((2, 2), dtype=bool), "B": np.zeros((2, 2), dtype=bool)}
        server.session.fullres_clipping_ratios = {"R": 0.0, "G": 0.0, "B": 0.0}
        server.session.fullres_metadata = {"image_width": 2, "image_height": 2}
        server.session.fullres_feature_dict = {"camera_input_bias": {}, "raw_metadata": {}}
        server.session.fullres_masks = {"luminance_zone_masks": {}}

        stock_profile = object()
        render_plan = {
            "pre_film_normalization": {
                "exposure_compensation_stops": 0.0,
                "contrast_compensation": 0.0,
                "highlights_compensation": 0.0,
                "shadows_compensation": 0.0,
                "whites_compensation": 0.0,
                "blacks_compensation": 0.0,
                "midtones_compensation": 0.0,
            }
        }
        rendered_linear = np.full((2, 2, 3), 0.25, dtype=np.float32)
        solver_instance = mock.Mock(solve=mock.Mock(return_value=render_plan))
        renderer_instance = mock.Mock(render=mock.Mock(return_value=rendered_linear))
        reporter_instance = mock.Mock(write_report=mock.Mock())
        tifffile_module = mock.Mock(imwrite=mock.Mock())

        with mock.patch.object(server, "_run_native_export") as native_mock:
            with mock.patch.object(server, "_load_stock_profile", return_value=stock_profile):
                with mock.patch.object(server, "RenderPlanSolver", return_value=solver_instance):
                    with mock.patch.object(server, "FilmRenderer", return_value=renderer_instance):
                        with mock.patch.object(server, "_apply_pre_film_sliders", return_value=np.zeros((2, 2, 3), dtype=np.float32)):
                            with mock.patch.object(server, "_apply_post_film_color", return_value=rendered_linear):
                                with mock.patch.object(server, "_apply_post_film_effects", return_value=rendered_linear):
                                    with mock.patch.object(server, "linear_to_srgb", return_value=rendered_linear):
                                        with mock.patch.object(server, "RenderReporter", return_value=reporter_instance):
                                            with mock.patch.dict("sys.modules", {"tifffile": tifffile_module}):
                                                response = self.client.post(
                                                    "/api/export",
                                                    json={
                                                        "filename": raw_filename,
                                                        "stock": "portra_400",
                                                        "export_format": "tiff",
                                                        "export_color_space": "adobergb",
                                                    },
                                                )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["format"], "16-bit TIFF")
        native_mock.assert_not_called()
        reporter_instance.write_report.assert_called_once()
        tifffile_module.imwrite.assert_called_once()

    def test_export_can_use_native_jpeg_path_with_quality_and_dpi(self):
        native_result = {
            "status": "success",
            "output_path": "D:/Codebases/DFEE/raw_files/example_portra_400_dfee.jpg",
            "report_path": "D:/Codebases/DFEE/raw_files/example_portra_400_report.json",
            "format": "JPEG",
            "engine": self._fake_engine_info(),
        }

        with mock.patch.object(server, "_run_native_export", return_value=native_result) as native_mock:
            response = self.client.post(
                "/api/export",
                json={
                    "filename": self._raw_filename(),
                    "stock": "portra_400",
                    "print_stock": "kodak_2383",
                    "export_format": "jpeg",
                    "jpeg_quality": 85,
                    "export_dpi": 240,
                    "embed_metadata": True,
                },
            )

        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json()["format"], "JPEG")
        native_mock.assert_called_once()
        payload = native_mock.call_args.args[0]
        self.assertEqual(payload["export_format"], "jpeg")
        self.assertEqual(payload["jpeg_quality"], 85)
        self.assertEqual(payload["export_dpi"], 240)

    def test_export_falls_back_to_python_pipeline_when_native_export_fails(self):
        raw_filename = self._raw_filename()
        server.session.filename = raw_filename
        server.session.fullres_rgb_linear = np.full((2, 2, 3), 0.25, dtype=np.float32)
        server.session.fullres_Y = np.full((2, 2), 0.25, dtype=np.float32)
        server.session.fullres_clipping_masks = {"R": np.zeros((2, 2), dtype=bool), "G": np.zeros((2, 2), dtype=bool), "B": np.zeros((2, 2), dtype=bool)}
        server.session.fullres_clipping_ratios = {"R": 0.0, "G": 0.0, "B": 0.0}
        server.session.fullres_metadata = {"image_width": 2, "image_height": 2}
        server.session.fullres_feature_dict = {"camera_input_bias": {}, "raw_metadata": {}}
        server.session.fullres_masks = {"luminance_zone_masks": {}}

        stock_profile = object()
        print_stock_profile = object()
        render_plan = {
            "pre_film_normalization": {
                "exposure_compensation_stops": 0.0,
                "contrast_compensation": 0.0,
                "highlights_compensation": 0.0,
                "shadows_compensation": 0.0,
                "whites_compensation": 0.0,
                "blacks_compensation": 0.0,
                "midtones_compensation": 0.0,
            }
        }
        rendered_linear = np.full((2, 2, 3), 0.25, dtype=np.float32)

        solver_instance = mock.Mock(solve=mock.Mock(return_value=render_plan))
        renderer_instance = mock.Mock(render=mock.Mock(return_value=rendered_linear))
        reporter_instance = mock.Mock(write_report=mock.Mock())
        image_instance = mock.Mock(save=mock.Mock())

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_EXPORT": "true"}, clear=False):
            with mock.patch.object(server, "_run_native_export", side_effect=RuntimeError("native export failed")):
                with mock.patch.object(server, "_load_stock_profile", return_value=stock_profile):
                    with mock.patch.object(server, "_load_print_stock_profile", return_value=print_stock_profile):
                        with mock.patch.object(server, "RenderPlanSolver", return_value=solver_instance):
                            with mock.patch.object(server, "FilmRenderer", return_value=renderer_instance):
                                with mock.patch.object(server, "_apply_pre_film_sliders", return_value=np.zeros((2, 2, 3), dtype=np.float32)):
                                    with mock.patch.object(server, "_apply_post_film_color", return_value=rendered_linear):
                                        with mock.patch.object(server, "_apply_post_film_effects", return_value=rendered_linear):
                                            with mock.patch.object(server, "linear_to_srgb", return_value=rendered_linear):
                                                with mock.patch.object(server, "RenderReporter", return_value=reporter_instance):
                                                    with mock.patch.object(server, "Image") as image_module:
                                                        image_module.fromarray.return_value = image_instance
                                                        response = self.client.post(
                                                            "/api/export",
                                                            json={
                                                                "filename": raw_filename,
                                                                "stock": "portra_400",
                                                                "print_stock": "kodak_2383",
                                                                "export_format": "png8",
                                                            },
                                                        )

        self.assertEqual(response.status_code, 200)
        payload = response.json()
        self.assertEqual(payload["status"], "success")
        self.assertEqual(payload["format"], "8-bit PNG")
        solver_instance.solve.assert_called_once()
        renderer_instance.render.assert_called_once()
        reporter_instance.write_report.assert_called_once()
        image_instance.save.assert_called_once()

    def test_export_returns_507_when_native_memory_budget_exceeded(self):
        native_error = RuntimeError("native memory budget exceeded")
        native_error.code = "EXPORT_MEMORY_BUDGET_EXCEEDED"
        native_error.user_message = "The export was stopped before rendering because it would exceed the current safe memory budget."
        native_error.detail = "Estimated native export peak exceeds safe memory budget."

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_EXPORT": "1"}, clear=False):
            with mock.patch.object(server, "_run_native_export", side_effect=native_error):
                response = self.client.post(
                    "/api/export",
                    json={
                        "filename": self._raw_filename(),
                        "stock": "portra_400",
                        "print_stock": "kodak_2383",
                        "export_format": "png16",
                    },
                )

        self.assertEqual(response.status_code, 507)
        self.assertIn("safe memory budget", response.json()["detail"])

    def test_select_can_warm_native_session_behind_flag(self):
        raw_filename = self._raw_filename()
        native_result = SimpleNamespace(
            metadata=SimpleNamespace(
                camera_make="Sony",
                camera_model="A7",
                lens_model="Lens",
                iso=200,
                shutter_speed=0.01,
                shutter_speed_str="1/100",
                aperture=2.8,
                focal_length=50.0,
                white_balance_multipliers=[2.0, 1.0, 1.2, 1.0],
                black_level=512,
                white_level=16383,
                image_height=4000,
                image_width=6000,
                raw_height=4000,
                raw_width=6000,
                metadata_json="{}",
            ),
            diagnostics=SimpleNamespace(
                tonal_skew="normal",
                dynamic_range_stops=11.2,
                midtone_anchor=0.18,
                highlight_headroom=0.24,
                shadow_depth=0.07,
                neon_risk=0.12,
                dominant_hues=["Red", "Blue", "Yellow"],
                palette_entropy=1.5,
                specular_ratio=0.004,
                neutral_confidence=0.85,
            ),
            engine=self._fake_engine_info(),
        )

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_SELECT": "1"}, clear=False):
            with mock.patch.object(server, "_get_native_select_result", return_value=native_result) as native_mock:
                response = self.client.post("/api/select", json={"filename": raw_filename})

        self.assertEqual(response.status_code, 200)
        payload = response.json()
        self.assertEqual(payload["status"], "loaded")
        self.assertIn("metadata", payload)
        self.assertIn("diagnostics", payload)
        self.assertEqual(payload["diagnostics"]["dominant_hues"], ["Red", "Blue", "Yellow"])
        native_mock.assert_called_once_with(raw_filename)

    def test_select_falls_back_to_python_analysis_when_native_path_fails(self):
        raw_filename = self._raw_filename()
        fake_analyzer = mock.Mock(analyze=mock.Mock(return_value=(
            {
                "tonal_distribution": {
                    "tonal_skew": "normal",
                    "dynamic_range_stops": 11.2,
                    "midtone_anchor": 0.18,
                    "highlight_headroom": 0.24,
                    "shadow_depth": 0.07,
                },
                "hue_saturation_state": {
                    "neon_risk": 0.12,
                    "dominant_hue_bins": ["Red", "Blue"],
                    "hue_entropy": 1.5,
                },
                "spatial_frequency": {
                    "specular_point_ratio": 0.004,
                },
            },
            {"luminance_zone_masks": {}},
        )))
        fake_bias = {"neutral_confidence": 0.85}
        fake_ingestor = mock.Mock(ingest=mock.Mock(return_value=self._fake_select_ingest()))

        with mock.patch.dict(server.os.environ, {"DFEE_USE_NATIVE_SELECT": "true"}, clear=False):
            with mock.patch.object(server, "_get_native_select_result", side_effect=RuntimeError("native select failed")):
                with mock.patch.object(server, "RawIngestor", return_value=fake_ingestor):
                    with mock.patch.object(server, "ImageStateAnalyzer", return_value=fake_analyzer):
                        with mock.patch.object(server, "CameraBiasEstimator") as bias_cls:
                            bias_cls.return_value.estimate_bias.return_value = fake_bias
                            with mock.patch.object(server, "linear_to_srgb", return_value=np.full((2, 2, 3), 0.25, dtype=np.float32)):
                                with mock.patch.object(server.cv2, "cvtColor", return_value=np.zeros((2, 2, 3), dtype=np.uint8)):
                                    with mock.patch.object(server.cv2, "imencode", return_value=(True, np.array([1, 2, 3], dtype=np.uint8))):
                                        response = self.client.post("/api/select", json={"filename": raw_filename})

        self.assertEqual(response.status_code, 200)
        payload = response.json()
        self.assertEqual(payload["status"], "loaded")
        self.assertIn("metadata", payload)
        self.assertIn("diagnostics", payload)


if __name__ == "__main__":
    unittest.main()
