"""WP-1 (EXP2362/EXP2363): a displayable primary of the WINDOW's size is accepted.

Real bug this catches: with DirectFlip/MPO reported, DXGI creates every flip-model back buffer
as a shared displayable primary of the window's size (CS 1.6: 640x480). The rc106 fixed-panel
test in AdmissionUmdDescribePrimary refused it, the runtime removed the application's device and
no frame was ever presented (EXP2362: 0 of ~3960). The invariant: creation accepts any size the
allocator supports, while every path that makes an allocation the FULL-SCREEN scan-out still
requires the committed/offered mode geometry (SetDisplayMode via the direct-flip contract in user
mode, the MPO removal path in the KMD).
"""
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AGX = ROOT / 'drivers/apple-agx'
UMD = AGX / 'render-admission/umd'

SHIM = r'''
#include <cassert>
#include <cstring>
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
#include "direct_flip_contract.h"
}
'''

MAIN = r'''
static bool describe(unsigned w, unsigned h, unsigned format, ADMISSION_UMD_DIRECT_FLIP_RESOURCE *d) {
  Mip mip={w,h,1}; Primary primary={}; D3D11DDIARG_CREATERESOURCE create={};
  create.pMipInfoList=&mip; create.ResourceDimension=3; create.Format=format;
  create.MipLevels=create.ArraySize=create.SampleDesc.Count=1;
  create.BindFlags=D3D10_DDI_BIND_RENDER_TARGET|D3D10_DDI_BIND_PRESENT|D3D10_DDI_BIND_SHADER_RESOURCE;
  /* EXP2362's refused call: misc 0x20002 = SHARED | DISPLAYABLE_SURFACE, pPrimaryDesc set */
  create.MiscFlags=D3D10_DDI_RESOURCE_MISC_SHARED|D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE;
  create.pPrimaryDesc=&primary;
  memset(d,0,sizeof(*d));
  return AdmissionUmdDescribePrimary(&create,d)!=0;
}
static bool full_screen(const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *d) {
  /* AdmissionUmdResourceIsExact: SetDisplayMode's gate */
  return AdmissionUmdDirectFlipCompatible(d,d,0u,D3DDDIFMT_A8R8G8B8)!=0;
}
int main() {
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE d;
  /* window-size primaries (CS 640x480, EXP2356/2357 1682x840, odd sizes) are created */
  const unsigned sizes[][2]={{640,480},{1682,840},{1281,721},{33,33},{8192,8192}};
  for (auto &s : sizes) {
    assert(describe(s[0],s[1],DXGI_FORMAT_B8G8R8A8_UNORM,&d));
    assert(d.Allocation.Width==s[0] && d.Allocation.Height==s[1]);
    assert(d.Displayable==1u && d.SegmentId==2u && d.Linear==1u);
    /* PITCH-1: the scanout broker's 64-byte stride, so the window can be a plane */
    assert(d.Allocation.Pitch%64u==0u && d.Allocation.Pitch>=s[0]*4u && d.Allocation.Pitch<s[0]*4u+64u);
    assert(d.Allocation.Size>=(unsigned long long)d.Allocation.Pitch*s[1]);
    if (s[0]==1682u) assert(d.Allocation.Pitch==6784u);
    /* ... but never become the full-screen scan-out through SetDisplayMode */
    assert(!full_screen(&d));
  }
  /* an offered panel mode still qualifies for SetDisplayMode */
  const unsigned modes[][2]={{2560,1600},{1920,1200},{1280,800},{800,600}};
  for (auto &m : modes) { assert(describe(m[0],m[1],DXGI_FORMAT_B8G8R8A8_UNORM,&d)); assert(full_screen(&d)); }
  /* unchanged: a primary must be BGRA8; the allocator limits stay */
  assert(!describe(640,480,DXGI_FORMAT_R8G8B8A8_UNORM,&d));
  assert(!describe(8193,480,DXGI_FORMAT_B8G8R8A8_UNORM,&d));
  assert(!describe(0,480,DXGI_FORMAT_B8G8R8A8_UNORM,&d));
  return 0;
}
'''


class Wp1WindowPrimary(unittest.TestCase):
    def test_window_size_primary_is_created_but_not_full_screen(self):
        umd = (UMD / 'src/umd.c').read_text()
        describe = re.search(r'static BOOLEAN AdmissionUmdDescribePrimary\(.*?^}', umd, re.S | re.M).group()
        self.assertNotIn('AppleAgxFixedPanelModeIndex', describe)
        with tempfile.TemporaryDirectory(prefix='wp1-') as tmp:
            src = Path(tmp) / 't.cpp'; exe = Path(tmp) / 't'
            objs = []
            for c in (AGX / 'render-admission/src/render_allocation.c', UMD / 'src/direct_flip_contract.c'):
                o = Path(tmp) / (c.stem + '.o')
                subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                                '-I' + str(AGX / 'render-admission/include'), '-I' + str(UMD / 'include'),
                                '-c', str(c), '-o', str(o)], check=True)
                objs.append(str(o))
            src.write_text(SHIM + describe + MAIN)
            r = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                                '-I' + str(AGX / 'render-admission/include'), '-I' + str(UMD / 'include'),
                                str(src)] + objs + ['-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            subprocess.run([str(exe)], check=True, timeout=30)

    def test_set_display_mode_keeps_the_mode_geometry_gate(self):
        umd = (UMD / 'src/umd.c').read_text()
        sdm = umd[umd.index('HRESULT APIENTRY AdmissionUmdSetDisplayMode(\n    DXGI_DDI_ARG_SETDISPLAYMODE *Args) {'):]
        sdm = sdm[:sdm.index('\n}\n')]
        self.assertIn('!AdmissionUmdResourceIsExact(resource)', sdm)

    def test_kmd_removal_requires_mode_geometry_before_commit(self):
        mpo = (AGX / 'render-admission/src/mpo_windows.c').read_text()
        start = mpo.index('  if (slot1) {')
        removal = mpo[start:mpo.index('InterlockedIncrement(&Context->Mpo.RemovalCommits);', start)]
        # STRETCH-1: AdmissionMpoEvaluateOne keeps the mode-geometry rule
        # (AdmissionMpoEvaluatePrimary) for a full-screen LayerIndex 0 plane
        # and applies the layer-record rules to a scaled/positioned one
        # (mpo_contract_test.c TestEvaluateOne).
        self.assertLess(removal.index('AdmissionMpoBuildRemoval('), removal.index('AdmissionMpoEvaluateOne('))
        self.assertLess(removal.index('AdmissionMpoEvaluateOne('), removal.index('AdmissionScanoutMpoCommit('))
        # STRETCH-1b: the refusal goes through the counting helper.
        self.assertIn('AdmissionMpoSet3TableRefusal(Context, result.Reason)', removal)


if __name__ == '__main__':
    unittest.main()
