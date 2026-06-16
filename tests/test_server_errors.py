import unittest
from pathlib import Path

from fastapi.testclient import TestClient

import server


BASE_DIR = Path(__file__).resolve().parent.parent


class TestServerRawFailureHandling(unittest.TestCase):
    def setUp(self):
        self.client = TestClient(server.app)
        server.session = server.ActiveSession()
        self._temp_files: list[Path] = []

    def tearDown(self):
        server.session = server.ActiveSession()
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


if __name__ == "__main__":
    unittest.main()
