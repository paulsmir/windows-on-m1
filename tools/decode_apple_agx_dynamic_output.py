#!/usr/bin/env python3
"""Decode one bounded Wom1DynamicOutputSnapshot binary record."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


FORMAT = struct.Struct("<12I3Q1024s")
VERSION = 1
DATA_BYTES = 1024


def _fnv1a(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def decode_receipt(raw: bytes, expected_image: bytes | None = None) -> dict:
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
        expected_layout,
        expected_foreground_color,
        observed_foreground_color,
        verification_valid,
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
    if expected_layout != 1:
        raise ValueError(f"unsupported expected layout {expected_layout}")
    if expected_foreground_color in (0, 0xA5A5A5A5):
        raise ValueError("expected foreground color is invalid")
    if verification_valid not in (1, 2):
        raise ValueError("verification validity is missing")
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
    decoded = {
        "version": version,
        "bytes": byte_count,
        "valid": valid,
        "fence": fence,
        "generation": generation,
        "data_bytes": data_bytes,
        "status": status,
        "expected_layout": "agx_tiled_64",
        "expected_foreground_color": hex(expected_foreground_color),
        "observed_foreground_color": hex(observed_foreground_color),
        "verification_valid": verification_valid,
        "source_gpu_va": hex(source_gpu_va),
        "source_physical": hex(source_physical),
        "fnv1a": hex(actual_hash),
        "prefix": data[:64].hex(),
    }
    if expected_image is not None:
        if len(expected_image) != DATA_BYTES:
            raise ValueError(
                f"expected image must be exactly {DATA_BYTES} bytes"
            )
        first_mismatch = next(
            (index for index, pair in enumerate(zip(data, expected_image))
             if pair[0] != pair[1]),
            None,
        )
        decoded["expected_image_match"] = first_mismatch is None
        decoded["expected_image_first_mismatch"] = first_mismatch
        decoded["expected_image_sha256"] = hashlib.sha256(
            expected_image).hexdigest()
    return decoded


def decode_path(path: Path, expected_image: Path | None = None) -> dict:
    expected = expected_image.read_bytes() if expected_image is not None else None
    return decode_receipt(path.read_bytes(), expected)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--raw-output", type=Path)
    parser.add_argument("--expected-image", type=Path)
    args = parser.parse_args()
    raw = args.receipt.read_bytes()
    expected = (
        args.expected_image.read_bytes()
        if args.expected_image is not None
        else None
    )
    decoded = decode_receipt(raw, expected)
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
