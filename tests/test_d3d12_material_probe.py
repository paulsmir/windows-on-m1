"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The RC44 discriminator must not report PASS for white/default output, a lost
StartInstance, wrong texture descriptor/layer, or an unwritten pixel. Execute
its actual readback oracle with independently specified images and corruptions.
Compile its full D3D12 call surface against the pinned SDK in C and x86 /TP.
This is an oracle/ABI test, not a claim that Windows or GPU pixels were tested.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICD = ROOT / 'drivers/apple-agx/windows/icd'
SDK = ROOT / '.local/reference/winsdk26100'


class MaterialProbe(unittest.TestCase):
    def test_readback_rejects_wrong_material_instance_and_clear(self):
        source = r'''
#include <assert.h>
#include <string.h>
#include "agx_d3d12_material_oracle.h"
int main(void) {
    unsigned char colour[1024], index[1024];
    const unsigned char gold[2][2][4] = {
      {{62,64,70,255}, {3,2,1,255}},
      {{150,106,78,255}, {5,7,0,255}}
    };
    for (unsigned c=0;c<2;c++) {
      memset(colour,0xcd,sizeof(colour)); memset(index,0xcd,sizeof(index));
      for (unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++) {
        memcpy(colour+y*256+x*4,gold[c][0],4);
        memcpy(index+y*256+x*4,gold[c][1],4);
      }
      assert(rc44_bad_pixels(colour,index,c)==0);
      /* One bad pixel anywhere must fail, including the last row/column. */
      for (unsigned y=0;y<4;y++) for(unsigned x=0;x<4;x++) for(unsigned k=0;k<4;k++) {
        unsigned o=y*256+x*4+k;
        colour[o]^=4; assert(rc44_bad_pixels(colour,index,c)==1); colour[o]^=4;
        index[o]^=4; assert(rc44_bad_pixels(colour,index,c)==1); index[o]^=4;
      }
      assert(rc44_bad_pixels(colour,index,1-c)==16);
      memset(colour,255,sizeof(colour)); assert(rc44_bad_pixels(colour,index,c)==16);
      memset(colour,0,sizeof(colour)); assert(rc44_bad_pixels(colour,index,c)==16);
    }
    assert(rc44_bad_pixels(colour,index,2)!=0);
    assert(rc44_bad_pixels(0,index,0)!=0);
    assert(rc44_bad_pixels(colour,0,0)!=0);
}
'''
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'test.c').write_text(source)
            for language in ('c', 'c++'):
                with self.subTest(language=language):
                    r = subprocess.run(['clang', '-x', language, '-I', str(ICD),
                                        '-fsanitize=address,undefined', str(p/'test.c'), '-o', str(p/'test')],
                                       capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stderr)
                    r = subprocess.run([str(p/'test')], capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stdout+r.stderr)

    def test_sdk_call_surface_c_and_x86_cpp(self):
        if not (SDK/'msvc/vcruntime.h').exists():
            self.skipTest('pinned Windows SDK/MSVC headers absent')
        source = r'''
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <d3d12.h>
#include <stdio.h>
#include <string.h>
static ID3D12Resource *committed_buffer(ID3D12Device*, D3D12_HEAP_TYPE, D3D12_RESOURCE_STATES);
static ID3D12Resource *uav_buffer(ID3D12Device*);
static void transition(ID3D12GraphicsCommandList*, ID3D12Resource*, D3D12_RESOURCE_STATES, D3D12_RESOURCE_STATES);
static HRESULT run_list(ID3D12CommandQueue*, ID3D12Fence*, ID3D12GraphicsCommandList*, UINT64, const char*);
#include "agx_d3d12_material_probe.h"
'''
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'test.c'; p.write_text(source)
            for target, language in (('aarch64', 'c'), ('x86_64', 'c'), ('i686', 'c++')):
                with self.subTest(target=target, language=language):
                    cmd = ['clang', '-target', target+'-pc-windows-msvc', '-x', language,
                           '-fms-extensions', '-fms-compatibility', '-fsyntax-only',
                           '-Werror=implicit-function-declaration', '-Wno-ignored-attributes',
                           '-Wno-ignored-pragma-intrinsic', '-Wno-nonportable-include-path',
                           '-Wno-pragma-pack', '-Wno-undefined-internal', '-I', str(ICD)]
                    for sub in ('um', 'shared', 'ucrt', 'msvc'):
                        cmd += ['-I', str(SDK/sub)]
                    r = subprocess.run([*cmd, str(p)], capture_output=True, text=True)
                    self.assertEqual(r.returncode, 0, r.stdout+r.stderr)

if __name__ == '__main__':
    unittest.main()
