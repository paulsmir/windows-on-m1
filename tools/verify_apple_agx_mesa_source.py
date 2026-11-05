#!/usr/bin/env python3
"""Verify the exact external Mesa source used by the Windows AGX build."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def fail(message: str) -> "NoReturn":
    raise ValueError(message)


def git(source: Path, *arguments: str) -> str:
    run = subprocess.run(
        ["git", "-C", str(source), *arguments],
        text=True,
        capture_output=True,
    )
    if run.returncode != 0:
        fail(f"git {' '.join(arguments)} failed: {run.stderr.strip()}")
    return run.stdout.strip()


def load_lock(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"lock cannot be read: {error}")
    if not isinstance(value, dict):
        fail("lock must contain a JSON object")
    required = {
        "repository": str,
        "commit": str,
        "license_path": str,
        "license_sha256": str,
        "required_paths": list,
    }
    for name, expected_type in required.items():
        if not isinstance(value.get(name), expected_type):
            fail(f"lock field {name} has the wrong type")
    if len(value["commit"]) != 40 or any(
        character not in "0123456789abcdef" for character in value["commit"]
    ):
        fail("lock commit must be a lowercase 40-character object ID")
    if len(value["license_sha256"]) != 64 or any(
        character not in "0123456789abcdef"
        for character in value["license_sha256"]
    ):
        fail("lock license_sha256 must be a lowercase SHA-256")
    paths = value["required_paths"]
    if not paths or any(not isinstance(item, str) or not item for item in paths):
        fail("lock required_paths must contain nonempty strings")
    if paths != sorted(set(paths)):
        fail("lock required_paths must be sorted and unique")
    return value


def verify(lock_path: Path, source: Path) -> dict:
    lock = load_lock(lock_path)
    if not source.is_dir():
        fail(f"source directory is missing: {source}")
    commit = git(source, "rev-parse", "HEAD")
    if commit != lock["commit"]:
        fail(f"Mesa commit mismatch: expected {lock['commit']}, got {commit}")
    remote = git(source, "remote", "get-url", "origin")
    if remote.rstrip("/") != lock["repository"].rstrip("/"):
        fail(f"Mesa repository mismatch: expected {lock['repository']}, got {remote}")
    dirty = git(source, "status", "--porcelain", "--untracked-files=no")
    if dirty:
        fail("Mesa checkout has modified tracked files")
    for relative in lock["required_paths"]:
        if not (source / relative).exists():
            fail(f"required Mesa path is missing: {relative}")
    license_path = source / lock["license_path"]
    license_sha256 = hashlib.sha256(license_path.read_bytes()).hexdigest()
    if license_sha256 != lock["license_sha256"]:
        fail(
            "Mesa license hash mismatch: "
            f"expected {lock['license_sha256']}, got {license_sha256}"
        )
    return {
        "commit": commit,
        "license_sha256": license_sha256,
        "required_paths": lock["required_paths"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        result = verify(arguments.lock, arguments.source)
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
