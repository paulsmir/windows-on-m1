#!/usr/bin/env python3
"""BO census: which buffer objects of at least 1 MiB a GPU batch ever referenced, and when.

Usage: agx_bo_life.py <umd.log>

Joins the BO census receipts of the D3D12 owner (field order: the comment above
owner_bo_census in drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp):

  measure-bo-born      {serial, handle, size/64K, VA>>16, VA>>32, winsys class,
                        origin (1 new, 2 BO cache reuse), Mesa flags, ms since process creation}
  measure-bo-first-ref {serial, handle, size/64K, batch kind (1 render, 2 compute),
                        ms since process creation}
  measure-bo-census    {small born, small size/64K, small first-referenced,
                        small referenced size/64K, large born, large first-referenced, ms}

Born and first-ref join on (pid, serial).  The D3D12 object class comes from the
rows of the same process: the measure-d3d12-create-heap-resource row open on the
born line's thread (committed buffer / committed texture / heap / placed or
reserved resource), or the measure-d3d12-heap / -heap-backing row with the
born line's VA (heap the owner made outside CreateHeapAndResource, e.g. a
descriptor heap; a deferred heap's backing).  Otherwise the class is the
winsys class (driver-internal BO).  Prints, per process and class, objects and
MiB created, never referenced, and first referenced within 0..1, 1..5, 5..30,
30..70 and >70 s of creation, plus the ten largest never-referenced objects.
"""
import re
import sys

ROW = re.compile(r".*?(measure-[a-z0-9-]+) hr=(\S+) pid=(\d+) tid=(\d+) .*?qpc=(-?\d+)(.*)")
WINSYS = {1: "General", 2: "Shader", 3: "Encoder"}
BUCKETS = ((1000, "0-1s"), (5000, "1-5s"), (30000, "5-30s"), (70000, "30-70s"), (None, ">70s"))
TERMINAL = ("measure-d3d12-heap-refused", "measure-d3d12-committed-texture", "measure-d3d12-placed")


def rows(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = ROW.match(line.replace("\r", ""))
            if not m:
                continue
            try:
                values = [int(x, 16) for x in m.group(6).split()]
            except ValueError:
                continue
            yield m.group(1), int(m.group(3)), int(m.group(4)), values


def scope_class(v):
    """Class of a measure-d3d12-create-heap-resource row {heap present, ByteSize/64K,
    CPU page property, pool, flags, resource present, type, width, layout, handles}."""
    heap, resource, kind = v[0], v[5], v[6]
    if not resource:
        return "heap" if heap else None
    noun = "buffer" if kind == 1 else "texture"
    return ("committed-" if heap else "placed/reserved-") + noun


class Process:
    def __init__(self):
        self.scope = {}     # tid -> [class, closes after the first born]
        self.born = {}      # serial -> record
        self.by_va = {}     # VA -> newest serial born there
        self.census = []
        self.orphans = 0
        self.repeats = 0


def parse(path):
    procs = {}
    for name, pid, tid, v in rows(path):
        p = procs.setdefault(pid, Process())
        if name == "measure-d3d12-create-heap-resource" and len(v) >= 10:
            cls = scope_class(v)
            p.scope[tid] = [cls, cls in ("committed-buffer", "heap")] if cls else None
        elif name in TERMINAL:
            p.scope.pop(tid, None)
        elif name == "measure-bo-born" and len(v) >= 9:
            va = (v[4] << 32) | (v[3] << 16)
            scope = p.scope.get(tid)
            rec = {"serial": v[0], "handle": v[1], "bytes": v[2] << 16, "va": va,
                   "winsys": v[5], "origin": v[6], "ms": v[8], "first": None,
                   "cls": scope[0] if scope else None}
            if scope and scope[1]:
                p.scope.pop(tid, None)
            p.born[v[0]] = rec
            p.by_va[va] = v[0]
        elif name in ("measure-d3d12-heap", "measure-d3d12-heap-backing"):
            if name == "measure-d3d12-heap":
                p.scope.pop(tid, None)
                va = (v[4] << 32) | (v[3] << 16) if len(v) >= 5 and v[0] in (0, 1, 2) else 0
                label = "heap (outside CreateHeapAndResource)"
            else:
                va = (v[2] << 32) | (v[1] << 16) if len(v) >= 3 else 0
                label = "deferred-heap backing"
            rec = p.born.get(p.by_va.get(va)) if va else None
            if rec and rec["cls"] is None:
                rec["cls"] = label
        elif name == "measure-bo-first-ref" and len(v) >= 5:
            rec = p.born.get(v[0])
            if rec is None:
                p.orphans += 1
            elif rec["first"] is not None:
                p.repeats += 1
            else:
                rec["first"] = (v[4], v[3])
        elif name == "measure-bo-census" and len(v) >= 7:
            p.census.append(v)
    return {pid: p for pid, p in procs.items() if p.born}


def label(rec):
    cls = rec["cls"] or "driver " + WINSYS.get(rec["winsys"], "class %d" % rec["winsys"])
    return cls + (" (cache reuse)" if rec["origin"] == 2 else "")


def mib(n):
    return "%.1f" % (n / (1 << 20))


def bucket(delay):
    for limit, name in BUCKETS:
        if limit is None or delay < limit:
            return name
    return BUCKETS[-1][1]


def report(pid, p):
    groups = {}
    for rec in p.born.values():
        for key in (label(rec), "TOTAL"):
            g = groups.setdefault(key, {"n": 0, "b": 0, "never": [0, 0],
                                        **{name: [0, 0] for _, name in BUCKETS}})
            g["n"] += 1
            g["b"] += rec["bytes"]
            slot = g["never"] if rec["first"] is None else g[bucket(max(0, rec["first"][0] - rec["ms"]))]
            slot[0] += 1
            slot[1] += rec["bytes"]
    span = (min(r["ms"] for r in p.born.values()), max(r["ms"] for r in p.born.values()))
    print("PID %d: %d BOs >= 1 MiB born %.1f..%.1f s after process start" %
          (pid, len(p.born), span[0] / 1000.0, span[1] / 1000.0))
    cols = ["never"] + [name for _, name in BUCKETS]
    print("  %-44s %6s %9s  %s" % ("class", "count", "MiB", "  ".join("%-14s" % c for c in cols)))
    order = sorted((k for k in groups if k != "TOTAL"), key=lambda k: -groups[k]["b"]) + ["TOTAL"]
    for key in order:
        g = groups[key]
        cells = "  ".join("%-14s" % ("%d/%s" % (g[c][0], mib(g[c][1]))) for c in cols)
        print("  %-44s %6d %9s  %s" % (key, g["n"], mib(g["b"]), cells))
    print("  (cells: objects/MiB; time buckets = first batch reference minus creation)")
    never = sorted((r for r in p.born.values() if r["first"] is None), key=lambda r: -r["bytes"])[:10]
    print("  largest never-referenced:")
    for r in never:
        print("    serial %6d handle 0x%08x %9s MiB va 0x%x born %.1f s  %s" %
              (r["serial"], r["handle"], mib(r["bytes"]), r["va"], r["ms"] / 1000.0, label(r)))
    if not never:
        print("    none")
    for v in p.census:
        print("  teardown census at %.1f s: <1 MiB born %d (%s MiB), referenced %d (%s MiB); "
              ">=1 MiB born %d, referenced %d" % (v[6] / 1000.0, v[0], mib(v[1] << 16), v[2],
                                                  mib(v[3] << 16), v[4], v[5]))
    if not p.census:
        print("  no measure-bo-census line (device not destroyed before exit): BOs < 1 MiB not reported")
    if p.orphans or p.repeats:
        print("  WARNING: %d first-ref lines without a born line, %d repeated first-ref lines"
              % (p.orphans, p.repeats))


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2])
        return 2
    procs = parse(argv[1])
    if not procs:
        print("error: no measure-bo-born lines in %s (the BO census needs a driver with it and "
              "APPLE_AGX_UMD_TRACE_FILE and APPLE_AGX_D3D12_BO_CENSUS=1 set for the D3D12 process)" % argv[1], file=sys.stderr)
        return 1
    for pid in sorted(procs):
        report(pid, procs[pid])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
