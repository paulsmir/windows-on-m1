#!/usr/bin/env python3
"""Run the DXBC parser and the DXBC -> NIR translator over a corpus of DXBC
containers (for example the shaders embedded in Windows' d2d1.dll,
dwmcore.dll and dwmscene.dll, extracted locally; such corpora stay outside
the repository) and summarize: translated, refused (with reasons), parse
failures, crashes.

usage: scan-corpus.py <corpus dir> <dxbc_parse_fixture> <dxbc_to_nir_fixture>
(the two harnesses tests/test_dxbc_parse.py and tests/test_dxbc_to_nir.py
build from tests/dxbc_parse_fixture.c and tests/dxbc_to_nir_fixture.c)
"""
import collections
from pathlib import Path
import struct
import subprocess
import sys

STAGES = "ps vs gs hs ds cs".split()


def model(data):
    count = struct.unpack_from("<I", data, 28)[0]
    for i in range(count):
        offset = struct.unpack_from("<I", data, 32 + 4 * i)[0]
        tag = data[offset:offset + 4]
        if tag in (b"SHEX", b"SHDR"):
            v = struct.unpack_from("<I", data, offset + 8)[0]
            kind = (v >> 16) & 0xffff
            return f"{STAGES[kind] if kind < 6 else '?'}_{(v >> 4) & 0xf}_{v & 0xf}"
        if tag == b"DXIL":
            return "dxil"
    return "none"


def main():
    corpus, parse_bin, nir_bin = map(Path, sys.argv[1:4])
    models, results, reasons = collections.Counter(), collections.Counter(), collections.Counter()
    for path in sorted(corpus.glob("*.dxbc")):
        models[model(path.read_bytes())] += 1
        parsed = subprocess.run([str(parse_bin), str(path)], capture_output=True, text=True)
        if parsed.returncode:
            results["parse_fail"] += 1
            reasons["parse: " + parsed.stdout.strip()[:60]] += 1
            continue
        run = subprocess.run([str(nir_bin), str(path)], capture_output=True, text=True)
        last = (run.stdout.strip().splitlines() or ["?"])[-1]
        if run.returncode:
            results["crash"] += 1
            reasons["crash: " + (run.stderr.strip().splitlines() or ["?"])[-1][:80]] += 1
        elif last.startswith("TRANSLATED"):
            results["translated"] += 1
        else:
            results["refused"] += 1
            reasons[last[:80]] += 1
    print("models", dict(models))
    print("results", dict(results))
    for reason, n in reasons.most_common():
        print(n, reason)


if __name__ == "__main__":
    main()
