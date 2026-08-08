#!/usr/bin/env python3
"""Build an atomic, self-validating standalone boot.bin."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from guest_layout import load_layout
from launch_profile import Debug, Display, parse_profile
from bootstrap_image import pack_bootstrap, parse_bootstrap
from standalone_image import IMAGE_ALIGNMENT, ImageError, pack_image, parse_image


def describe_stages(stage0: bytes, stage1: bytes) -> dict:
    stage0_hash = hashlib.sha256(stage0).hexdigest()
    stage1_hash = hashlib.sha256(stage1).hexdigest()
    if stage0_hash == stage1_hash:
        raise ImageError("stage-0 and stage-1 identities must differ")
    return {
        "stage0": {"role": "bootstrap", "sha256": stage0_hash, "size": len(stage0)},
        "stage1": {"role": "hypervisor", "sha256": stage1_hash, "size": len(stage1)},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    m1n1_group = parser.add_mutually_exclusive_group(required=True)
    m1n1_group.add_argument("--m1n1", type=Path,
                            help="raw m1n1 for the legacy direct standalone image")
    m1n1_group.add_argument("--stage1-m1n1", type=Path,
                            help="raw inner m1n1 for a two-stage image")
    parser.add_argument("--stage0-m1n1", type=Path,
                        help="raw outer m1n1 that validates and chainloads Stage 1")
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-commit")
    parser.add_argument("--compiler")
    parser.add_argument("--display", choices=[value.value for value in Display], default="physical")
    parser.add_argument("--debug", choices=[value.value for value in Debug], default="off")
    args = parser.parse_args()

    layout = load_layout(args.layout)
    profile = parse_profile(display=args.display, debug=args.debug)
    if args.stage0_m1n1 is not None and args.stage1_m1n1 is None:
        parser.error("--stage0-m1n1 requires --stage1-m1n1")
    if args.stage1_m1n1 is not None and args.stage0_m1n1 is None:
        parser.error("--stage1-m1n1 requires --stage0-m1n1")

    m1n1_path = args.stage1_m1n1 or args.m1n1
    m1n1 = m1n1_path.read_bytes()
    if len(m1n1) % IMAGE_ALIGNMENT:
        raise ImageError(
            "m1n1 input does not end at its 16 KiB-aligned _payload_start; "
            "use the raw m1n1.bin build artifact"
        )
    inner = pack_image(
        m1n1,
        args.firmware.read_bytes(),
        layout_version=layout.layout_version,
        flags=profile.manifest_flags,
    )
    inner_manifest, firmware = parse_image(inner)
    outer_manifest = None
    stages = None
    if args.stage0_m1n1 is not None:
        stage0 = args.stage0_m1n1.read_bytes()
        if len(stage0) % IMAGE_ALIGNMENT:
            raise ImageError(
                "Stage 0 input does not end at its 16 KiB-aligned _payload_start; "
                "use the raw m1n1.bin build artifact"
            )
        stages = describe_stages(stage0, m1n1)
        image = pack_bootstrap(stage0, inner, flags=profile.manifest_flags)
        outer_manifest, decoded_inner = parse_bootstrap(image)
        decoded_manifest, decoded_firmware = parse_image(decoded_inner)
        assert decoded_manifest == inner_manifest
        assert decoded_firmware == firmware
    else:
        image = inner

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
    if stages is not None:
        metadata = {
            "format_version": 1,
            "image": {"sha256": digest, "size": len(image)},
            "source_commit": args.source_commit,
            "compiler": args.compiler,
            **stages,
        }
        (args.output.parent / "BUILD-METADATA.json").write_text(
            json.dumps(metadata, indent=2, sort_keys=True) + "\n"
        )
    print(f"output: {args.output}")
    print(f"size: {len(image)} bytes")
    if outer_manifest is not None:
        print(f"outer manifest offset: 0x{outer_manifest.manifest_offset:x}")
        print(f"outer payload offset: 0x{outer_manifest.payload_offset:x}")
        print(f"outer CRC32: {outer_manifest.crc32:08x}")
        print(f"inner manifest offset: 0x{inner_manifest.manifest_offset:x}")
        print(f"inner payload offset: 0x{inner_manifest.payload_offset:x}")
    else:
        print(f"manifest offset: 0x{inner_manifest.manifest_offset:x}")
        print(f"payload offset: 0x{inner_manifest.payload_offset:x}")
    print(f"firmware CRC32: {inner_manifest.crc32:08x}")
    print(f"profile: display={profile.display.value} debug={profile.debug.value}")
    print(f"SHA-256: {digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
