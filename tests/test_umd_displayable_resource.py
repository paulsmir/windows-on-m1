"""Replay EXP917's real composition buffer against the production validator."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class DisplayableResourceTests(unittest.TestCase):
    def test_observed_bgra_displayable_buffer_preserves_storage_contract(self):
        source = (RENDER / 'umd/src/umd.c').read_text()
        function = re.search(
            r'static BOOLEAN AdmissionUmdDescribePrimary\(.*?^}',
            source, re.S | re.M).group(0)
        # Values are the pinned WDK26100 enum values, including actual PRESENT80.
        shim = r'''
#include <assert.h>
#include <string.h>
#include "direct_flip_contract.h"
typedef int BOOLEAN;
typedef unsigned UINT;
#define TRUE 1
#define FALSE 0
#define ZeroMemory(p,n) memset(p,0,n)
#define D3D10DDIRESOURCE_TEXTURE2D 3u
#define D3D10_DDI_USAGE_DEFAULT 0u
#define DXGI_FORMAT_B8G8R8A8_UNORM 87u
#define DXGI_FORMAT_B8G8R8A8_UNORM_SRGB 91u
#define DXGI_FORMAT_R8G8B8A8_UNORM 28u
#define D3D10_DDI_BIND_SHADER_RESOURCE 8u
#define D3D10_DDI_BIND_RENDER_TARGET 32u
#define D3D10_DDI_BIND_PRESENT 128u
#define D3D10_DDI_RESOURCE_MISC_SHARED 2u
#define D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT 8u
#define D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE 0x20000u
#define D3DKMDT_GDISURFACE_TEXTURE 2u
#define D3DDDIFMT_A8R8G8B8 21u
#define D3DDDIFMT_A8B8G8R8 32u
struct Mip { unsigned TexelWidth,TexelHeight,TexelDepth; };
typedef struct {
 struct Mip *pMipInfoList;
 unsigned ResourceDimension,Format,MipLevels,ArraySize,BindFlags;
 unsigned Usage,MapFlags,MiscFlags;
 struct {unsigned Count,Quality;} SampleDesc;
 void *pInitialDataUP,*pPrimaryDesc;
} D3D11DDIARG_CREATERESOURCE;
'''
        replay = r'''
int main(void) {
 struct Mip mip={2560u,1600u,1u};
 D3D11DDIARG_CREATERESOURCE c={0};
 ADMISSION_UMD_DIRECT_FLIP_RESOURCE observed={0},reference={0};
 c.pMipInfoList=&mip;c.ResourceDimension=3u;c.Format=87u;
 c.MipLevels=1u;c.ArraySize=1u;c.SampleDesc.Count=1u;
 c.BindFlags=0xa8u;c.MiscFlags=0x20002u;
 assert(AdmissionUmdDescribePrimary(&c,&observed));
 assert(observed.Linear==1u && observed.Displayable==1u && observed.SegmentId==2u);
 assert(observed.Allocation.Format==21u && observed.Allocation.Pitch==10240u);
 assert(observed.Allocation.Size==16384000ULL);
 assert(AdmissionAllocationDescriptionValid(&observed.Allocation));
 c.MiscFlags=2u;assert(AdmissionUmdDescribePrimary(&c,&reference));
 assert(memcmp(&observed,&reference,sizeof(observed))==0);
 c.MiscFlags=0x20002u;
 /* EXP1038: WinUI flip-model RGBA8 back buffers carry the displayable
    flag (no DisplayableSupport cap); refusing them removed the device. They
    stay a non-scanout RGBA allocation (Displayable 0, A8B8G8R8). */
 c.Format=28u;c.pMipInfoList[0].TexelWidth=600u;c.pMipInfoList[0].TexelHeight=200u;
 assert(AdmissionUmdDescribePrimary(&c,&observed));
 assert(observed.Displayable==0u && observed.Allocation.Format==32u);
 c.pMipInfoList[0].TexelWidth=2560u;c.pMipInfoList[0].TexelHeight=1600u;
 c.Format=10u;assert(!AdmissionUmdDescribePrimary(&c,&observed));
 c.Format=87u;
 unsigned forbidden[]={0x1u,0x800u,0x1000u,0x2000u,0x4000u,0x10000u,0x80000000u};
 for(unsigned i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);++i){
  c.MiscFlags=0x20002u|forbidden[i];
  assert(!AdmissionUmdDescribePrimary(&c,&observed));
 }
 c.MiscFlags=0x20002u;c.ArraySize=2u;
 assert(!AdmissionUmdDescribePrimary(&c,&observed));
 c.ArraySize=1u;c.SampleDesc.Count=2u;
 assert(!AdmissionUmdDescribePrimary(&c,&observed));
 c.SampleDesc.Count=1u;c.pInitialDataUP=&c;
 assert(!AdmissionUmdDescribePrimary(&c,&observed));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'replay.c').write_text(shim + function + replay)
            subprocess.run([
                os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined',
                '-I', str(RENDER / 'include'), '-I', str(RENDER / 'umd/include'),
                str(p / 'replay.c'), str(RENDER / 'src/render_allocation.c'),
                '-o', str(p / 'replay')], check=True)
            subprocess.run([str(p / 'replay')], check=True)
