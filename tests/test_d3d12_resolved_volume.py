"""Resolved volume contract: real DDI/owner functions, exact pinned WDK records."""
from pathlib import Path
import os,subprocess,tempfile,unittest,re
from test_d3d12_view_mapping import block
from d3d12_volume_fixture_dependencies import line_query_dependencies
from test_dxbc_to_nir import local_tree,host_flags
ROOT=Path(__file__).resolve().parents[1]
DDI=ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
OWNER=ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
class ResolvedVolume(unittest.TestCase):
 def test_bounded_volume_has_resolved_allocation_before_owner(self):
  b=local_tree('accelerated-desktop-ad03','mesa-build');m=local_tree('reference','mesa')
  if not b or not m:self.skipTest('host Mesa unavailable')
  owner=OWNER.read_text();ddi=DDI.read_text()
  funcs='\n'.join(block(owner,x) for x in ['static unsigned block_bytes(', 'extern "C" unsigned AgxD3d12FormatBlockBytes(', 'static bool texture1d_template(', 'extern "C" HRESULT AgxD3d12QueryTexture1DLayout(', 'static bool texture3d_template(', 'static bool texture3d_layout(', 'extern "C" HRESULT AgxD3d12QueryTexture3DLayout(', 'extern "C" HRESULT AgxD3d12QueryTexture3D(', 'extern "C" HRESULT AgxD3d12OwnerCreateTexture3D('])
  source=(ROOT/'tests/d3d12_volume_query_fixture.cpp').read_text().split('int main()')[0]
  if 'D3D12DDI_RT_TEXTURE1D=' not in source:source=source.replace('D3D12DDI_RT_TEXTURE2D=3,','D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RT_TEXTURE2D=3,')
  funcs+='\n'+line_query_dependencies(owner)
  funcs+='\n'+block(ddi,'static bool LineLayoutInfo(')
  funcs+='\n'+block(ddi,'static bool VolumeLayoutInfo(')
  record='typedef struct {unsigned long long Bytes, NativeBytes, DepthStride, MipOffsets[16];} AGX_D3D12_VOLUME_LAYOUT;'
  source=source.replace('/* VOLUME_LAYOUT_RECORD */',record).replace('/* ACTUAL_QUERY */',funcs).replace('/* ACTUAL_TEXTURE_BYTES */',block(ddi,'static UINT64 TextureBytes('))
  source+='''int main(){ D3D12DDIARG_CREATERESOURCE_0003 r={};r.ResourceType=D3D12DDI_RT_TEXTURE3D;r.SampleDesc.Count=1;r.Width=r.Height=4;r.DepthOrArraySize=8;r.MipLevels=2;r.Format=42;assert(TextureBytes(&r)==0x20000);assert(query_calls==1&&!create_calls); }'''
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)/'fixture.cpp';p.write_text(source);exe=Path(t)/'test'
   libs=[b/x for x in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
   r=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',*host_flags(b),'-I',str(m/'src/gallium/include'),'-I',str(ROOT/'drivers/apple-agx/mesa/winsys'),str(p),'-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)

 def test_exact_wdk_registry_and_getcaps_buffers(self):
  header=(ROOT/'.local/reference/wdk26100/d3d12umddi.h').read_text()
  names=['D3D12DDICAPS_TYPE','D3D12DDI_TEXTURE_LAYOUT_CAPS','D3D12DDI_TEXTURE_LAYOUT_CAPS_0001','D3D12DDI_TEXTURE_LAYOUT','D3D12DDI_SWIZZLE_PATTERN','D3D12DDI_SWIZZLE_BIT_ENTRY','D3D12DDI_SWIZZLE_PATTERN_DESC_0003','D3D12DDI_SWIZZLE_PATTERN_DESC_0004','D3D12DDI_ROW_MAJOR_LAYOUT_SUB_CAPS','D3D12DDI_ROW_MAJOR_LAYOUT_FLAGS','D3D12DDI_ROW_MAJOR_LAYOUT_CAPS','D3D12DDIARG_GETCAPS']
  declarations=[]
  for name in names:
   found=re.search(r'typedef (?:struct|enum) '+name+r'\s*\{.*?\}\s*'+name+r'\s*;',header,re.S)
   self.assertIsNotNone(found,name);declarations.append(found.group())
  ddi=DDI.read_text()
  functions='\n'.join(block(ddi,x) for x in ['template<class T, class F> static HRESULT Answer(', 'static HRESULT VolumeLayoutCaps(', 'static HRESULT APIENTRY GetCaps('])
  source=(ROOT/'tests/d3d12_resolved_caps_fixture.cpp').read_text().replace('/* PINNED_WDK */','\n'.join(declarations)).replace('/* ACTUAL_CAPS */',functions)
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)/'caps.cpp';p.write_text(source);exe=Path(t)/'test'
   r=subprocess.run(['clang++','-std=c++17','-include','initializer_list','-fsanitize=address,undefined',str(p),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr)
 def test_real_allocation_resolved_create_existing_and_subresource_roundtrip(self):
  b=local_tree('accelerated-desktop-ad03','mesa-build');m=local_tree('reference','mesa')
  if not b or not m:self.skipTest('host Mesa unavailable')
  owner=OWNER.read_text();ddi=DDI.read_text();self.assertIn('return CreateVolumeResource(device,heap_arg,heap,resource_arg,resource);',block(ddi,'static HRESULT APIENTRY CreateHeapAndResource('));h=(ROOT/'.local/reference/wdk26100/d3d12umddi.h').read_text()
  funcs='\n'.join(block(owner,x) for x in ['static unsigned block_bytes(', 'extern "C" unsigned AgxD3d12FormatBlockBytes(', 'static bool texture1d_template(', 'extern "C" HRESULT AgxD3d12QueryTexture1DLayout(', 'static bool texture3d_template(', 'static bool texture3d_layout(', 'extern "C" HRESULT AgxD3d12QueryTexture3DLayout(', 'extern "C" HRESULT AgxD3d12QueryTexture3D(', 'extern "C" HRESULT AgxD3d12OwnerCreateTexture3D('])
  funcs+='\n'+line_query_dependencies(owner)
  funcs+='\n'+block(ddi,'static bool LineLayoutInfo(')
  funcs+='\n'+block(ddi,'static bool VolumeLayoutInfo(')
  prefix=(ROOT/'tests/d3d12_volume_query_fixture.cpp').read_text().split('int main()')[0]
  prefix=prefix.replace('using D3D12DDI_TEXTURE_LAYOUT=unsigned;','').replace('#define D3D12DDI_TL_UNDEFINED 0','').replace('unsigned Flags,Layout;','unsigned Flags;D3D12DDI_TEXTURE_LAYOUT Layout;').replace('enum {D3D12DDI_RT_TEXTURE2D=3,','enum {D3D12DDI_RT_BUFFER=1,D3D12DDI_RT_TEXTURE2D=3,')
  if 'D3D12DDI_RT_TEXTURE1D=' not in prefix:prefix=prefix.replace('D3D12DDI_RT_TEXTURE2D=3,','D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RT_TEXTURE2D=3,')
  prefix='using UINT32=unsigned;using UINT16=unsigned short;\n'+prefix
  record='typedef struct {unsigned long long Bytes, NativeBytes, DepthStride, MipOffsets[16];} AGX_D3D12_VOLUME_LAYOUT;'
  def declaration(name):
   r=re.search(r'typedef (?:struct|enum) '+name+r'\s*\{.*?\}\s*'+name+r'\s*;',h,re.S);self.assertIsNotNone(r,name);return r.group()
  prefix=prefix.replace('/* VOLUME_LAYOUT_RECORD */',declaration('D3D12DDI_TEXTURE_LAYOUT')+'\n'+record)
  # Exact linked native AIL, not an invented matching size/offset implementation.
  start=prefix.index('extern "C" int AgxWin32AsahiQueryUncompressedResourceLayout(');end=prefix.index('\nstatic void receipt',start)
  prefix=prefix[:start]+'''extern "C" int AgxWin32AsahiQueryUncompressedResourceLayout(const pipe_resource *t,AGX_WIN32_ASAHI_RESOURCE_LAYOUT *r){
   ++query_calls;queried=*t;memset(r,0,sizeof(*r));if(!query_ok)return 0;
   ail_layout a={};a.width_px=t->width0;a.height_px=t->height0;a.depth_px=t->depth0;a.sample_count_sa=1;a.levels=t->last_level+1;a.mipmapped_z=true;a.tiling=AIL_TILING_GPU;a.format=t->format;a.renderable=true;ail_make_miptree(&a);
   r->SizeBytes=a.size_B;r->LayerStrideBytes=a.layer_stride_B;r->SparseTableSizeBytes=a.sparse_table_size_B;r->RequestedLevels=t->last_level+1;r->AllocatedLevels=a.levels;r->Modifier=0x1234;
   for(unsigned i=0;i<a.levels;i++)r->LevelOffsets[i]=a.level_offsets_B[i];return 1;
  }
  '''+prefix[end:]
  prefix=prefix.replace('/* ACTUAL_QUERY */',funcs).replace('/* ACTUAL_TEXTURE_BYTES */',block(ddi,'static UINT64 TextureBytes('))
  names=['D3D12DDI_RESOURCE_ALLOCATION_INFO','D3D12DDI_SUBRESOURCE_INFO','D3D12DDI_MEMORY_POOL','D3D12DDI_CPU_PAGE_PROPERTY','D3D12DDI_HEAP_FLAGS','D3D12DDIARG_CREATEHEAP_0001']
  fixture=(ROOT/'tests/d3d12_resolved_roundtrip_fixture.cpp').read_text().replace('/* PINNED_ALLOCATION_ABI */','\n'.join(declaration(n) for n in names))
  functions=block(ddi,'struct AgxD3d12Heap {')+';\n'+block(ddi,'struct AgxD3d12Resource {')+';\n'
  functions+='\n'.join(block(ddi,x) for x in ['static VOID APIENTRY CheckResourceAllocationInfo(', 'static HRESULT CreateVolumeResource(', 'static VOID APIENTRY CheckExistingResourceAllocationInfo(', 'static VOID APIENTRY CheckSubresourceInfo('])
  source=prefix+fixture.replace('/* ACTUAL_RESOURCE_FUNCTIONS */',functions)
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)/'roundtrip.cpp';p.write_text(source);exe=Path(t)/'test'
   libs=[b/x for x in ['src/asahi/layout/libasahi_layout.a','src/util/libmesa_util.a','src/util/libmesa_util_simd.a','src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
   r=subprocess.run(['clang++','-std=c++17','-Werror=sign-compare','-include','initializer_list','-fsanitize=address,undefined',*host_flags(b),'-I',str(m/'src/gallium/include'),'-I',str(ROOT/'drivers/apple-agx/mesa/winsys'),str(p),'-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],capture_output=True,text=True)
   self.assertEqual(r.returncode,0,r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stderr)
