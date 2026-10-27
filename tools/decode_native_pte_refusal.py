#!/usr/bin/env python3
"""Decode REG_BINARY Wom1G3NativePteRefusal v1 (render_native_staging_receipt.h).

Accepts a binary file, or a kmd-receipts text dump ("Name hex" lines).
The first refused native-staging page is kept; RefusedGroups/RefusedUpdates
count all later ones. Handles are identities only, never dereferenced.
"""
import argparse
import json
from pathlib import Path
import struct

REASONS = {1: "owner", 2: "segment", 3: "range", 4: "ring-page-at-other-offset", 5: "foreign"}
U32 = ["Version", "Bytes", "Reason", "UpdateFlags", "StartIndex", "NumPageTableEntries",
       "PageIndex", "InputIndex", "Segment", "OsProcessId", "PageTableLevel", "Reserved"]
U64 = ["Qpc", "ProcessId", "Allocation", "PageOwner", "FirstPteVirtualAddress",
       "AllocationOffsetInBytes", "PageVa", "PageAllocationOffset", "PageIpa",
       "InputPteFlags", "InputPageAddress", "ExpectedIpa", "BindingOffset",
       "BindingBytes", "RingOffset", "RefusedGroups", "RefusedUpdates"]
SIZE = 4 * len(U32) + 8 * len(U64)


def decode(data):
    if len(data) != SIZE:
        raise ValueError(f"expected {SIZE} bytes, got {len(data)}")
    values = struct.unpack("<%dI%dQ" % (len(U32), len(U64)), data)
    out = dict(zip(U32 + U64, values))
    if out["Version"] != 1 or out["Bytes"] != SIZE:
        raise ValueError("unsupported version/layout")
    out["ReasonName"] = REASONS.get(out["Reason"], "unknown")
    flags = out["UpdateFlags"]
    out["UpdateFlagNames"] = [n for b, n in ((1, "Repeat"), (2, "InitialUpdate"),
                              (4, "NotifyEviction"), (8, "Use64KBPages")) if flags & b]
    for key in U64[2:]:
        out[key] = hex(out[key])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path)
    path = parser.parse_args().path
    raw = path.read_bytes()
    if len(raw) != SIZE:
        for line in raw.decode(errors="replace").splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] == "Wom1G3NativePteRefusal":
                raw = bytes.fromhex(parts[1])
                break
    print(json.dumps(decode(raw), indent=2))


if __name__ == "__main__":
    main()
