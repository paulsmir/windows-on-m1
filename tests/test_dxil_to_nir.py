"""DXIL (SM 6.0) translates to the DXBC frontend's NIR conventions.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? D3D12 C44: the owner and
the root-signature resolver consume DXIL shaders unchanged only if
agx_dxil_to_nir produces the same binding map (kind, space, register ->
slot), stage I/O locations, shader-buffer/image/texture slot spans and vertex
input bases as dxbc_to_nir does for the same HLSL (FXC SM 5.x vs DXC SM 6.0
fixtures, tests/fixtures/dxil/c44). Found while building it: vertex inputs
all read attribute 0 without driver_location (EXP1297 class), num_ssbos
missed the append counter slot, textures/images_used stayed empty.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_dxbc_to_nir import local_tree, host_flags, ROOT, DXBC

FIXTURES = ROOT / 'tests/fixtures/dxil/c44'
DXIL = ROOT / 'drivers/apple-agx/mesa/dxil'
MESA_LIBS = ("src/asahi/compiler/libasahi_compiler.a", "src/asahi/isa/libagx2_disasm.a",
             "src/asahi/layout/libasahi_layout.a", "src/compiler/spirv/libvtn.a", "src/compiler/nir/libnir.a",
             "src/compiler/libcompiler.a", "src/util/libmesa_util.a", "src/util/libmesa_util_simd.a",
             "src/c11/impl/libmesa_util_c11.a", "src/util/blake3/libblake3.a")


class DxilToNir(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = local_tree("accelerated-desktop-ad03", "mesa-build")
        mesa = local_tree("reference", "mesa")
        spirv = local_tree("reference", "dxil-spirv")
        archives = local_tree("dxil-work", "build-dxil-spirv")
        if not build or not mesa or not spirv or not archives or not (build / MESA_LIBS[3]).exists():
            raise unittest.SkipTest("host Mesa/libvtn or dxil-spirv build not available")
        cls.tmp = tempfile.TemporaryDirectory(prefix="dxil-to-nir-")
        tmp = Path(cls.tmp.name)
        cls.dxil, cls.dxbc = tmp / "dxil_to_nir_fixture", tmp / "dxbc_to_nir_fixture"
        subprocess.run(["clang++", "-std=c++17", "-c", "-I", str(spirv), str(DXIL / "agx_dxil_checked.cpp"),
                        "-o", str(tmp / "checked.o")], check=True, capture_output=True)
        common = ["-std=c11", "-Werror=implicit-function-declaration", *host_flags(build),
                  "-I", str(build / "src/asahi"), "-I", str(mesa / "src/asahi"), "-I", str(mesa / "src"),
                  "-I", str(build / "src"), "-I", str(DXBC), "-I", str(DXIL), "-I", str(spirv)]
        libs = [str(build / p) for p in MESA_LIBS]
        dlibs = [str(p) for p in sorted(archives.glob("*.a"))]
        link = ["-L/opt/homebrew/opt/zstd/lib", "-lzstd", "-lz", "-lpthread"]
        subprocess.run(["clang", *common, str(ROOT / "tests/dxil_to_nir_fixture.c"), str(DXIL / "agx_dxil_to_nir.c"), str(DXIL / "agx_dxil_program.c"),
                        str(DXBC / "dxbc_to_nir.c"), str(DXBC / "dxbc_parse.c"), str(tmp / "checked.o"),
                        "-o", str(cls.dxil), *libs, *dlibs, *dlibs, *link, "-lc++",
                        "-L/opt/homebrew/lib", "-lSPIRV-Tools-shared"], check=True, capture_output=True)
        subprocess.run(["clang", *common, str(ROOT / "tests/dxbc_to_nir_fixture.c"), str(DXBC / "dxbc_to_nir.c"),
                        str(DXBC / "dxbc_parse.c"), "-o", str(cls.dxbc), *libs, *link,
                        "-L/opt/homebrew/lib", "-lSPIRV-Tools-shared"], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def translate(self, binary, path, *args):
        r = subprocess.run([str(binary), str(path), *args], capture_output=True, text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr[-3000:])
        self.assertNotIn("REFUSED", r.stdout, r.stdout)
        bindings = sorted(re.findall(r"^BINDING (.*)$", r.stdout, re.M))
        summary = re.search(r"^TRANSLATED (.*)$", r.stdout, re.M)
        self.assertIsNotNone(summary, r.stdout)
        fields = dict(re.findall(r"([a-z-]+) (\S+)", summary.group(1)))
        return bindings, fields

    def parity(self, name, ignore=()):
        dxil_bindings, dxil = self.translate(self.dxil, FIXTURES / f"{name}.dxil")
        dxbc_bindings, dxbc = self.translate(self.dxbc, FIXTURES / f"{name}.dxbc", "--d3d12-heap")
        for key in ("stage", "inputs", "outputs", "textures", "ssbos", "images", "vs-input-bases",
                    "early-output-stores", "shared-derefs", "agx-unlowered-bitfield", "gathered-images", "image-span"):
            if key not in ignore:
                self.assertEqual(dxil[key], dxbc[key], f"{name} {key}: DXIL {dxil} DXBC {dxbc}")
        if dxbc_bindings:
            self.assertEqual(dxil_bindings, dxbc_bindings, name)
        return dxil

    def test_no_vulkan_dispatch_base_reaches_the_backend(self):
        # EXP1857: spirv_to_nir lowers GlobalInvocationId to the global id
        # plus load_base_global_invocation_id (a Vulkan dispatch base); the
        # AGX backend aborts on it ("Unhandled intrinsic").
        r = subprocess.run([str(self.dxil), str(FIXTURES / "par_cs.dxil"), "--print-final"], capture_output=True,
                           text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        self.assertIn("TRANSLATED", r.stdout)
        for intrinsic in ("load_base_global_invocation_id", "load_base_workgroup_id"):
            self.assertNotIn(intrinsic, r.stdout)

    def test_compute_resource_classes_match_the_dxbc_slots(self):
        cs = self.parity("par_cs")
        self.assertEqual(cs["block"], "8x8x1")
        self.assertEqual(cs["shared"], "256")  # groupshared uint[64] (FXC reports 512)

    def test_vertex_inputs_read_their_registers(self):
        self.parity("par_vs")

    def test_pixel_targets_depth_and_comparison_match(self):
        self.parity("par_ps")

    def test_program_part_translates_like_its_container(self):
        # The D3D12 runtime hands CreateShader the DXIL program part (version
        # token, size, "DXIL", bitcode), not the container.
        data = (FIXTURES / "par_cs.dxil").read_bytes()
        count = int.from_bytes(data[28:32], "little")
        for i in range(count):
            offset = int.from_bytes(data[32 + 4 * i:36 + 4 * i], "little")
            if data[offset:offset + 4] == b"DXIL":
                size = int.from_bytes(data[offset + 4:offset + 8], "little")
                part = Path(self.tmp.name) / "par_cs.program"
                part.write_bytes(data[offset + 8:offset + 8 + size])
                break
        self.assertEqual(self.translate(self.dxil, part), self.translate(self.dxil, FIXTURES / "par_cs.dxil"))

    def test_unbounded_and_oversized_ranges_read_the_heap(self):
        # C47 for DXIL: unbounded SRV/sampler ranges and one larger than the
        # free slots become heap ranges numbered as the DXBC frontend numbers
        # them (samplers first), read through marked bindless handles and the
        # sampler map in cb14 -- the root-signature resolver fills heap_first
        # by those numbers.
        self.parity("heap_ps")
        r = subprocess.run([str(self.dxil), str(FIXTURES / "heap_ps.dxil"), "--print-final"], capture_output=True,
                           text=True, timeout=120)
        self.assertEqual(r.stdout.count("@bindless_image_agx"), 4)
        self.assertIn("sampler_handle", r.stdout)

    def test_unbounded_uav_ranges_read_the_heap(self):
        # Typed and raw UAV ranges become heap ranges after the sampler and
        # SRV ranges; typed accesses carry no format so the descriptor's
        # format applies (the DXBC frontend's typed UAV contract).
        self.parity("heap_cs")
        r = subprocess.run([str(self.dxil), str(FIXTURES / "heap_cs.dxil"), "--print-final"], capture_output=True,
                           text=True, timeout=120)
        self.assertEqual(r.stdout.count("@bindless_image_agx"), 5)
        self.assertEqual(r.stdout.count("@bindless_image_atomic"), 1)
        for line in r.stdout.splitlines():
            if "image_dim=2D" in line:
                self.assertIn("format=none", line)

    def test_register_range_takes_consecutive_slots(self):
        self.parity("par_range_ps")


if __name__ == "__main__":
    unittest.main()
