#!/usr/bin/env python3
"""Decode one bounded Wom1DynamicOutputSnapshot binary record."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct


FORMAT = struct.Struct("<8I3Q1024s")
VERSION = 1
DATA_BYTES = 1024


def _fnv1a(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def decode_receipt(raw: bytes) -> dict:
    if len(raw) != FORMAT.size:
        raise ValueError(f"receipt must be exactly {FORMAT.size} bytes")
    (
        version,
        byte_count,
        valid,
        fence,
        generation,
        data_bytes,
        status,
        reserved,
        source_gpu_va,
        source_physical,
        declared_hash,
        data,
    ) = FORMAT.unpack(raw)
    if version != VERSION:
        raise ValueError(f"unsupported version {version}")
    if byte_count != FORMAT.size:
        raise ValueError(f"bytes field is {byte_count}, expected {FORMAT.size}")
    if valid != 1 or fence == 0 or generation == 0:
        raise ValueError("snapshot identity is incomplete")
    if data_bytes != DATA_BYTES or status != 0 or reserved != 0:
        raise ValueError("snapshot payload metadata is invalid")
    if (
        source_gpu_va == 0
        or source_physical == 0
        or source_gpu_va >= (1 << 40)
        or source_physical >= (1 << 40)
        or (source_gpu_va & 0x3FFF)
        or (source_physical & 0x3FFF)
    ):
        raise ValueError("snapshot source range is invalid")
    actual_hash = _fnv1a(data)
    if declared_hash != actual_hash:
        raise ValueError(
            f"snapshot hash mismatch: declared={declared_hash:#x} "
            f"actual={actual_hash:#x}"
        )
    return {
        "version": version,
        "bytes": byte_count,
        "valid": valid,
        "fence": fence,
        "generation": generation,
        "data_bytes": data_bytes,
        "status": status,
        "source_gpu_va": hex(source_gpu_va),
        "source_physical": hex(source_physical),
        "fnv1a": hex(actual_hash),
        "prefix": data[:64].hex(),
    }


def decode_path(path: Path) -> dict:
    return decode_receipt(path.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--raw-output", type=Path)
    args = parser.parse_args()
    raw = args.receipt.read_bytes()
    decoded = decode_receipt(raw)
    if args.raw_output is not None:
        args.raw_output.write_bytes(raw[FORMAT.size - DATA_BYTES :])
    rendered = json.dumps(decoded, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(rendered, end="")
    else:
        args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
