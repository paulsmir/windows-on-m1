"""DXBC parser (drivers/apple-agx/mesa/dxbc) against the Microsoft compiler.

The fixtures under tests/fixtures/dxbc are HLSL sources compiled by fxc
(Windows SDK 10.0.26100, /O3) into objects (.dxbc) with fxc's own listing
(.asm) of the same object.  The parser must decode every instruction of
every object -- shader models 4.0, 4.1, 5.0 and 5.1, all six stages, UAVs,
atomics, groupshared memory, class linkage, doubles, tessellation phases --
consuming exactly the DWORDs each instruction declares, and its rendering of
each instruction (mnemonic, operand registers, indices, relative addressing,
component selection, modifiers) must equal fxc's listing line.

Invariant caught: any misdecoded token field (operand type, index
representation, swizzle/mask, extended opcode or operand token, declaration
length) shifts or changes an operand and breaks the equality.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DXBC = ROOT / "drivers/apple-agx/mesa/dxbc"
FIXTURES = ROOT / "tests/fixtures/dxbc"
NAMES = ["vs40", "ps40", "gs40", "ps41", "cs40", "cs41", "cs50", "hs50", "hs50p", "ds50", "ps50", "ps50b", "gs50", "ps51"]
# the shaders the D3D smoke tools compile at run time (tools/dxbc/extract-smoke-shaders.py)
NAMES += ["smoke/" + line.split()[0] for line in
          (FIXTURES / "smoke/targets.txt").read_text().splitlines() if line.strip()]
TESTS = {"if", "breakc", "continuec", "retc", "discard", "callc"}


def split_top(text, sep):
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return [p.strip() for p in parts]


def mnemonic(raw):
    name = re.sub(r"\([^)]*\)", "", raw).lower()
    while True:
        for suffix in ("_sat", "_indexable", "_aoffimmi", "_uint", "_opc"):
            if name.endswith(suffix):
                name = name[: -len(suffix)]
                break
        else:
            break
    base = name.rsplit("_", 1)[0] if "_" in name else name
    if base in TESTS and name.endswith(("_nz", "_z")):
        name = base
    if name.startswith("sync"):
        name = "sync"
    if name.startswith("dcl_resource_") and name not in ("dcl_resource_raw", "dcl_resource_structured"):
        name = "dcl_resource"
    if name.startswith("dcl_uav_typed"):
        name = "dcl_uav_typed"
    return name


def immediates(operand):
    m = re.fullmatch(r"(-?)([ld])\((.*)\)", operand)
    if not m:
        return operand
    return f"{m.group(1)}{m.group(2)}{len(split_top(m.group(3), ','))}"


def listing(path):
    lines = []
    for line in path.read_text(errors="replace").splitlines():
        text = line.strip()
        if not text or text.startswith("//") or text.startswith("{"):
            continue
        if re.fullmatch(r"(vs|ps|gs|hs|ds|cs)_\d_\d", text):
            continue
        # the mnemonic ends at the first space outside parentheses
        depth, end = 0, len(text)
        for i, ch in enumerate(text):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == " " and depth == 0:
                end = i
                break
        name = mnemonic(text[:end])
        if name.startswith("dcl_") or name.startswith("hs_") or name == "customdata":
            lines.append(name)
            continue
        ops = [immediates(o) for o in split_top(text[end:], ",")]
        lines.append(name + (" " + ", ".join(ops) if ops else ""))
    return lines


class DxbcParse(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="dxbc-parse-")
        cls.binary = Path(cls.tmp.name) / "dxbc_parse_fixture"
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I", str(DXBC),
                        str(ROOT / "tests/dxbc_parse_fixture.c"), str(DXBC / "dxbc_parse.c"),
                        "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_fixtures_match_fxc_listings(self):
        for name in NAMES:
            with self.subTest(name=name):
                result = subprocess.run([str(self.binary), str(FIXTURES / f"{name}.dxbc")],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                ours = result.stdout.splitlines()
                theirs = listing(FIXTURES / f"{name}.asm")
                for i, (a, b) in enumerate(zip(ours, theirs)):
                    self.assertEqual(a, b, f"{name} instruction {i}")
                self.assertEqual(len(ours), len(theirs), name)

    def test_truncated_and_mutated_programs_stay_in_bounds(self):
        # The UMD parses bytecode handed over by the runtime: a malformed
        # program must be refused (or parsed) without an out-of-bounds read.
        for name in NAMES:
            with self.subTest(name=name):
                result = subprocess.run([str(self.binary), "--mutate", str(FIXTURES / f"{name}.dxbc")],
                                        capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-2000:])
                m = re.search(r"MUTATED ok (\d+) refused (\d+)", result.stdout)
                self.assertTrue(m and int(m.group(2)) > 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
