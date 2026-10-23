#!/usr/bin/env python3
"""Join a GravityMark run's UMD heap/CPU-access receipts with the KMD G4 refusal.

Usage: agx_gm_alloc_join.py <umd.log> <kmd-receipts.txt>

Prints every D3D12 heap (size, GPU VA, CPU page property from the preceding
measure-d3d12-heap-sizes row), the T7g measure-bo-cpu-access slots, each
Wom1G4SubmitFailure v3 range (T7b layout) and which heap and CPU-access slot
contain its Va.  Exit 0 always; it is a decode aid, not a gate.
"""
import re
import struct
import sys

ROW = re.compile(r".*?(measure-[a-z0-9-]+) hr=(\S+) .*?qpc=(\d+) (.*)")
CPU_PROP = {0: "NOT_AVAILABLE", 1: "WRITE_COMBINE", 2: "WRITE_BACK"}
CPU_FLAGS = ["Mapped", "LockedBase", "OwnerPointers", "ActiveLocks", "Direct", "SystemDirect",
             "Borrowed", "PrivateStaging", "Transition", "NativeBo", "Saturated"]


def umd_rows(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = ROW.match(line.replace("\r", ""))
            if m:
                words = m.group(4).split()
                try:
                    yield m.group(1), m.group(2), int(m.group(3)), [int(x, 16) for x in words]
                except ValueError:
                    continue


def g4_failures(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("Wom1G4SubmitFailure "):
                continue
            b = bytes.fromhex(line.split()[1])
            version = struct.unpack_from("<I", b, 0)[0]
            if version < 3 or len(b) < 128:
                continue
            r = struct.unpack_from("<13I", b, 32)
            va = struct.unpack_from("<Q", b, 88)[0]
            yield {"kind": r[8], "va": va, "bytes": r[10], "write": r[11]}


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2])
        return 2
    heaps, cpu, pending = [], {}, None
    for name, hr, qpc, v in umd_rows(argv[1]):
        if name == "measure-d3d12-heap-sizes" and len(v) >= 8 and v[0]:
            pending = (v[2], v[6], v[7])  # CPU page property, resource type, width
        elif name == "measure-d3d12-heap" and len(v) >= 5:
            size, va = v[1] << 16, (v[4] << 32) | (v[3] << 16)
            heaps.append((va, size, hr, pending))
            pending = None
        elif name == "measure-bo-cpu-access" and len(v) >= 16:
            va, size = v[6] | (v[7] << 32), v[8] | (v[9] << 32)
            cpu[(v[3] | (v[4] << 32), va)] = (va, size, v[0] >> 8 & 0xff, v[10:15], v[15])
    print("HEAPS %d, %d MiB" % (len(heaps), sum(h[1] for h in heaps) >> 20))
    for va, size, hr, p in heaps:
        if size >= 32 << 20:
            prop = CPU_PROP.get(p[0], p[0]) if p else "?"
            res = " resource type %d width %d" % (p[1], p[2]) if p and p[1] else ""
            print("  heap 0x%x %4d MiB hr=%s cpu=%s%s" % (va, size >> 20, hr, prop, res))
    print("CPU-ACCESS SLOTS %d" % len(cpu))
    for va, size, reason, counts, flags in sorted(cpu.values()):
        names = "|".join(n for i, n in enumerate(CPU_FLAGS) if flags >> i & 1)
        print("  slot 0x%x %4d MiB reason %d maps/locks/owner/heapmaps/depth %s %s"
              % (va, size >> 20, reason, counts, names))
    for g in g4_failures(argv[2]):
        heap = [h for h in heaps if h[0] <= g["va"] < h[0] + h[1]]
        slot = [c for c in cpu.values() if c[0] <= g["va"] < c[0] + c[1]]
        print("G4 kind %d va 0x%x %d MiB write %d -> heap %s, cpu-access %s"
              % (g["kind"], g["va"], g["bytes"] >> 20, g["write"],
                 "0x%x" % heap[0][0] if heap else "none (committed/owner BO)",
                 "MAPPED" if slot else "none"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
