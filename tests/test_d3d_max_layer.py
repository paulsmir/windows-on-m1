"""T1: the D3D array-layer clamp without libagx_txs (APPLE_AGX_D3D_MAX_LAYER=1).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
winsys/agx_win32_d3d_texture_crawl.h agx_win32_d3d_max_layer reads the
layer count straight from descriptor bits (Depth 110..123, Layout 4..5,
Depth (linear) 128..138) instead of running libagx_txs.  It is exact only
while (a) those bits are where the pinned genxml packs them, (b) libagx_txs
keeps its selection (null -> 0, linear 2D array -> depth_linear, else depth)
and lower_regular_texture its clamp (umin(f2u32(fmax(rne(l), 0)), n - 1)),
and (c) the only zero-page texture descriptor is agx_set_null_texture with
four constant-0 swizzles (the one case where the clamp differs, harmlessly).
The fixture evaluates the helper's NIR on packed descriptors (including a
linear 2D array, whose Depth bits are the Stride field) against libagx's
rule; a Mesa bump or a wrong bit position fails here.  The pass must stay
off by default, rewrite only divergent-handle samples, and run in the
projection right before agx_nir_lower_texture for D3D contexts.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from test_d3d10_shader_fields_init import function_body, local_tree, project

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
CAPTURE = local_tree('experiments', 'EXP2138-nullps', 'evidence', 'EXP2139-capture', 'capture',
                     '9fa121805cf5ea58-s4-r0.bin')


class MaxLayerFixture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import agx_shader_cost
        cls.tmp = tempfile.TemporaryDirectory(prefix='d3d-max-layer-')
        cls.binary = agx_shader_cost.build(cls.tmp.name)
        if cls.binary is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest('host Mesa / dxil-spirv trees absent')
        cls.env = {k: v for k, v in os.environ.items() if k != 'APPLE_AGX_D3D_MAX_LAYER'}

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def selftest(self, env):
        r = subprocess.run([str(self.binary), '--selftest-max-layer'], capture_output=True, text=True, env=env)
        return r.returncode, r.stdout

    def test_helper_matches_libagx_on_packed_descriptors(self):
        rc, out = self.selftest(self.env)
        self.assertEqual(rc, 0, out)
        rows = re.findall(r'^MAXLAYER (\S+) got (\d+) want (\d+)( \(null\))? (ok|FAIL)$', out, re.M)
        self.assertEqual({r[0] for r in rows},
                         {'twiddled-2d-array', 'linear-2d-array', 'linear-2d', 'cube-array', '1d-array', 'null'})
        self.assertTrue(all(r[4] == 'ok' for r in rows), out)
        self.assertIn(('linear-2d-array', '1', '1', '', 'ok'), rows)

    def test_only_divergent_handles_and_only_when_enabled(self):
        self.assertIn('MAXLAYER rewritten 0\n', self.selftest(self.env)[1])
        self.assertIn('MAXLAYER rewritten 1\n', self.selftest({**self.env, 'APPLE_AGX_D3D_MAX_LAYER': '1'})[1])

    def test_gravitymark_gbuffer_ps_gets_cheaper(self):
        if not CAPTURE:
            self.skipTest('EXP2139 capture absent')
        import agx_shader_cost
        args = ('--fs-backend', '--root-views=1,0')
        off = agx_shader_cost.cost(self.binary, CAPTURE, *args, env={'APPLE_AGX_D3D_MAX_LAYER': '0'})
        on = agx_shader_cost.cost(self.binary, CAPTURE, *args, env={'APPLE_AGX_D3D_MAX_LAYER': '1'})
        self.assertLess(on['instrs'], off['instrs'])
        self.assertLessEqual(on['gprs'], off['gprs'])


class MaxLayerPremises(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mesa = local_tree('reference', 'mesa')
        if not mesa:
            raise unittest.SkipTest('reference Mesa tree not available')
        cls.mesa = mesa

    def test_libagx_txs_layer_selection(self):
        cl = (self.mesa / 'src/asahi/libagx/texture.cl').read_text()
        txs = cl[cl.index('libagx_txs('):cl.index('libagx_texture_samples(')]
        self.assertRegex(txs, r'if \(d\.address == AGX_ZERO_PAGE_ADDRESS\)\s+return 0;')
        self.assertRegex(txs, r'uint3 size = \(uint3\)\(d\.width, d\.height, d\.depth\);')
        self.assertRegex(txs, r'if \(is_2d && is_array && d\.layout == AGX_LAYOUT_LINEAR\)\s+size\.z = d\.depth_linear;')
        self.assertRegex(txs, r'if \(is_1d && is_array\)\s+size\.y = size\.z;')

    def test_lower_regular_texture_clamp(self):
        c = (self.mesa / 'src/asahi/compiler/agx_nir_lower_texture.c').read_text()
        body = function_body(c, 'lower_regular_texture')
        self.assertIn('unclamped_layer = nir_fround_even(b, unclamped_layer);', body)
        self.assertRegex(body, r'nir_f2u32\(b, nir_fmax\(b, unclamped_layer, nir_imm_float\(b, 0\.0f\)\)\);')
        self.assertIn("if (!(tex->backend_flags & AGX_TEXTURE_FLAG_NO_CLAMP) && !is_txf) {", body)
        self.assertIn('nir_def *max_layer = nir_iadd_imm(b, nr_layers, -1);', body)

    def test_null_texture_descriptor(self):
        helpers = (self.mesa / 'src/asahi/lib/agx_helpers.h').read_text()
        body = function_body(helpers, 'agx_set_null_texture')
        for channel in 'rgba':
            self.assertIn(f'cfg.swizzle_{channel} = AGX_CHANNEL_0;', body)
        self.assertIn('cfg.address = AGX_ZERO_PAGE_ADDRESS;', body)
        self.assertNotIn('depth', body)

    def test_genxml_fields(self):
        xml = (self.mesa / 'src/asahi/genxml/cmdbuf.xml').read_text()
        tex = xml[xml.index('<struct name="Texture" size="24">'):]
        tex = tex[:tex.index('</struct>')]
        self.assertIn('<field name="Layout" size="2" start="4" type="Layout"/>', tex)
        self.assertIn('<field name="Depth" size="14" start="110" type="uint" modifier="minus(1)"/>', tex)
        self.assertIn('<field name="Depth (linear)" size="11" start="128" type="uint" modifier="minus(1)"/>', tex)
        self.assertIn('<value name="Linear" value="0"/>', xml)


class MaxLayerProjection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='d3d-max-layer-projection-')
        out = project(Path(cls.tmp.name) / 'out')
        if out is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest('reference Mesa tree not available')
        cls.state = (out / 'src/gallium/drivers/asahi/agx_state.c').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_d3d_contexts_run_the_pass_right_before_texture_lowering(self):
        body = function_body(self.state, 'agx_shader_initialize')
        call = re.search(r'if \(d3d_robust\)\s+NIR_PASS\(_, nir, agx_win32_lower_d3d_layer_clamp\);\s+'
                         r'NIR_PASS\(_, nir, agx_nir_lower_texture\);', body)
        self.assertTrue(call, 'layer clamp call')
        self.assertEqual(body.count('agx_win32_lower_d3d_layer_clamp'), 1)


if __name__ == '__main__':
    unittest.main()
