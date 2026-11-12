"""BP-1: D3D12 swap-chain back buffers whose heap carries
D3D12DDI_HEAP_FLAG_PRIMARY are created as kernel primaries
(investigation/analysis/BP-1-d3d12-primary-back-buffers.md).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- the EXP2361 defect itself: the runtime's PRIMARY mark dropped between
  CreateHeapAndResource and D3DDDI_ALLOCATIONINFO.Flags.Primary;
- WRITTEN_PRIMARY requested for RGBA8 storage (the KMD refuses the
  allocation unless the broker scans RGBA8888 out: EXP1038);
- a D3D12 primary turned into a device-associated mode-setting primary,
  which PL-1 would retain past DestroyResource and release by HandleList;
- create and open disagreeing on Displayable for the same storage;
- any change for resources created without the request.
The mapping and the actual umd.c serialization are executed on the host.
"""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AGX = ROOT / 'drivers/apple-agx'
UMD = AGX / 'render-admission/umd'


class PrimaryBackBufferTests(unittest.TestCase):
    def test_mapping_and_retention(self):
        with tempfile.TemporaryDirectory(prefix='bp1-map-') as tmp:
            exe = Path(tmp) / 'primary_backbuffer_test'
            subprocess.run([
                os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-Wconversion', '-Wsign-conversion', '-pedantic',
                '-fsanitize=address,undefined', '-I', str(UMD / 'include'),
                str(UMD / 'tests/primary_backbuffer_test.c'), '-o', str(exe),
            ], cwd=ROOT, check=True)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn('primary_backbuffer_test: OK', run.stdout)

    def test_header_is_neutral(self):
        text = (UMD / 'include/umd_primary_backbuffer.h').read_text()
        self.assertEqual(re.findall(r'^#include', text, re.M), [])

    def test_actual_serialization_create_and_open_agree(self):
        umd = (UMD / 'src/umd.c').read_text()
        describe = re.search(r'static BOOLEAN AdmissionUmdDescribePrimary\(.*?^}', umd, re.S | re.M).group()
        start = umd.index('  ZeroMemory(&resourceData, sizeof(resourceData));')
        serialize = umd[start:umd.index('  allocationInfo.VidPnSourceId', start)]
        # open derives Displayable through the same storage rule
        opened = re.search(r'^VOID APIENTRY AdmissionUmdOpenResource\([^;]*?\) \{.*?^}', umd, re.S | re.M).group()
        self.assertIn('resource->DirectFlip.Displayable =\n      AdmissionUmdStorageDisplayable(description->Format);', opened)
        create = re.search(r'^VOID AdmissionUmdCreateResourceWithMetadata\([^;]*?\) \{.*?^}', umd, re.S | re.M).group()
        for line in ('allocationInfo.VidPnSourceId = primary.VidPnSourceId;',
                     'allocationInfo.Flags.Primary = primary.AllocationPrimary;',
                     'primary.DeviceAssociated != 0u ? NULL : RuntimeResource.handle;',
                     'retirement->Primary = primary.RetirementPrimary != 0u;',
                     'retirement->DeviceAssociated = allocate.hResource == NULL;'):
            self.assertIn(line, create)
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
#define D3DKMDT_GDISURFACE_TEXTURE 1u /* d3dkmdt.h */
#define D3DDDIFMT_A8R8G8B8 21u
#define D3DDDIFMT_A8B8G8R8 32u
struct Mip {unsigned TexelWidth,TexelHeight,TexelDepth;};
struct Primary {struct {struct {unsigned Numerator,Denominator;} RefreshRate;} ModeDesc;};
struct D3D11DDIARG_CREATERESOURCE {
 Mip *pMipInfoList; unsigned ResourceDimension,Format,MipLevels,ArraySize,BindFlags;
 unsigned Usage,MapFlags,MiscFlags; struct {unsigned Count,Quality;} SampleDesc;
 void *pInitialDataUP; Primary *pPrimaryDesc;
};
extern "C" {
#include "render_allocation.h"
}
#include "direct_flip_contract.h"
#include "umd_primary_backbuffer.h"
'''
        main = r'''
int main() {
 static_assert(ADMISSION_UMD_STORAGE_A8R8G8B8 == D3DDDIFMT_A8R8G8B8, "storage");
 for (unsigned format : {87u,91u,28u}) for (UINT CreateFlags : {0u,ADMISSION_UMD_CREATE_RUNTIME_PRIMARY})
 for (UINT MetadataFlags : {0u,ADMISSION_PRESENT_RESOURCE_CONVERTED}) {
  Mip mip={1280,720,1}; D3D11DDIARG_CREATERESOURCE create={};
  create.pMipInfoList=&mip; create.ResourceDimension=3; create.Format=format;
  create.MipLevels=create.ArraySize=create.SampleDesc.Count=1;
  create.BindFlags=D3D10_DDI_BIND_RENDER_TARGET|D3D10_DDI_BIND_PRESENT|D3D10_DDI_BIND_SHADER_RESOURCE;
  const auto *CreateResource=&create;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE description={};
  assert(AdmissionUmdDescribePrimary(CreateResource,&description));
  // create and open (AdmissionUmdStorageDisplayable on the stored format) agree
  assert(description.Displayable==AdmissionUmdStorageDisplayable(description.Allocation.Format));
  const bool bgra=format!=28u;
  assert(description.Displayable==(bgra?1u:0u));
  ADMISSION_PRESENT_RESOURCE_DATA resourceData={};
  ADMISSION_UMD_PRIMARY_MAPPING primary={};
  struct {const void *pPrivateDriverData; unsigned PrivateDriverDataSize;} allocate={},allocationInfo={};
SERIALIZE
  // the request is UMD-local: the KMD's resource-data validator accepts the result
  assert(AdmissionPresentResourceDataValid(&resourceData));
  assert((resourceData.Flags&~(ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY|ADMISSION_PRESENT_RESOURCE_CONVERTED))==0u);
  assert((resourceData.Flags&ADMISSION_PRESENT_RESOURCE_CONVERTED)==MetadataFlags);
  const bool written=(resourceData.Flags&ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY)!=0u;
  assert(written==(bgra && CreateFlags!=0u));
  assert(primary.AllocationPrimary==(CreateFlags?1u:0u) && primary.VidPnSourceId==0u);
  assert(primary.DeviceAssociated==0u && primary.RetirementPrimary==0u);
  assert(resourceData.RefreshNumerator==0u && resourceData.RefreshDenominator==0u);
  assert(allocate.pPrivateDriverData==&resourceData && allocationInfo.pPrivateDriverData==&description.Allocation);
 }
}
'''.replace('SERIALIZE', serialize)
        with tempfile.TemporaryDirectory(prefix='bp1-serialize-') as tmp:
            src = Path(tmp) / 'test.cpp'; exe = Path(tmp) / 'test'; obj = Path(tmp) / 'allocation.o'
            src.write_text(shim + describe + main)
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I' + str(AGX / 'render-admission/include'), '-c',
                            str(AGX / 'render-admission/src/render_allocation.c'), '-o', str(obj)], check=True)
            result = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                     '-fsanitize=address,undefined',
                                     '-I' + str(AGX / 'render-admission/include'), '-I' + str(UMD / 'include'),
                                     str(src), str(obj), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(exe)], check=True, timeout=30)

    def test_d3d12_heap_primary_reaches_the_allocation(self):
        ddi = (AGX / 'd3d12/agx_d3d12_ddi.cpp').read_text()
        start = ddi.index('static HRESULT APIENTRY CreateHeapAndResource(')
        create = ddi[start:ddi.index('\n}\n', start)]
        self.assertIn('const bool runtime_primary = presentable && (heap_arg->Flags & D3D12DDI_HEAP_FLAG_PRIMARY) != 0;',
                      create)
        self.assertIn('runtime_resource.handle, runtime_primary ? 1 : 0,', create)
        owner = (AGX / 'mesa/winsys/agx_d3d12_windows.cpp').read_text()
        start = owner.index('extern "C" HRESULT AgxD3d12OwnerCreatePresentationTexture(')
        present = owner[start:owner.index('\n}\n', start)]
        self.assertIn('runtime_primary ? ADMISSION_UMD_CREATE_RUNTIME_PRIMARY : 0u', present)
        # the request is passed only as CreateFlags, never as wire metadata
        self.assertIn('record->Converted ? ADMISSION_PRESENT_RESOURCE_CONVERTED : 0u,', present)
        self.assertEqual(owner.count('ADMISSION_UMD_CREATE_RUNTIME_PRIMARY'), 1)


if __name__ == '__main__':
    unittest.main()
