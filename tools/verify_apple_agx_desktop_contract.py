#!/usr/bin/env python3
"""Validate the planning contract for the accelerated Windows desktop path."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


EXPECTED_PIPELINE_MASKS = {
    "D3D_FEATURE_LEVEL_10_0": 1,
}
FRONTENDS = {"mesa-d3d-reuse", "thin-d3d-over-mesa"}


def _nonempty_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError(name)
    return value


def validate_contract(value: dict[str, Any], locked_mesa_commit: str) -> bool:
    if value.get("schema_version") != 1:
        raise ValueError("schema")
    if value.get("goal") != "accelerated-windows-desktop":
        raise ValueError("goal")
    if value.get("kmd_model") != "WDDM3.0":
        raise ValueError("kmd_model")
    if value.get("mesa_commit") != locked_mesa_commit:
        raise ValueError("mesa_commit")
    if value.get("frontend") not in FRONTENDS:
        raise ValueError("frontend")
    _nonempty_string(value.get("umd_ddi"), "umd_ddi")
    feature_level = _nonempty_string(value.get("feature_level"), "feature_level")

    reviewed = value.get("mandatory_inventory_reviewed")
    if type(reviewed) is not bool:
        raise ValueError("mandatory_inventory_reviewed")

    rows = value.get("requirements")
    if not isinstance(rows, list) or not rows:
        raise ValueError("requirements")
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("requirement")
        for key in ("id", "class", "source", "implementation_file", "test_id"):
            _nonempty_string(row.get(key), key)
        identifier = row["id"]
        if identifier in seen:
            raise ValueError("duplicate requirement")
        seen.add(identifier)
        for key in ("mandatory", "implemented", "test_passed"):
            if type(row.get(key)) is not bool:
                raise ValueError(key)

    required = [row for row in rows if row["mandatory"]]
    if not required:
        raise ValueError("mandatory inventory")
    complete = reviewed and all(
        row["implemented"] and row["test_passed"] for row in required
    )

    mask = value.get("advertised_pipeline_mask")
    selected_mask = value.get("selected_pipeline_mask")
    if type(mask) is not int or mask < 0:
        raise ValueError("mask")
    if type(selected_mask) is not int or selected_mask < 1:
        raise ValueError("selected_pipeline_mask")
    expected_mask = EXPECTED_PIPELINE_MASKS.get(feature_level)
    if expected_mask is None or selected_mask != expected_mask:
        raise ValueError("selected pipeline mask")
    if mask != 0 and not complete:
        raise ValueError("pipeline advertised before mandatory implementation")
    if mask != 0 and mask != selected_mask:
        raise ValueError("selected pipeline mask")
    return complete


def _load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("top-level object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", required=True, type=Path)
    parser.add_argument("--mesa-lock", required=True, type=Path)
    args = parser.parse_args()
    try:
        contract = _load_json(args.contract)
        lock = _load_json(args.mesa_lock)
        locked_commit = _nonempty_string(lock.get("commit"), "lock commit")
        complete = validate_contract(contract, locked_commit)
        mandatory = sum(1 for row in contract["requirements"] if row["mandatory"])
        print(
            json.dumps(
                {
                    "advertised_pipeline_mask": contract["advertised_pipeline_mask"],
                    "implementation_complete": complete,
                    "mandatory_requirements": mandatory,
                    "selected_pipeline_mask": contract["selected_pipeline_mask"],
                },
                separators=(",", ":"),
                sort_keys=True,
            )
        )
        return 0
    except (OSError, json.JSONDecodeError, ValueError, TypeError) as error:
        print(f"desktop contract error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
