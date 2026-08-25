#!/usr/bin/env python3
"""Capture J313 AGX ADT metadata without hardware writes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "m1n1_windows" / "proxyclient"))

from tools.agx_inventory import required_paths  # noqa: E402


def _process_lines():
    result = subprocess.run(
        ["ps", "-axo", "pid=,command="],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.splitlines()


def ensure_guest_inactive(root, process_lines=None):
    """Refuse to open m1n1.setup while a guest runner owns the proxy."""

    root = Path(root).resolve()
    pid_path = root / "guest.pid"
    try:
        pid = int(pid_path.read_text().strip())
        os.kill(pid, 0)
    except (FileNotFoundError, ProcessLookupError, ValueError):
        pass
    except PermissionError as exc:
        raise RuntimeError(f"cannot verify guest runner {pid}") from exc
    else:
        raise RuntimeError(f"guest runner {pid} is active; live inventory is unsafe")

    if process_lines is None:
        process_lines = _process_lines()
    markers = ("run_uefi.py", "scripts/run-assisted.sh", "scripts/supervise-assisted.sh")
    root_text = str(root)
    for line in process_lines:
        fields = line.strip().split(maxsplit=1)
        if len(fields) != 2 or not fields[0].isdigit():
            continue
        process_id, command = fields
        if root_text in command and any(marker in command for marker in markers):
            raise RuntimeError(
                f"active guest process {process_id} owns the proxy; "
                "live inventory is unsafe"
            )


def json_value(value):
    """Convert decoded ADT values into deterministic JSON-safe values."""

    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, bytes):
        return {"bytes_hex": value.hex()}
    if all(hasattr(value, field) for field in ("phandle", "name", "args")):
        return {
            "phandle": int(value.phandle),
            "name": str(value.name),
            "args": [int(item) for item in value.args],
        }
    if isinstance(value, (list, tuple)):
        return [json_value(item) for item in value]
    try:
        return [json_value(item) for item in value]
    except TypeError as exc:
        raise TypeError(f"unsupported ADT value type {type(value).__name__}") from exc


def node_record(node):
    """Return only values already decoded from the in-memory ADT."""

    try:
        registers = [
            [int(base), int(size)]
            for base, size in (
                node.get_reg(index) for index in range(len(node.reg))
            )
        ]
    except (AttributeError, KeyError, TypeError, ValueError, IndexError) as exc:
        raise RuntimeError(f"cannot decode reg for {node._path}") from exc
    return {
        "reg": registers,
        "interrupts": [int(value) for value in getattr(node, "interrupts", [])],
        "properties": {
            key: json_value(value)
            for key, value in sorted(node._properties.items())
            if key != "name"
        },
    }


def _text(value):
    if isinstance(value, bytes):
        return value.split(b"\0", 1)[0].decode("ascii")
    return str(value)


def platform_name(adt):
    """Return m1n1's decoded target identifier, for example J313."""

    return _text(adt.target_type)


def capture_raw():
    """Read the live ADT after the caller has passed the ownership guard."""

    from m1n1.setup import u  # noqa: E402
    from m1n1.hw.uat import UAT  # noqa: E402

    chosen = u.adt["/chosen"]
    arm_io = u.adt["/arm-io"]
    platform = platform_name(u.adt)
    firmware_version = _text(chosen.firmware_version)
    identity_fields = {
        "target_type": platform,
        "chip_id": int(chosen.chip_id),
        "firmware_version": firmware_version,
    }
    identity = hashlib.sha256(
        json.dumps(identity_fields, sort_keys=True).encode()
    ).hexdigest()
    nodes = {path: node_record(u.adt[path]) for path in required_paths()}
    return {
        "format_version": 1,
        "platform": platform,
        "adt_identity": identity,
        "firmware": {
            "generation": _text(arm_io.getprop("soc-generation")).replace("H", "G"),
            "version": _text(u.version),
        },
        "uat": {
            "page_size": int(UAT.PAGE_SIZE),
            "num_contexts": int(UAT.NUM_CONTEXTS),
            "address_bits": int(UAT.L0_OFF + (UAT.L0_SIZE.bit_length() - 1)),
        },
        "nodes": nodes,
        "dependencies": list(required_paths()),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    ensure_guest_inactive(ROOT)
    data = capture_raw()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
