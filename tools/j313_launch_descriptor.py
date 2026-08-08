#!/usr/bin/env python3
"""Pack the shared J313 launch descriptor consumed by m1n1."""

from __future__ import annotations

import struct
from typing import Iterable, Sequence


J313_TARGET = 0x3331334A
J313_SCHEMA_REVISION = 1
J313_MPIDRS = (0, 1, 2, 3, 0x10100, 0x10101, 0x10102, 0x10103)
MAX_REGIONS = 16
DESCRIPTOR_SIZE = 632


def pack_descriptor(
    *,
    boot: tuple[int, int, int, Sequence[int]],
    regions: Iterable[tuple[int, int, int, int]],
    devices: Sequence[int],
    adt_size: int,
    adt_digest: bytes,
    mpidrs: Sequence[int] = J313_MPIDRS,
) -> bytes:
    regions = tuple(regions)
    args = tuple(boot[3])
    if len(regions) > MAX_REGIONS:
        raise ValueError("descriptor accepts at most 16 regions")
    if len(mpidrs) != 8:
        raise ValueError("J313 descriptor requires exactly 8 MPIDRs")
    if len(args) != 4:
        raise ValueError("boot args must contain four registers")
    if len(devices) != 10:
        raise ValueError("devices must contain six 64-bit and four 32-bit values")
    if len(adt_digest) != 32:
        raise ValueError("ADT digest must be exactly 32 bytes")

    out = bytearray()
    out += struct.pack("<IIQ", J313_TARGET, J313_SCHEMA_REVISION, 0)
    out += struct.pack("<7Q", boot[0], boot[1], boot[2], *args)
    out += struct.pack("<Q32sII", adt_size, adt_digest, len(regions), len(mpidrs))
    for region in regions:
        out += struct.pack("<IIQQ", *region)
    out += bytes((MAX_REGIONS - len(regions)) * struct.calcsize("<IIQQ"))
    out += struct.pack("<8Q", *mpidrs)
    out += struct.pack("<6Q4I", *devices)
    if len(out) != DESCRIPTOR_SIZE:
        raise AssertionError(f"descriptor ABI drift: {len(out)} != {DESCRIPTOR_SIZE}")
    return bytes(out)
