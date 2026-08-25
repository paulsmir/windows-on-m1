"""Reproducibly package two independent AGX clear-frame captures."""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from tools.agx_frame_fixture import (
    FixtureError,
    _canonical_zip_bytes,
    _load_json,
    _read_members,
    build_manifest,
)


_SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
READBACK = bytes([0x11, 0x22, 0x33, 0xFF]) * 256


class CaptureError(RuntimeError):
    """Two cold captures did not satisfy the reproducibility contract."""


@dataclass(frozen=True)
class CaptureInput:
    frame_path: Path
    final_attachment_path: Path
    identity: dict
    capture_program_sha256: str
    proxy_identity: str
    m1n1_base: int


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _read_bytes(path: Path, boundary: str) -> bytes:
    try:
        return Path(path).read_bytes()
    except OSError as exc:
        raise CaptureError(f"cannot read {boundary}: {exc}") from exc


def _capture_members(capture: CaptureInput) -> dict[str, bytes]:
    try:
        return _read_members(Path(capture.frame_path))
    except FixtureError as exc:
        raise CaptureError(f"invalid frame capture: {exc}") from exc


def _object_metadata(members: dict[str, bytes]):
    if "objects.json" not in members:
        raise CaptureError("object metadata member is missing")
    try:
        objects = _load_json(members["objects.json"], "objects.json")
    except FixtureError as exc:
        raise CaptureError(f"invalid object metadata: {exc}") from exc
    if not isinstance(objects, list):
        raise CaptureError("object metadata must be an array")
    return objects


def _compare_object_metadata(first, second) -> None:
    if not isinstance(first, list) or not isinstance(second, list):
        raise CaptureError("object metadata must be arrays")
    if len(first) != len(second):
        raise CaptureError("object metadata count differs")
    for index, (left, right) in enumerate(zip(first, second)):
        if not isinstance(left, dict) or not isinstance(right, dict):
            raise CaptureError(f"object metadata entry {index} is malformed")
        if left.get("addr") != right.get("addr"):
            raise CaptureError(f"object address differs at index {index}")
        if left.get("map_flags") != right.get("map_flags"):
            raise CaptureError(f"object map flags differ at index {index}")
        for field in ("file", "name", "size"):
            if left.get(field) != right.get(field):
                raise CaptureError(f"object metadata {field} differs at index {index}")


def compare_captures(first: CaptureInput, second: CaptureInput) -> dict:
    """Compare every trusted byte from two independent cold captures."""

    if not first.proxy_identity or not second.proxy_identity:
        raise CaptureError("proxy identity must be nonempty")
    if first.proxy_identity == second.proxy_identity:
        raise CaptureError("proxy identity must differ between cold captures")
    if (
        isinstance(first.m1n1_base, bool)
        or isinstance(second.m1n1_base, bool)
        or not isinstance(first.m1n1_base, int)
        or not isinstance(second.m1n1_base, int)
        or first.m1n1_base <= 0
        or second.m1n1_base <= 0
    ):
        raise CaptureError("m1n1 base must be a positive integer")
    if first.m1n1_base == second.m1n1_base:
        raise CaptureError("m1n1 base must differ between cold captures")
    if first.identity != second.identity:
        raise CaptureError("source identity differs between cold captures")
    for digest in (first.capture_program_sha256, second.capture_program_sha256):
        if not isinstance(digest, str) or not _SHA256_RE.fullmatch(digest):
            raise CaptureError("capture program hash is malformed")
    if first.capture_program_sha256 != second.capture_program_sha256:
        raise CaptureError("capture program differs between cold captures")

    left = _capture_members(first)
    right = _capture_members(second)
    if set(left) != set(right):
        raise CaptureError("capture member inventory differs")
    if left.get("cmdbuf.json") != right.get("cmdbuf.json"):
        raise CaptureError("command buffer differs between cold captures")
    left_objects = _object_metadata(left)
    right_objects = _object_metadata(right)
    _compare_object_metadata(left_objects, right_objects)
    for index, item in enumerate(left_objects):
        member = item.get("file") if isinstance(item, dict) else None
        if not isinstance(member, str) or member not in left or member not in right:
            raise CaptureError(f"object member is missing at index {index}")
        if left[member] != right[member]:
            raise CaptureError(f"object bytes differ at index {index}")
    for name in sorted(left):
        if name not in {"cmdbuf.json", "objects.json"} and not any(
            isinstance(item, dict) and item.get("file") == name
            for item in left_objects
        ):
            if left[name] != right[name]:
                raise CaptureError(f"capture member differs: {name}")

    first_output = _read_bytes(first.final_attachment_path, "final attachment")
    second_output = _read_bytes(second.final_attachment_path, "final attachment")
    if first_output != second_output:
        raise CaptureError("final attachment differs between cold captures")
    canonical_left = _canonical_zip_bytes(left)
    canonical_right = _canonical_zip_bytes(right)
    if canonical_left != canonical_right:
        raise CaptureError("canonical capture bytes differ")
    return {
        "canonical_frame": canonical_left,
        "expected_output": first_output,
        "fixture_sha256": _sha256(canonical_left),
        "expected_output_sha256": _sha256(first_output),
        "identity": dict(first.identity),
        "capture_program_sha256": first.capture_program_sha256,
    }


def _json_bytes(value) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def _write_temporary(path: Path, data: bytes) -> None:
    path.write_bytes(data)


def package_capture(
    first: CaptureInput,
    second: CaptureInput,
    *,
    capture_program: Path,
    destination: Path,
) -> tuple[Path, Path]:
    """Atomically publish one fixture only after both cold captures match."""

    destination = Path(destination)
    if destination.exists() and (
        not destination.is_dir() or any(destination.iterdir())
    ):
        raise CaptureError("destination must be an initially empty directory")
    program = _read_bytes(capture_program, "capture program")
    program_hash = _sha256(program)
    if program_hash not in {
        first.capture_program_sha256,
        second.capture_program_sha256,
    } or first.capture_program_sha256 != second.capture_program_sha256:
        raise CaptureError("capture program bytes do not match both receipts")

    compared = compare_captures(first, second)
    try:
        manifest = build_manifest(
            Path(first.frame_path),
            identity=compared["identity"],
            capture_program_sha256=program_hash,
            expected_output=compared["expected_output"],
        )
    except FixtureError as exc:
        raise CaptureError(f"fixture manifest rejected: {exc}") from exc
    provenance = {
        "capture_program_sha256": program_hash,
        "fixture_sha256": compared["fixture_sha256"],
        "final_attachment_sha256": compared["expected_output_sha256"],
        "identity": compared["identity"],
        "cold_boots": [
            {
                "m1n1_base": capture.m1n1_base,
                "proxy_identity": capture.proxy_identity,
            }
            for capture in (first, second)
        ],
    }

    staging = destination.with_name(destination.name + ".tmp")
    if staging.exists():
        raise CaptureError(f"stale packaging directory exists: {staging}")
    staging.mkdir(parents=True)
    try:
        _write_temporary(staging / "frame.agx", compared["canonical_frame"])
        _write_temporary(staging / "manifest.json", _json_bytes(manifest))
        _write_temporary(staging / "provenance.json", _json_bytes(provenance))
        if destination.exists():
            destination.rmdir()
        staging.replace(destination)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return destination / "frame.agx", destination / "manifest.json"


def capture_program_from_environment() -> Path | None:
    value = os.environ.get("AGX_CAPTURE_PROGRAM")
    if value is None:
        return None
    path = Path(value)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise CaptureError("AGX_CAPTURE_PROGRAM must name an executable file")
    return path


def run_capture_program(executable: Path, output: Path) -> Path:
    executable = Path(executable)
    if not executable.is_file() or not os.access(executable, os.X_OK):
        raise CaptureError("capture program must be an executable file")
    output = Path(output)
    temporary = output.with_name(output.name + ".tmp")
    temporary.unlink(missing_ok=True)
    try:
        result = subprocess.run(
            [str(executable), str(temporary)],
            check=False,
            capture_output=True,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise CaptureError(f"capture program failed: {exc}") from exc
    if result.returncode != 0:
        temporary.unlink(missing_ok=True)
        raise CaptureError(f"capture program exited with status {result.returncode}")
    data = _read_bytes(temporary, "capture-program output")
    if len(data) != 1024:
        temporary.unlink(missing_ok=True)
        raise CaptureError(f"capture program must produce one 1024-byte RGBA8 image")
    if data != READBACK:
        temporary.unlink(missing_ok=True)
        raise CaptureError("capture program pixels do not match 11 22 33 ff")
    temporary.replace(output)
    return output


def _receipt(path: Path) -> CaptureInput:
    try:
        value = json.loads(Path(path).read_text())
        return CaptureInput(
            frame_path=Path(value["frame_path"]),
            final_attachment_path=Path(value["final_attachment_path"]),
            identity=value["identity"],
            capture_program_sha256=value["capture_program_sha256"],
            proxy_identity=value["proxy_identity"],
            m1n1_base=value["m1n1_base"],
        )
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as exc:
        raise CaptureError(f"invalid capture receipt {path}: {exc}") from exc


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    package = commands.add_parser("package-two")
    package.add_argument("--first-receipt", type=Path, required=True)
    package.add_argument("--second-receipt", type=Path, required=True)
    package.add_argument("--capture-program", type=Path, required=True)
    package.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        frame, manifest = package_capture(
            _receipt(args.first_receipt),
            _receipt(args.second_receipt),
            capture_program=args.capture_program,
            destination=args.destination,
        )
        print(json.dumps({"fixture": str(frame), "manifest": str(manifest)}, sort_keys=True))
        return 0
    except CaptureError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
