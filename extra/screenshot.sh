#!/bin/bash
# Encode the latest complete async framebuffer publication as a PNG.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${1:-$ROOT/screen.png}"
PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

"$PYTHON" - "$ROOT" "$OUT" <<'PY'
import json
import os
from pathlib import Path
import struct
import sys
import time
import zlib

root = Path(sys.argv[1])
output = Path(sys.argv[2])

for attempt in range(4):
    try:
        before = (root / "fb-info.json").read_bytes()
        metadata = json.loads(before)
        raw = (root / "fb.raw").read_bytes()
        after = (root / "fb-info.json").read_bytes()
        if before != after:
            raise ValueError("publication changed while reading")
        if len(raw) != metadata["size"]:
            raise ValueError("raw frame size does not match metadata")
        if zlib.crc32(raw) & 0xFFFFFFFF != metadata["crc32"]:
            raise ValueError("raw frame checksum does not match metadata")
        break
    except (OSError, KeyError, ValueError, json.JSONDecodeError) as error:
        if attempt == 3:
            raise SystemExit(f"No complete framebuffer publication: {error}")
        time.sleep(0.05)

width = metadata["width"]
height = metadata["height"]
stride = metadata["stride"]
rows = bytearray()
source = memoryview(raw)
for y in range(height):
    line = source[y * stride:y * stride + width * 4]
    rgb = bytearray(width * 3)
    rgb[0::3] = line[2::4]
    rgb[1::3] = line[1::4]
    rgb[2::3] = line[0::4]
    rows.append(0)
    rows.extend(rgb)


def png_chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


png = (b"\x89PNG\r\n\x1a\n"
       + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
       + png_chunk(b"IDAT", zlib.compress(rows, 6))
       + png_chunk(b"IEND", b""))
output.parent.mkdir(parents=True, exist_ok=True)
temporary = output.with_name(output.name + ".part")
temporary.write_bytes(png)
os.replace(temporary, output)
print(f"{output}: {width}x{height}, generation {metadata['generation']}")
PY
