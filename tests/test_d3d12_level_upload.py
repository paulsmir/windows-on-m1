"""rc103: whole-level texture uploads through one compute copy
(agx_d3d12_windows.cpp level_upload, mesa/winsys/agx_d3d12_level_ring.h,
mesa/winsys/agx_win32_asahi_level_upload.h projected into agx_blit.c by
mesa/scripts/native-asahi-level-upload.py; APPLE_AGX_D3D12_LEVEL_UPLOAD).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- EXP2037a: every CopyTextureRegion into a GPU-only BC texture level was a
  render-pass blit that flushed the previous level's batch (1262
  submissions in GravityMark's scored frame 2->3). A whole-level upload must
  record no blit: one CPU tiling and one compute copy per level.
- The ring is reused only after a drain: no CPU write may land in a span a
  recorded copy still reads (the model GPU reads spans late, so a reuse
  corrupts the texture), and every level must arrive tiled.
- The ring is not an RC65 upload target: an Execute-start snapshot of its
  dirty pages would make transfer_held skip it (UploadRingPending) while a
  copy of the same submission reads a span written after the snapshot.
- Partial levels, layouts the native layout refuses, mappable textures, a
  refused compute copy and a failed ring creation keep the blit or CPU path
  with identical contents; APPLE_AGX_D3D12_LEVEL_UPLOAD=0 restores the blit
  for every upload.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_dxbc_to_nir import ROOT, local_tree, host_flags
from test_d3d12_view_mapping import block

OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
RING = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_level_ring.h'
FUNCTIONS = ('static unsigned block_bytes(',
             'static unsigned texture_block(enum pipe_format format, unsigned *bw, unsigned *bh) {',
             'static unsigned level_extent(unsigned extent, unsigned level) {',
             'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {',
             'static enum pipe_format bc_staging_format(', 'static void gpu_copy_blocks(',
             'static bool staged_upload_discard(', 'static bool level_upload_enabled(',
             'static bool level_stage_read_enabled(',
             'static HRESULT level_upload(', 'static HRESULT staged_upload(', 'static HRESULT staged_download(',
             'extern "C" HRESULT AgxD3d12OwnerCopyTexture(')


def owner_functions(text):
    return '\n'.join(block(text, n) for n in FUNCTIONS if n in text)


class LevelUploadOwner(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not mesa or not build:
            raise unittest.SkipTest('pinned Mesa reference and host build not present')
        cls.tmp = tempfile.TemporaryDirectory(prefix='level-upload-')
        header = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.h').read_text()
        end = header.index('} AGX_D3D12_TEXTURE_COPY;') + len('} AGX_D3D12_TEXTURE_COPY;')
        record = header[header.rfind('typedef struct {', 0, end):end]
        fixture = (ROOT / 'tests/d3d12_level_upload_fixture.cpp').read_text()
        fixture = fixture.replace('/* ACTUAL_COPY_RECORD */', record).replace(
            '/* ACTUAL_OWNER_COPY */', owner_functions(OWNER.read_text())).replace(
            '/* LEVEL_RING_HEADER */', RING.read_text())
        src, exe = Path(cls.tmp.name) / 'level.cpp', Path(cls.tmp.name) / 'level'
        src.write_text(fixture)
        libs = [build / p for p in ('src/util/libmesa_util.a', 'src/util/libmesa_util_simd.a',
                                    'src/c11/impl/libmesa_util_c11.a', 'src/util/blake3/libblake3.a')]
        r = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function', '-fsanitize=address,undefined',
                            *host_flags(build), '-I' + str(mesa / 'src/gallium/include'), str(src), '-o', str(exe),
                            *map(str, libs), '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
                           capture_output=True, text=True)
        if r.returncode:
            raise AssertionError(r.stderr[-6000:])
        cls.exe = exe

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_mode(self, mode, switch):
        env = {k: v for k, v in os.environ.items() if k != 'APPLE_AGX_D3D12_LEVEL_UPLOAD'}
        if switch is not None:
            env['APPLE_AGX_D3D12_LEVEL_UPLOAD'] = switch
        r = subprocess.run([str(self.exe), mode], capture_output=True, text=True, env=env, timeout=120)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr[-4000:])
        self.assertIn('PASS', r.stdout)
        return r.stdout

    def test_whole_levels_take_one_compute_copy_each(self):
        """Unset or any value but exactly 0: no blit for whole GPU-only levels,
        the ring drains exactly when full, every level arrives tiled."""
        for switch in (None, '1', '00', ''):
            with self.subTest(switch=switch):
                self.run_mode('on', switch)

    def test_kill_switch_restores_the_blit(self):
        self.run_mode('off', '0')


class LevelRing(unittest.TestCase):
    def test_placement_and_switch(self):
        """Spans in order, 256-byte aligned, a drain (1) exactly when the
        next level does not fit, never (-1) for an empty or oversized level."""
        src = RING.read_text() + r'''
#include <assert.h>
int main(void) {
  unsigned long long at = 7;
  assert(AgxD3d12LevelRingPlace(1024, 0, 16, &at) == 0 && at == 0);
  assert(AgxD3d12LevelRingAdvance(0, 16) == 256);
  assert(AgxD3d12LevelRingPlace(1024, 768, 256, &at) == 0 && at == 768);
  assert(AgxD3d12LevelRingAdvance(768, 256) == 1024);
  assert(AgxD3d12LevelRingPlace(1024, 1024, 16, &at) == 1 && at == 0);
  assert(AgxD3d12LevelRingPlace(1024, 768, 257, &at) == 1 && at == 0);
  assert(AgxD3d12LevelRingPlace(1024, 0, 1024, &at) == 0 && at == 0);
  assert(AgxD3d12LevelRingPlace(1024, 0, 1025, &at) == -1);
  assert(AgxD3d12LevelRingPlace(1024, 0, 0, &at) == -1);
  assert(AgxD3d12LevelRingPlace(1024, 100, 16, &at) == -1); /* misaligned use */
  assert(AgxD3d12LevelRingPlace(1024, 2048, 16, &at) == -1);
  assert(AgxD3d12LevelRingPlace(1024, 0, 16, 0) == -1);
  assert(AgxD3d12LevelRingPlace(AGX_D3D12_LEVEL_RING_BYTES, 0, 16ull << 20, &at) == 0);
  assert(AgxD3d12LevelUploadEnabled(0, 0) && AgxD3d12LevelUploadEnabled("1", 1));
  assert(AgxD3d12LevelUploadEnabled("00", 2) && AgxD3d12LevelUploadEnabled("", 0));
  assert(!AgxD3d12LevelUploadEnabled("0", 1));
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='level-ring-') as tmp:
            for compiler, name in (('clang', 'ring.c'), ('clang++', 'ring.cpp')):
                path = Path(tmp) / name
                path.write_text(src)
                exe = Path(tmp) / (name + '.out')
                r = subprocess.run([compiler, '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                                    str(path), '-o', str(exe)], capture_output=True, text=True)
                self.assertEqual(r.returncode, 0, r.stderr)
                self.assertEqual(subprocess.run([str(exe)]).returncode, 0)

    def test_owner_wiring(self):
        """The level path is tried first in the staging blit, its ring is an
        UPLOAD-heap buffer, it drains through the synchronous submit, and the
        owner releases the ring and the scratch."""
        text = OWNER.read_text()
        staged = block(text, 'static HRESULT staged_upload(')
        self.assertIn('{\n  /* rc103: a whole GPU-only level takes one compute copy instead */\n'
                      '  const HRESULT level = level_upload(owner, copy, texture, buffer, tbox, bbox);\n'
                      '  if (level != S_FALSE) return level;\n', staged)
        self.assertLess(text.index('static HRESULT level_upload('), text.index('static HRESULT staged_upload('))
        level = block(text, 'static HRESULT level_upload(')
        self.assertIn('AgxD3d12OwnerCreateBuffer(owner, AGX_D3D12_LEVEL_RING_BYTES, TRUE, &owner->LevelRing)', level)
        create = level.index('AgxD3d12OwnerCreateBuffer(owner, AGX_D3D12_LEVEL_RING_BYTES, TRUE, &owner->LevelRing)')
        remove = level.index('remove_upload_target(owner, static_cast<struct pipe_resource *>(owner->LevelRing.Resource));')
        self.assertLess(create, remove)
        self.assertLess(remove, level.index('memcpy(static_cast<unsigned char *>(owner->LevelRing.Cpu)'))
        self.assertIn('AgxD3d12OwnerSubmitAndWait(owner)', level)
        self.assertLess(level.index('AgxD3d12OwnerSubmitAndWait(owner)'), level.index('memcpy(static_cast<unsigned char *>(owner->LevelRing.Cpu)'))
        self.assertLess(level.index('memcpy(static_cast<unsigned char *>(owner->LevelRing.Cpu)'), level.index('AgxWin32AsahiCopyToLevel('))
        # rc113: the write-combined source is copied once into cached memory
        # before ail_tile reads it
        self.assertLess(level.index('memcpy(owner->LevelLinear, bmap'), level.index('AgxWin32AsahiTileLevel('))
        self.assertIn('"APPLE_AGX_D3D12_LEVEL_STAGE_READ"', block(text, 'static bool level_stage_read_enabled('))
        self.assertIn('"APPLE_AGX_D3D12_LEVEL_UPLOAD"', block(text, 'static bool level_upload_enabled('))
        destroy = block(text, 'extern "C" HRESULT AgxD3d12OwnerDestroy(')
        self.assertIn('AgxD3d12OwnerDestroyBuffer(owner, &owner->LevelRing)', destroy)
        self.assertIn('HeapFree(GetProcessHeap(), 0, owner->LevelScratch)', destroy)
        self.assertIn('HeapFree(GetProcessHeap(), 0, owner->LevelLinear)', destroy)
        self.assertNotIn('getenv', level)


if __name__ == '__main__':
    unittest.main()
