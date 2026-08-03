#!/usr/bin/env python3
"""Build an atomic, self-validating standalone boot.bin."""

import argparse
import hashlib
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from guest_layout import load_layout
from standalone_image import IMAGE_ALIGNMENT, ImageError, pack_image, parse_image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--m1n1", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    layout = load_layout(args.layout)
    m1n1 = args.m1n1.read_bytes()
    if len(m1n1) % IMAGE_ALIGNMENT:
        raise ImageError(
            "m1n1 input does not end at its 16 KiB-aligned _payload_start; "
            "use the raw m1n1.bin build artifact"
        )
    image = pack_image(
        m1n1,
        args.firmware.read_bytes(),
        layout_version=layout.layout_version,
    )
    manifest, _ = parse_image(image)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{args.output.name}.", dir=args.output.parent, delete=False
        ) as temporary:
            temporary_path = Path(temporary.name)
            temporary.write(image)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_path, 0o644)
        os.replace(temporary_path, args.output)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)

    digest = hashlib.sha256(image).hexdigest()
    print(f"output: {args.output}")
    print(f"size: {len(image)} bytes")
    print(f"manifest offset: 0x{manifest.manifest_offset:x}")
    print(f"payload offset: 0x{manifest.payload_offset:x}")
    print(f"firmware CRC32: {manifest.crc32:08x}")
    print(f"SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
