"""C76 executes the existing producer and the new shared-allocation decoder."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
AGX = ROOT / 'drivers/apple-agx'

class SharedOpen(unittest.TestCase):
    def test_creation_record_round_trip_and_rejections(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Opening must preserve the creator's format, size, dimensions and pitch,
        and refuse truncated/foreign/mismatched data before importing a BO.
        Executes the actual producer serialization and description functions.
        """
        header = AGX / 'd3d12/agx_d3d12_shared_open.h'
        self.assertTrue(header.exists(), 'D3D12 shared metadata decoder is missing')
        umd = (AGX / 'render-admission/umd/src/umd.c').read_text()
        describe = re.search(r'static BOOLEAN AdmissionUmdDescribePrimary\(.*?^}', umd, re.S | re.M).group()
        start = umd.index('  ZeroMemory(&resourceData, sizeof(resourceData));')
        serialize = umd[start:umd.index('  allocationInfo.VidPnSourceId', start)]
        shim = r'''
#include <cassert>
#include <cstring>
#include <initializer_list>

using UINT=unsigned; using BOOLEAN=int;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define D3D10DDIRESOURCE_TEXTURE2D 3u
#define D3D10_DDI_USAGE_DEFAULT 0u
#define DXGI_FORMAT_B8G8R8A8_UNORM 87u
#define DXGI_FORMAT_B8G8R8A8_UNORM_SRGB 91u
#define DXGI_FORMAT_R8G8B8A8_UNORM 28u
#define DXGI_FORMAT_A8_UNORM 65u
#define D3D10_DDI_BIND_SHADER_RESOURCE 8u
#define D3D10_DDI_BIND_RENDER_TARGET 32u
#define D3D10_DDI_BIND_PRESENT 128u
#define D3D10_DDI_RESOURCE_MISC_SHARED 2u
#define D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT 8u
#define D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE 0x20000u
#define D3DKMDT_GDISURFACE_TEXTURE 2u
#define D3DDDIFMT_A8R8G8B8 21u
#define D3DDDIFMT_A8B8G8R8 32u
struct Mip {unsigned TexelWidth,TexelHeight,TexelDepth;};
struct Primary {struct {struct {unsigned Numerator,Denominator;} RefreshRate;} ModeDesc;};
struct D3D11DDIARG_CREATERESOURCE {
 Mip *pMipInfoList; unsigned ResourceDimension,Format,MipLevels,ArraySize,BindFlags;
 unsigned Usage,MapFlags,MiscFlags; struct {unsigned Count,Quality;} SampleDesc;
 void *pInitialDataUP; Primary *pPrimaryDesc;
};
#include "agx_d3d12_shared_open.h"
#include "direct_flip_contract.h"
#include "umd_primary_backbuffer.h"
'''
        main = r'''
int main() {
 for (UINT MetadataFlags : {0u,2u}) for (unsigned format : {28u,87u,91u}) for (unsigned width : {1u,17u,257u,1920u}) {
  Mip mip={width,43,1}; D3D11DDIARG_CREATERESOURCE create={};
  create.pMipInfoList=&mip; create.ResourceDimension=3; create.Format=format;
  create.MipLevels=create.ArraySize=create.SampleDesc.Count=1; create.BindFlags=32|8|128;
  const auto *CreateResource=&create;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE description={};
  assert(AdmissionUmdDescribePrimary(CreateResource,&description));
  ADMISSION_PRESENT_RESOURCE_DATA resourceData={};
  const UINT CreateFlags=0; ADMISSION_UMD_PRIMARY_MAPPING primary={};
  struct {const void *pPrivateDriverData; unsigned PrivateDriverDataSize;} allocate={},allocationInfo={};
SERIALIZE
  unsigned char rw[sizeof(resourceData)+1], aw[sizeof(description.Allocation)+1];
  memcpy(rw+1,allocate.pPrivateDriverData,allocate.PrivateDriverDataSize);
  memcpy(aw+1,allocationInfo.pPrivateDriverData,allocationInfo.PrivateDriverDataSize);
  AgxD3d12SharedDescription parsed={};
  auto parse=[&]() {return AgxD3d12DecodeShared(rw+1,sizeof(resourceData),aw+1,sizeof(description.Allocation),&parsed);};
  if (MetadataFlags) {assert(resourceData.Flags==2); assert(!parse()); continue;}
  assert(parse()); assert(parsed.Format==format && parsed.Width==width && parsed.Height==43);
  assert(parsed.Pitch==((width*4+15)&~15u));
  assert(parsed.Bytes==((static_cast<unsigned long long>(parsed.Pitch)*43+127)&~127ull));
  assert(!AgxD3d12DecodeShared(rw+1,sizeof(resourceData)-1,aw+1,sizeof(description.Allocation),&parsed));
  assert(!AgxD3d12DecodeShared(rw+1,sizeof(resourceData),aw+1,sizeof(description.Allocation)-1,&parsed));
  assert(!AgxD3d12DecodeShared(nullptr,sizeof(resourceData),aw+1,sizeof(description.Allocation),&parsed));
  for (unsigned bad : {0u,65u,24u,0xffffffffu}) {
   auto changed=resourceData; changed.Format=bad; memcpy(rw+1,&changed,sizeof(changed)); assert(!parse());
  }
  auto legacy=resourceData; legacy.Version=3;
  assert(AdmissionPresentResourceDataValid(&legacy));
  memcpy(rw+1,&legacy,sizeof(legacy)); assert(!parse());
  auto converted=resourceData; converted.Flags|=2u;
  assert(AdmissionPresentResourceDataValid(&converted));
  memcpy(rw+1,&converted,sizeof(converted)); assert(!parse());
  memcpy(rw+1,&resourceData,sizeof(resourceData));
  auto bad=description.Allocation; bad.Format=bad.Format==21?32:21;
  memcpy(aw+1,&bad,sizeof(bad)); assert(!parse());
  bad=description.Allocation; bad.Size=static_cast<unsigned long long>(bad.Pitch)*bad.Height-1;
  memcpy(aw+1,&bad,sizeof(bad)); assert(!parse());
  bad=description.Allocation; bad.CpuVisible=1;
  memcpy(aw+1,&bad,sizeof(bad)); assert(!parse());
  bad=description.Allocation; bad.Reserved=1;
  memcpy(aw+1,&bad,sizeof(bad)); assert(!parse());
  bad=description.Allocation; bad.Size=0x1000001;
  memcpy(aw+1,&bad,sizeof(bad)); assert(!parse());
  memcpy(aw+1,&description.Allocation,sizeof(description.Allocation));
  resourceData.Version++; memcpy(rw+1,&resourceData,sizeof(resourceData)); assert(!parse());
 }
}
'''.replace('SERIALIZE', serialize)
        with tempfile.TemporaryDirectory(prefix='c76-roundtrip-') as tmp:
            src=Path(tmp)/'test.cpp'; exe=Path(tmp)/'test'; src.write_text(shim+describe+main)
            obj=Path(tmp)/'allocation.o'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+str(AGX/'render-admission/include'),'-c',str(AGX/'render-admission/src/render_allocation.c'),
                '-o',str(obj)],check=True)
            result=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+str(AGX/'render-admission/include'),'-I'+str(AGX/'render-admission/umd/include'),'-I'+str(AGX/'d3d12'),str(src),str(obj),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(exe)],check=True,timeout=30)

    def test_open_preserves_creator_accounting_and_allocation_identity(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Open must publish the same DDI allocation size as creation, not the
        smaller native linear byte extent, and must retain the opened handle.
        Execute production TextureBytes and Open with only Windows/owner calls
        substituted; the native import's logical size must remain unchanged.
        """
        ddi=(AGX/'d3d12/agx_d3d12_ddi.cpp').read_text()
        def body(signature):
            start=ddi.index(signature)
            return ddi[start:ddi.index('\n}',start)+2]
        size=body('static UINT64 TextureBytes(')
        opened=body('static HRESULT APIENTRY OpenHeapAndResource(')
        opened=re.sub(r'AGX_REJECT\([^)]*\)', 'reject_open', opened)
        existing=body('static VOID APIENTRY CheckExistingResourceAllocationInfo(')
        shim=r'''
#include <cassert>
#include <cstring>
#include <initializer_list>
#define APPLE_AGX_D3D12_NATIVE_OWNER
#define APIENTRY
#define FAILED(x) ((x)<0)
using UINT=unsigned; using UINT64=unsigned long long; using DXGI_FORMAT=unsigned;
using HRESULT=int; using VOID=void; using D3DKMT_HANDLE=unsigned;
constexpr UINT64 kHeapPage=65536;
constexpr unsigned D3D12DDI_TL_ROW_MAJOR=1;
struct D3D12DDI_RESOURCE_ALLOCATION_INFO {UINT64 ResourceDataAlignment,AdditionalDataHeaderAlignment,AdditionalDataAlignment,ResourceDataSize;unsigned Layout,MipLevelSwizzleTransition;};
unsigned open_receipts,receipt_allocation,receipt_format,receipt_pitch;
void AdmissionUmdDiagnostic(const char *name,int,const unsigned *values,unsigned n) {
 if(!strcmp(name,"d3d12-open-heap-resource")) {
  assert(n==5);++open_receipts;receipt_allocation=values[0];receipt_format=values[1];receipt_pitch=values[4];
 }
}
constexpr HRESULT S_OK=0,E_FAIL=-1;
constexpr unsigned D3D12DDI_RT_TEXTURE1D=2,D3D12DDI_RT_TEXTURE2D=3,D3D12DDI_RT_TEXTURE3D=4;
constexpr unsigned AGX_VOLUME_MAX_SLOTS=6;
struct AGX_D3D12_VOLUME_LAYOUT {UINT64 Bytes;};
struct D3D12DDIARG_CREATERESOURCE_0003 {
 UINT ResourceType,Format; UINT64 Width; UINT Height,DepthOrArraySize,MipLevels;
 struct {UINT Count,Quality;} SampleDesc;
};
bool LineLayoutInfo(const void*,void*,void*) {return false;}
bool VolumeLayoutInfo(const void*,void*,void*,void*,unsigned) {return false;}
bool AgxD3d12FormatMultisample(unsigned,unsigned) {return false;}
unsigned AgxD3d12FormatBlock(unsigned f,unsigned *w,unsigned *h) {
 *w=*h=1; return f==28 || f==87 || f==91 ? 4:0;
}
struct AgxD3d12SharedDescription {unsigned Format,Width,Height,Pitch;UINT64 Bytes;};
struct AgxD3d12Heap {void *native,*presentation; UINT64 gpuva,bytes; unsigned allocation;};
struct AgxD3d12Resource {AgxD3d12Heap *heap; AgxD3d12Heap own_heap;UINT64 gpuva,bytes;unsigned type,format,width,height,layers,levels,volume_layout,volume_transition;};
struct AgxD3d12Device {};
struct Handle {void *pDrvPrivate;};
using D3D12DDI_HDEVICE=Handle; using D3D12DDI_HHEAP=Handle; using D3D12DDI_HRESOURCE=Handle;
struct D3D12DDI_HRTRESOURCE {void *handle;};
struct Info {unsigned hAllocation;const void *pPrivateDriverData;unsigned PrivateDriverDataSize;};
struct D3D12DDIARG_OPENHEAP_0003 {struct {unsigned handle;} hKMResource;Info *pOpenAllocationInfo; const void *pPrivateDriverData; unsigned PrivateDriverDataSize;};
struct AGX_D3D12_OWNER {};
struct AGX_D3D12_BUFFER {void *Resource; UINT64 GpuVa,Bytes;};
AgxD3d12Device *DeviceOf(Handle h) {return static_cast<AgxD3d12Device*>(h.pDrvPrivate);}
AGX_D3D12_OWNER owner;
AGX_D3D12_OWNER *EnsureOwner(AgxD3d12Device*) {return &owner;}
bool DecodeOpenedTexture(const D3D12DDIARG_OPENHEAP_0003 *arg,AgxD3d12SharedDescription *out) {
 if(!arg || !arg->pPrivateDriverData) return false;
 *out=*static_cast<const AgxD3d12SharedDescription*>(arg->pPrivateDriverData);return true;
}
unsigned imports;UINT64 native_bytes;bool fail_import;
HRESULT AgxD3d12OwnerOpenSharedTexture(AGX_D3D12_OWNER*,unsigned kr,unsigned allocation,const void*,unsigned,
 const void *rd,unsigned,void*,AGX_D3D12_BUFFER *out,void **presentation) {
 assert(kr==17 && allocation==29);++imports;
 native_bytes=static_cast<const AgxD3d12SharedDescription*>(rd)->Bytes;
 if(fail_import)return E_FAIL;
 *out={&owner,0,native_bytes};*presentation=&owner;return S_OK;
}
HRESULT reject_open(Handle,const D3D12DDIARG_OPENHEAP_0003*,Handle,D3D12DDI_HRTRESOURCE,Handle){return -2;}
'''
        main=r'''
int main() {
 for(unsigned format:{28u,87u,91u}) for(unsigned variant:{0u,1u}) {
  const unsigned width=variant?4096:37,height=variant?17:29;
  D3D12DDIARG_CREATERESOURCE_0003 created={};
  created.ResourceType=3;created.Format=format;created.Width=width;created.Height=height;
  created.DepthOrArraySize=created.MipLevels=created.SampleDesc.Count=1;
  const UINT64 expected=TextureBytes(&created);
  assert(expected==(variant?524288ull:65536ull));
  AgxD3d12SharedDescription desc={format,width,height,(width*4+15)&~15u,0};
  desc.Bytes=(static_cast<UINT64>(desc.Pitch)*height+127)&~127ull;
  Info info={29,nullptr,0};D3D12DDIARG_OPENHEAP_0003 arg={{17},&info,&desc,sizeof(desc)};
  AgxD3d12Device device; AgxD3d12Heap heap={}; AgxD3d12Resource resource={};
  assert(OpenHeapAndResource({&device},&arg,{&heap},{&device},{&resource})==S_OK);
  assert(resource.heap==&heap && heap.allocation==29 && heap.native==&owner && heap.presentation==&owner);
  assert(heap.bytes==expected && resource.bytes==expected && native_bytes==desc.Bytes);
  D3D12DDI_RESOURCE_ALLOCATION_INFO published={};D3DKMT_HANDLE handle=0;
  CheckExistingResourceAllocationInfo({&device},{&resource},&published,&handle);
  assert(published.ResourceDataSize==expected && handle==29);
  assert(open_receipts==imports && receipt_allocation==29 && receipt_format==format && receipt_pitch==desc.Pitch);
  assert(resource.format==format && resource.width==width && resource.height==height && resource.layers==1 && resource.levels==1);
  assert(OpenHeapAndResource({&device},&arg,{nullptr},{&device},{&resource})==S_OK);
  assert(resource.heap==&resource.own_heap && resource.own_heap.bytes==expected);
  fail_import=true; const auto before=imports;
  assert(OpenHeapAndResource({&device},&arg,{&heap},{&device},{&resource})==E_FAIL);
  assert(open_receipts==imports);
  assert(imports==before+1 && !heap.native && !heap.presentation && !heap.allocation);
  fail_import=false;
 }
}
'''
        with tempfile.TemporaryDirectory(prefix='c76-open-') as tmp:
            src=Path(tmp)/'test.cpp';exe=Path(tmp)/'test';src.write_text(shim+size+opened+existing+main)
            result=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            subprocess.run([str(exe)],check=True,timeout=30)

    def test_shared_linear_upload_publishes_decoded_backing(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        C77 returned success while CopyTexture wrote only a cached Direct CPU
        shadow. Execute production copy translation and wire decode with two
        resource views of one allocation. A shared upload must record GPU work,
        preserve decoded pitch/format and publish both writes without BO teardown.
        RC85: the owner's upload staging is mapped PIPE_MAP_DISCARD_WHOLE_RESOURCE
        (Asahi shadows a busy staging without copying its dead contents; EXP2015b
        paid 1301 whole-staging CPU copies in the first frame) unless it exceeds
        Asahi's 6 MiB copy-shadow bound or APPLE_AGX_D3D12_STAGED_DISCARD=0.
        Gallium storage callbacks stand in for hardware; this is not a GPU test.
        """
        from test_d3d12_view_mapping import block
        from test_dxbc_to_nir import local_tree, host_flags
        build=local_tree('accelerated-desktop-ad03','mesa-build')
        mesa=local_tree('reference','mesa')
        if not build or not mesa:
            self.skipTest('host Mesa unavailable')
        owner=(AGX/'mesa/winsys/agx_d3d12_windows.cpp').read_text()
        signatures=['static unsigned block_bytes(',
                    'static unsigned level_extent(unsigned extent, unsigned level) {',
                    'static unsigned texture_block(',
                    'static unsigned texture_layer_extent(const struct pipe_resource *resource,unsigned level) {',
                    'static enum pipe_format bc_staging_format(', 'static void gpu_copy_blocks(',
                    'static bool staged_upload_discard(',
                    'static HRESULT staged_upload(', 'static HRESULT staged_download(',
                    'extern "C" HRESULT AgxD3d12OwnerCopyTexture(']
        funcs='\n'.join(block(owner, signature) for signature in signatures)
        # rc103: a shared texture never takes the whole-level path (level_upload
        # refuses PIPE_BIND_SHARED before any work); test_d3d12_level_upload runs it
        funcs=funcs.replace('static HRESULT staged_upload(', '#define S_FALSE 1\n'
            'static HRESULT level_upload(AGX_D3D12_OWNER *,const AGX_D3D12_TEXTURE_COPY *,pipe_resource *texture,'
            'pipe_resource *,const pipe_box &,const pipe_box &) {assert(texture->bind & PIPE_BIND_SHARED);return S_FALSE;}\n'
            'static HRESULT staged_upload(', 1)
        header=(AGX/'mesa/winsys/agx_d3d12_windows.h').read_text()
        end=header.index('} AGX_D3D12_TEXTURE_COPY;')+len('} AGX_D3D12_TEXTURE_COPY;')
        record=header[header.rfind('typedef struct {',0,end):end]
        source=(ROOT/'tests/d3d12_shared_content_fixture.cpp').read_text().replace(
            '/* ACTUAL_COPY_RECORD */',record).replace('/* ACTUAL_OWNER_COPY */',funcs)
        umd=(AGX/'render-admission/umd/src/umd.c').read_text()
        start=umd.index('  ZeroMemory(&resourceData, sizeof(resourceData));')
        source=source.replace('/* ACTUAL_DESCRIBE */',block(umd,'static BOOLEAN AdmissionUmdDescribePrimary('))
        source=source.replace('/* ACTUAL_SERIALIZE */',umd[start:umd.index('  allocationInfo.VidPnSourceId',start)])
        with tempfile.TemporaryDirectory(prefix='shared-content-') as tmp:
            src=Path(tmp)/'copy.cpp';exe=Path(tmp)/'copy';obj=Path(tmp)/'allocation.o'
            src.write_text(source)
            subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',
                '-I'+str(AGX/'render-admission/include'),'-c',str(AGX/'render-admission/src/render_allocation.c'),
                '-o',str(obj)],check=True)
            libs=[build/p for p in ['src/util/libmesa_util.a','src/util/libmesa_util_simd.a',
                                   'src/c11/impl/libmesa_util_c11.a','src/util/blake3/libblake3.a']]
            result=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',*host_flags(build),
                '-I'+str(mesa/'src/gallium/include'),'-I'+str(AGX/'d3d12'),
                '-I'+str(AGX/'render-admission/umd/include'),'-I'+str(AGX/'render-admission/include'),str(src),str(obj),
                '-o',str(exe),*map(str,libs),'-L/opt/homebrew/opt/zstd/lib','-lzstd','-lz','-lpthread'],
                capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            # RC85 A/B switch: the plain staging map
            env=dict(os.environ,APPLE_AGX_D3D12_STAGED_DISCARD='0')
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30,env=env)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
