"""The AGX immediate flags keep their 64-bit values under the MSVC ABI.

Invariant caught: Mesa's generated agx_opcodes.h declares AGX_IMMEDIATE_* as
enumerators `(1ull << i)`. Under the MSVC ABI an enumerator is an int, so a
flag at bit 32 is 0: AGX_IMMEDIATE_SIMD_OP vanishes from the opcode table and
agx_pack never packs a reduction op. Every simd_reduce/simd_prefix then
executes as AND (EXP1306 disassembly "simd r4, r4, and"; FL 11_0 atomics
256/0/32 instead of 256/1515/1920; PSInvocations once per SIMD group). The
closure compiles the generated table against a projected header that declares
the flags as 64-bit constants.
"""
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-asahi-runtime-closure.py"
FLAG = re.compile(r"(AGX_IMMEDIATE_\w+) = \(1ull << (\d+)\),")


def local(*parts):
    for base in (ROOT / ".local", ROOT.parents[1] / ".local"):
        path = base.joinpath(*parts)
        if path.exists():
            return path
    return None


def msvc_check(header_dir, flags):
    """clang targeting the MSVC ABI (freestanding: no Windows SDK on the
    host; util/macros.h reduced to the one macro the header uses): every flag
    equals its bit."""
    asserts = "".join('_Static_assert(%s == (1ull << %s), "%s");\n' % (n, b, n) for n, b in flags)
    with tempfile.TemporaryDirectory() as tmp:
        (Path(tmp) / "util").mkdir()
        (Path(tmp) / "util/macros.h").write_text("#define UNREACHABLE(msg) ((void)0)\n")
        unit = Path(tmp) / "flags.c"
        unit.write_text('#include "agx_opcodes.h"\n' + asserts)
        return subprocess.run(["clang", "--target=aarch64-pc-windows-msvc", "-ffreestanding",
                               "-fsyntax-only", "-I", str(header_dir), "-I", tmp, str(unit)],
                              capture_output=True, text=True)


class AgxImmediateFlagsLlp64(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.generated = local("accelerated-desktop-ad03", "mesa-build", "src", "asahi", "compiler")
        if cls.generated is None or shutil.which("clang") is None:
            raise unittest.SkipTest("generated Mesa compiler sources or clang not available")
        cls.header = cls.generated / "agx_opcodes.h"
        cls.flags = FLAG.findall(cls.header.read_text())
        spec = spec_from_file_location("closure_opcode_flags", SCRIPT)
        cls.closure = module_from_spec(spec)
        spec.loader.exec_module(cls.closure)

    def test_generated_enum_truncates_bit_32_under_msvc(self):
        self.assertGreater(len(self.flags), 32)
        result = msvc_check(self.generated, self.flags)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("AGX_IMMEDIATE_SIMD_OP", result.stderr)

    def test_projected_header_keeps_every_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            unit, include, provenance = self.closure.prepare_opcode_flags_source(
                self.generated / "agx_opcodes.c", Path(tmp))
            result = msvc_check(include, self.flags)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(unit.parent, include)
            self.assertEqual(unit.read_bytes(), (self.generated / "agx_opcodes.c").read_bytes())
            self.assertNotIn("enum agx_immediate", (include / "agx_opcodes.h").read_text())
            self.assertEqual(provenance["flags"], len(self.flags))

    def test_the_closure_compiles_the_projected_table_first(self):
        text = SCRIPT.read_text()
        self.assertIn("prepare_opcode_flags_source(", text)
        self.assertIn("includes.insert(0, opcode_include)", text)


if __name__ == "__main__":
    unittest.main()
