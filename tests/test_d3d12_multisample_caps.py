"""C32: multisample caps against D3D12Core's device-initialisation rule.

WHAT REAL BUG OR INVARIANT: EXP1827 reported 2x/4x quality levels with no
format-support bit and D3D12CreateDevice failed (0x887A0020): for each
typeless format D3D12Core!CDevice::LLOCompleteLayerConstruction walks its
cast set, and a member without MULTISAMPLE_RENDERTARGET must report 0
levels at every count above 1, a member with it the typeless format's
levels, the bit must agree across the set, and the typeless format must
not report the bit. The actual CheckFormatSupport and
CheckMultisampleQualityLevels must satisfy that for every DXGI cast set.
"""
from pathlib import Path
import re,subprocess,tempfile,unittest
from test_d3d12_view_mapping import block
ROOT=Path(__file__).resolve().parents[1]
DDI=ROOT/'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'
DXGI=ROOT/'.local/reference/winsdk26100/shared/dxgiformat.h'
WDK=ROOT/'.local/reference/wdk26100/d3d12umddi.h'
# depth/stencil and view-only members of the typeless families (DXGI cast sets)
EXTRA={'R32G8X24':['D32_FLOAT_S8X24_UINT','R32_FLOAT_X8X24_TYPELESS','X32_TYPELESS_G8X24_UINT'],
       'R24G8':['D24_UNORM_S8_UINT','R24_UNORM_X8_TYPELESS','X24_TYPELESS_G8_UINT'],
       'R32':['D32_FLOAT'],'R16':['D16_UNORM'],'R10G10B10A2':['R10G10B10_XR_BIAS_A2_UNORM']}
def cast_sets():
    names=re.findall(r'\bDXGI_FORMAT_(\w+)\s*=\s*\d+',DXGI.read_text())
    sets={}
    for n in names:
        if not n.endswith('_TYPELESS') or n.count('_TYPELESS')!=1 or n.startswith(('X','R32_FLOAT','R24_UNORM')): continue
        prefix=n[:-len('_TYPELESS')]
        # a member is PREFIX_<type>: one more underscore-free type word
        # (R8G8_B8G8_UNORM and R32_FLOAT_X8X24_TYPELESS belong elsewhere)
        members=[m for m in names if m!=n and re.fullmatch(re.escape(prefix)+r'_[A-Z0-9]+(_SRGB)?',m)]
        sets[n]=members+[m for m in EXTRA.get(prefix,[]) if m in names]
    return sets
class MultisampleCaps(unittest.TestCase):
    def test_actual_caps_follow_the_cast_set_rule(self):
        s=DDI.read_text()
        defines='\n'.join(re.findall(r'#define D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_\w+\s+0x[0-9A-Fa-f]+',WDK.read_text()))
        sets=cast_sets()
        self.assertIn('DXGI_FORMAT_R8G8B8A8_TYPELESS',['DXGI_FORMAT_'+k for k in sets])
        table=''.join('{DXGI_FORMAT_%s,{%s}},\n'%(t,','.join('DXGI_FORMAT_'+m for m in ms)) for t,ms in sets.items())
        src='''#include <cstdio>
#include <vector>
#define APIENTRY
#define VOID void
#define APPLE_AGX_D3D12_NATIVE_OWNER 1
using UINT=unsigned;
#include "%s"
%s
struct D3D12DDI_HDEVICE{void*p;};using D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS=unsigned;
/* ACTUAL */
struct Set{DXGI_FORMAT typeless;std::vector<DXGI_FORMAT> members;};
static const Set sets[]={
%s};
int main(){
 unsigned bad=0,ms_sets=0;
 for(const Set &set:sets){
  UINT sup=0,parent[32]={};CheckFormatSupport({},set.typeless,&sup);
  if(sup&D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET){printf("typeless %%d reports the bit\\n",set.typeless);++bad;}
  for(UINT n=2;n<32;++n)CheckMultisampleQualityLevels({},set.typeless,n,0,&parent[n]);
  int bit=-1;
  for(DXGI_FORMAT m:set.members){
   UINT ms=0;CheckFormatSupport({},m,&ms);const int has=(ms&D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET)!=0;
   if(bit<0)bit=has;else if(bit!=has){printf("set %%d: bit disagrees at %%d\\n",set.typeless,m);++bad;}
   for(UINT n=2;n<32;++n){UINT l=0;CheckMultisampleQualityLevels({},m,n,0,&l);
    if(has?l!=parent[n]:l!=0){printf("set %%d member %%d count %%u levels %%u parent %%u bit %%d\\n",set.typeless,m,n,l,parent[n],has);++bad;}}
  }
  ms_sets+=parent[4]!=0;
 }
 printf("sets=%%zu multisampled=%%u bad=%%u\\n",sizeof(sets)/sizeof(sets[0]),ms_sets,bad);
 return bad||ms_sets<8;
}
'''%(DXGI,defines,table)
        actual='\n'.join([block(s,'static bool MultisampleCastSet('),block(s,'static VOID APIENTRY CheckFormatSupport('),
                          block(s,'static VOID APIENTRY CheckMultisampleQualityLevels(')])
        actual=actual.replace('AgxD3d12FormatMultisample','/*unused*/AgxD3d12FormatMultisample')
        src=src.replace('/* ACTUAL */',actual)
        with tempfile.TemporaryDirectory(prefix='d3d12-ms-') as t:
            p=Path(t)/'ms.cpp';p.write_text(src);exe=Path(t)/'ms'
            r=subprocess.run(['clang++','-std=c++17','-Wno-everything',str(p),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr[-3000:])
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stdout[-3000:])
            self.assertIn('bad=0',r.stdout)
if __name__=='__main__':unittest.main()
