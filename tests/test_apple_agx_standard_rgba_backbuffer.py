"""EXP693: a base-runtime RGBA backbuffer must retain real channel identity."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class RgbaBackbufferTests(unittest.TestCase):
    def test_descriptor_preserves_format_and_rejects_rgba_primary(self):
        source = (RENDER / 'umd/src/umd.c').read_text()
        body = re.search(r'static BOOLEAN AdmissionUmdDescribePrimary\(.*?^}', source, re.S | re.M).group(0)
        shim = r'''
#include <assert.h>
#include <string.h>
#include "direct_flip_contract.h"
#define BOOLEAN int
#define FALSE 0
#define TRUE 1
#define UINT unsigned int
#define ZeroMemory(p,n) memset(p,0,n)
#define D3D10DDIRESOURCE_TEXTURE2D 3u
#define DXGI_FORMAT_B8G8R8A8_UNORM 87u
#define DXGI_FORMAT_R8G8B8A8_UNORM 28u
#define D3D10_DDI_BIND_PRESENT 0x400u
#define D3DKMDT_GDISURFACE_TEXTURE 2u
#define D3DDDIFMT_A8R8G8B8 21u
#define D3DDDIFMT_A8B8G8R8 32u
struct Mip {unsigned TexelWidth,TexelHeight;};
typedef struct {
 struct Mip *pMipInfoList;
 unsigned ResourceDimension,Format,MipLevels,ArraySize,BindFlags;
 struct {unsigned Count,Quality;} SampleDesc;
 void *pInitialDataUP,*pPrimaryDesc;
} D3D11DDIARG_CREATERESOURCE;
'''
        test = r'''
int main(void) {
 struct Mip mip={2560,1600};
 D3D11DDIARG_CREATERESOURCE c={0};
 ADMISSION_UMD_DIRECT_FLIP_RESOURCE d={0};
 c.pMipInfoList=&mip;c.ResourceDimension=3;c.Format=28;
 c.MipLevels=1;c.ArraySize=1;c.BindFlags=0x400;c.SampleDesc.Count=1;
 assert(AdmissionUmdDescribePrimary(&c,&d));
 assert(d.Allocation.Format==32 && d.Displayable==0);
 assert(!AdmissionUmdDirectFlipCompatible(&d,&d,0,21));
 c.pPrimaryDesc=&c;
 assert(!AdmissionUmdDescribePrimary(&c,&d));
 c.Format=87;
 assert(AdmissionUmdDescribePrimary(&c,&d));
 assert(d.Allocation.Format==21 && d.Displayable==1);
 assert(AdmissionUmdDirectFlipCompatible(&d,&d,0,21));
 c.Format=10;assert(!AdmissionUmdDescribePrimary(&c,&d));
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'rgba.c').write_text(shim + body + test)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Werror',
                            '-I', str(RENDER / 'include'), '-I', str(RENDER / 'umd/include'),
                            str(p / 'rgba.c'), str(RENDER / 'src/render_allocation.c'),
                            str(RENDER / 'umd/src/direct_flip_contract.c'), '-o', str(p / 'rgba')], check=True)
            subprocess.run([str(p / 'rgba')], check=True)
