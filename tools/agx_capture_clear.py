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
_COMMIT_RE = re.compile(r"[0-9a-f]{40}\Z")
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


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise CaptureError(result.stderr.strip() or f"git failed in {root}")
    return result.stdout.strip()


def _read_identity(path: Path) -> dict:
    try:
        identity = json.loads(Path(path).read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CaptureError(f"invalid capture identity {path}: {exc}") from exc
    required = {
        "board", "chip_generation", "firmware_version", "m1n1_commit",
        "mesa_commit", "adt_sha256",
    }
    if not isinstance(identity, dict) or set(identity) != required:
        raise CaptureError("capture identity fields do not match the fixed contract")
    for field in ("m1n1_commit", "mesa_commit"):
        if not isinstance(identity[field], str) or not _COMMIT_RE.fullmatch(identity[field]):
            raise CaptureError(f"capture identity {field} is not a 40-character commit")
    if not isinstance(identity["adt_sha256"], str) or not _SHA256_RE.fullmatch(
        identity["adt_sha256"]
    ):
        raise CaptureError("capture identity adt_sha256 is malformed")
    return identity


def preflight_capture_source(
    mesa_source: Path,
    shim_launcher: Path,
    capture_program: Path,
    identity_path: Path,
    contract_path: Path,
    expected_program_sha256: str,
) -> dict:
    """Verify the pinned, clean capture producer without touching hardware."""

    mesa_source = Path(mesa_source).resolve()
    shim_launcher = Path(shim_launcher).resolve()
    capture_program = Path(capture_program).resolve()
    identity = _read_identity(identity_path)
    try:
        from tools.agx_contract import load_contract
        contract = load_contract(contract_path)
    except Exception as exc:
        raise CaptureError(f"cannot load AGX contract: {exc}") from exc
    expected_identity = {
        "board": contract.platform,
        "chip_generation": contract.firmware.generation,
        "firmware_version": contract.firmware.version,
        "m1n1_commit": contract.source.m1n1_commit,
        "adt_sha256": contract.source.adt_identity,
    }
    if any(identity[field] != value for field, value in expected_identity.items()):
        raise CaptureError("capture identity does not match AGX contract")
    if not (mesa_source / ".git").exists():
        raise CaptureError(f"Mesa source is not a Git checkout: {mesa_source}")
    if _git(mesa_source, "status", "--porcelain"):
        raise CaptureError("Mesa source is dirty")
    mesa_commit = _git(mesa_source, "rev-parse", "HEAD")
    if mesa_commit != identity["mesa_commit"]:
        raise CaptureError("Mesa source commit does not match capture identity")
    try:
        shim_launcher.relative_to(mesa_source)
    except ValueError as exc:
        raise CaptureError("shim launcher must belong to the pinned Mesa source") from exc
    if not shim_launcher.is_file() or not os.access(shim_launcher, os.X_OK):
        raise CaptureError("shim launcher must be an executable file")
    relative_launcher = shim_launcher.relative_to(mesa_source)
    try:
        _git(mesa_source, "ls-files", "--error-unmatch", str(relative_launcher))
    except CaptureError as exc:
        raise CaptureError("shim launcher is not tracked by pinned Mesa") from exc
    if not capture_program.is_file() or not os.access(capture_program, os.X_OK):
        raise CaptureError("capture program must be an executable file")
    program_sha256 = _sha256(_read_bytes(capture_program, "capture program"))
    if (
        not isinstance(expected_program_sha256, str)
        or not _SHA256_RE.fullmatch(expected_program_sha256)
        or program_sha256 != expected_program_sha256
    ):
        raise CaptureError("capture program SHA-256 does not match preregistration")
    return {
        "mesa_source": str(mesa_source),
        "mesa_commit": mesa_commit,
        "shim_launcher": str(shim_launcher),
        "capture_program": str(capture_program),
        "capture_program_sha256": program_sha256,
        "identity": identity,
    }


def write_capture_receipt(
    output: Path,
    *,
    frame_path: Path,
    final_attachment_path: Path,
    identity: dict,
    capture_program: Path,
    proxy_identity: str,
    m1n1_base: int,
) -> dict:
    """Atomically bind one raw capture to its live cold-boot identity."""

    if not proxy_identity:
        raise CaptureError("proxy identity must be nonempty")
    if isinstance(m1n1_base, bool) or not isinstance(m1n1_base, int) or m1n1_base <= 0:
        raise CaptureError("m1n1 base must be a positive integer")
    _capture_members(CaptureInput(
        Path(frame_path), Path(final_attachment_path), identity, "0" * 64,
        proxy_identity, m1n1_base,
    ))
    final = _read_bytes(final_attachment_path, "final attachment")
    if len(final) != 1024 or final != READBACK:
        raise CaptureError("final attachment is not the fixed 16x16 RGBA8 clear")
    program_sha256 = _sha256(_read_bytes(capture_program, "capture program"))
    receipt = {
        "frame_path": str(Path(frame_path).resolve()),
        "final_attachment_path": str(Path(final_attachment_path).resolve()),
        "identity": dict(identity),
        "capture_program_sha256": program_sha256,
        "proxy_identity": proxy_identity,
        "m1n1_base": m1n1_base,
    }
    temporary = Path(output).with_name(Path(output).name + ".tmp")
    temporary.write_bytes(_json_bytes(receipt))
    temporary.replace(output)
    return receipt


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
    preflight = commands.add_parser("preflight")
    preflight.add_argument("--mesa-source", type=Path, required=True)
    preflight.add_argument("--shim-launcher", type=Path, required=True)
    preflight.add_argument("--capture-program", type=Path, required=True)
    preflight.add_argument("--identity", type=Path, required=True)
    preflight.add_argument("--contract", type=Path, required=True)
    preflight.add_argument("--capture-program-sha256", required=True)
    receipt = commands.add_parser("live-receipt")
    receipt.add_argument("--frame", type=Path, required=True)
    receipt.add_argument("--final-attachment", type=Path, required=True)
    receipt.add_argument("--capture-program", type=Path, required=True)
    receipt.add_argument("--identity", type=Path, required=True)
    receipt.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "preflight":
            print(json.dumps(preflight_capture_source(
                args.mesa_source, args.shim_launcher, args.capture_program, args.identity,
                args.contract, args.capture_program_sha256,
            ), indent=2, sort_keys=True))
            return 0
        if args.command == "live-receipt":
            if not os.environ.get("M1N1DEVICE"):
                raise CaptureError("M1N1DEVICE is required")
            root = Path(__file__).resolve().parents[1]
            proxyclient = root / "m1n1_windows" / "proxyclient"
            if str(proxyclient) not in sys.path:
                sys.path.insert(0, str(proxyclient))
            from m1n1.setup import u

            identity = _read_identity(args.identity)
            if u.adt.target_type.upper() != identity["board"]:
                raise CaptureError("live platform does not match capture identity")
            if u.version != identity["firmware_version"]:
                raise CaptureError("live firmware does not match capture identity")
            data = write_capture_receipt(
                args.output,
                frame_path=args.frame,
                final_attachment_path=args.final_attachment,
                identity=identity,
                capture_program=args.capture_program,
                proxy_identity=f"{u.adt.target_type}:{u.version}:{int(u.base):x}",
                m1n1_base=int(u.base),
            )
            print(json.dumps(data, indent=2, sort_keys=True))
            return 0
        frame, manifest = package_capture(
            _receipt(args.first_receipt), _receipt(args.second_receipt),
            capture_program=args.capture_program, destination=args.destination,
        )
        print(json.dumps({"fixture": str(frame), "manifest": str(manifest)}, sort_keys=True))
        return 0
    except CaptureError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
