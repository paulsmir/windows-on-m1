#!/usr/bin/env python3
"""Verify the pinned Mesa reuse/replacement boundary for Apple AGX AD03."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


CLASS_ACTION = {
    "FRONTEND": "reuse",
    "COMPILER": "reuse",
    "ENCODER": "reuse",
    "SOFTWARE_ONLY": "reject-as-hardware",
    "DRM_ONLY": "replace-with-wddm",
}


def fail(message):
    raise ValueError(message)


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], text=True).strip()


def verify(contract, mesa):
    if contract.get("schema_version") != 1:
        fail("schema_version")
    if contract.get("pipeline_advertised") is not False:
        fail("pipeline_advertised must remain false in AD03")
    commit = git(mesa, "rev-parse", "HEAD")
    if commit != contract.get("mesa_commit"):
        fail("mesa commit")
    if git(mesa, "status", "--porcelain"):
        fail("mesa checkout is dirty")
    license_path = mesa / contract.get("license_path", "")
    if not license_path.is_file():
        fail("license path")
    license_hash = hashlib.sha256(license_path.read_bytes()).hexdigest()
    if license_hash != contract.get("license_sha256"):
        fail("license hash")
    sources = contract.get("sources")
    if not isinstance(sources, list) or not sources:
        fail("sources")
    seen = set()
    classes = set()
    reused = replaced = 0
    for entry in sources:
        path = entry.get("path")
        classification = entry.get("classification")
        action = entry.get("action")
        if not path or path in seen:
            fail("duplicate or empty source path")
        seen.add(path)
        if classification not in CLASS_ACTION:
            fail("source classification")
        classes.add(classification)
        if action != CLASS_ACTION[classification]:
            fail(f"{classification} action")
        source = mesa / path
        if not source.is_file():
            fail(f"source missing: {path}")
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        if digest != entry.get("sha256"):
            fail(f"source hash: {path}")
        text = source.read_text(errors="replace")
        signatures = entry.get("signatures")
        if not isinstance(signatures, list) or not signatures:
            fail(f"source signatures: {path}")
        for signature in signatures:
            if signature not in text:
                fail(f"source signature: {path}: {signature}")
        if action == "reuse":
            reused += 1
        else:
            replaced += 1
    if classes != set(CLASS_ACTION):
        fail("classification coverage")
    generated = contract.get("generated_sources")
    if not isinstance(generated, list) or len(generated) < 4:
        fail("generated_sources")
    for item in generated:
        if not item.get("output") or not (mesa / item.get("generator", "")).is_file():
            fail("generated source contract")
    forbidden = contract.get("forbidden_hardware_links")
    if not isinstance(forbidden, list) or len(set(forbidden)) != len(forbidden):
        fail("forbidden_hardware_links")
    return {
        "schema_version": 1,
        "mesa_commit": commit,
        "source_count": len(sources),
        "reused": reused,
        "replaced": replaced,
        "pipeline_advertised": False,
        "classifications": sorted(classes),
        "generated_sources": len(generated),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", type=Path, required=True)
    parser.add_argument("--mesa-root", type=Path, required=True)
    args = parser.parse_args()
    try:
        contract = json.loads(args.contract.read_text())
        print(json.dumps(verify(contract, args.mesa_root), indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
