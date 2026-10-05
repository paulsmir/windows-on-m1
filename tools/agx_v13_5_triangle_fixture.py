#!/usr/bin/env python3
"""Normalize one exact hardware-proven V13_5 triangle frame for Win32 ABI use."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import zipfile


ENCODER_VA = 0x1500044000
STATE_VA = 0x1500000000
PIPELINE_VA = 0x1100020000
VERTEX_VA = 0x15001D8000
VERTEX_SHADER_VA = 0x1100064000
FRAGMENT_SHADER_VA = 0x110006C000
SCISSOR_VA = 0x15000C8000


class FixtureError(ValueError):
    pass


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little")


def patch_usc_address(record: bytearray, offset: int) -> None:
    value = int.from_bytes(record[offset:offset + 8], "little")
    record[offset:offset + 8] = (value & 0xFFFFFF).to_bytes(8, "little")


def patch_usc_shader(record: bytearray, offset: int) -> None:
    value = int.from_bytes(record[offset:offset + 6], "little")
    record[offset:offset + 6] = (value & 0xFFFF).to_bytes(6, "little")


def object_bytes(archive: zipfile.ZipFile, objects: list[dict],
                 address: int, minimum: int) -> bytes:
    matches = [item for item in objects if item.get("addr") == address]
    if len(matches) != 1 or not matches[0].get("file"):
        raise FixtureError(f"missing object at {address:#x}")
    data = archive.read(matches[0]["file"])
    if len(data) != matches[0].get("size") or len(data) < minimum:
        raise FixtureError(f"invalid object size at {address:#x}")
    return data


def normalize(frame_path: Path, expected_frame_sha256: str,
              visible_path: Path) -> tuple[dict[str, bytes], dict]:
    frame = frame_path.read_bytes()
    if sha256(frame) != expected_frame_sha256.lower():
        raise FixtureError("frame SHA256 mismatch")
    visible = visible_path.read_bytes()
    if len(visible) != 16 * 16 * 4:
        raise FixtureError("visible image size mismatch")
    colours = [visible[index:index + 4] for index in range(0, len(visible), 4)]
    red = sum(pixel == b"\xff\x00\x00\xff" for pixel in colours)
    background = sum(pixel == b"\x11\x22\x33\xff" for pixel in colours)
    if red != 72 or background != 184 or red + background != 256:
        raise FixtureError("visible triangle proof mismatch")

    with zipfile.ZipFile(frame_path) as archive:
        cmdbuf = json.loads(archive.read("cmdbuf.json"))
        objects = json.loads(archive.read("objects.json"))
        if (cmdbuf.get("encoder_ptr") != ENCODER_VA or
                cmdbuf.get("fb_width") != 16 or
                cmdbuf.get("fb_height") != 16 or
                cmdbuf.get("attachment_count") != 1):
            raise FixtureError("native command identity mismatch")
        encoder_source = object_bytes(archive, objects, ENCODER_VA, 64)
        state_source = object_bytes(archive, objects, STATE_VA, 0x2044)
        pipeline_source = object_bytes(archive, objects, PIPELINE_VA, 0x1014)
        vertex_source = object_bytes(archive, objects, VERTEX_VA, 24)
        vertex_shader_source = object_bytes(
            archive, objects, VERTEX_SHADER_VA, 68)
        fragment_shader_source = object_bytes(
            archive, objects, FRAGMENT_SHADER_VA, 112)
        scissor_source = object_bytes(archive, objects, SCISSOR_VA, 16)

    if (u32(encoder_source, 0) != 0x4000002E or
            u32(encoder_source, 4) != 0x00001002 or
            u32(encoder_source, 8) != 0x00020000 or
            u32(encoder_source, 12) != 0x00000404 or
            u32(encoder_source, 16) != 0 or
            u32(encoder_source, 24) != 0x00000B15 or
            u32(encoder_source, 28) != 0x00001000 or
            u32(encoder_source, 32) != 0x00001115 or
            u32(encoder_source, 36) != 0x00002000 or
            u32(encoder_source, 40) != 0x61C00600 or
            u32(encoder_source, 56) != 0xC0000000):
        raise FixtureError("native VDM stream mismatch")
    viewport_ppp = state_source[0x1000:0x1000 + 44]
    draw_ppp = state_source[0x2000:0x2000 + 68]
    if (u32(viewport_ppp, 0) != 0x00000D00 or
            u32(draw_ppp, 0) != 0x08A600FF or
            u32(draw_ppp, 0x30) != 0x00001002 or
            u32(draw_ppp, 0x34) != 0x00021000 or
            u32(draw_ppp, 0x40) != 4):
        raise FixtureError("native PPP stream mismatch")

    encoder = bytearray(260)
    encoder[:64] = encoder_source[:64]
    encoder[8:12] = b"\0" * 4
    for offset in (24, 32):
        word = u32(encoder, offset) & ~0xFF
        encoder[offset:offset + 4] = word.to_bytes(4, "little")
        encoder[offset + 4:offset + 8] = b"\0" * 4
    encoder[128:128 + len(viewport_ppp)] = viewport_ppp
    encoder[192:192 + len(draw_ppp)] = draw_ppp
    encoder[192 + 0x34:192 + 0x38] = b"\0" * 4

    pipeline = bytearray(84)
    pipeline[:24] = pipeline_source[:24]
    pipeline[64:84] = pipeline_source[0x1000:0x1014]
    patch_usc_address(pipeline, 0)
    patch_usc_shader(pipeline, 12)
    patch_usc_shader(pipeline, 68)

    assets = {
        "vertex-data.bin": vertex_source[:24],
        "vertex-shader.bin": vertex_shader_source[:68],
        "fragment-shader.bin": fragment_shader_source[:112],
        "pipeline.bin": bytes(pipeline),
        "encoder.bin": bytes(encoder),
        "descriptor.bin": bytes(8),
        "scissor.bin": scissor_source[:16],
        "depth-bias.bin": bytes(12),
    }
    manifest = {
        "schema": 1,
        "source": {
            "frame_sha256": expected_frame_sha256.lower(),
            "visible_sha256": sha256(visible),
            "mesa_commit": "7a4f24061fa56ef7eff12132dd7b1461d5a890d8",
            "platform": "J313",
            "firmware": "V13_5",
            "red_pixels": red,
            "background_pixels": background,
        },
        "assets": {name: sha256(data) for name, data in assets.items()},
        "relocations": [
            {"kind": "DescriptorAddress", "destination": 0,
             "target": "vertex-data"},
            {"kind": "UscBufferAddress40", "destination": 0,
             "target": "descriptor"},
            {"kind": "UscShaderOffset32", "destination": 12,
             "target": "vertex-shader"},
            {"kind": "UscShaderOffset32", "destination": 68,
             "target": "fragment-shader"},
            {"kind": "VdmPipelineOffset32", "destination": 8,
             "target": "pipeline", "target_offset": 0},
            {"kind": "PppStateAddress40", "destination": 24,
             "target": "encoder", "target_offset": 128},
            {"kind": "PppStateAddress40", "destination": 32,
             "target": "encoder", "target_offset": 192},
            {"kind": "VdmPipelineOffset32", "destination": 244,
             "target": "pipeline", "target_offset": 64},
        ],
    }
    return assets, manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frame", type=Path, required=True)
    parser.add_argument("--frame-sha256", required=True)
    parser.add_argument("--visible", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    assets, manifest = normalize(
        args.frame, args.frame_sha256, args.visible)
    args.output.mkdir(parents=True, exist_ok=False)
    for name, data in assets.items():
        (args.output / name).write_bytes(data)
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
