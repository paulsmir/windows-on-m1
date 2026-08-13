#!/usr/bin/env python3
"""Create and verify provenance manifests for J313 build artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path

J313_GUEST_CONTRACT = {
    "layout_version": 1,
    "phys_base": "0x850000000",
    "ram_end": "0xa00000000",
    "virtual_fb_base": "0x85f000000",
    "virtual_fb_width": 2560,
    "virtual_fb_height": 1600,
    "virtual_fb_stride": 10240,
    "cpu_count": 8,
}

ARTIFACT_ROLES = {
    "boot.bin": "standalone-bootstrap",
    "m1n1-stage0.bin": "autonomous-stage0",
    "m1n1-stage1.bin": "autonomous-stage1",
    "m1n1.macho": "assisted-chainload",
    "J313_EFI.fd": "guest-firmware",
}


class ManifestError(RuntimeError):
    pass


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args], check=False, capture_output=True, text=True
    )
    if result.returncode:
        raise ManifestError(result.stderr.strip() or f"git failed in {root}")
    return result.stdout.strip()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _clean_revision(root: Path) -> str:
    if _git(root, "status", "--porcelain", "--untracked-files=no"):
        raise ManifestError(f"tracked source is dirty: {root}")
    return _git(root, "rev-parse", "HEAD")


def _source_revision(root: Path, *, allow_dirty: bool) -> dict:
    commit = _git(root, "rev-parse", "HEAD")
    dirty = bool(_git(root, "status", "--porcelain", "--untracked-files=no"))
    if dirty and not allow_dirty:
        raise ManifestError(f"tracked source is dirty: {root}")
    result = {"commit": commit, "dirty": dirty}
    if dirty:
        diff = subprocess.run(
            ["git", "-C", str(root), "diff", "--binary", "HEAD"],
            check=True, capture_output=True,
        ).stdout
        result["diff_sha256"] = hashlib.sha256(diff).hexdigest()
    return result


def create_manifest(
    root: Path,
    artifact_dir: Path,
    profile: str,
    display: str,
    debug: str,
    artifact_names: list[str],
    *,
    compiler: str,
    allow_dirty: bool = False,
    artifact_roles: dict[str, str] | None = None,
) -> Path:
    root = root.resolve()
    artifact_dir = artifact_dir.resolve()
    if profile not in {"release", "debug"}:
        raise ManifestError(f"unsupported profile: {profile}")
    if allow_dirty and profile != "debug":
        raise ManifestError("dirty source is allowed only for debug artifacts")
    if not compiler.strip():
        raise ManifestError("compiler identity is empty")
    layout_path = root / "config" / "j313-guest-layout.json"
    try:
        layout = json.loads(layout_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read guest layout: {layout_path}") from error
    guest_contract = {key: layout.get(key) for key in J313_GUEST_CONTRACT}
    if guest_contract != J313_GUEST_CONTRACT:
        raise ManifestError("guest layout does not match the J313 release contract")
    root_revision = _source_revision(root, allow_dirty=allow_dirty)
    revisions = {
        "root_commit": root_revision["commit"],
        "source_dirty": root_revision["dirty"],
    }
    if root_revision["dirty"]:
        revisions["root_diff_sha256"] = root_revision["diff_sha256"]
    for name in ("m1n1_windows", "mu"):
        path = root / name
        if (path / ".git").exists() or (path / ".git").is_file():
            revision = _source_revision(path, allow_dirty=allow_dirty)
            revisions[f"{name}_commit"] = revision["commit"]
            revisions[f"{name}_dirty"] = revision["dirty"]
            if revision["dirty"]:
                revisions[f"{name}_diff_sha256"] = revision["diff_sha256"]
    roles = ARTIFACT_ROLES if artifact_roles is None else artifact_roles
    artifacts = {}
    for name in artifact_names:
        path = artifact_dir / name
        if not path.is_file():
            raise ManifestError(f"artifact is missing: {path}")
        record = {"size": path.stat().st_size, "sha256": _sha256(path)}
        if name in roles:
            record["role"] = roles[name]
        artifacts[name] = record
    data = {
        "format_version": 2,
        "platform": "j313",
        "profile": profile,
        "display": display,
        "debug": debug,
        "compiler": compiler,
        "guest_layout_sha256": _sha256(layout_path),
        "guest_contract": guest_contract,
        **revisions,
        "artifacts": artifacts,
    }
    path = artifact_dir / "MANIFEST.json"
    temporary = path.with_suffix(".json.new")
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)
    return path


def verify_manifest(
    path: Path,
    expected_profile: str | None = None,
    expected_roles: dict[str, str] | None = None,
    expected_display: str | None = None,
    expected_debug: str | None = None,
) -> dict:
    path = path.resolve()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read manifest: {path}") from error
    if data.get("format_version") != 2 or data.get("platform") != "j313":
        raise ManifestError("unsupported artifact manifest")
    if expected_profile is not None and data.get("profile") != expected_profile:
        raise ManifestError(
            f"profile mismatch: expected {expected_profile}, got {data.get('profile')}"
        )
    if expected_display is not None and data.get("display") != expected_display:
        raise ManifestError(
            f"display mode mismatch: expected {expected_display}, got {data.get('display')}"
        )
    if expected_debug is not None and data.get("debug") != expected_debug:
        raise ManifestError(
            f"debug mode mismatch: expected {expected_debug}, got {data.get('debug')}"
        )
    if not data.get("compiler") or len(data.get("guest_layout_sha256", "")) != 64:
        raise ManifestError("incomplete artifact provenance")
    if data.get("guest_contract") != J313_GUEST_CONTRACT:
        raise ManifestError("guest contract mismatch")
    for name, record in data.get("artifacts", {}).items():
        artifact = path.parent / name
        if not artifact.is_file():
            raise ManifestError(f"artifact is missing: {artifact}")
        if artifact.stat().st_size != record.get("size") or _sha256(artifact) != record.get("sha256"):
            raise ManifestError(f"artifact hash mismatch: {artifact}")
    for name, role in (expected_roles or {}).items():
        if data.get("artifacts", {}).get(name, {}).get("role") != role:
            raise ManifestError(f"artifact role mismatch: {name} is not {role}")
    if not data.get("artifacts"):
        raise ManifestError("manifest contains no artifacts")
    return data


def main() -> int:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    create = subparsers.add_parser("create")
    create.add_argument("--root", type=Path, required=True)
    create.add_argument("--directory", type=Path, required=True)
    create.add_argument("--profile", choices=("release", "debug"), required=True)
    create.add_argument("--display", required=True)
    create.add_argument("--debug", required=True)
    create.add_argument("--compiler", required=True)
    create.add_argument("--allow-dirty", action="store_true")
    create.add_argument("artifacts", nargs="+")
    verify = subparsers.add_parser("verify")
    verify.add_argument("manifest", type=Path)
    verify.add_argument("--profile", choices=("release", "debug"))
    verify.add_argument("--display")
    verify.add_argument("--debug")
    verify.add_argument("--require-role", action="append", default=[], metavar="NAME=ROLE")
    args = parser.parse_args()
    try:
        if args.command == "create":
            print(
                create_manifest(
                    args.root,
                    args.directory,
                    args.profile,
                    args.display,
                    args.debug,
                    args.artifacts,
                    compiler=args.compiler,
                    allow_dirty=args.allow_dirty,
                )
            )
        else:
            roles = {}
            for value in args.require_role:
                if "=" not in value:
                    raise ManifestError(f"invalid artifact role requirement: {value}")
                name, role = value.split("=", 1)
                if not name or not role:
                    raise ManifestError(f"invalid artifact role requirement: {value}")
                roles[name] = role
            data = verify_manifest(
                args.manifest, args.profile, roles,
                expected_display=args.display, expected_debug=args.debug,
            )
            print(f"validated {data['platform']} {data['profile']} artifacts")
    except ManifestError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
