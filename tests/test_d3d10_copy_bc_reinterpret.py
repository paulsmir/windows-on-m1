"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

EXP1909: a legal uint4 cube -> BC6H cube-array copy removed GravityMark's
D3D11 device. Copies preserve block bytes, use source texels and destination
pixels, and validate each side's block size. Equal byte size alone is not
DXGI compatibility. Tiny BC mips still contain one full block.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def function(text, name):
    start = text.index('\n' + name + '(')
    start = text.rindex('static ', 0, start)
    return text[start:text.index('\n}\n', start) + 3]


def compile_run(source):
    with tempfile.TemporaryDirectory(prefix='d3d10-bc-copy-') as tmp:
        src, exe = Path(tmp) / 'copy.cpp', Path(tmp) / 'copy'
        src.write_text(source)
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-Wall',
                        '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                        str(src), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=10)


class CopyBcReinterpret(unittest.TestCase):
    def test_region_admits_block_reinterpretation(self):
        text = SCRIPT.read_text()
        body = text[text.index("'ResourceCopyRegion','''"):]
        body = body[:body.index("''')")]
        self.assertIn('AgxD3d10BlockCopyCompatible(source->Format, destination->Format)', body)
        self.assertIn('AgxD3d10CopyBlockAxis', body)
        self.assertIn('AgxD3d10CopyBlocks', body)

    def test_format_pairs_and_scaled_bounds(self):
        text = SCRIPT.read_text()
        self.assertIn('\nAgxD3d10BlockCopyCompatible(', text)
        helpers = '\n'.join(function(text, n) for n in (
            'AgxD3d10BlockCopyClass', 'AgxD3d10BlockCopyCompatible',
            'AgxD3d10CopyBlockAxis'))
        names = sorted(set(re.findall(r'DXGI_FORMAT_\w+', helpers)) | {
            'DXGI_FORMAT_R32G32B32A32_FLOAT', 'DXGI_FORMAT_R16G16B16A16_FLOAT'})
        enum = 'enum DXGI_FORMAT {' + ','.join(names) + '};\n'
        compile_run('#include <cassert>\n#include <cstdint>\n' + enum + helpers + r'''
int main() {
  assert(AgxD3d10BlockCopyCompatible(DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_BC6H_UF16));
  assert(AgxD3d10BlockCopyCompatible(DXGI_FORMAT_BC7_UNORM_SRGB, DXGI_FORMAT_R32G32B32A32_SINT));
  assert(AgxD3d10BlockCopyCompatible(DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_BC1_UNORM));
  assert(AgxD3d10BlockCopyCompatible(DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_R32G32_UINT));
  assert(!AgxD3d10BlockCopyCompatible(DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_BC6H_UF16));
  assert(!AgxD3d10BlockCopyCompatible(DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_BC7_UNORM));
  assert(!AgxD3d10BlockCopyCompatible(DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_BC1_UNORM));
  assert(!AgxD3d10BlockCopyCompatible(DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC7_UNORM));
  unsigned s=0,d=0;
  assert(AgxD3d10CopyBlockAxis(128,512,1,4,0,128,0,&s,&d) && s==128 && d==512);
  assert(AgxD3d10CopyBlockAxis(512,128,4,1,0,512,0,&s,&d) && s==512 && d==128);
  assert(AgxD3d10CopyBlockAxis(7,24,1,4,2,5,8,&s,&d) && s==3 && d==12);
  assert(!AgxD3d10CopyBlockAxis(128,512,1,4,0,128,4,&s,&d));
  assert(!AgxD3d10CopyBlockAxis(128,512,1,4,0,2,1,&s,&d));
  assert(!AgxD3d10CopyBlockAxis(512,128,4,1,1,8,0,&s,&d));
  assert(!AgxD3d10CopyBlockAxis(512,128,4,1,0,7,0,&s,&d));
  assert(AgxD3d10CopyBlockAxis(2,1,4,1,0,4,0,&s,&d) && s==2 && d==1);
  assert(AgxD3d10CopyBlockAxis(1,2,1,4,0,1,0,&s,&d) && s==1 && d==2);
  assert(!AgxD3d10CopyBlockAxis(2,1,4,1,4,8,0,&s,&d));
  assert(!AgxD3d10CopyBlockAxis(128,512,1,4,0,~0u,0,&s,&d));
}
''')

    def test_mapped_copy_preserves_rows_layers_and_unmaps_on_failure(self):
        text = SCRIPT.read_text()
        self.assertIn('\nAgxD3d10CopyBlocks(', text)
        body = function(text, 'AgxD3d10CopyBlocks')
        compile_run(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
typedef int HRESULT;
#define S_OK 0
#define E_OUTOFMEMORY -1
#define PIPE_MAP_READ 1
#define PIPE_MAP_WRITE 2
#define PIPE_MAP_DISCARD_RANGE 4
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_resource { unsigned format; unsigned char bytes[256]; };
struct pipe_transfer { unsigned stride, layer_stride; };
struct pipe_context {
 void *(*texture_map)(pipe_context*,pipe_resource*,unsigned,unsigned,const pipe_box*,pipe_transfer**);
};
static unsigned util_format_get_blocksize(unsigned f) { return f; }
static unsigned util_format_get_blockwidth(unsigned) { return 4; }
static unsigned util_format_get_blockheight(unsigned) { return 4; }
static unsigned unmaps, maps, fail;
static pipe_transfer tr[2]={{32,96},{48,128}};
static void pipe_texture_unmap(pipe_context*,pipe_transfer*) { ++unmaps; }
static void *map(pipe_context*,pipe_resource *r,unsigned,unsigned,const pipe_box*,pipe_transfer **t) {
 unsigned i=maps++; if (fail==i+1) return nullptr; *t=&tr[i]; return r->bytes;
}
''' + body + r'''
int main() {
 pipe_context p={map}; pipe_resource a={16,{}},b={16,{}};
 for (unsigned i=0;i<sizeof(a.bytes);++i) a.bytes[i]=(unsigned char)(i*13+7);
 memset(b.bytes,0xcc,sizeof(b.bytes));
 pipe_box sb={0,0,0,2,2,2},db={0,0,0,1,1,2};
 assert(AgxD3d10CopyBlocks(&p,&b,0,&db,&a,0,&sb)==S_OK);
 assert(maps==2 && unmaps==2);
 for (unsigned z=0;z<2;++z) {
   assert(memcmp(b.bytes+z*128,a.bytes+z*96,16)==0);
   for (unsigned i=16;i<128;++i) assert(b.bytes[z*128+i]==0xcc);
 }
 maps=unmaps=0;
 memset(b.bytes,0xcc,sizeof(b.bytes));
 sb.width=sb.height=8; db.width=db.height=2;
 assert(AgxD3d10CopyBlocks(&p,&b,0,&db,&a,0,&sb)==S_OK);
 for (unsigned z=0;z<2;++z) for (unsigned y=0;y<2;++y) {
   assert(memcmp(b.bytes+z*128+y*48,a.bytes+z*96+y*32,32)==0);
   for (unsigned i=32;i<48;++i) assert(b.bytes[z*128+y*48+i]==0xcc);
 }
 maps=unmaps=0; fail=2;
 assert(AgxD3d10CopyBlocks(&p,&b,0,&db,&a,0,&sb)==E_OUTOFMEMORY);
 assert(unmaps==1);
 maps=unmaps=0; fail=1;
 assert(AgxD3d10CopyBlocks(&p,&b,0,&db,&a,0,&sb)==E_OUTOFMEMORY);
 assert(unmaps==0);
}
''')


if __name__ == '__main__':
    unittest.main()
