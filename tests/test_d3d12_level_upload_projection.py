"""rc103: the native helpers of whole-level uploads
(mesa/winsys/agx_win32_asahi_level_upload.h), projected into agx_blit.c by
mesa/scripts/native-asahi-level-upload.py.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- The helpers must follow Asahi's own buffer copy (agx_resource_copy_region:
  fallible compute-batch getter, agx_batch_init_state, reads then writes,
  libagx_copy_uint4) and transfer tiling (ail_tile of a GPU-tiled level), and
  must refuse lossless-compressed, linear, depth/stencil, multisampled,
  shared and partial levels.
- They must be declared through the included header: an implicit
  declaration is a build error on the Windows toolchain, which host tests of
  other units do not compile (EXP1929).
- A second projection over projected sources must refuse.
"""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

from test_dxbc_to_nir import ROOT, local_tree, host_flags
from test_d3d12_view_mapping import block
from test_d3d10_shader_fields_init import project

HELPERS = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_level_upload.h'
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-level-upload.py'


@unittest.skipUnless(local_tree('reference', 'mesa') and local_tree('accelerated-desktop-ad03', 'mesa-build'),
                     'pinned Mesa reference and host build not present')
class LevelUploadProjection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='level-projection-')
        cls.out = project(Path(cls.tmp.name) / 'native')
        cls.blit = (cls.out / 'src/gallium/drivers/asahi/agx_blit.c').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_helpers_follow_asahi_buffer_copy_and_transfer_tiling(self):
        copy = block(self.blit, '\nAgxWin32AsahiCopyToLevel(')
        self.assertIn('if (ctx->any_faults', copy)
        acquire = copy.index('struct agx_batch *batch = agx_get_compute_batch(ctx);')
        self.assertEqual(copy[acquire:copy.index('agx_batch_init_state(batch);')].split(),
                         'struct agx_batch *batch = agx_get_compute_batch(ctx); if (!batch) return 0;'.split())
        order = [copy.index(s) for s in ('agx_batch_init_state(batch);', 'agx_batch_reads(batch, from);',
                                         'agx_batch_writes(batch, dst, level);', 'libagx_copy_uint4(batch,')]
        self.assertEqual(order, sorted(order))
        self.assertIn('ail_get_layer_level_B(&dst->layout, layer, level)', copy)
        tile = block(self.blit, '\nAgxWin32AsahiTileLevel(')
        self.assertIn('ail_tile(dst, (void *)(uintptr_t)linear, &rsrc->layout, level,', tile)
        self.assertLess(tile.index('memset(dst, 0'), tile.index('ail_tile('))
        bytes = block(self.blit, '\nAgxWin32AsahiLevelUploadBytes(')
        for guard in ('layout->compressed', 'AIL_TILING_LINEAR', 'ail_is_level_twiddled_uncompressed(layout, level)',
                      'util_format_is_depth_or_stencil', 'texture->nr_samples > 1', 'AGX_BO_SHARED | AGX_BO_SHAREABLE',
                      'width_px != u_minify(layout->width_px, level)', 'ail_get_level_size_B(layout, level)'):
            self.assertIn(guard, bytes)
        self.assertIn('#include "agx_win32_asahi_level_upload.h"', self.blit)
        # the original buffer copy and blit bodies are untouched
        self.assertIn('if (!agx_device(pipe->screen)->windows_private &&', self.blit)

    def test_projected_blit_unit_declares_the_helpers(self):
        """agx_blit.c compiles its injected helpers with declarations from the
        included header and the Asahi functions they call; the only host
        gap left is the projection's existing windows_private field."""
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        p = Path(self.tmp.name) / 'stub'
        p.mkdir(exist_ok=True)
        (p / 'xf86drm.h').write_text('#pragma once\nint drmSyncobjImportSyncFile(int, unsigned, int);\n')
        out = self.out
        includes = [p, out / 'src', out / 'include', out / 'include/drm-uapi', out / 'src/asahi/lib',
                    out / 'src/asahi/libagx', out / 'src/asahi/layout', out / 'src/asahi/compiler',
                    out / 'src/gallium/drivers/asahi', build / 'src/asahi', build / 'src/asahi/genxml',
                    build / 'src/asahi/libagx', build / 'src/poly', build / 'src/compiler/nir',
                    mesa / 'src/gallium/auxiliary', mesa / 'src/gallium/include', mesa / 'src/virtio/vdrm',
                    ROOT / 'drivers/apple-agx/mesa/winsys', ROOT / 'drivers/apple-agx/shared/include']
        r = subprocess.run(['clang', '-std=c11', '-DAPPLE_AGX_GPUVA_WINSYS', '-Wno-c23-extensions',
                            '-Werror=implicit-function-declaration', '-include', str(p / 'xf86drm.h'),
                            *['-I' + str(i) for i in includes], *host_flags(build), '-fsyntax-only',
                            str(out / 'src/gallium/drivers/asahi/agx_blit.c')], capture_output=True, text=True)
        errors = [l for l in r.stderr.splitlines() if 'error:' in l]
        self.assertTrue(all('windows_private' in l for l in errors), r.stderr[-4000:])
        self.assertNotIn('fatal error', r.stderr)

    def test_projection_refuses_a_second_run(self):
        spec = importlib.util.spec_from_file_location('level_upload_again', SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with self.assertRaises(RuntimeError):
            module.project_blit(self.blit)

    def test_header_is_plain_c_and_cxx(self):
        text = HELPERS.read_text()
        self.assertIn('extern "C"', text)
        for banned in ('<stdio.h>', '<stdlib.h>', '<windows.h>'):
            self.assertNotIn(banned, text)



if __name__ == '__main__':
    unittest.main()
