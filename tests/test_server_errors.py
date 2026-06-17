import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

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
            with mock.patch.object(server.logger, "info") as info_log:
                server._log_native_engine_startup_status()

        fake_session.list_profiles.assert_called_once_with()
        info_log.assert_any_call(
            "Native engine startup status: version=%s libraw_enabled=%s cuda_mode=%s cuda_compiled=%s cuda_available=%s cuda_active=%s device_count=%s device_name=%s fallback_reason=%s",
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
            with mock.patch.object(server.logger, "warning") as warning_log:
                server._log_native_engine_startup_status()

        warning_log.assert_called_once()
        self.assertEqual(warning_log.call_args.args[0], "Native engine startup probe unavailable: %s")
        self.assertEqual(str(warning_log.call_args.args[1]), "bridge missing")

    def test_raw_image_can_use_native_preview_behind_flag(self):
        native_preview = SimpleNamespace(
            jpeg_bytes=b"native-jpeg-bytes",
            content_type="image/jpeg",
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


if __name__ == "__main__":
    unittest.main()
