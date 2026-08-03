import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import zlib

from extra.display_server import DisplayRequestHandler, create_server


class TestTelemetryStatusReader(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.handler = DisplayRequestHandler.__new__(DisplayRequestHandler)
        self.handler.root = self.root

    def test_reads_available_or_unavailable_object(self):
        for telemetry in (
            {"state": "available", "last_sequence": 9, "findings": ["running"]},
            {"state": "unavailable", "error": "usb link lost"},
        ):
            with self.subTest(state=telemetry["state"]):
                (self.root / "hang-telemetry-status.json").write_text(json.dumps(telemetry))
                self.assertEqual(self.handler._read_telemetry(), telemetry)

    def test_rejects_non_object_or_unknown_state(self):
        for telemetry in ([], {"state": "mystery"}):
            with self.subTest(telemetry=telemetry):
                (self.root / "hang-telemetry-status.json").write_text(json.dumps(telemetry))
                with self.assertRaises(ValueError):
                    self.handler._read_telemetry()

    def test_viewer_polls_and_displays_telemetry_status(self):
        viewer = Path(__file__).resolve().parent.parent / "extra" / "viewer.html"
        source = viewer.read_text("utf-8")
        self.assertIn('id="diagnostic"', source)
        self.assertIn("fetch('/telemetry'", source)
        self.assertIn("updated_at", source)


class TestDisplayServer(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tempdir.cleanup)
        self.root = Path(self.tempdir.name)
        self.server = create_server(self.root, host="127.0.0.1", port=0)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def write_frame(self, generation=7):
        frame = bytes(range(64))
        metadata = {
            "generation": generation,
            "frame_id": 12,
            "base": 0x5000,
            "width": 8,
            "height": 2,
            "stride": 32,
            "size": len(frame),
            "format": "B8G8R8X8",
            "crc32": zlib.crc32(frame) & 0xFFFFFFFF,
        }
        (self.root / "fb.raw").write_bytes(frame)
        (self.root / "fb-info.json").write_text(json.dumps(metadata))
        return frame, metadata

    def get(self, path, headers=None):
        request = Request(self.base + path, headers=headers or {})
        with urlopen(request, timeout=2) as response:
            return response.status, response.headers, response.read()

    def test_meta_and_frame_return_literal_publication(self):
        frame, metadata = self.write_frame()
        status, headers, body = self.get("/meta")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body), metadata)
        self.assertEqual(headers["Cache-Control"], "no-store")

        status, headers, body = self.get("/frame")
        self.assertEqual(status, 200)
        self.assertEqual(body, frame)
        self.assertEqual(headers["ETag"], '"generation-7"')

    def test_matching_generation_etag_returns_304(self):
        self.write_frame()
        with self.assertRaises(HTTPError) as caught:
            self.get("/frame", {"If-None-Match": '"generation-7"'})
        self.assertEqual(caught.exception.code, 304)

    def test_absent_files_return_503_and_server_recovers(self):
        for path in ("/meta", "/frame"):
            with self.subTest(path=path), self.assertRaises(HTTPError) as caught:
                self.get(path)
            self.assertEqual(caught.exception.code, 503)

        frame, _ = self.write_frame(9)
        self.assertEqual(self.get("/frame")[2], frame)

    def test_telemetry_endpoint_returns_latest_atomic_status(self):
        with self.assertRaises(HTTPError) as caught:
            self.get("/telemetry")
        self.assertEqual(caught.exception.code, 503)

        telemetry = {
            "state": "available",
            "last_sequence": 41,
            "findings": ["timer-progress", "guest-pc-static"],
            "ring": {
                "capacity": 256,
                "count": 4,
                "oldest_sequence": 38,
                "next_sequence": 42,
            },
        }
        (self.root / "hang-telemetry-status.json").write_text(json.dumps(telemetry))
        status, headers, body = self.get("/telemetry")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body), telemetry)
        self.assertEqual(headers["Cache-Control"], "no-store")

    def test_malformed_telemetry_status_is_unavailable(self):
        (self.root / "hang-telemetry-status.json").write_text("[]")
        with self.assertRaises(HTTPError) as caught:
            self.get("/telemetry")
        self.assertEqual(caught.exception.code, 503)


if __name__ == "__main__":
    unittest.main()
