#!/usr/bin/env python3
"""Decode one exact Wom1DynamicStoreReceipt binary record."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct


FORMAT = struct.Struct("<10I26Q")
VERSION = 1


def _hex(value: int) -> str:
    return f"0x{value:x}"


def decode_receipt(raw: bytes) -> dict:
    if len(raw) != FORMAT.size:
        raise ValueError(f"receipt must be exactly {FORMAT.size} bytes")
    values = FORMAT.unpack(raw)
    (
        version, byte_count, valid, fence, generation, destination_bytes,
        store_pipeline, partial_store0, partial_store1, reserved,
        destination_gpu_va, destination_physical, attachment_gpu_va,
        pipeline_base_raw, load_pipeline, reload0, reload1,
        clear_page_fnv, reload_page_fnv, store_page_fnv,
        clear_uniform, store_texture, store_uniform,
        store_shader_gpu_va, store_shader_fnv,
        render_target_gpu_va, rt0, rt1, rt2, render_target_fnv,
        companion_gpu_va, companion0, companion1, companion2, companion3,
        companion_fnv,
    ) = values
    if version != VERSION:
        raise ValueError(f"unsupported receipt version {version}")
    if byte_count != FORMAT.size:
        raise ValueError(f"receipt bytes field is {byte_count}, expected {FORMAT.size}")
    if valid != 1:
        raise ValueError(f"receipt valid field is {valid}, expected 1")
    if fence == 0:
        raise ValueError("receipt fence must be nonzero")
    if reserved != 0:
        raise ValueError("receipt reserved field must be zero")
    return {
        "version": version,
        "bytes": byte_count,
        "valid": valid,
        "fence": fence,
        "generation": generation,
        "destination_bytes": destination_bytes,
        "destination_gpu_va": _hex(destination_gpu_va),
        "destination_physical": _hex(destination_physical),
        "attachment_gpu_va": _hex(attachment_gpu_va),
        "work": {
            "pipeline_base_raw": _hex(pipeline_base_raw),
            "load": _hex(load_pipeline),
            "store": _hex(store_pipeline),
            "reload0": _hex(reload0),
            "reload1": _hex(reload1),
            "partial_store0": _hex(partial_store0),
            "partial_store1": _hex(partial_store1),
        },
        "pipeline_pages": {
            "clear_fnv1a": _hex(clear_page_fnv),
            "reload_fnv1a": _hex(reload_page_fnv),
            "store_fnv1a": _hex(store_page_fnv),
            "clear_uniform_word": _hex(clear_uniform),
            "store_texture_word": _hex(store_texture),
            "store_uniform_word": _hex(store_uniform),
        },
        "store_shader_gpu_va": _hex(store_shader_gpu_va),
        "store_shader_fnv1a": _hex(store_shader_fnv),
        "render_target": {
            "gpu_va": _hex(render_target_gpu_va),
            "qword0": _hex(rt0),
            "qword1": _hex(rt1),
            "qword2": _hex(rt2),
            "fnv1a": _hex(render_target_fnv),
        },
        "companion": {
            "gpu_va": _hex(companion_gpu_va),
            "qword0": _hex(companion0),
            "qword1": _hex(companion1),
            "qword2": _hex(companion2),
            "qword3": _hex(companion3),
            "fnv1a": _hex(companion_fnv),
        },
    }


def decode_path(path: Path) -> dict:
    return decode_receipt(path.read_bytes())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("receipt", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    decoded = decode_path(args.receipt)
    rendered = json.dumps(decoded, indent=2, sort_keys=True) + "\n"
    if args.output is None:
        print(rendered, end="")
    else:
        args.output.write_text(rendered, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
