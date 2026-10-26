"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Query lifetime projections can pass reduced replay fixtures while referring
to an undeclared helper, an invalid real Mesa field, or a mismatched libagx
dispatch macro. Compile the complete projected query and batch units with
the real header closure, and enforce the pipe_context downcast ABI.
This is a host syntax gate; it does not replace the Windows package build.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, local_tree, project
from test_dxbc_to_nir import host_flags


class QueryNativeCompile(unittest.TestCase):
    def test_complete_query_and_batch_translation_units(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        self.assertIsNotNone(mesa)
        self.assertIsNotNone(build)
        with tempfile.TemporaryDirectory(prefix='agx-query-native-') as td:
            p = Path(td)
            out = project(p / 'native')
            device = out / 'src/asahi/lib/agx_device.h'
            device.write_text(device.read_text().replace(
                '#ifdef _WIN32\n   void *windows_private; /* Windows native allocation owner, never a GPU address */\n#endif',
                '   void *windows_private; /* Host compile platform shim. */'))
            (p / 'xf86drm.h').write_text(
                '/* Host declaration for the unused Linux import branch. */\n'
                '#pragma once\nint drmSyncobjImportSyncFile(int, unsigned, int);\n')
            includes = [
                p, out / 'src', out / 'include', out / 'include/drm-uapi',
                out / 'src/asahi/lib', out / 'src/asahi/libagx',
                out / 'src/asahi/layout', out / 'src/asahi/compiler',
                out / 'src/gallium/drivers/asahi', build / 'src/asahi',
                build / 'src/asahi/genxml', build / 'src/asahi/libagx',
                build / 'src/poly', build / 'src/compiler/nir',
                mesa / 'src/gallium/auxiliary', mesa / 'src/gallium/include',
                mesa / 'src/virtio/vdrm', ROOT / 'drivers/apple-agx/mesa/winsys',
                ROOT / 'drivers/apple-agx/shared/include',
            ]
            flags = [
                '-std=c11', '-DAPPLE_AGX_GPUVA_WINSYS', '-Wno-c23-extensions',
                '-Werror=implicit-function-declaration',
                '-Werror=incompatible-pointer-types',
                '-include', str(p / 'xf86drm.h'),
                *['-I' + str(directory) for directory in includes],
                *host_flags(build),
            ]
            (p / 'context_abi.c').write_text('''
#include "agx_state.h"
#include "agx_win32_query_timestamp.h"
_Static_assert(offsetof(struct agx_context, base) == 0,
               "pipe_context must stay first for the native downcast");
_Static_assert(AGX_MAX_BATCHES <= AGX_WIN32_QUERY_TIMESTAMP_MAX_SOURCES,
               "timestamp snapshots must fit the reduction kernel");
''')
            for unit in (out / 'src/gallium/drivers/asahi/agx_query.c',
                         out / 'src/gallium/drivers/asahi/agx_batch.c',
                         p / 'context_abi.c'):
                with self.subTest(unit=unit.name):
                    result = subprocess.run(
                        ['clang', *flags, '-fsyntax-only', str(unit)],
                        capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
