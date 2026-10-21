"""AGX compiler enum bit-fields keep their values under the MSVC ABI.

Invariant caught: clang-cl (the closure's compiler for the Asahi units)
follows the MSVC ABI, where an enum bit-field is a signed int. Every
agx_compiler.h field whose enumerators exceed the signed range of its width
reads back negative: AGX_GATHER_B (5) in the 3-bit gather field became -3,
agx_pack's (I->gather << 23) also set bits 26-31 and every GatherBlue/
GatherAlpha returned zero on the AGX (EXP1847/EXP1848 C42J/C42M/C42N on
R11G11B10/RGBA8/RGBA16F); AGX_SIZE_64, AGX_INDEX_REGISTER/UNDEF,
AGX_DIM_2D_MS_ARRAY and AGX_INTERPOLATION_CENTROID/SAMPLE_REGISTER are
truncated the same way. The closure compiles the compiler units against a
projection with unsigned bit-fields; no overflowing enum bit-field may remain.
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
FIELD = re.compile(r"^\s*enum (\w+)\s+(\w+)\s*:\s*(\d+)\s*;", re.M)
ENUM = re.compile(r"enum (\w+) \{(.*?)\};", re.S)
VALUE = re.compile(r"(\w+)\s*=\s*(0x[0-9a-fA-F]+|\d+)")


def local(*parts):
    for base in (ROOT / ".local", ROOT.parents[1] / ".local"):
        path = base.joinpath(*parts)
        if path.exists():
            return path
    return None


def enum_maxima(*texts):
    maxima = {}
    for text in texts:
        for name, body in ENUM.findall(text):
            values = [int(v, 0) for _, v in VALUE.findall(body)]
            if values:
                maxima[name] = max(values)
    return maxima


def overflowing(text, maxima):
    return [(enum, field, int(width)) for enum, field, width in FIELD.findall(text)
            if enum in maxima and maxima[enum] >= 1 << (int(width) - 1)]


class AgxEnumBitfieldsMsAbi(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mesa = local("reference", "mesa")
        generated = local("accelerated-desktop-ad03", "mesa-build", "src", "asahi", "compiler")
        if mesa is None or generated is None:
            raise unittest.SkipTest("pinned Mesa sources not available")
        cls.compiler_dir = mesa / "src/asahi/compiler"
        cls.header = (cls.compiler_dir / "agx_compiler.h").read_text()
        cls.maxima = enum_maxima(cls.header, (generated / "agx_opcodes.h").read_text())
        spec = spec_from_file_location("closure_bitfields", SCRIPT)
        cls.closure = module_from_spec(spec)
        spec.loader.exec_module(cls.closure)

    def test_pinned_header_has_overflowing_enum_bitfields(self):
        found = {field for _, field, _ in overflowing(self.header, self.maxima)}
        self.assertEqual(found, {"size", "type", "dim", "gather", "interpolation"})

    def test_projection_leaves_no_overflowing_enum_bitfield(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / "evidence"
            home.mkdir()
            for header in self.compiler_dir.glob("*.h"):
                shutil.copy2(header, home / header.name)
            unit = home / "agx_pack.c"
            unit.write_text('#include "agx_compiler.h"\n')
            out = Path(tmp) / "out"
            units, include, provenance = self.closure.prepare_unsigned_bitfield_header([unit], out)
            projected = (include / "agx_compiler.h").read_text()
            self.assertEqual(overflowing(projected, self.maxima), [])
            self.assertEqual(units, [include / "agx_pack.c"])
            self.assertEqual(provenance["fields"], ["size", "type", "dim", "gather", "interpolation"])
            self.assertEqual(len(FIELD.findall(projected)), 0)

    def test_native_projection_unsigns_every_overflowing_linker_field(self):
        mesa = local("reference", "mesa")
        linker = (mesa / "src/asahi/lib/agx_linker.h").read_text()
        maxima = enum_maxima((mesa / "src/util/blend.h").read_text(), linker)
        # pipe_blendfactor's values come from implicit increments after
        # explicit ones; INV_SRC1_ALPHA is the invert bit plus SRC1_ALPHA (10)
        maxima["pipe_blendfactor"] = 0x10 | 10
        maxima["pipe_blend_func"] = 4
        fields = overflowing(linker, maxima)
        self.assertEqual({f for _, f, _ in fields},
                         {"rgb_func", "rgb_src_factor", "rgb_dst_factor", "alpha_func",
                          "alpha_src_factor", "alpha_dst_factor", "overlap"})
        native = (ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py").read_text()
        for line in linker.splitlines():
            m = FIELD.match(line)
            if m and (m.group(1), m.group(2), int(m.group(3))) in fields:
                self.assertIn("('" + line + "'", native, line)

    def test_projection_accepts_the_partly_unsigned_compiler_evidence(self):
        # the pinned compiler evidence (EXP1466 core353) already declares size
        # and type unsigned; dim, gather and interpolation are still enums
        text = self.header.replace("enum agx_size size       : 2;", "unsigned size       : 2;").replace(
            "enum agx_index_type type : 3;", "unsigned type : 3;")
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp) / "evidence"
            home.mkdir()
            (home / "agx_compiler.h").write_text(text)
            unit = home / "agx_pack.c"
            unit.write_text('#include "agx_compiler.h"\n')
            _, include, provenance = self.closure.prepare_unsigned_bitfield_header([unit], Path(tmp) / "out")
            self.assertEqual(provenance["fields"], ["dim", "gather", "interpolation"])
            self.assertEqual(overflowing((include / "agx_compiler.h").read_text(), self.maxima), [])

    @unittest.skipIf(shutil.which("clang") is None, "clang not available")
    def test_msvc_abi_reads_the_enum_field_back_negative(self):
        source = ("enum g { G_B = 5 }; struct s { enum g gather : 3; unsigned u : 3; };\n"
                  "_Static_assert(sizeof(struct s) == 4, \"one storage unit\");\n"
                  "int f(struct s *p) { p->gather = G_B; p->u = G_B; return p->gather == G_B; }\n")
        with tempfile.TemporaryDirectory() as tmp:
            unit = Path(tmp) / "bf.c"
            unit.write_text(source)
            result = subprocess.run(["clang", "--target=aarch64-pc-windows-msvc", "-ffreestanding",
                                     "-fsyntax-only", "-Wbitfield-constant-conversion", str(unit)],
                                    capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("changes value from 5 to -3", result.stderr)
        self.assertEqual(result.stderr.count("changes value"), 1)

    def test_the_closure_compiles_against_the_projection(self):
        text = SCRIPT.read_text()
        self.assertIn("prepare_unsigned_bitfield_header(", text)
        self.assertIn("includes.insert(1, bitfield_include)", text)


if __name__ == "__main__":
    unittest.main()
