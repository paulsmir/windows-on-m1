"""rc110: the tile-buffer byte limit switch (APPLE_AGX_TILE_BUFFER_BYTES).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- The projected src/asahi/lib/agx_tilebuffer.c must take its MAX_BYTES_PER_TILE
  from winsys/agx_win32_tile_limit.h: unset, the limit is Asahi's 32767 and a
  32 B/px single-sampled layout (GravityMark's four RGBA16F G-buffer targets)
  keeps the 32x16 utile (byte-identical control stream); 32768 gives it the
  32x32 utile; an invalid value is ignored. Compiled from the projected
  source against the host Mesa layout library, so a moved anchor or a
  parse regression shows here, not on hardware.
- The KMD/UMD submit check (shared/include/apple_agx_g4_submit.h) accepts
  exactly the configurations the switch can produce (32 B x 32 x 32) and
  refuses the next size up, so the UMD and the KMD agree on the limit.
"""
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_dxbc_to_nir import local_tree, host_flags, HOST_LIBS  # noqa: E402
import test_d3d10_shader_fields_init as projection  # noqa: E402

HEADER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_tile_limit.h'
SHARED = ROOT / 'drivers/apple-agx/shared/include'

MAIN = r'''
#include <stdio.h>
#include <stdlib.h>
#include "agx_tilebuffer.h"
#include "agx_win32_tile_limit.h"
#include "apple_agx_g4_submit.h"

int main(int argc, char **argv)
{
   enum pipe_format formats[8] = {PIPE_FORMAT_NONE};
   unsigned n = argc > 1 ? (unsigned)atoi(argv[1]) : 4u;
   enum pipe_format f = argc > 2 && argv[2][0] == '8' ? PIPE_FORMAT_R8G8B8A8_UNORM
                                                       : PIPE_FORMAT_R16G16B16A16_FLOAT;
   for (unsigned i = 0; i < n && i < 8; ++i)
      formats[i] = f;
   struct agx_tilebuffer_layout tib = agx_build_tilebuffer_layout(formats, (uint8_t)n, 1, false);
   struct drm_asahi_cmd_render cmd = {0};
   agx_tilebuffer_set_drm_cmd(&cmd, &tib);
   printf("sample %u tile %u utile %ux%u valid %d limit %u\n", tib.sample_size_B, tib.tile_size,
          cmd.utile_width_px, cmd.utile_height_px,
          AppleAgxG4TileBufferValid(tib.sample_size_B, 1u, cmd.utile_width_px, cmd.utile_height_px),
          agx_win32_max_bytes_per_tile());
   return 0;
}
'''


class TileLimitParse(unittest.TestCase):
    """The header's parser, compiled as C11 (plain C: the x86 closure also builds it as C++)."""

    def build(self, tmp, lang):
        source = Path(tmp) / ('parse.c' if lang == 'c' else 'parse.cpp')
        source.write_text('#include "agx_win32_tile_limit.h"\n#include <stdio.h>\n'
                          'int main(int argc, char **argv) { printf("%u\\n", '
                          'agx_win32_tile_limit_parse(argc > 1 ? argv[1] : (const char *)0)); return 0; }\n')
        binary = Path(tmp) / 'parse'
        cmd = ['clang', '-Wall', '-Wextra', '-Werror', '-I', str(HEADER.parent), str(source), '-o', str(binary)]
        if lang == 'c':
            cmd[1:1] = ['-std=c11']
        else:
            cmd[1:1] = ['-std=c++17', '-x', 'c++']
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        return binary

    def parse(self, binary, value):
        args = [str(binary)] + ([value] if value is not None else [])
        return int(subprocess.run(args, check=True, capture_output=True, text=True).stdout)

    def test_parse(self):
        with tempfile.TemporaryDirectory(prefix='tile-limit-') as tmp:
            for lang in ('c', 'c++'):
                binary = self.build(tmp, lang)
                self.assertEqual(self.parse(binary, None), 32767)
                self.assertEqual(self.parse(binary, ''), 32767)
                self.assertEqual(self.parse(binary, '32768'), 32768)
                self.assertEqual(self.parse(binary, '0x8000'), 32768)
                self.assertEqual(self.parse(binary, '32767'), 32767)
                self.assertEqual(self.parse(binary, '16383'), 16383)
                self.assertEqual(self.parse(binary, '8192'), 8192)
                self.assertEqual(self.parse(binary, '8191'), 32767)
                self.assertEqual(self.parse(binary, '32769'), 32767)
                self.assertEqual(self.parse(binary, '65536'), 32767)
                self.assertEqual(self.parse(binary, 'abc'), 32767)
                self.assertEqual(self.parse(binary, '32768x'), 32767)
                self.assertEqual(self.parse(binary, '-1'), 32767)


class ProjectedTilebuffer(unittest.TestCase):
    """The projected agx_tilebuffer.c picks the utile from the switch."""

    def test_projected_layout_follows_the_switch(self):
        build_tree = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        if not build_tree or not mesa:
            self.skipTest('host Mesa build or reference tree not available')
        with tempfile.TemporaryDirectory(prefix='tile-proj-') as tmp:
            out = projection.project(Path(tmp) / 'out')
            if out is None:
                self.skipTest('reference Mesa tree not available')
            projected = out / 'src/asahi/lib/agx_tilebuffer.c'
            text = projected.read_text()
            self.assertIn('#include "agx_win32_tile_limit.h"', text)
            self.assertIn('#define MAX_BYTES_PER_TILE agx_win32_max_bytes_per_tile()', text)
            self.assertNotIn('(32768 - 1)', text)
            main = Path(tmp) / 'main.c'
            main.write_text(MAIN)
            binary = Path(tmp) / 'tile'
            flags = host_flags(build_tree)
            includes = []
            for d in (HEADER.parent, SHARED, out / 'src/asahi/lib', mesa / 'src/asahi/lib',
                      build_tree / 'src/asahi/genxml', mesa / 'src/asahi/layout', mesa / 'src/asahi',
                      mesa / 'src', mesa / 'include', build_tree / 'src', build_tree / 'include',
                      mesa / 'include/drm-uapi'):
                includes += ['-I', str(d)]
            libs = [str(build_tree / p) for p in ('src/asahi/layout/libasahi_layout.a',
                                                    'src/util/libmesa_util.a', 'src/util/libmesa_util_simd.a',
                                                    'src/c11/impl/libmesa_util_c11.a', 'src/util/blake3/libblake3.a')]
            r = subprocess.run(['clang', '-std=c11', *flags, *includes, str(projected), str(main), '-o', str(binary),
                                *libs, '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread', *HOST_LIBS],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr[-3000:])

            def run(env_value, *args):
                env = dict(os.environ)
                env.pop('APPLE_AGX_TILE_BUFFER_BYTES', None)
                if env_value is not None:
                    env['APPLE_AGX_TILE_BUFFER_BYTES'] = env_value
                return subprocess.run([str(binary), *args], env=env, check=True,
                                      capture_output=True, text=True).stdout.strip()

            # four RGBA16F targets: 32 B/px (Asahi: 32x16; 32768: 32x32)
            self.assertEqual(run(None), 'sample 32 tile 512 utile 32x16 valid 1 limit 32767')
            self.assertEqual(run('32768'), 'sample 32 tile 1024 utile 32x32 valid 1 limit 32768')
            self.assertEqual(run('abc'), 'sample 32 tile 512 utile 32x16 valid 1 limit 32767')
            self.assertEqual(run('32769'), 'sample 32 tile 512 utile 32x16 valid 1 limit 32767')
            self.assertEqual(run('16383'), 'sample 32 tile 256 utile 16x16 valid 1 limit 16383')
            # four RGBA8 targets (16 B/px) fit 32x32 under either limit
            self.assertEqual(run(None, '4', '8'), 'sample 16 tile 1024 utile 32x32 valid 1 limit 32767')
            self.assertEqual(run('32768', '4', '8'), 'sample 16 tile 1024 utile 32x32 valid 1 limit 32768')
            # one RGBA16F target: unchanged by the switch
            self.assertEqual(run('32768', '1'), 'sample 8 tile 1024 utile 32x32 valid 1 limit 32768')


if __name__ == '__main__':
    unittest.main()
