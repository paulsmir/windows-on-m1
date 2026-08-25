"""Canonical, fail-closed AGX frame fixture handling."""

import argparse
from dataclasses import dataclass
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import sys
from types import MappingProxyType
import zipfile


FIXTURE_VERSION = 1
MAX_MEMBER_SIZE = 16 * 1024 * 1024
MAX_TOTAL_SIZE = 64 * 1024 * 1024
CANONICAL_ZIP_TIME = (1980, 1, 1, 0, 0, 0)

_SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
_COMMIT_RE = re.compile(r"[0-9a-f]{40}\Z")

PAGE_SIZE = 0x4000
PIPELINE_RANGE = (0x1100000000, 0x1200000000)
DATA_RANGE = (0x1500000000, 0x1700000000)


class FixtureError(RuntimeError):
    """An AGX frame fixture violated its immutable safety contract."""


@dataclass(frozen=True)
class FrameObject:
    name: str
    gpu_va: int
    size: int
    map_flags: tuple[tuple[str, int], ...]
    sha256: str
    data: bytes


@dataclass(frozen=True)
class ValidatedFrame:
    fixture_sha256: str
    command_buffer: object
    objects: tuple[FrameObject, ...]
    output_gpu_va: int
    output_size: int
    poison_sha256: str
    expected_output_sha256: str


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _integer(value, boundary: str, *, minimum: int = 0) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise FixtureError(f"{boundary} must be an integer >= {minimum}")
    return value


def _hash(value, boundary: str) -> str:
    if not isinstance(value, str) or not _SHA256_RE.fullmatch(value):
        raise FixtureError(f"{boundary} must be a lowercase SHA-256")
    return value


def _safe_member_name(name: str) -> None:
    if not name or "\\" in name:
        raise FixtureError(f"ZIP path traversal is forbidden: {name!r}")
    path = PurePosixPath(name)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        raise FixtureError(f"ZIP path traversal is forbidden: {name!r}")


def _read_members(frame_path: Path) -> dict[str, bytes]:
    members = {}
    total = 0
    try:
        archive = zipfile.ZipFile(frame_path, "r")
    except (OSError, zipfile.BadZipFile) as exc:
        raise FixtureError(f"invalid frame ZIP: {exc}") from exc
    with archive:
        for info in archive.infolist():
            _safe_member_name(info.filename)
            if info.filename in members:
                raise FixtureError(f"duplicate member: {info.filename}")
            if info.is_dir():
                raise FixtureError(f"unlisted member directory: {info.filename}")
            size = _integer(info.file_size, "ZIP member size")
            total += size
            if size > MAX_MEMBER_SIZE or total > MAX_TOTAL_SIZE:
                raise FixtureError("ZIP compression bomb exceeds fixture limits")
            data = archive.read(info)
            if len(data) != size:
                raise FixtureError(f"member size changed while reading: {info.filename}")
            members[info.filename] = data
    return members


def _canonical_zip_bytes(members: dict[str, bytes]) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(members):
            info = zipfile.ZipInfo(name, CANONICAL_ZIP_TIME)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, members[name])
    return output.getvalue()


def canonicalize_zip(source: Path, destination: Path) -> str:
    """Write a deterministic frame ZIP and return its SHA-256."""

    members = _read_members(Path(source))
    encoded = _canonical_zip_bytes(members)
    destination = Path(destination)
    temporary = destination.with_name(destination.name + ".tmp")
    temporary.write_bytes(encoded)
    temporary.replace(destination)
    return _sha256(encoded)


def _atomic_json(path: Path, value) -> None:
    path = Path(path)
    temporary = path.with_name(path.name + ".tmp")
    encoded = json.dumps(value, indent=2, sort_keys=True) + "\n"
    temporary.write_text(encoded)
    temporary.replace(path)


def _load_json(data: bytes, boundary: str):
    try:
        return json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FixtureError(f"malformed JSON in {boundary}") from exc


def _load_manifest(path: Path) -> dict:
    try:
        manifest = json.loads(Path(path).read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FixtureError(f"malformed JSON in manifest: {exc}") from exc
    if not isinstance(manifest, dict):
        raise FixtureError("manifest must be a JSON object")
    return manifest


def _read_json_file(path: Path, boundary: str):
    try:
        return json.loads(Path(path).read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise FixtureError(f"malformed JSON in {boundary}: {exc}") from exc


def _freeze(value):
    if isinstance(value, dict):
        return MappingProxyType({key: _freeze(item) for key, item in value.items()})
    if isinstance(value, list):
        return tuple(_freeze(item) for item in value)
    return value


def _validate_identity(manifest: dict, expected: dict) -> None:
    identity = manifest.get("identity")
    if not isinstance(identity, dict):
        raise FixtureError("identity must be an object")
    if set(identity) != set(expected):
        raise FixtureError("identity fields do not match the expected contract")
    for field, expected_value in expected.items():
        value = identity.get(field)
        if field in ("m1n1_commit", "mesa_commit"):
            if not isinstance(value, str) or not _COMMIT_RE.fullmatch(value):
                raise FixtureError(f"identity {field} must be a 40-character commit")
        if field == "adt_sha256":
            _hash(value, "identity adt_sha256")
        if value != expected_value:
            raise FixtureError(
                f"identity {field} must equal {expected_value!r}, got {value!r}"
            )


def _private_range(gpu_va: int, size: int) -> tuple[int, int]:
    if gpu_va % PAGE_SIZE:
        raise FixtureError("object gpu_va must be 0x4000 aligned")
    if size % PAGE_SIZE:
        raise FixtureError("object size must be 0x4000 aligned")
    end = gpu_va + size
    if end <= gpu_va or end > (1 << 64):
        raise FixtureError("object range overflows the GPU address space")
    if not any(gpu_va >= start and end <= limit for start, limit in (
        PIPELINE_RANGE,
        DATA_RANGE,
    )):
        raise FixtureError(
            f"object range is outside private GPU VA allowlists: {gpu_va:#x}..{end:#x}"
        )
    return gpu_va, end


def _object_records(
    members: dict[str, bytes],
    manifest: dict,
    object_json,
) -> tuple[FrameObject, ...]:
    if not isinstance(object_json, list) or not object_json:
        raise FixtureError("objects.json must be a nonempty array")
    records = manifest.get("objects")
    if not isinstance(records, list) or len(records) != len(object_json):
        raise FixtureError("manifest objects must match objects.json length")

    result = []
    intervals = []
    for index, source in enumerate(object_json):
        if not isinstance(source, dict):
            raise FixtureError(f"objects.json entry {index} must be an object")
        member = source.get("file")
        if not isinstance(member, str) or member not in members:
            raise FixtureError(f"missing member for object: {member!r}")
        name = source.get("name")
        if not isinstance(name, str) or not name:
            raise FixtureError(f"object name {index} must be a nonempty string")
        gpu_va = _integer(source.get("addr"), "object gpu_va")
        size = _integer(source.get("size"), "object size", minimum=1)
        interval = _private_range(gpu_va, size)
        intervals.append((interval[0], interval[1], index))
        if len(members[member]) != size:
            raise FixtureError(f"object size does not match member bytes: {member}")
        map_flags = source.get("map_flags")
        if not isinstance(map_flags, dict) or not map_flags:
            raise FixtureError("object map_flags must be a nonempty object")
        for flag, value in map_flags.items():
            if not isinstance(flag, str):
                raise FixtureError("map_flags keys must be strings")
            _integer(value, f"map_flags {flag}")

        record = records[index]
        if not isinstance(record, dict):
            raise FixtureError(f"manifest object {index} must be an object")
        expected = {
            "member": member,
            "name": name,
            "gpu_va": gpu_va,
            "size": size,
            "map_flags": map_flags,
            "sha256": _sha256(members[member]),
        }
        for field, expected_value in expected.items():
            if record.get(field) != expected_value:
                raise FixtureError(
                    f"object {index} {field} does not match objects.json"
                )
        result.append(FrameObject(
            name=name,
            gpu_va=gpu_va,
            size=size,
            map_flags=tuple(sorted(map_flags.items())),
            sha256=expected["sha256"],
            data=bytes(members[member]),
        ))

    intervals.sort()
    for previous, current in zip(intervals, intervals[1:]):
        if current[0] < previous[1]:
            raise FixtureError(
                f"overlapping objects: indexes {previous[2]} and {current[2]}"
            )
    return tuple(result)


def _contains(objects: tuple[FrameObject, ...], address: int) -> bool:
    return any(obj.gpu_va <= address < obj.gpu_va + obj.size for obj in objects)


def _validate_command_buffer(
    command_buffer,
    objects: tuple[FrameObject, ...],
    output: dict,
) -> None:
    if not isinstance(command_buffer, dict):
        raise FixtureError("cmdbuf.json must be an object")
    encoder_ptr = _integer(command_buffer.get("encoder_ptr"), "encoder_ptr")
    if not _contains(objects, encoder_ptr):
        raise FixtureError("encoder_ptr does not resolve to a frame object")
    for field in ("ds_flags", "depth_buffer", "stencil_buffer"):
        if _integer(command_buffer.get(field), field) != 0:
            raise FixtureError(f"{field} must be zero for the private clear")
    width = _integer(command_buffer.get("fb_width"), "fb_width")
    height = _integer(command_buffer.get("fb_height"), "fb_height")
    if (width, height) != (16, 16):
        raise FixtureError("frame dimensions must equal 16 by 16")
    count = _integer(command_buffer.get("attachment_count"), "attachment_count")
    if count != 1:
        raise FixtureError("attachment_count must equal one")
    attachments = command_buffer.get("attachments")
    if not isinstance(attachments, list) or len(attachments) < count:
        raise FixtureError("attachments must contain the declared attachment")
    attachment = attachments[0]
    if not isinstance(attachment, dict):
        raise FixtureError("attachment must be an object")
    if _integer(attachment.get("type"), "attachment type") != 0:
        raise FixtureError("attachment type must be color")
    pointer = _integer(attachment.get("pointer"), "attachment pointer")
    if pointer != output["gpu_va"]:
        raise FixtureError("attachment pointer must equal output gpu_va")
    if _integer(attachment.get("size"), "attachment size") != output["size"]:
        raise FixtureError("attachment size must equal output size")


def _validate_member_inventory(members: dict[str, bytes], manifest: dict) -> None:
    inventory = manifest.get("members")
    if not isinstance(inventory, dict):
        raise FixtureError("manifest members must be an object")
    actual = set(members)
    declared = set(inventory)
    missing = sorted(declared - actual)
    if missing:
        raise FixtureError(f"missing member: {missing}")
    unlisted = sorted(actual - declared)
    if unlisted:
        raise FixtureError(f"unlisted member: {unlisted}")
    for name, data in members.items():
        record = inventory[name]
        if not isinstance(record, dict):
            raise FixtureError(f"member record must be an object: {name}")
        size = _integer(record.get("size"), f"member size for {name}")
        if size != len(data):
            raise FixtureError(f"member size mismatch: {name}")
        expected_hash = _hash(record.get("sha256"), f"member hash for {name}")
        if expected_hash != _sha256(data):
            raise FixtureError(f"member hash mismatch: {name}")


def build_manifest(
    frame_path: Path,
    *,
    identity: dict,
    capture_program_sha256: str,
    expected_output: bytes,
) -> dict:
    """Build the complete manifest for a canonical private clear fixture."""

    members = _read_members(Path(frame_path))
    if "cmdbuf.json" not in members or "objects.json" not in members:
        raise FixtureError("missing member: cmdbuf.json or objects.json")
    command_buffer = _load_json(members["cmdbuf.json"], "cmdbuf.json")
    object_json = _load_json(members["objects.json"], "objects.json")
    if not isinstance(command_buffer, dict):
        raise FixtureError("cmdbuf.json must be an object")
    if not isinstance(object_json, list) or not object_json:
        raise FixtureError("objects.json must be a nonempty array")
    if not isinstance(identity, dict):
        raise FixtureError("identity must be an object")
    _hash(capture_program_sha256, "capture_program_sha256")

    count = _integer(command_buffer.get("attachment_count"), "attachment_count")
    attachments = command_buffer.get("attachments")
    if count != 1 or not isinstance(attachments, list) or len(attachments) < 1:
        raise FixtureError("fixture must declare exactly one attachment")
    attachment = attachments[0]
    if not isinstance(attachment, dict):
        raise FixtureError("attachment must be an object")
    output_gpu_va = _integer(attachment.get("pointer"), "attachment pointer")
    output_size = _integer(attachment.get("size"), "attachment size", minimum=1)
    if not isinstance(expected_output, bytes):
        raise FixtureError("expected output must be bytes")
    if len(expected_output) != output_size:
        raise FixtureError(
            f"expected output size must equal {output_size}, got {len(expected_output)}"
        )

    records = []
    output_member = None
    for index, source in enumerate(object_json):
        if not isinstance(source, dict):
            raise FixtureError(f"objects.json entry {index} must be an object")
        member = source.get("file")
        if not isinstance(member, str) or member not in members:
            raise FixtureError(f"missing member for object: {member!r}")
        record = {
            "member": member,
            "name": source.get("name"),
            "gpu_va": source.get("addr"),
            "size": source.get("size"),
            "map_flags": source.get("map_flags"),
            "sha256": _sha256(members[member]),
        }
        records.append(record)
        if record["gpu_va"] == output_gpu_va and record["size"] == output_size:
            if output_member is not None:
                raise FixtureError("output attachment matches multiple objects")
            output_member = member
    if output_member is None:
        raise FixtureError("output attachment does not match a frame object")

    manifest = {
        "fixture_version": FIXTURE_VERSION,
        "identity": dict(identity),
        "capture_program_sha256": capture_program_sha256,
        "fixture_sha256": _sha256(_canonical_zip_bytes(members)),
        "members": {
            name: {"size": len(data), "sha256": _sha256(data)}
            for name, data in sorted(members.items())
        },
        "objects": records,
        "command_buffer_sha256": _sha256(members["cmdbuf.json"]),
        "output": {
            "gpu_va": output_gpu_va,
            "size": output_size,
            "width": _integer(command_buffer.get("fb_width"), "fb_width"),
            "height": _integer(command_buffer.get("fb_height"), "fb_height"),
            "format": "RGBA8",
            "poison_sha256": _sha256(members[output_member]),
            "expected_output_sha256": _sha256(expected_output),
        },
    }
    _validate_identity(manifest, identity)
    return manifest


def validate_fixture(
    frame_path: Path,
    manifest_path: Path,
    expected_identity: dict,
) -> ValidatedFrame:
    """Validate an immutable fixture without extracting or importing m1n1."""

    members = _read_members(Path(frame_path))
    manifest = _load_manifest(Path(manifest_path))
    _validate_member_inventory(members, manifest)

    if _integer(manifest.get("fixture_version"), "fixture_version") != FIXTURE_VERSION:
        raise FixtureError(f"fixture_version must equal {FIXTURE_VERSION}")
    _validate_identity(manifest, expected_identity)
    _hash(manifest.get("capture_program_sha256"), "capture_program_sha256")

    if "cmdbuf.json" not in members or "objects.json" not in members:
        raise FixtureError("missing member: cmdbuf.json or objects.json")
    command_buffer = _load_json(members["cmdbuf.json"], "cmdbuf.json")
    object_json = _load_json(members["objects.json"], "objects.json")
    command_hash = _hash(
        manifest.get("command_buffer_sha256"), "command buffer hash"
    )
    if command_hash != _sha256(members["cmdbuf.json"]):
        raise FixtureError("command buffer hash mismatch")

    fixture_hash = _sha256(_canonical_zip_bytes(members))
    declared_fixture_hash = _hash(
        manifest.get("fixture_sha256"), "fixture_sha256"
    )
    if fixture_hash != declared_fixture_hash:
        raise FixtureError("fixture hash mismatch")

    output = manifest.get("output")
    if not isinstance(output, dict):
        raise FixtureError("manifest output must be an object")
    output_gpu_va = _integer(output.get("gpu_va"), "output gpu_va")
    output_size = _integer(output.get("size"), "output size", minimum=1)
    if output_size != PAGE_SIZE:
        raise FixtureError("output size must equal one 0x4000 page")
    if (_integer(output.get("width"), "output width"),
            _integer(output.get("height"), "output height")) != (16, 16):
        raise FixtureError("output dimensions must equal 16 by 16")
    if output.get("format") != "RGBA8":
        raise FixtureError("output format must equal RGBA8")
    poison_hash = _hash(output.get("poison_sha256"), "poison hash")
    expected_output_hash = _hash(
        output.get("expected_output_sha256"), "expected output hash"
    )
    if expected_output_hash == poison_hash:
        raise FixtureError("expected output hash must differ from poison hash")

    objects = _object_records(members, manifest, object_json)
    matching_output = [
        obj for obj in objects
        if obj.gpu_va == output_gpu_va and obj.size == output_size
    ]
    if len(matching_output) != 1:
        raise FixtureError("output gpu_va and size must match one frame object")
    if matching_output[0].sha256 != poison_hash:
        raise FixtureError("poison hash must match the initial output object")
    _validate_command_buffer(command_buffer, objects, {
        "gpu_va": output_gpu_va,
        "size": output_size,
    })
    return ValidatedFrame(
        fixture_sha256=fixture_hash,
        command_buffer=_freeze(command_buffer),
        objects=objects,
        output_gpu_va=output_gpu_va,
        output_size=output_size,
        poison_sha256=poison_hash,
        expected_output_sha256=expected_output_hash,
    )


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    canonicalize = commands.add_parser("canonicalize")
    canonicalize.add_argument("--source", type=Path, required=True)
    canonicalize.add_argument("--destination", type=Path, required=True)

    manifest = commands.add_parser("manifest")
    manifest.add_argument("--frame", type=Path, required=True)
    manifest.add_argument("--identity", type=Path, required=True)
    manifest.add_argument("--capture-program-sha256", required=True)
    manifest.add_argument("--expected-output", type=Path, required=True)
    manifest.add_argument("--output", type=Path, required=True)

    verify = commands.add_parser("verify")
    verify.add_argument("--frame", type=Path, required=True)
    verify.add_argument("--manifest", type=Path, required=True)
    verify.add_argument("--identity", type=Path, required=True)
    return parser


def main(argv=None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "canonicalize":
            print(canonicalize_zip(args.source, args.destination))
            return 0
        identity = _read_json_file(args.identity, "identity")
        if args.command == "manifest":
            try:
                expected_output = args.expected_output.read_bytes()
            except OSError as exc:
                raise FixtureError(f"cannot read expected output: {exc}") from exc
            manifest = build_manifest(
                args.frame,
                identity=identity,
                capture_program_sha256=args.capture_program_sha256,
                expected_output=expected_output,
            )
            _atomic_json(args.output, manifest)
            print(_sha256(args.output.read_bytes()))
            return 0
        validated = validate_fixture(args.frame, args.manifest, identity)
        print(json.dumps({
            "expected_output_sha256": validated.expected_output_sha256,
            "fixture_sha256": validated.fixture_sha256,
            "object_count": len(validated.objects),
            "output_gpu_va": validated.output_gpu_va,
            "output_size": validated.output_size,
        }, sort_keys=True))
        return 0
    except FixtureError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
