"""C83: D3D12 CopyTextureRegion between a BC format and the integer format of
its block size (format reinterpretation), executed with the production code.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1913 GravityMark D3D12
copies its GPU-encoded blocks from an R32G32B32A32_UINT 128x128 texture into
a BC6H texture (one 16-byte texel per 4x4 block). The owner refused every
such copy (texture_region_copy_valid requires one element shape), so the
execute failed with E_INVALIDARG and the device was removed. The copy must
place texel (x, y) of the integer level at block (x, y) of the BC level and
back, take the source box in source units and the destination offset in
destination texels, keep the 1x1/2x2 BC tails (a whole block each), leave
neighbouring blocks untouched and refuse misaligned or oversized boxes and
pairs of different element sizes.
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
#include <cstdio>
#include <cstring>
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/format/u_format.h"
using HRESULT = int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define SUCCEEDED(x) ((x) >= 0)
using UINT=unsigned;
struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingCopies;};
struct AGX_D3D12_BUFFER {void *Resource;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *) {}};
static void receipt(const char *,HRESULT,UINT,UINT,UINT) {}
/* PRODUCTION */
/* a fake level store: element rows of each resource's level 0..2, layer 0..1,
 * mapped as Asahi does (a BC box maps whole blocks: stride = blocks * bytes) */
struct Store { pipe_resource r; unsigned char bytes[2][3][64 * 64 * 16]; };
static Store *stores[8]; static unsigned nstores, maps;
static pipe_transfer transfers[2]; static unsigned ntransfer; static bool gpu_only; static unsigned gpu_copies;
static Store *find(pipe_resource *r) { for (unsigned i = 0; i < nstores; ++i) if (&stores[i]->r == r) return stores[i]; return nullptr; }
static void *map(pipe_context *, pipe_resource *r, unsigned level, unsigned, const pipe_box *box, pipe_transfer **t) {
    Store *s = find(r); assert(s && !(r->flags & PIPE_RESOURCE_FLAG_UNMAPPABLE));
    unsigned bw = 0, bh = 0; const unsigned bytes = texture_block(r->format, &bw, &bh);
    const unsigned lw = level_extent(r->width0, level), lh = level_extent(r->height0, level);
    assert(box->x % bw == 0 && box->y % bh == 0 && box->width > 0 && box->height > 0);
    assert(unsigned(box->x + box->width) <= lw && unsigned(box->y + box->height) <= lh); /* Asahi maps inside the level */
    const unsigned row = (lw + bw - 1) / bw * bytes;
    ++maps; pipe_transfer *x = &transfers[ntransfer++ % 2]; *x = {};
    x->stride = row; x->layer_stride = sizeof(s->bytes[0]); *t = x; /* layer z+1 of the level */
    return s->bytes[box->z][level] + (box->y / bh) * row + (box->x / bw) * bytes;
}
static void unmap(pipe_context *, pipe_transfer *) {}
static Store *make(pipe_format f, unsigned w, unsigned h, unsigned levels, unsigned layers) {
    Store *s = new Store(); s->r.target = layers > 1 ? PIPE_TEXTURE_2D_ARRAY : PIPE_TEXTURE_2D; s->r.format = f;
    s->r.width0 = w; s->r.height0 = h; s->r.depth0 = 1; s->r.array_size = layers; s->r.last_level = levels - 1;
    for (unsigned i = 0; i < sizeof(s->bytes); ++i) (&s->bytes[0][0][0])[i] = (unsigned char)(i * 31 + nstores * 7 + 1);
    if (gpu_only && util_format_is_compressed(f)) s->r.flags=PIPE_RESOURCE_FLAG_UNMAPPABLE;
    stores[nstores++] = s; return s;
}
static HRESULT run(pipe_context *pipe, Store *d, unsigned dl, unsigned dz, unsigned dx, unsigned dy, Store *s, unsigned sl,
                   unsigned sz, unsigned sx, unsigned sy, unsigned w, unsigned h, unsigned layers) {
    AGX_D3D12_OWNER owner={pipe,0};AGX_D3D12_BUFFER dst={&d->r},src={&s->r};
    HRESULT hr=AgxD3d12OwnerCopyTextureRegion(&owner,&dst,dl,dz,dx,dy,&src,sl,sz,sx,sy,w,h,layers);
    /* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A copy-only raw BC
     * blit must make tile-remap/present gates flush queued work first. */
    if(SUCCEEDED(hr)) assert(owner.PendingCopies==1);
    return hr;
}
static void blit(pipe_context *, const pipe_blit_info *b) {
    Store *d=find(b->dst.resource), *s=find(b->src.resource);assert(d && s);
    unsigned bytes=util_format_get_blocksize(b->src.format);
    assert(b->src.format==b->dst.format && (bytes==8 || bytes==16) && b->mask==PIPE_MASK_RGBA);
    assert(b->src.box.width==b->dst.box.width && b->src.box.height==b->dst.box.height);
    for(int z=0;z<b->src.box.depth;++z) for(int y=0;y<b->src.box.height;++y) {
      unsigned sr=util_format_get_nblocksx(s->r.format,level_extent(s->r.width0,b->src.level))*bytes;
      unsigned dr=util_format_get_nblocksx(d->r.format,level_extent(d->r.width0,b->dst.level))*bytes;
      memcpy(d->bytes[b->dst.box.z+z][b->dst.level]+(b->dst.box.y+y)*dr+b->dst.box.x*bytes,
             s->bytes[b->src.box.z+z][b->src.level]+(b->src.box.y+y)*sr+b->src.box.x*bytes,b->src.box.width*bytes);
    }
    ++gpu_copies;
}
static void run_suite(bool gpu) {
    gpu_only=gpu;nstores=maps=gpu_copies=0;
    pipe_context pipe = {}; pipe.texture_map = map; pipe.texture_unmap = unmap; pipe.blit=blit;
    /* the EXP1913 shape: R32G32B32A32_UINT 16x16 (3 levels) -> BC6H 64x64, a 4x4 texel box to block (1, 2) */
    Store *u = make(PIPE_FORMAT_R32G32B32A32_UINT, 16, 16, 3, 2), *bc = make(PIPE_FORMAT_BPTC_RGB_UFLOAT, 64, 64, 3, 2);
    Store before = *bc;
    assert(run(&pipe, bc, 0, 1, 4, 8, u, 0, 1, 8, 4, 4, 4, 1) == S_OK);
    const unsigned brow = 16 * 16; /* BC6H 64 wide: 16 blocks of 16 bytes */
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x) {
            const bool inside = x >= 1 && x < 5 && y >= 2 && y < 6;
            const unsigned char *got = bc->bytes[1][0] + y * brow + x * 16;
            const unsigned char *want = inside ? u->bytes[1][0] + (4 + y - 2) * 256 + (8 + x - 1) * 16 : before.bytes[1][0] + y * brow + x * 16;
            assert(!memcmp(got, want, 16));
        }
    assert(!memcmp(bc->bytes[0][0], before.bytes[0][0], sizeof(bc->bytes[0][0]))); /* other layer untouched */
    /* back: BC6H 8x8 texels at (8, 4) -> 2x2 texels of the integer level at (3, 5) */
    Store *u2 = make(PIPE_FORMAT_R32G32B32A32_SINT, 16, 16, 1, 1);
    assert(run(&pipe, u2, 0, 0, 3, 5, bc, 0, 1, 8, 4, 8, 8, 1) == S_OK);
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x)
            assert(!memcmp(u2->bytes[0][0] + (5 + y) * 256 + (3 + x) * 16, bc->bytes[1][0] + (1 + y) * brow + (2 + x) * 16, 16));
    /* the BC level tails: level 2 is 16x16 texels; a 1x1 integer texel fills a whole 4x4 block of a 2x2 level */
    Store *tail = make(PIPE_FORMAT_BPTC_RGBA_UNORM, 8, 8, 3, 1);
    assert(run(&pipe, tail, 2, 0, 0, 0, u, 2, 0, 3, 2, 1, 1, 1) == S_OK);
    assert(!memcmp(tail->bytes[0][2], u->bytes[0][2] + 2 * 64 + 3 * 16, 16));
    /* and back from that 2x2 tail: the runtime's whole-block box 4x4, or the level's 2x2 */
    assert(run(&pipe, u2, 0, 0, 0, 0, tail, 2, 0, 0, 0, 4, 4, 1) == S_OK);
    assert(run(&pipe, u2, 0, 0, 0, 0, tail, 2, 0, 0, 0, 2, 2, 1) == S_OK);
    assert(!memcmp(u2->bytes[0][0], tail->bytes[0][2], 16));
    /* two layers in one call */
    assert(run(&pipe, bc, 1, 0, 0, 0, u, 1, 0, 0, 0, 8, 8, 2) == S_OK);
    assert(!memcmp(bc->bytes[1][1] + 7 * (8 * 16) + 7 * 16, u->bytes[1][1] + 7 * 128 + 7 * 16, 16));
    /* refusals: misaligned destination, a box past the destination, partial blocks inside the BC source,
     * different element sizes (BC1 8-byte blocks vs 16-byte texels), same-shape pairs (not this path), layers */
    const unsigned before_maps = maps;
    pipe_box db, sb; unsigned rows; unsigned long long row;
    assert(!reinterpret_copy_boxes(&bc->r, 0, 0, 2, 0, &u->r, 0, 0, 0, 0, 4, 4, 1, &db, &sb, &rows, &row));
    assert(!reinterpret_copy_boxes(&bc->r, 0, 0, 56, 0, &u->r, 0, 0, 0, 0, 4, 4, 1, &db, &sb, &rows, &row));
    assert(!reinterpret_copy_boxes(&u2->r, 0, 0, 0, 0, &bc->r, 0, 0, 0, 0, 6, 8, 1, &db, &sb, &rows, &row));
    assert(!reinterpret_copy_boxes(&u2->r, 0, 0, 0, 0, &bc->r, 0, 0, 2, 0, 4, 4, 1, &db, &sb, &rows, &row));
    Store *bc1 = make(PIPE_FORMAT_DXT1_RGBA, 64, 64, 1, 1);
    assert(!reinterpret_copy_boxes(&bc1->r, 0, 0, 0, 0, &u->r, 0, 0, 0, 0, 4, 4, 1, &db, &sb, &rows, &row));
    assert(!reinterpret_copy_boxes(&u2->r, 0, 0, 0, 0, &u->r, 0, 0, 0, 0, 4, 4, 1, &db, &sb, &rows, &row));
    assert(!reinterpret_copy_boxes(&bc->r, 0, 1, 0, 0, &u->r, 0, 0, 0, 0, 4, 4, 2, &db, &sb, &rows, &row));
    assert(maps == before_maps);
    if(gpu) assert(gpu_copies==6 && !maps);
    for(unsigned i=0;i<nstores;++i) delete stores[i];
}
int main() {
    run_suite(false);run_suite(true);
    puts("C83_REINTERPRET_COPY_PASS");
    return 0;
}
'''


class ReinterpretCopy(unittest.TestCase):
    def test_bc_integer_reinterpret_copies(self):
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        mesa = local_tree('reference', 'mesa')
        if not build or not mesa:
            self.skipTest('host Mesa build not available')
        owner = OWNER.read_text()
        self.assertIn('static bool reinterpret_copy_boxes(', owner)
        code = ''.join(block(owner, marker) + '\n' for marker in (
            'static unsigned level_extent(unsigned extent, unsigned level) {',
            'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {',
            'static unsigned texture_block(enum pipe_format format, unsigned *bw, unsigned *bh) {',
            'static enum pipe_format bc_staging_format(',
            'static void gpu_copy_blocks(',
            'static bool reinterpret_copy_axis(',
            'static bool reinterpret_copy_boxes(',
            'static HRESULT copy_reinterpret_blocks(',
            'static bool texture_region_copy_valid(',
            'static unsigned region_copy_extent(',
            'extern "C" HRESULT AgxD3d12OwnerCopyTextureRegion('))
        source = FIXTURE.replace('/* PRODUCTION */', code)
        with tempfile.TemporaryDirectory(prefix='d3d12-reinterpret-') as tmp:
            path = Path(tmp) / 'copy.cpp'
            exe = Path(tmp) / 'copy'
            path.write_text(source)
            libs = [build / p for p in ['src/util/libmesa_util.a', 'src/util/libmesa_util_simd.a',
                                        'src/c11/impl/libmesa_util_c11.a', 'src/util/blake3/libblake3.a']]
            run = subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-fsanitize=address,undefined',
                                  *host_flags(build), '-I', str(mesa / 'src/gallium/include'), str(path), '-o', str(exe),
                                  *map(str, libs), '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread'],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr[-4000:])
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('C83_REINTERPRET_COPY_PASS', run.stdout)


if __name__ == '__main__':
    unittest.main()
