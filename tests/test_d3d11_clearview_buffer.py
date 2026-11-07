"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

ClearView rectangles on buffer RTVs use element coordinates and must never
reach a texture blitter. Empty/clipped rectangles change no bytes; nRects=0
clears the whole view. WDK PFND3D11_1DDI_CLEARVIEW + BUFFER_RTV contract.
"""
import unittest
from test_d3d10_copy_bc_reinterpret import ROOT, compile_run
from test_d3d11_buffer_rtv import body, projected
from test_d3d11_buffer_rtv_clear import PREFIX

WDDM = ROOT / 'drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp'


class ClearViewBuffer(unittest.TestCase):
    def test_actual_clearview_rectangles_clip_to_elements(self):
        prefix = PREFIX[:PREFIX.index('static void clear(')]
        compile_run(prefix + r'''
using LONG=int;
#define D3D10DDI_HT_RENDERTARGETVIEW 1
#define D3D11DDI_HT_UNORDEREDACCESSVIEW 2
#define E_INVALIDARG -3
#define MIN2(a,b) std::min(a,b)
#define MAX2(a,b) std::max(a,b)
struct D3D10DDI_HRENDERTARGETVIEW {RenderTargetView*pDrvPrivate; operator RenderTargetView*()const{return pDrvPrivate;}};
struct D3D11DDI_HUNORDEREDACCESSVIEW {void*pDrvPrivate;};
struct D3D10_DDI_RECT {int left,top,right,bottom;};
static unsigned whole;
static void clear(pipe_context*,pipe_resource*,unsigned offset,unsigned size,const void*pixel,int element){
 assert(element==4);++calls;for(unsigned i=offset/4;i<(offset+size)/4;++i)memcpy(&bytes[i],pixel,4);
}
static void refuse(Device*,const char*,int e,UINT){error=e;}
static bool AgxD3d10WindowsClearRenderTargetRect(pipe_context*,pipe_surface*,pipe_color_union*,unsigned,unsigned,unsigned,unsigned){assert(false);return false;}
static bool AgxD3d11ClearUavView(Device*,D3D11DDI_HUNORDEREDACCESSVIEW,const float*,const D3D10_DDI_RECT*,UINT){assert(false);return false;}
static void full(Device*,D3D10DDI_HRENDERTARGETVIEW,float*){++whole;}
static void uav(Device*,D3D11DDI_HUNORDEREDACCESSVIEW,const float*){assert(false);}
struct {decltype(&full)pfnClearRenderTargetView;decltype(&uav)pfnClearUnorderedAccessViewFloat;}base11={full,uav};
''' + 'bool AgxD3d11ClearBufferRtv(Device *device, RenderTargetView *view, const pipe_color_union *color, UINT first, UINT count)\n' + body(projected('OutputMerger.cpp'),'AgxD3d11ClearBufferRtv') + '\nvoid ClearView(Device*hDevice,unsigned type,RenderTargetView*hView,const float color[4],const D3D10_DDI_RECT*rects,UINT nRects)\n' + body(WDDM.read_text(),'ClearView') + r'''
int main(){(void)base11;pipe_context p={nullptr,nullptr,nullptr,clear};Device d={&p,nullptr,{}};pipe_resource r={PIPE_BUFFER,1};
 RenderTargetView v={{&r,R32UI,0,0},7,13};float color[4]={123,0,0,0};
 D3D10_DDI_RECT rects[]={{2,0,5,1},{10,0,20,1},{-4,0,1,1},{2,1,5,2},{4,0,4,1},{20,0,30,1}};
 std::fill(bytes,bytes+64,0xdeadbeefu);ClearView(&d,1,&v,color,rects,6);
 assert(!error && calls==3);
 for(unsigned i=0;i<64;++i){bool changed=i==7||(i>=9&&i<12)||(i>=17&&i<20);assert(bytes[i]==(changed?123u:0xdeadbeefu));}
 predicate=false;ClearView(&d,1,&v,color,nullptr,0);assert(whole==0 && calls==4);
 for(unsigned i=7;i<20;++i)assert(bytes[i]==123u);
}
''')

if __name__ == '__main__': unittest.main()
