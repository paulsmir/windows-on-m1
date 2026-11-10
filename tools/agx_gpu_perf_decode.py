#!/usr/bin/env python3
"""Decode the rc84 Wom1GpuPerf receipt (render_gpu_perf.h, version 1).

The KMD samples the firmware-written HwDataA fields at every job completion:
actual/target/current GPU performance state, frequency, temperature, power,
utilization and the firmware's update timestamp. The time between two
samples goes to the actual state of the second one; intervals longer than
the gap limit (50 ms) are counted as gaps and add no time.

Usage: agx_gpu_perf_decode.py Wom1GpuPerf.bin [--since earlier.bin] [--json]

--since subtracts an earlier snapshot of the same boot (counts, ticks and
gaps), e.g. read before and after one GravityMark run; maxima, temperature
and the ring stay those of the later snapshot.
"""
import argparse
import json
import struct
import sys

# J313 ADT perf-states (the RegionC encoder's AppleAgxRegionCJ313Frequencies).
J313_PSTATE_MHZ = [0, 396, 528, 720, 924, 1128, 1278]

PSTATES = 16
RING = 32
HEADER = struct.Struct("<4I4Q16Q48I8I")
SAMPLE = struct.Struct("<QQ8I")


def f32(bits):
    return struct.unpack("<f", struct.pack("<I", bits))[0]


def decode(blob):
    if len(blob) < HEADER.size + RING * SAMPLE.size:
        raise ValueError(f"Wom1GpuPerf too short: {len(blob)} bytes")
    v = HEADER.unpack_from(blob, 0)
    version, size, samples, invalid = v[0:4]
    if version != 1 or size != HEADER.size + RING * SAMPLE.size:
        raise ValueError(f"unsupported Wom1GpuPerf version {version} size {size}")
    qpc_frequency, last_qpc, sampled_ticks, limited_ticks = v[4:8]
    actual_ticks = v[8:24]
    actual_count, target_count, cur_count = v[24:40], v[40:56], v[56:72]
    (limited_count, gaps, temp_min, temp_max, avg_power_max, cur_power_max,
     freq_bits_max, update_changes) = v[72:80]
    ms = (lambda ticks: ticks * 1000.0 / qpc_frequency) if qpc_frequency else (lambda ticks: 0.0)
    ring = []
    for i in range(min(samples, RING)):
        index = (samples - min(samples, RING) + i) % RING
        s = SAMPLE.unpack_from(blob, HEADER.size + index * SAMPLE.size)
        ring.append({
            "qpc": s[0], "update_ts": s[1],
            "actual": s[2] & 0xff, "target": (s[2] >> 8) & 0xff, "current": (s[2] >> 16) & 0xff,
            "freq_mhz": f32(s[3]), "temp_c": s[4], "avg_power_mw": s[5],
            "cur_power_mw": s[6], "utilization": f32(s[7]), "pwr_status": s[8],
            "perf_tgt": s[9] & 0xff, "perf_tgt_2": (s[9] >> 8) & 0xff,
            "perf_tgt_applied": bool(s[9] & 0x10000), "perf_tgt_ignored": bool(s[9] & 0x20000),
        })
    return {
        "version": version, "samples": samples, "invalid": invalid, "gaps": gaps,
        "update_changes": update_changes,
        "sampled_ms": ms(sampled_ticks), "limited_ms": ms(limited_ticks),
        "limited_count": limited_count,
        "actual_ms": [ms(t) for t in actual_ticks],
        "actual_count": list(actual_count), "target_count": list(target_count),
        "current_count": list(cur_count),
        "temp_c": [temp_min if temp_min != 0xffffffff else None, temp_max],
        "avg_power_max_mw": avg_power_max, "cur_power_max_mw": cur_power_max,
        "freq_mhz_max": f32(freq_bits_max),
        "ring": ring,
    }


def since(later, earlier):
    """later minus earlier for the cumulative fields of one boot's receipt."""
    if earlier["samples"] > later["samples"]:
        raise ValueError("the earlier snapshot has more samples (another boot?)")
    d = dict(later)
    for key in ("samples", "invalid", "gaps", "update_changes", "sampled_ms", "limited_ms",
                "limited_count"):
        d[key] = later[key] - earlier[key]
    for key in ("actual_ms", "actual_count", "target_count", "current_count"):
        d[key] = [a - b for a, b in zip(later[key], earlier[key])]
    return d


def report(d):
    lines = [f"Wom1GpuPerf v{d['version']}: {d['samples']} samples, {d['gaps']} gaps, "
             f"{d['invalid']} invalid, update_ts changed {d['update_changes']} times"]
    if d["samples"] > 1 and d["update_changes"] == 0:
        lines.append("WARNING: update_ts never changed: the HwDataA fields are not live")
    total = d["sampled_ms"] or 1.0
    lines.append(f"sampled {d['sampled_ms']:.1f} ms; below target {d['limited_ms']:.1f} ms "
                 f"({100.0 * d['limited_ms'] / total:.1f} %), {d['limited_count']} samples")
    lines.append("state  MHz   time_ms  time%  actual  target  current")
    for i in range(PSTATES):
        if d["actual_count"][i] or d["target_count"][i] or d["current_count"][i]:
            mhz = J313_PSTATE_MHZ[i] if i < len(J313_PSTATE_MHZ) else "?"
            lines.append(f"{i:5}  {mhz!s:>4}  {d['actual_ms'][i]:8.1f}  "
                         f"{100.0 * d['actual_ms'][i] / total:5.1f}  {d['actual_count'][i]:6}  "
                         f"{d['target_count'][i]:6}  {d['current_count'][i]:7}")
    lines.append(f"temp_c min/max {d['temp_c'][0]}/{d['temp_c'][1]}; avg_power max "
                 f"{d['avg_power_max_mw']} mW; cur_power max {d['cur_power_max_mw']} mW; "
                 f"freq max {d['freq_mhz_max']:.0f} MHz")
    if d["ring"]:
        last = d["ring"][-1]
        verdict = ("applied" if last["perf_tgt_applied"] else
                   "ignored" if last["perf_tgt_ignored"] else "profile")
        lines.append(f"perf_tgt_utilization {last['perf_tgt']}/{last['perf_tgt_2']} % "
                     f"(Wom1PerfTgtUtilization {verdict})")
    for s in d["ring"][-8:]:
        lines.append(f"  qpc {s['qpc']} a/t/c {s['actual']}/{s['target']}/{s['current']} "
                     f"{s['freq_mhz']:.0f} MHz {s['temp_c']} C {s['avg_power_mw']} mW avg "
                     f"{s['cur_power_mw']} mW cur util {s['utilization']:.1f} "
                     f"pwr_status {s['pwr_status']} ts {s['update_ts']}")
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("path")
    parser.add_argument("--since")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    with open(args.path, "rb") as handle:
        decoded = decode(handle.read())
    if args.since:
        with open(args.since, "rb") as handle:
            decoded = since(decoded, decode(handle.read()))
    print(json.dumps(decoded, indent=1) if args.json else report(decoded))
    return 0


if __name__ == "__main__":
    sys.exit(main())
