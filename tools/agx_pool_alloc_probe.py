#!/usr/bin/env python3
"""Decode the KMD receipt Wom1PoolAllocProbe (POOL-PROBE diagnostic, v1).

Layout and field meanings: ADMISSION_POOL_ALLOC_PROBE_RECEIPT in
drivers/apple-agx/render-admission/include/render_pool_demand.h (116 u64,
928 bytes): Version, Arms, PerArm, FieldsPerArm, TriggerSlabAllocCalls,
TriggerSlabFastMisses, TriggerSlabChunked, ProbeUs, then Arm[12][9] =
Calls, Ok, SumUs, MinUs, MaxUs, GranulesOk, Contig2M, Reused, FreeUs.

Input: a raw .bin of the value, or a kmd-receipts-<tag>.txt dump (lines
"<ValueName> <hex>").  No live registry access.  The ratios printed at the
end are the quantities of the pre-registered reading in
investigation/GPU_CURRENT_STATE.md (entry 2026-11-10T21:27:02Z); this tool
prints them and draws no conclusion.
"""
import struct
import sys

NAME = "Wom1PoolAllocProbe"
ARMS = ("B1 cached 64K", "B2 cached 64K", "A1 wc 64K", "A2 wc 64K",
        "B3 cached 64K", "C1 wc 256K", "B4 cached 64K", "C2 wc 1M",
        "B5 cached 64K", "D1 wc prefer", "B6 cached 64K", "A3 wc 64K")
FIELDS = ("Calls", "Ok", "SumUs", "MinUs", "MaxUs", "GranulesOk",
          "Contig2M", "Reused", "FreeUs")


def load(path):
    data = open(path, "rb").read()
    if path.endswith(".bin"):
        return data
    for line in data.decode("utf-8", "replace").splitlines():
        parts = line.split(" ", 1)
        if len(parts) == 2 and parts[0] == NAME:
            return bytes.fromhex(parts[1].strip())
    raise SystemExit("%s: no %s value" % (path, NAME))


def decode(blob):
    if len(blob) != 8 * (8 + len(ARMS) * len(FIELDS)):
        raise SystemExit("unexpected size %d (v1 is 928 bytes)" % len(blob))
    words = struct.unpack("<%dQ" % (len(blob) // 8), blob)
    version, arms, per_arm, fields = words[:4]
    if (version, arms, per_arm, fields) != (1, len(ARMS), 8, len(FIELDS)):
        raise SystemExit("unexpected header %r" % (words[:4],))
    head = dict(zip(("TriggerSlabAllocCalls", "TriggerSlabFastMisses",
                     "TriggerSlabChunked", "ProbeUs"), words[4:8]))
    rows = [dict(zip(FIELDS, words[8 + i * len(FIELDS):8 + (i + 1) * len(FIELDS)]))
            for i in range(len(ARMS))]
    return head, rows


def mean(row):
    return row["SumUs"] / row["Calls"] if row["Calls"] else float("nan")


def ratio(a, b):
    return "%.2f" % (a / b) if b else "n/a"


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: agx_pool_alloc_probe.py <Wom1PoolAllocProbe.bin | kmd-receipts-<tag>.txt>")
    head, rows = decode(load(argv[1]))
    print("POOL_PROBE " + " ".join("%s=%d" % kv for kv in head.items()))
    print("%-2s %-14s %5s %3s %9s %8s %8s %8s %6s %5s %6s %8s" % (
        "#", "arm", "calls", "ok", "mean_us", "min_us", "max_us", "sum_us",
        "gran", "c2m", "reuse", "free_us"))
    for i, (name, r) in enumerate(zip(ARMS, rows)):
        print("%-2d %-14s %5d %3d %9.1f %8d %8d %8d %6d %5d %6d %8d" % (
            i, name, r["Calls"], r["Ok"], mean(r), r["MinUs"], r["MaxUs"],
            r["SumUs"], r["GranulesOk"], r["Contig2M"], r["Reused"], r["FreeUs"]))
    b = (mean(rows[0]) + mean(rows[1])) / 2.0
    a1, a2, c1, c2, d1, a3 = (mean(rows[i]) for i in (2, 3, 5, 7, 9, 11))
    print("POOL_PROBE_RATIOS b_us=%.1f a1/b=%s a2/a1=%s c1/a1=%s c2/a1=%s d1/a1=%s a3/a1=%s a2_reused=%d/256" % (
        b, ratio(a1, b), ratio(a2, a1), ratio(c1, a1), ratio(c2, a1),
        ratio(d1, a1), ratio(a3, a1), rows[3]["Reused"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
