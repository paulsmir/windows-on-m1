"""DXBC -> NIR translator (drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c).

Every shader model 4.0/4.1 fixture (VS, PS, GS; constant buffers, the
immediate constant buffer, indexable temporaries, loops, switch, discard,
every 4.x texture operation, system values, geometry emit) and the SM 5
compute and geometry fixtures (UAVs, groupshared memory, atomics, counters,
barriers, bit operations; instancing and streams) must translate
into NIR that validates after the tgsi_to_nir finalization sequence,
Asahi's I/O lowering and agx_preprocess_nir, using the AGX compiler's NIR
options.  The remaining SM 5 fixtures (tessellation, class linkage,
doubles, attribute evaluation, SM 5.1 ranges) must be refused cleanly until
the translator grows to cover them.

Invariant caught: invalid NIR (code after a jump, component-count or type
mismatches, unbalanced control flow, undeclared registers) or a crash on a
program the frontend would hand over.

Built against the host Mesa tree the UMD's Mesa is cut from (26.3.0-devel
9aa1215f, .local/accelerated-desktop-ad03/mesa-build); skipped where absent.
"""
import os
from pathlib import Path
import shlex
import subprocess
import json
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DXBC = ROOT / "drivers/apple-agx/mesa/dxbc"
FIXTURES = ROOT / "tests/fixtures/dxbc"
TRANSLATED = {"vs40": "MESA_SHADER_VERTEX", "ps40": "MESA_SHADER_FRAGMENT",
              "gs40": "MESA_SHADER_GEOMETRY", "ps41": "MESA_SHADER_FRAGMENT",
              # cs_4_x (compute on 10.x hardware): raw/structured buffers,
              # groupshared memory written at SV_GroupIndex, group sync
              "cs40": "MESA_SHADER_COMPUTE", "cs41": "MESA_SHADER_COMPUTE",
              # SM 5: UAVs, groupshared memory, atomics, counters, sync, bit ops
              "cs50": "MESA_SHADER_COMPUTE",
              # SM 5: geometry instancing and two output streams
              "gs50": "MESA_SHADER_GEOMETRY",
              # SM 5 pixel: attribute evaluation, gather4_c/_po, UAV atomics,
              # coverage in and out, conservative depth
              "ps50b": "MESA_SHADER_FRAGMENT"}
# tessellation stages, class linkage / doubles / evaluation, SM 5.1 ranges
REFUSED = ["hs50", "ds50", "ps50", "ps51"]
# the shaders the D3D smoke tools compile at run time (tools/dxbc/extract-smoke-shaders.py)
STAGES = {"vs": "MESA_SHADER_VERTEX", "gs": "MESA_SHADER_GEOMETRY", "ps": "MESA_SHADER_FRAGMENT"}
for line in (FIXTURES / "smoke/targets.txt").read_text().splitlines():
    if line.strip():
        name, target = line.split()
        TRANSLATED["smoke/" + name] = STAGES[target[:2]]


def local_tree(*parts):
    for base in ([Path(os.environ["AGX_LOCAL_TREE"])] if "AGX_LOCAL_TREE" in os.environ else []) + \
            [ROOT / ".local", ROOT.parents[1] / ".local"]:
        path = base.joinpath(*parts)
        if path.exists():
            return path
    return None


def host_flags(build):
    """Defines and include paths libnir.a was compiled with."""
    commands = json.loads((build / "compile_commands.json").read_text())
    for entry in commands:
        if entry["file"].endswith("nir_validate.c"):
            args = shlex.split(entry["command"])
            directory = Path(entry["directory"])
            # macOS has no <endian.h>; util/u_endian.h falls back to the
            # compiler's byte-order macros without HAVE_ENDIAN_H
            flags = [a for a in args if a.startswith("-D") and a != "-DHAVE_ENDIAN_H"]
            for a in args:
                if a.startswith("-I"):
                    path = Path(a[2:])
                    flags.append("-I" + str(path if path.is_absolute() else directory / path))
            return flags
    raise AssertionError("no libnir compile command")


class DxbcToNir(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = local_tree("accelerated-desktop-ad03", "mesa-build")
        mesa = local_tree("reference", "mesa")
        if not build or not mesa:
            raise unittest.SkipTest("host Mesa build not available")
        cls.tmp = tempfile.TemporaryDirectory(prefix="dxbc-to-nir-")
        cls.binary = Path(cls.tmp.name) / "dxbc_to_nir_fixture"
        libs = [build / p for p in (
            "src/asahi/compiler/libasahi_compiler.a", "src/asahi/isa/libagx2_disasm.a",
            "src/asahi/layout/libasahi_layout.a", "src/compiler/nir/libnir.a",
            "src/compiler/libcompiler.a", "src/util/libmesa_util.a",
            "src/util/libmesa_util_simd.a", "src/c11/impl/libmesa_util_c11.a",
            "src/util/blake3/libblake3.a")]
        subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Werror=implicit-function-declaration",
                        *host_flags(build), "-I", str(build / "src/asahi"), "-I", str(mesa / "src/asahi"),
                        "-I", str(DXBC), str(ROOT / "tests/dxbc_to_nir_fixture.c"),
                        str(DXBC / "dxbc_to_nir.c"), str(DXBC / "dxbc_parse.c"),
                        "-o", str(cls.binary), *map(str, libs),
                        "-L/opt/homebrew/opt/zstd/lib", "-lzstd", "-lz", "-lpthread"],
                       check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_fixture(self, name):
        return subprocess.run([str(self.binary), str(FIXTURES / f"{name}.dxbc")],
                              capture_output=True, text=True, timeout=60)

    def test_shader_model_4_fixtures_translate_and_validate(self):
        for name, stage in TRANSLATED.items():
            with self.subTest(name=name):
                result = self.run_fixture(name)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-3000:])
                self.assertIn(f"TRANSLATED stage {stage}", result.stdout)

    def test_resource_bindings_fit_the_gallium_ranges(self):
        # A shader buffer or image slot beyond PIPE_MAX_SHADER_BUFFERS (32) /
        # PIPE_MAX_SHADER_IMAGES (64) cannot be bound through
        # set_shader_buffers / set_shader_images: an append/consume counter
        # placed there is never backed.  UAV registers are u0..u7 on the
        # D3D11.0 DDI (D3D11_PS_CS_UAV_REGISTER_COUNT).
        for name in TRANSLATED:
            with self.subTest(name=name):
                result = self.run_fixture(name)
                m = re.search(r"ssbos (\d+) images (\d+)", result.stdout)
                self.assertTrue(m, result.stdout + result.stderr[-2000:])
                self.assertLessEqual(int(m.group(1)), 16, "shader buffers: 8 UAVs + 8 counters")
                self.assertLessEqual(int(m.group(2)), 8, "images: 8 typed UAVs")
        # cs50 uses an append/consume counter
        m = re.search(r"ssbos (\d+)", self.run_fixture("cs50").stdout)
        self.assertGreater(int(m.group(1)), 8)

    def test_compute_block_and_groupshared_size(self):
        # Dispatch launches the shader's own numthreads block
        # (pipe_grid_info.block) and Asahi sizes threadgroup memory from
        # shared_size: both come from dcl_thread_group / dcl_tgsm_*.
        for name, expected in {"cs40": "block 64x1x1 shared 256 shared-derefs 0",
                               "cs41": "block 8x4x1 shared 0 shared-derefs 0",
                               "cs50": "block 8x8x1 shared 1028 shared-derefs 0"}.items():
            with self.subTest(name=name):
                self.assertIn(expected, self.run_fixture(name).stdout)

    def test_shader_model_5_fixtures_are_refused_cleanly(self):
        for name in REFUSED:
            with self.subTest(name=name):
                result = self.run_fixture(name)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr[-3000:])
                self.assertTrue(result.stdout.startswith("REFUSED "), result.stdout)


if __name__ == "__main__":
    unittest.main()
