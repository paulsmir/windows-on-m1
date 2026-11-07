#!/usr/bin/env python3
"""Per-package performance regression gate (EXP2209 lessons: a stale packaged
OpenGL ICD dropped CS 1.6 to software GL, and a DWM device re-creation loop
cost it 2.5x, while the correctness suite stayed 119/0).

Usage:
  agx_perf_gate.py [--baseline investigation/perf-baseline.json]
                   [--gm UMD.log ...] [--cs CS-BENCH.LOG ...] [--tolerance 2.0]

--gm: GravityMark HD PERF UMD traces (summarised with agx_gm_perf's 10 + 60 s
window); --cs: cs-bench.log files of the standard CS 1.6 benchmark (UTF-16 or
UTF-8, the "<frames> frames <seconds> seconds <fps> fps" console line). Each
metric present in the baseline is compared: a measurement more than
--tolerance percent below its baseline is a FAIL; a missing measurement for a
metric that was requested is a FAIL. Prints one PERF_GATE line per metric and
a final PERF_GATE_PASS or PERF_GATE_FAIL; exit 0 / 1.
"""
import argparse
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import agx_gm_perf  # noqa: E402

CS_LINE = re.compile(r"(\d+) frames ([0-9.]+) seconds ([0-9.]+) fps")


def read_text(path):
    raw = Path(path).read_bytes()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff") or raw[1:2] == b"\x00":
        return raw.decode("utf-16", errors="replace")
    return raw.decode("utf-8", errors="replace")


def cs_fps(path):
    hits = CS_LINE.findall(read_text(path))
    return float(hits[-1][2]) if hits else None


def gm_fps(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        try:
            return agx_gm_perf.summarize(handle)["fps"]
        except ValueError:
            return None


def gate(baseline, measured, tolerance):
    """baseline/measured: {metric: fps or None}; returns (ok, lines)."""
    lines, ok = [], True
    for metric, values in sorted(measured.items()):
        base = baseline.get(metric, {}).get("fps")
        if base is None:
            lines.append(f"PERF_GATE {metric} NO_BASELINE values={values}")
            continue
        good = [v for v in values if v is not None]
        if not good or len(good) != len(values):
            ok = False
            lines.append(f"PERF_GATE {metric} FAIL missing-measurement values={values} baseline={base}")
            continue
        worst = min(good)
        delta = 100.0 * (worst - base) / base
        verdict = "PASS" if delta >= -tolerance else "FAIL"
        ok = ok and verdict == "PASS"
        lines.append(f"PERF_GATE {metric} {verdict} worst={worst:.3f} baseline={base:.3f} delta={delta:+.2f}% "
                     f"tolerance=-{tolerance:.1f}% source={baseline[metric].get('source', '?')}")
    lines.append("PERF_GATE_PASS" if ok else "PERF_GATE_FAIL")
    return ok, lines


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default=str(HERE.parent / "investigation/perf-baseline.json"))
    ap.add_argument("--gm", nargs="*", default=[])
    ap.add_argument("--cs", nargs="*", default=[])
    ap.add_argument("--tolerance", type=float, default=2.0)
    a = ap.parse_args(argv)
    baseline = json.loads(Path(a.baseline).read_text(encoding="utf-8"))
    measured = {}
    if a.gm:
        measured["gravitymark_hd_perf"] = [gm_fps(p) for p in a.gm]
    if a.cs:
        measured["cs16_benchmark"] = [cs_fps(p) for p in a.cs]
    ok, lines = gate(baseline, measured, a.tolerance)
    print("\n".join(lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
