#!/usr/bin/env python3
"""Serve the latest atomically published BGRX framebuffer to a canvas viewer."""

import argparse
import http.server
import json
from pathlib import Path
from urllib.parse import urlsplit
import zlib


class DisplayHTTPServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class DisplayRequestHandler(http.server.BaseHTTPRequestHandler):
    root: Path
    viewer_path: Path

    def _send_bytes(self, status, content_type, body, extra_headers=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for name, value in (extra_headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def _unavailable(self, message):
        self._send_bytes(
            503,
            "text/plain; charset=utf-8",
            (message + "\n").encode("utf-8"),
            {"Retry-After": "1"},
        )

    def _read_metadata(self):
        metadata = json.loads((self.root / "fb-info.json").read_text("utf-8"))
        required = {
            "generation", "frame_id", "base", "width", "height", "stride",
            "size", "format", "crc32",
        }
        if not required.issubset(metadata):
            raise ValueError("incomplete framebuffer metadata")
        if metadata["generation"] < 1 or metadata["size"] < 1:
            raise ValueError("invalid framebuffer publication")
        return metadata

    def _read_telemetry(self):
        telemetry = json.loads(
            (self.root / "hang-telemetry-status.json").read_text("utf-8")
        )
        if not isinstance(telemetry, dict):
            raise ValueError("telemetry status is not an object")
        if telemetry.get("state") not in {"available", "streaming", "unavailable"}:
            raise ValueError("invalid telemetry state")
        return telemetry

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/":
            try:
                body = self.viewer_path.read_bytes()
            except OSError as error:
                self._unavailable(f"viewer unavailable: {error}")
                return
            self._send_bytes(200, "text/html; charset=utf-8", body)
            return

        if path == "/meta":
            try:
                metadata = self._read_metadata()
                if not (self.root / "fb.raw").is_file():
                    raise FileNotFoundError("fb.raw")
                body = (json.dumps(metadata, sort_keys=True) + "\n").encode("utf-8")
            except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                self._unavailable(f"framebuffer unavailable: {error}")
                return
            self._send_bytes(200, "application/json", body)
            return

        if path == "/telemetry":
            try:
                telemetry = self._read_telemetry()
                body = (json.dumps(telemetry, sort_keys=True) + "\n").encode("utf-8")
            except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                self._unavailable(f"telemetry unavailable: {error}")
                return
            self._send_bytes(200, "application/json", body)
            return

        if path == "/frame":
            try:
                metadata = self._read_metadata()
                raw_path = self.root / "fb.raw"
                if not raw_path.is_file():
                    raise FileNotFoundError("fb.raw")
                etag = f'"generation-{metadata["generation"]}"'
                if self.headers.get("If-None-Match") == etag:
                    self.send_response(304)
                    self.send_header("ETag", etag)
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    return
                frame = raw_path.read_bytes()
                if len(frame) != metadata["size"]:
                    raise ValueError("frame size does not match metadata")
                if zlib.crc32(frame) & 0xFFFFFFFF != metadata["crc32"]:
                    raise ValueError("frame checksum does not match metadata")
            except (OSError, ValueError, TypeError, json.JSONDecodeError) as error:
                self._unavailable(f"framebuffer unavailable: {error}")
                return
            self._send_bytes(
                200,
                "application/octet-stream",
                frame,
                {"ETag": etag},
            )
            return

        self.send_error(404)

    def log_message(self, _format, *_args):
        pass


def create_server(root, host="127.0.0.1", port=8766, viewer_path=None):
    root = Path(root).resolve()
    viewer = Path(viewer_path or Path(__file__).with_name("viewer.html")).resolve()
    handler = type(
        "ConfiguredDisplayRequestHandler",
        (DisplayRequestHandler,),
        {"root": root, "viewer_path": viewer},
    )
    return DisplayHTTPServer((host, port), handler)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8766)
    args = parser.parse_args()

    server = create_server(args.root, args.host, args.port)
    print(
        f"virtual display on http://{args.host}:{server.server_port}/ "
        f"(frames: {args.root})",
        flush=True,
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
