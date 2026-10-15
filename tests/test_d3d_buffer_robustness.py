"""D3D-H6: D3D10/11 out-of-bounds buffer semantics on the projected Asahi driver.

Microsoft's rules (investigation/analysis/D3D-H6-buffer-robustness.md):
constant-buffer reads past the bound size, or from a slot with nothing
bound, return 0 (D3D11.3 functional spec 7.5); ld_raw / ld_structured on a
UAV return 0 for each out-of-bounds 32-bit component and read the in-bounds
ones; store_raw / store_structured write only the in-bounds components;
atomics out of bounds write nothing; out-of-bounds vertex fetch returns 0
with the format's defaults (8.19.2).

Invariants caught:
- the D3D UMD's device context was created with flags 0, so Asahi lowered
  no constant-buffer or UAV bound at all: an application's out-of-bounds
  index read or wrote whatever GPU memory followed the buffer (or faulted);
- Asahi's GL lowering, the only one it has, returns the buffer's first
  element for an out-of-bounds load (D3D: 0), drops a whole store whose
  last component is out of bounds (D3D: write the in-bounds part) and
  clamps vertex fetch to element 0 (D3D: 0);
- an unbound constant-buffer slot kept the base and size of the buffer last
  bound there in the batch (or base 0: a GPU fault once the load is
  clamped to offset 0);
- the OpenGL ICD keeps the robustness its contexts ask for.

The lowering is run on host NIR (the host Mesa build the UMD's Mesa is cut
from, 9aa1215f) with constant sizes and folded; the projection checks run
the real projection prepare-only.  Both skip where the trees are absent.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, local_tree, project
from test_dxbc_to_nir import host_flags

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / "drivers/apple-agx/mesa/winsys"
FIXTURE = ROOT / "tests/d3d_buffer_robustness_fixture.c"


class D3dBufferAccessLowering(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = local_tree("accelerated-desktop-ad03", "mesa-build")
        mesa = local_tree("reference", "mesa")
        if not build or not mesa:
            raise unittest.SkipTest("host Mesa build not available")
        cls.tmp = tempfile.TemporaryDirectory(prefix="d3d-robustness-")
        binary = Path(cls.tmp.name) / "d3d_buffer_robustness_fixture"
        libs = [build / p for p in (
            "src/asahi/compiler/libasahi_compiler.a", "src/asahi/isa/libagx2_disasm.a",
            "src/asahi/layout/libasahi_layout.a", "src/compiler/nir/libnir.a",
            "src/compiler/libcompiler.a", "src/util/libmesa_util.a",
            "src/util/libmesa_util_simd.a", "src/c11/impl/libmesa_util_c11.a",
            "src/util/blake3/libblake3.a")]
        built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11",
                                "-Werror=implicit-function-declaration",
                                *host_flags(build), "-I", str(build / "src/asahi"),
                                "-I", str(mesa / "src/asahi"), "-I", str(WINSYS), str(FIXTURE),
                                "-o", str(binary), *map(str, libs),
                                "-L/opt/homebrew/opt/zstd/lib", "-lzstd", "-lz", "-lpthread"],
                               capture_output=True, text=True)
        if built.returncode:
            cls.tmp.cleanup()
            raise AssertionError("fixture build failed:\n" + built.stderr[-4000:])
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        if run.returncode:
            cls.tmp.cleanup()
            raise AssertionError("fixture failed:\n" + run.stdout + run.stderr[-4000:])
        cls.results = {}
        for line in run.stdout.splitlines():
            name, _, rest = line.partition(" ")
            cls.results[name] = rest.split()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def expect(self, cases):
        for name, expected in cases.items():
            with self.subTest(case=name):
                self.assertEqual(self.results.get(name), expected.split())

    def test_constant_buffer_reads_past_the_bound_size_return_zero(self):
        self.expect({
            "ubo_in_bounds": "load_ubo@16 load_ubo@20 load_ubo@24 load_ubo@28",
            "ubo_past_end": "0 0 0 0",
            "ubo_unbound": "0 0 0 0",
            "ubo_wrapping_offset": "0 0 0 0"})

    def test_uav_loads_return_zero_for_each_out_of_bounds_component(self):
        self.expect({
            "ssbo_load_in_bounds": "load_ssbo@0 load_ssbo@4 load_ssbo@8 load_ssbo@12",
            "ssbo_load_partial": "load_ssbo@8 load_ssbo@12 0 0",
            "ssbo_load_past_end": "0 0",
            "ssbo_load_unbound": "0",
            "ssbo_load_wrapping_offset": "0 0"})

    def test_uav_stores_write_only_the_in_bounds_components(self):
        self.expect({
            "ssbo_store_in_bounds": "stores 0x4",
            "ssbo_store_partial": "stores 8x1 12x1",
            "ssbo_store_past_end": "stores",
            "ssbo_store_masked_partial": "stores 4x1",
            "ssbo_store_wrapping_offset": "stores"})

    def test_atomics_are_left_to_the_gl_lowering(self):
        # nir_lower_robust_access predicates them (nothing written, result 0)
        self.expect({"ssbo_atomic": "kept"})


class D3dBufferRobustnessProjection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="d3d-robustness-projection-")
        out = project(Path(cls.tmp.name) / "out")
        if out is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest("reference Mesa tree not available")
        asahi = out / "src/gallium/drivers/asahi"
        cls.state = (asahi / "agx_state.c").read_text()
        cls.state_h = (asahi / "agx_state.h").read_text()
        cls.pipe = (asahi / "agx_pipe.c").read_text()
        cls.uniforms = (asahi / "agx_uniforms.c").read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_d3d_device_context_is_robust_with_d3d_semantics(self):
        context = self.state_h[self.state_h.index("struct agx_context {"):]
        context = context[:context.index("\n};")]
        self.assertTrue(re.search(r"\n   bool windows_d3d_robust;", context),
                        "struct agx_context has no windows_d3d_robust")
        body = function_body(self.pipe, "agx_create_context")
        upstream = body.index("ctx->robust = (flags & PIPE_CONTEXT_ROBUST_BUFFER_ACCESS);")
        flag = re.search(r"ctx->windows_d3d_robust = \w+ && \w+->D3dRobustness;", body)
        self.assertTrue(flag and flag.start() > upstream, "D3D flag not taken from the backend")
        self.assertRegex(body[flag.end():], r"if \(ctx->windows_d3d_robust\)\s+ctx->robust = true;")

    def test_vertex_fetch_uses_the_d3d_robustness_level(self):
        body = function_body(self.state, "agx_update_vs")
        self.assertTrue(re.search(r"\.prolog\.vs\.robustness\.level =\s+"
                               r"ctx->windows_d3d_robust \? AGX_ROBUSTNESS_D3D :\s+"
                                  r"ctx->robust \? AGX_ROBUSTNESS_GL : AGX_ROBUSTNESS_DISABLED,",
                                  body), "vertex fetch robustness level")

    def test_d3d_shaders_get_the_d3d_buffer_lowering_before_the_gl_one(self):
        self.assertTrue('#include "agx_win32_d3d_robustness.h"' in self.state,
                        "agx_state.c does not include the D3D lowering")
        calls = re.findall(r"\bagx_shader_initialize\(([^;]*)\);", self.state)
        self.assertEqual(len(calls), 2)
        for call in calls:
            self.assertRegex(call, r"ctx->robust,\s*ctx->windows_d3d_robust$")
        body = function_body(self.state, "agx_shader_initialize")
        d3d = re.search(r"if \(d3d_robust\)\s+NIR_PASS\(_, nir, agx_win32_lower_d3d_buffer_access\);", body)
        gl = re.search(r"NIR_PASS\(_, nir, nir_lower_robust_access,\s+d3d_robust \? "
                       r"agx_win32_should_lower_robustness_d3d : should_lower_robustness,\s+&robust\);",
                       body)
        self.assertTrue(d3d and gl and d3d.end() < gl.start(), "D3D lowering order")
        self.assertFalse(re.search(r"[^:] should_lower_robustness, &robust\);", body),
                         "the upstream GL-only call remains")
        flt = function_body(self.state, "agx_win32_should_lower_robustness_d3d")
        handled = re.search(r"case nir_intrinsic_load_ubo:\s+case nir_intrinsic_load_ssbo:\s+"
                            r"case nir_intrinsic_store_ssbo:\s+return false;", flt)
        self.assertTrue(handled, "D3D filter leaves UBO/SSBO loads and stores to the GL lowering")
        self.assertRegex(flt, r"const bool robust = true;[\s\S]*return should_lower_robustness\(intr, &robust\);")

    def test_unbound_constant_buffers_read_the_zero_page(self):
        body = function_body(self.uniforms, "agx_set_cbuf_uniforms")
        bound = body.index("u_foreach_bit(cb, st->cb_mask)")
        d3d = body.index("if (batch->ctx->windows_d3d_robust)")
        self.assertGreater(d3d, bound, "unbound slots must be rewritten after the bound ones")
        tail = body[d3d:]
        self.assertIn("for (unsigned cb = 0; cb < PIPE_MAX_CONSTANT_BUFFERS; ++cb)", tail)
        self.assertRegex(tail, r"!\(st->cb_mask & BITFIELD_BIT\(cb\)\) \|\| !st->cb\[cb\]\.buffer")
        self.assertRegex(tail, r"unif->ubo_base\[cb\] = zero->va->addr;\s+unif->ubo_size\[cb\] = 0;")
        self.assertIn("agx_device(batch->ctx->base.screen)->zero_bo", tail)


class D3dBufferRobustnessOwners(unittest.TestCase):
    def test_only_the_d3d_umd_asks_for_d3d_semantics(self):
        header = (WINSYS / "agx_win32_asahi_bo.h").read_text()
        backend = header[header.index("typedef struct {\n  struct agx_device *Native;"):]
        self.assertIn("  int D3dRobustness;", backend[:backend.index("} AGX_WIN32_ASAHI_BACKEND;")])
        device = (WINSYS / "agx_d3d10_windows.cpp").read_text()
        create = function_body(device, "HRESULT AgxD3d10WindowsCreateDevice")
        flag = create.index("owner->Backend.D3dRobustness=1;")
        self.assertLess(flag, create.index("AgxWin32AsahiScreenCreateForWindows(&owner->Backend"))
        icd = (ROOT / "drivers/apple-agx/windows/icd/agx_wgl_icd.cpp").read_text()
        self.assertNotIn("D3dRobustness", icd)


if __name__ == "__main__":
    unittest.main()
