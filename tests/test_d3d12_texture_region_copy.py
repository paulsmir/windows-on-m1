"""Execute the production texture-to-texture region copy validation.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1838 MiniEngine: BC3
mip-level copies between textures (CopyTextureRegion subresource to
subresource) were refused E_INVALIDARG because the validation measured
elements with block_bytes, which knows only 1x1 texels; the device was
removed. Copies between formats with the same element (texel or 4x4 block)
must pass, block origins must be aligned, a partial block is legal only at
the source level's edge, and mismatched element sizes or shapes must be
refused.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import local_tree, host_flags
from test_d3d12_view_mapping import block

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'

FIXTURE = r'''
#include <cassert>
#include "pipe/p_state.h"
#include "util/format/u_format.h"
/* PRODUCTION */
static pipe_resource tex(pipe_format format, unsigned w, unsigned h, unsigned levels, unsigned layers = 1) {
    pipe_resource r = {};
    r.target = layers > 1 ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D;
    r.format = format; r.width0 = w; r.height0 = h; r.depth0 = 1; r.array_size = layers; r.last_level = levels - 1;
    return r;
}
int main() {
    pipe_resource bc3 = tex(PIPE_FORMAT_DXT5_SRGBA, 64, 64, 7), bc3b = tex(PIPE_FORMAT_DXT5_SRGBA, 64, 64, 4);
    /* whole-level BC copies (the MiniEngine mips 64..8) */
    for (unsigned l = 0; l < 4; ++l) {
        const unsigned e = 64u >> l;
        assert(texture_region_copy_valid(&bc3b, l, 0, 0, 0, &bc3, l, 0, 0, 0, e, e, 1));
    }
    /* the 2x2 and 1x1 levels: a partial block at the level's edge */
    assert(texture_region_copy_valid(&bc3, 5, 0, 0, 0, &bc3, 5, 0, 0, 0, 2, 2, 1));
    assert(texture_region_copy_valid(&bc3, 6, 0, 0, 0, &bc3, 6, 0, 0, 0, 1, 1, 1));
    /* the runtime's block-aligned box of those levels (D3D validates BC copy
     * boxes against the block-aligned level size; EXP1907 C72 in D3D11) */
    assert(texture_region_copy_valid(&bc3, 5, 0, 0, 0, &bc3, 5, 0, 0, 0, 4, 4, 1));
    assert(texture_region_copy_valid(&bc3, 6, 0, 0, 0, &bc3, 6, 0, 0, 0, 4, 4, 1));
    assert(!texture_region_copy_valid(&bc3, 5, 0, 0, 0, &bc3, 5, 0, 0, 0, 8, 4, 1));
    /* unaligned origins and partial blocks inside the level */
    assert(!texture_region_copy_valid(&bc3, 0, 0, 2, 0, &bc3, 0, 0, 0, 0, 8, 8, 1));
    assert(!texture_region_copy_valid(&bc3, 0, 0, 0, 0, &bc3, 0, 0, 4, 2, 8, 8, 1));
    assert(!texture_region_copy_valid(&bc3, 0, 0, 0, 0, &bc3, 0, 0, 0, 0, 6, 8, 1));
    /* outside the level, missing level */
    assert(!texture_region_copy_valid(&bc3, 0, 0, 60, 0, &bc3, 0, 0, 0, 0, 8, 8, 1));
    assert(!texture_region_copy_valid(&bc3b, 4, 0, 0, 0, &bc3, 4, 0, 0, 0, 4, 4, 1));
    /* element mismatch: BC1 (8-byte blocks) vs BC3, BC3 vs a 16-byte texel */
    pipe_resource bc1 = tex(PIPE_FORMAT_DXT1_RGBA, 64, 64, 1), rgba32 = tex(PIPE_FORMAT_R32G32B32A32_UINT, 16, 16, 1);
    assert(!texture_region_copy_valid(&bc1, 0, 0, 0, 0, &bc3, 0, 0, 0, 0, 8, 8, 1));
    assert(!texture_region_copy_valid(&rgba32, 0, 0, 0, 0, &bc3, 0, 0, 0, 0, 4, 4, 1));
    /* texel formats keep working, layer ranges are bounded */
    pipe_resource a = tex(PIPE_FORMAT_R8G8B8A8_UNORM, 32, 32, 1, 4), b = tex(PIPE_FORMAT_R8G8B8A8_UINT, 32, 32, 1, 4);
    assert(texture_region_copy_valid(&a, 0, 1, 3, 5, &b, 0, 2, 7, 1, 9, 11, 2));
    assert(!texture_region_copy_valid(&a, 0, 3, 0, 0, &b, 0, 0, 0, 0, 8, 8, 2));
    return 0;
}
'''


class TextureRegionCopy(unittest.TestCase):
    def test_bc_and_texel_region_copies(self):
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        if not build or not mesa:
            self.skipTest('host Mesa build not available')
        owner = OWNER.read_text()
        code = ''.join(block(owner, marker) + '\n' for marker in (
            'static unsigned level_extent(unsigned extent, unsigned level) {',
            'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {',
            'static unsigned texture_block(enum pipe_format format, unsigned *bw, unsigned *bh) {',
            'static bool texture_region_copy_valid('))
        libs = [build / p for p in ('src/util/libmesa_util.a', 'src/util/libmesa_util_simd.a',
                                    'src/c11/impl/libmesa_util_c11.a', 'src/util/blake3/libblake3.a')]
        with tempfile.TemporaryDirectory(prefix='d3d12-region-copy-') as tmp:
            source, binary = Path(tmp) / 'copy.cpp', Path(tmp) / 'copy'
            source.write_text(FIXTURE.replace('/* PRODUCTION */', code))
            run = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-fsanitize=address,undefined',
                                  *host_flags(build), '-I', str(mesa / 'src/gallium/include'), str(source), '-o', str(binary),
                                  *map(str, libs), '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr[-3000:])
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
