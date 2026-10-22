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
        cls.mesa = mesa
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

    def test_geometry_shader_matches_the_dxbc_layout(self):
        # Array inputs at the signature registers, emit/cut per stream 0 and
        # the same primitive declarations the owner's GS path reads.
        self.parity("par_gs")
        header = {}
        for kind, path, extra in (("dxil", self.dxil, ()), ("dxbc", self.dxbc, ("--d3d12-heap",))):
            r = subprocess.run([str(path), str(FIXTURES / f"par_gs.{kind}"), *extra, "--print-final"],
                               capture_output=True, text=True, timeout=120)
            self.assertEqual(r.returncode, 0, r.stderr[-2000:])
            header[kind] = [line for line in r.stdout.splitlines()
                            if line.split(":")[0] in ("output_primitive", "input_primitive", "vertices_out",
                                                      "invocations", "vertices_in", "uses_end_primitive",
                                                      "active_stream_mask")]
            header[kind + "_emits"] = r.stdout.count("@emit_vertex (stream_id=0)")
        self.assertEqual(len(header["dxil"]), 7, header)
        self.assertEqual(header["dxil"], header["dxbc"])
        self.assertEqual(header["dxil_emits"], header["dxbc_emits"])

    def final(self, name):
        r = subprocess.run([str(self.dxil), str(FIXTURES / f"{name}.dxil"), "--print-final"], capture_output=True,
                           text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        self.assertIn("TRANSLATED", r.stdout)
        return r.stdout

    def test_hull_and_domain_shaders_link_by_register(self):
        # Control points link hull to domain shader at VAR0 + register, also
        # SV_Position (C37/EXP1834); the domain shader reads the tess factors
        # as inputs (Asahi's glsl_tess_levels_as_inputs, poly_nir_lower_tes),
        # the DXBC frontend as system values, hence its narrower input mask.
        self.parity("par_hs")
        self.parity("par_ds", ignore=("inputs",))
        hs, ds = self.final("par_hs"), self.final("par_ds")
        self.assertIn("store_per_vertex_output", hs)
        self.assertNotRegex(hs, r"store_per_vertex_output .*VARYING_SLOT_POS")
        self.assertNotRegex(ds, r"load_per_vertex_input .*VARYING_SLOT_POS")
        field = lambda text, key: re.search(rf"^{key}: (\S+)", text, re.M).group(1)
        self.assertEqual(field(hs, "patch_outputs_written"), field(ds, "patch_inputs_read"))
        self.assertEqual(field(hs, "tcs_vertices_out"), "3")

    # intrinsics that reach the AGX backend only after a pass outside Asahi's
    # sources: the tess coordinate (nir_lower_tess_coord_z in
    # poly_nir_lower_tes), the fragment position (the owner's
    # dxbc_nir_lower_fragment_position before create_fs_state)
    LOWERED_OUTSIDE_ASAHI = {"load_tess_coord", "load_frag_coord"}

    def test_every_final_intrinsic_has_an_asahi_handler(self):
        # EXP1857: load_base_global_invocation_id passed every offline check
        # and aborted the AGX backend on the guest ("Unhandled intrinsic");
        # no Asahi source (compiler, lib, poly, Gallium driver) names it.
        known = set()
        for tree in ("src/asahi", "src/gallium/drivers/asahi", "src/poly"):
            for source in (self.mesa / tree).rglob("*.[ch]"):
                known.update(re.findall(r"nir_intrinsic_([a-z0-9_]+)", source.read_text(errors="replace")))
        self.assertIn("ballot", known)
        for fixture in sorted(FIXTURES.glob("*.dxil")):
            final = self.final(fixture.stem)
            unhandled = set(re.findall(r"@([a-z0-9_]+)", final)) - known - self.LOWERED_OUTSIDE_ASAHI
            self.assertEqual(unhandled, set(), fixture.name)

    def test_wave_intrinsics_lower_to_agx_subgroup_operations(self):
        # C48: every SM 6.0 wave and quad intrinsic (32-wide waves)
        cs, ps = self.final("wave_cs"), self.final("wave_ps")
        for op in ("@ballot", "@reduce", "@exclusive_scan", "@read_invocation", "@load_subgroup_invocation"):
            self.assertIn(op, cs)
        for op in ("@quad_swap_horizontal", "@quad_swap_vertical", "@quad_swap_diagonal", "@quad_broadcast"):
            self.assertIn(op, ps)

    def test_runtime_converted_passthrough_hull_shader_translates(self):
        # EXP1858: under R1 the runtime converts DXBC to DXIL (dxilconv); a
        # hull shader without a control point phase names no entry function
        # and was refused ("0 entry points"), removing the device at the draw.
        self.parity("conv_hs", ignore=("inputs",))
        self.parity("conv_ds", ignore=("inputs",))
        self.assertIn("store_per_vertex_output", self.final("conv_hs"))

    def xfb(self, name, decl):
        r = subprocess.run([str(self.dxil), str(FIXTURES / f"{name}.dxil"), f"--xfb={decl}"], capture_output=True,
                           text=True, timeout=120)
        self.assertEqual(r.returncode, 0, r.stderr[-2000:])
        return re.search(r"^XFB .*$", r.stdout, re.M).group(0)

    def test_runtime_converted_stream_output_captures_signature_registers(self):
        # EXP1858 C39A: the R1 runtime converts the DXBC vertex shader to
        # DXIL, so its stream output (SV_Position register 0, COLOR register 1,
        # stride 32) must be built on the DXIL translation; SV_Position is a
        # builtin there and only the output signature names its register.
        self.parity("conv_vs", ignore=("inputs",))
        self.assertEqual(self.xfb("conv_vs", "1;0.0.4.0.0,1.0.4.0.4;8"),
                         "XFB OK outputs 2 loc0.f+0 loc33.f+16 stride 32")
        self.assertEqual(self.xfb("conv_vs", "1;1.2.2.0.0;2"), "XFB OK outputs 1 loc33.c+0 stride 8")
        # without the signature's position register, or an output nothing writes
        self.assertTrue(self.xfb("conv_vs", "0;0.0.4.0.0,1.0.4.0.4;8").startswith("XFB REFUSED"))
        self.assertTrue(self.xfb("conv_vs", "1;0.0.4.0.0,2.0.4.0.4;8").startswith("XFB REFUSED"))

    def test_register_range_takes_consecutive_slots(self):
        self.parity("par_range_ps")


if __name__ == "__main__":
    unittest.main()
