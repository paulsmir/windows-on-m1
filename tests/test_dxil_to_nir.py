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
                        str(DXBC / "dxbc_parse.c"), str(DXBC / "dxbc_tir_gs.c"), "-o", str(cls.dxbc), *libs, *link,
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

    def test_dynamic_srv_range_ends_at_the_next_dynamic_range(self):
        # Night Raid GT2 (EXP1879G): a dynamically indexed Cube range filled
        # every used slot after its base, ran into the following dynamic 2D
        # range and refused the pixel shader (owner reason 2, SRV dimensions).
        self.parity("dynrange_ps")
        r = subprocess.run([str(self.dxil), str(FIXTURES / "dynrange_ps.dxil")], capture_output=True, text=True,
                           timeout=120)
        line = re.search(r"^DIMENSIONS (.*)$", r.stdout, re.M).group(1)
        expected = "srv 1 uav 1 t0=1 " + " ".join(f"t{i}=2" for i in range(1, 13)) + " t13=1 t14=1 t15=1"
        self.assertEqual(line, expected)

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

    def test_rasterizer_ordered_views_take_the_pixel_interlock(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? 12_1 ROVs: without
        # FragmentShaderPixelInterlockEXT spirv_to_nir refuses dxil-spirv's
        # ROV module, and AGX compiles begin_invocation_interlock (wait_pix)
        # but no end_invocation_interlock (Asahi holds the section to the end
        # of the shader), so an end reaching the backend aborts it.
        final = self.final("rov_ps")
        self.assertIn("pixel_interlock_ordered: true", final)
        self.assertEqual(final.count("@begin_invocation_interlock"), 1)
        self.assertNotIn("end_invocation_interlock", final)

    def test_wave_intrinsics_lower_to_agx_subgroup_operations(self):
        # C48: every SM 6.0 wave and quad intrinsic (32-wide waves)
        cs, ps = self.final("wave_cs"), self.final("wave_ps")
        for op in ("@ballot", "@reduce", "@exclusive_scan", "@read_invocation", "@load_subgroup_invocation"):
            self.assertIn(op, cs)
        for op in ("@quad_swap_horizontal", "@quad_swap_vertical", "@quad_swap_diagonal", "@quad_broadcast"):
            self.assertIn(op, ps)

    def test_typed_uav_loads_keep_the_resource_format(self):
        # EXP1867: C49A's runtime-converted shader (dxilconv) loaded RGBA16F
        # and RGBA8 UAVs as one channel: dxil-spirv declared every read typed
        # UAV R32 (r32_float/r32_uint). Loads are formatless, as the DXBC
        # frontend's that passed on J313.
        self.parity("conv_c49_cs")
        final = self.final("conv_c49_cs")
        formats = re.findall(r"@image_load \(.*format=(\w+)", final)
        self.assertEqual(len(formats), 4)
        self.assertEqual(set(formats), {"none"})

    def test_view_id_reads_view_zero(self):
        # C50: SM 6.1 with ViewInstancingTier NOT_SUPPORTED: SV_ViewID must
        # read 0 (View Instancing spec); dxil-spirv without multiview emits
        # the constant, so no view-index intrinsic reaches the AGX backend
        # (Asahi's Gallium driver has no load_view_index handler).
        final = self.final("view_ps")
        self.assertNotIn("view_index", final)
        self.assertRegex(final, r"vec4 %\d+, %\d+, %\d+ \(0x0\), %\d+ \(0x3f800000\)")

    def test_runtime_converted_passthrough_hull_shader_translates(self):
        # EXP1858: under R1 the runtime converts DXBC to DXIL (dxilconv); a
        # hull shader without a control point phase names no entry function
        # and was refused ("0 entry points"), removing the device at the draw.
        self.parity("conv_hs", ignore=("inputs",))
        self.parity("conv_ds", ignore=("inputs",))
        self.assertIn("store_per_vertex_output", self.final("conv_hs"))
        # EXP1858-1860: a D3D triangle_cw hull shader is GL ccw (upper-left
        # versus lower-left domain origin); both frontends agree
        dxbc = subprocess.run([str(self.dxbc), str(FIXTURES / "conv_hs.dxbc"), "--d3d12-heap", "--print-final"],
                              capture_output=True, text=True, timeout=120).stdout
        self.assertRegex(self.final("conv_hs"), r"(?m)^ccw: true")
        self.assertRegex(dxbc, r"(?m)^ccw: true")
        ccw = subprocess.run([str(self.dxil), str(FIXTURES.parent / "conv-probe" / "hs_const_ccw.dxil"), "--print-final"],
                             capture_output=True, text=True, timeout=120).stdout
        self.assertIn("TRANSLATED", ccw)
        self.assertNotRegex(ccw, r"(?m)^ccw: true")

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

    def test_every_runtime_converted_probe_shader_translates(self):
        # EXP1858: an R1 device gets every DXBC shader as dxilconv DXIL. C41A's
        # udiv (dx.op UDiv with two outputs, which DXC never emits) made
        # dxil-spirv type its helper with the result struct: invalid SPIR-V,
        # an assertion in spirv_to_nir (vtn_handle_function_call) and a dead
        # process on the guest (patches/dxil-spirv-0001-dxbc-udiv-operand-type).
        known = set()
        for tree in ("src/asahi", "src/gallium/drivers/asahi", "src/poly"):
            for source in (self.mesa / tree).rglob("*.[ch]"):
                known.update(re.findall(r"nir_intrinsic_([a-z0-9_]+)", source.read_text(errors="replace")))
        shaders = sorted((FIXTURES.parent / "conv-probe").glob("*.dxil"))
        self.assertGreaterEqual(len(shaders), 49)
        for shader in shaders:
            r = subprocess.run([str(self.dxil), str(shader), "--print-final"], capture_output=True, text=True,
                               timeout=120)
            self.assertEqual(r.returncode, 0, f"{shader.name}: {r.stderr[-1500:]}")
            self.assertIn("TRANSLATED", r.stdout, shader.name)
            # EXP1859: dxilconv's SignedZeroInfNanPreserve marked fmax, and
            # Asahi's fmax_agx fails validation with the flag (x64 HDR abort)
            self.assertNotIn("preserve:", r.stdout, shader.name)
            self.assertNotIn("float_controls_execution_mode", r.stdout, shader.name)
            unhandled = set(re.findall(r"@([a-z0-9_]+)", r.stdout)) - known - self.LOWERED_OUTSIDE_ASAHI
            self.assertEqual(unhandled, set(), shader.name)

    def test_indexed_tess_factor_stores_keep_their_components(self):
        # EXP1859: dxilconv writes SV_TessFactor in a loop (fork phase over
        # an index range); Asahi's options let indexed compact stores through,
        # nir_lower_io then counted the index in slots (outer[1] landed at
        # TESS_LEVEL_INNER) and the converted pipelines drew nothing.
        def levels(text):
            return sorted(re.findall(r"@store_output .*?base=(\d+), range=\d+, wrmask=x, component=(\d), "
                                     r"src_type=float32, io location=VARYING_SLOT_(TESS_LEVEL_\w+)", text))
        dxbc = subprocess.run([str(self.dxbc), str(FIXTURES / "conv_hs.dxbc"), "--d3d12-heap", "--print-final"],
                              capture_output=True, text=True, timeout=120).stdout
        self.assertEqual(levels(self.final("conv_hs")), levels(dxbc))
        self.assertEqual(len(levels(dxbc)), 4)

    def test_image_count_spans_every_used_image_slot(self):
        # EXP1859 C41B: a raw UAV at u0 left the DXIL translation one image
        # variable short; Asahi clamps image indices to num_images - 1, so
        # u2's GetDimensions read u1 (the DXBC frontend's EXP1841 rule).
        for shader in sorted((FIXTURES.parent / "conv-probe").glob("*.dxil")) + sorted(FIXTURES.glob("*.dxil")):
            _, info = self.translate(self.dxil, shader)
            self.assertGreaterEqual(int(info["images"]), int(info["image-span"]), shader.name)

    def test_register_range_takes_consecutive_slots(self):
        self.parity("par_range_ps")


if __name__ == "__main__":
    unittest.main()
