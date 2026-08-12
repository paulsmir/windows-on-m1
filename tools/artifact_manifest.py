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


def create_manifest(
    root: Path,
    artifact_dir: Path,
    profile: str,
    display: str,
    debug: str,
    artifact_names: list[str],
    *,
    compiler: str,
) -> Path:
    root = root.resolve()
    artifact_dir = artifact_dir.resolve()
    if profile not in {"release", "debug"}:
        raise ManifestError(f"unsupported profile: {profile}")
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
    revisions = {"root_commit": _clean_revision(root)}
    for name in ("m1n1_windows", "mu"):
        path = root / name
        if (path / ".git").exists() or (path / ".git").is_file():
            revisions[f"{name}_commit"] = _clean_revision(path)
    artifacts = {}
    for name in artifact_names:
        path = artifact_dir / name
        if not path.is_file():
            raise ManifestError(f"artifact is missing: {path}")
        artifacts[name] = {"size": path.stat().st_size, "sha256": _sha256(path)}
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


def verify_manifest(path: Path, expected_profile: str | None = None) -> dict:
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
    create.add_argument("artifacts", nargs="+")
    verify = subparsers.add_parser("verify")
    verify.add_argument("manifest", type=Path)
    verify.add_argument("--profile", choices=("release", "debug"))
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
                )
            )
        else:
            data = verify_manifest(args.manifest, args.profile)
            print(f"validated {data['platform']} {data['profile']} artifacts")
    except ManifestError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
