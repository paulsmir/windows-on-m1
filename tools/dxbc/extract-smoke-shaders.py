#!/usr/bin/env python3
"""Extract the HLSL shaders embedded in the D3D smoke tools
(drivers/apple-agx/windows/icd/agx_*.c, `static const char name[] = "...";`)
into tests/fixtures/dxbc/smoke/<tool>_<name>.hlsl, with the target each
tool compiles them for (vs/gs/ps by name prefix), so that the DXBC parser
and translator are checked offline on the shaders the hardware smokes run.
"""
import ast
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
ICD = ROOT / "drivers/apple-agx/windows/icd"
OUT = ROOT / "tests/fixtures/dxbc/smoke"


def literals(body):
    # concatenated C string literals -> text
    return "".join(ast.literal_eval(m.group(0)) for m in re.finditer(r'"(?:[^"\\]|\\.)*"', body))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    targets = []
    for source in sorted(ICD.glob("agx_*.c")):
        text = source.read_text()
        if "D3DCompile" not in text:
            continue
        tool = source.stem.removeprefix("agx_")
        # C comments may sit between the literal pieces
        piece = r'(?:\s*(?:/\*.*?\*/\s*)*"(?:[^"\\]|\\.)*")'
        for m in re.finditer(r"static const char (\w+)\[\]\s*=((?:" + piece + r")+)\s*;", text, re.S):
            name, body = m.group(1), m.group(2)
            stage = name[:2]
            if stage not in ("vs", "gs", "ps"):
                continue
            path = OUT / f"{tool}_{name}.hlsl"
            path.write_text(literals(body))
            targets.append((path.stem, f"{stage}_4_0"))
    (OUT / "targets.txt").write_text("".join(f"{n} {t}\n" for n, t in targets))
    print(len(targets), "shaders")


if __name__ == "__main__":
    sys.exit(main())
