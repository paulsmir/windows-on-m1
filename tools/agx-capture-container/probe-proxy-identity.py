#!/usr/bin/env python3
"""Capture and compare m1n1 proxy identities without enabling AGX."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "m1n1_windows" / "proxyclient"))


def atomic_json(path: Path, value: dict) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def text(value) -> str:
    if isinstance(value, bytes):
        return value.split(b"\0", 1)[0].decode("ascii")
    return str(value)


def capture(path: Path) -> dict:
    from m1n1.proxy import M1N1Proxy, UartInterface
    from m1n1.proxyutils import ProxyUtils, bootstrap_port

    interface = UartInterface()
    try:
        proxy = M1N1Proxy(interface, debug=False)
        bootstrap_port(interface, proxy)
        utilities = ProxyUtils(proxy)
        value = {
            "format_version": 1,
            "platform": text(utilities.adt.target_type),
            "firmware": text(utilities.version),
            "m1n1_base": int(utilities.base),
        }
        atomic_json(path, value)
        return value
    finally:
        interface.dev.close()


def read_identity(path: Path) -> dict:
    value = json.loads(Path(path).read_text())
    required = {"format_version", "platform", "firmware", "m1n1_base"}
    if set(value) != required or value["format_version"] != 1:
        raise ValueError(f"invalid proxy identity: {path}")
    if not isinstance(value["m1n1_base"], int) or value["m1n1_base"] <= 0:
        raise ValueError(f"invalid proxy base: {path}")
    return value


def receipt(before_path: Path, after_path: Path, output: Path) -> dict:
    before = read_identity(before_path)
    after = read_identity(after_path)
    if before["platform"] != after["platform"]:
        raise ValueError("proxy platform changed across reboot")
    if before["firmware"] != after["firmware"]:
        raise ValueError("proxy firmware changed across reboot")
    if before["m1n1_base"] == after["m1n1_base"]:
        raise ValueError("proxy base did not change across reboot")
    value = {
        "format_version": 1,
        "before": before,
        "after": after,
        "fresh_proxy": True,
    }
    atomic_json(output, value)
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    capture_command = commands.add_parser("capture")
    capture_command.add_argument("--output", type=Path, required=True)
    receipt_command = commands.add_parser("receipt")
    receipt_command.add_argument("--before", type=Path, required=True)
    receipt_command.add_argument("--after", type=Path, required=True)
    receipt_command.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "capture":
        print(json.dumps(capture(args.output), sort_keys=True))
    else:
        print(json.dumps(receipt(args.before, args.after, args.output), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
