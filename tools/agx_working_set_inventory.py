#!/usr/bin/env python3
"""rc87: GPU working-set inventory of a D3D12 run from its UMD receipts.

Usage: agx_working_set_inventory.py <umd.log> [--pid N] [--at-qpc Q]

Walks the D3D12 creation receipts in order (agx_d3d12_ddi.cpp):
  measure-d3d12-create-heap-resource {heap?, ByteSize/64K, CPU page property,
      pool, heap flags, resource?, type, width, layout, handles}
  measure-d3d12-heap {2 native | 3 deferred (rc87: 64 KiB stub) | cpu_visible,
      bytes/64K, allocation?, va...}
  measure-d3d12-heap-backing {bytes/64K, ...} (rc87: a deferred heap backed)
  measure-d3d12-committed-texture {format, width, height, bytes/64K, bind}
      (bind 0x100 presentable, 0x200 placed)
  measure-d3d12-placed {reason, base?, offset, width}
  measure-d3d12-descriptor-heap {type, descriptors, flags, gpu?}
  measure-bo-create {MiB, class, alloc us, bind us, assoc us} (>= 10 ms only)
and prints the bytes each origin asked for against the bytes the UMD backed
with its own native BO (every one of them lives in segment 3, the pool,
allocation_windows.c), plus the residency trims.  D3D12 resources that the
app creates are never destroyed in GravityMark's scene, so the cumulative
set at a point is that point's working set; --at-qpc stops there.

Heap flags are DDI allow-flags (d3d12umddi.h D3D12DDI_HEAP_FLAGS):
NON_RT_DS_TEXTURES 0x2, BUFFERS 0x4, RT_DS_TEXTURES 0x20.  A heap without
BUFFERS can only hold placed textures; the native owner gives every placed
texture a BO of its own (C36), so such a heap's own backing is never read.
Exit 0 always: a decode aid, not a gate.
"""
import argparse
import re
import sys
from collections import defaultdict

ROW = re.compile(r"(measure-[a-z0-9-]+) hr=(\S+) pid=(\d+) tid=\d+ session=\d+ qpc=(\d+)(.*)")
CPU = {0: "DEFAULT", 1: "UPLOAD", 2: "READBACK"}
HEAP_BUFFERS = 0x4
MIB = float(1 << 20)


def rows(path, pid):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = ROW.search(line)
            if not m or (pid is not None and int(m.group(3)) != pid):
                continue
            try:
                values = [int(x, 16) for x in m.group(5).split()]
            except ValueError:
                continue
            yield m.group(1), m.group(2), int(m.group(4)), values


def inventory(path, pid=None, at_qpc=None):
    inv = defaultdict(lambda: [0, 0, 0])  # origin -> [count, requested, backed]
    pending = None
    trims = [0, 0]
    first_pid = pid
    for name, hr, qpc, v in rows(path, pid):
        if at_qpc is not None and qpc > at_qpc:
            break
        if name == "measure-d3d12-create-heap-resource" and len(v) >= 10:
            pending = {"heap": v[0], "size": v[1] << 16, "cpu": v[2], "flags": v[4],
                       "res": v[5], "type": v[6], "width": v[7]}
        elif name == "measure-d3d12-heap" and len(v) >= 5:
            backed = (v[1] << 16) if hr == "0x00000000" else 0
            if v[0] == 3 and backed:  # rc87 deferred texture-only heap: a 64 KiB stub
                backed = 0x10000
            if pending and pending["heap"]:
                cpu = CPU.get(pending["cpu"], "?")
                if pending["flags"] & HEAP_BUFFERS or not pending["flags"]:
                    origin = "app heap/committed buffer %s" % cpu
                else:
                    origin = "app texture-only heap %s (flags 0x%x)" % (cpu, pending["flags"])
                requested = pending["size"]
            else:
                origin = "driver CPU-visible heap (descriptor/internal)"
                requested = backed
            e = inv[origin]
            e[0] += 1
            e[1] += requested
            e[2] += backed
            pending = None
        elif name == "measure-d3d12-committed-texture" and len(v) >= 5:
            backed = (v[3] << 16) if hr == "0x00000000" else 0
            placed = bool(v[4] & 0x200)
            # AGX_D3D12_BIND_*: RENDER_TARGET 0x1, DEPTH_STENCIL 0x4
            kind = "presentable" if v[4] & 0x100 else ("rt/ds" if v[4] & 0x5 else "sampled")
            origin = ("app placed texture %s (own BO; heap already counted)" if placed
                      else "app committed texture %s") % kind
            e = inv[origin]
            e[0] += 1
            e[1] += 0 if placed else backed
            e[2] += backed
            pending = None
        elif name == "measure-d3d12-heap-backing" and len(v) >= 3:
            e = inv["app texture-only heap backed for a tile mapping"]
            e[0] += 1
            e[2] += v[0] << 16 if hr == "0x00000000" else 0
        elif name == "measure-d3d12-placed" and len(v) >= 4:
            e = inv["app placed buffer (aliases its heap)"]
            e[0] += 1
            e[1] += 0
            pending = None
        elif name == "measure-d3d12-descriptor-heap" and len(v) >= 4:
            e = inv["  (descriptor heaps, shader-visible: %d)" % (v[2] & 1)]
            e[0] += 1
        elif name == "measure-trim" and len(v) >= 3:
            trims[0] += 1
            trims[1] += v[1] | (v[2] << 32)
    return inv, trims


def main(argv):
    p = argparse.ArgumentParser()
    p.add_argument("log")
    p.add_argument("--pid", type=int)
    p.add_argument("--at-qpc", type=int)
    a = p.parse_args(argv[1:])
    inv, trims = inventory(a.log, a.pid, a.at_qpc)
    req = sum(e[1] for e in inv.values())
    backed = sum(e[2] for e in inv.values())
    print("%-62s %5s %10s %10s" % ("origin", "count", "asked MiB", "BO MiB"))
    for origin in sorted(inv, key=lambda k: -inv[k][2]):
        c, r, b = inv[origin]
        print("%-62s %5d %10.1f %10.1f" % (origin, c, r / MIB, b / MIB))
    print("%-62s %5s %10.1f %10.1f" % ("TOTAL", "", req / MIB, backed / MIB))
    print("trims %d, %.1f MiB" % (trims[0], trims[1] / MIB))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
