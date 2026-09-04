#!/usr/bin/env python3
"""Verify the immutable principal artifacts of the AGX capture environment."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys


EXPECTED_MESA_COMMIT = "7a4f24061fa56ef7eff12132dd7b1461d5a890d8"
EXPECTED_ARTIFACTS = {
    "agx-clear-capture",
    "asahi_dri.so",
    "libEGL.so.1.0.0",
    "libGLESv2.so.2.0.0",
    "libasahi_m1n1_drm_shim.so",
}


def fail(message: str) -> "NoReturn":
    raise SystemExit(message)


def main() -> int:
    if len(sys.argv) != 2:
        fail(f"usage: {Path(sys.argv[0]).name} EXPORT_DIRECTORY")
    root = Path(sys.argv[1]).resolve()
    manifest_path = root / "manifest.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"invalid capture environment manifest: {error}")
    if manifest.get("schema") != 1:
        fail("unsupported capture environment manifest schema")
    if manifest.get("mesa_commit") != EXPECTED_MESA_COMMIT:
        fail("capture environment Mesa commit mismatch")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, dict) or set(artifacts) != EXPECTED_ARTIFACTS:
        fail("capture environment artifact set mismatch")
    for name in sorted(EXPECTED_ARTIFACTS):
        path = root / name
        try:
            payload = path.read_bytes()
        except OSError as error:
            fail(f"cannot read {name}: {error}")
        if not payload.startswith(b"\x7fELF"):
            fail(f"{name} is not an ELF object")
        actual = hashlib.sha256(payload).hexdigest()
        if artifacts[name] != actual:
            fail(f"capture environment hash mismatch for {name}")
    print(f"AGX capture environment verified: {EXPECTED_MESA_COMMIT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
