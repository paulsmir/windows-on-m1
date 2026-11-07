"""Texture creation success follows native resource, not buffer-only GPUVA.
Microsoft GetGPUVirtualAddress returns zero for every texture. Actual scene
helper is extracted; query/screen/BO mapping boundaries are controlled and are
not claimed as native layout, real allocation, or hardware proof.
"""
from pathlib import Path
import subprocess,tempfile,unittest
from test_d3d12_view_mapping import block
from test_dxbc_to_nir import local_tree,host_flags
ROOT=Path(__file__).resolve().parents[1]
OWNER=ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
SCENE=ROOT/'drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c'
def fixture(owner,scene,root=ROOT,oracle=None):
 prefix=(root/'tests/d3d12_volume_query_fixture.cpp').read_text().split('int main()')[0]
 if '#define E_FAIL' not in prefix:prefix=prefix.replace('#define E_INVALIDARG (-1)','#define E_FAIL (-4)\n#define E_INVALIDARG (-1)')
 prefix=prefix.replace('static UINT query_calls=0,create_calls=0;','static UINT query_calls=0,create_calls=0,release_calls=0,map_calls=0;\nstatic bool fail_create=false;')
 fake='static UINT64 AgxWin32AsahiBufferGpuAddress(pipe_resource *) {return 0x1000000;}'
 assert fake in prefix
 actual=block(scene,'APPLE_AGX_U64 AgxWin32AsahiBufferGpuAddress(')
 boundary='''using APPLE_AGX_U64=unsigned long long;
struct MockBo {UINT64 gpu;};
struct agx_resource {pipe_resource base;MockBo *bo;};
static struct agx_resource *agx_resource(pipe_resource *p){return reinterpret_cast<struct agx_resource *>(p);}
static UINT64 agx_map_gpu(struct agx_resource *r){++map_calls;return r->bo->gpu;}
'''
 prefix=prefix.replace(fake,boundary+actual)
 prefix=prefix.replace('static void AgxWin32AsahiResourceRelease(pipe_resource **r) {*r=nullptr;}','static void AgxWin32AsahiResourceRelease(pipe_resource **r) {++release_calls;*r=nullptr;}')
 prefix=prefix.replace('return &created_resource;','return fail_create?nullptr:&created_resource;')
 funcs='\n'.join(block(owner,n) for n in ['static unsigned block_bytes(', 'static unsigned texture_block(', 'extern "C" unsigned AgxD3d12FormatBlock(', 'static bool texture1d_template(', 'extern "C" HRESULT AgxD3d12QueryTexture1DLayout(', 'extern "C" HRESULT AgxD3d12OwnerCreateTexture1D(', 'static bool texture3d_template(', 'static bool texture3d_layout(', 'extern "C" HRESULT AgxD3d12OwnerCreateTexture3D('])
 record='typedef struct {unsigned long long Bytes, NativeBytes, DepthStride, MipOffsets[16];} AGX_D3D12_VOLUME_LAYOUT;'
 return prefix.replace('/* VOLUME_LAYOUT_RECORD */',record).replace('/* ACTUAL_QUERY */',funcs).replace('/* ACTUAL_TEXTURE_BYTES */','')+(oracle or root/'tests/d3d12_texture_creation_fixture.cpp').read_text()
class TextureCreation(unittest.TestCase):
 def test_actual_texture_constructors_accept_resource_with_zero_buffer_gpuva(self):
  b=local_tree('accelerated-desktop-ad03','mesa-build');m=local_tree('reference','mesa')
  if not b or not m:self.skipTest('host Mesa unavailable')
  source=fixture(OWNER.read_text(),SCENE.read_text())
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)/'creation.cpp';p.write_text(source);exe=Path(t)/'creation'
   libs=[b/x for x in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
   built=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',*host_flags(b),'-I',str(m/'src/gallium/include'),'-I',str(ROOT/'drivers/apple-agx/mesa/winsys'),str(p),'-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True)
   self.assertEqual(run.returncode,0,run.stdout+run.stderr)
   self.assertIn('failures=0',run.stdout)
if __name__=='__main__':unittest.main()
