"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

ClearView currently refuses every typed UAV rectangle. A rectangle addresses
XY inside the view, across its selected array/volume slices, preserving its
mip and typed format. Asahi has no clear_texture callback: use the existing
mapped packed-byte fallback, not a NULL call or a lossy conversion through
the resource format. R32G32_TYPELESS storage can be UNORM32 while its view is FLOAT. Buffer rectangles use elements, not bytes.
"""
import unittest
from test_d3d10_copy_bc_reinterpret import ROOT, compile_run
from test_d3d11_buffer_rtv import body

SRC=ROOT/'drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp'


class ClearViewUav(unittest.TestCase):
    def test_typed_uav_rectangle_range_and_native_fallback(self):
        source=SRC.read_text()
        self.assertIn('\nAgxD3d11ClearUavView(',source)
        compile_run(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <climits>
#include <algorithm>
using UINT=unsigned;using FLOAT=float;
#define PIPE_BUFFER 0
#define PIPE_TEXTURE_3D 3
#define PIPE_MAP_WRITE 2
#define MIN2(a,b) std::min(a,b)
#define MAX2(a,b) std::max(a,b)
struct D3D10_DDI_RECT{int left,top,right,bottom;};
struct pipe_box{int x,y,z,width,height,depth;};
struct pipe_resource{unsigned target,width0,height0,format;};
struct pipe_transfer{unsigned stride,layer_stride;};
union pipe_color_union{float f[4];int i[4];unsigned ui[4];};
struct pipe_context{void(*clear_buffer)(pipe_context*,pipe_resource*,unsigned,unsigned,const void*,int);void(*clear_texture)(pipe_context*,pipe_resource*,unsigned,const pipe_box*,const void*);void*(*texture_map)(pipe_context*,pipe_resource*,unsigned,unsigned,const pipe_box*,pipe_transfer**);void(*texture_unmap)(pipe_context*,pipe_transfer*);};
struct Device{pipe_context*pipe;};
struct pipe_image_view{pipe_resource*resource;unsigned format;union {struct{unsigned offset,size;}buf;struct{unsigned level,first_layer,last_layer;}tex;}u;};
struct AgxUnorderedAccessView{Device*owner_device;bool typed;pipe_image_view image;};
using D3D10DDI_HDEVICE=Device*;using D3D11DDI_HUNORDEREDACCESSVIEW=AgxUnorderedAccessView*;
static Device*CastDevice(Device*d){return d;}static AgxUnorderedAccessView*CastUnorderedAccessView(AgxUnorderedAccessView*v){return v;}
static unsigned util_format_get_blocksize(unsigned f){return f==0||f==99?8:4;}
static unsigned util_format_get_blockwidth(unsigned){return 1;}
static unsigned util_format_get_blockheight(unsigned){return 1;}
static bool util_format_is_pure_integer(unsigned f){return f==1||f==2;}
static bool util_format_is_pure_sint(unsigned f){return f==2;}
static void util_format_pack_rgba(unsigned f,void*d,const void*s,unsigned n){assert(n==1);memcpy(d,s,util_format_get_blocksize(f));}
static unsigned u_minify(unsigned x,unsigned l){return std::max(x>>l,1u);}
static unsigned calls,offset,size,level;static pipe_box last;static uint32_t pixel;
static uint8_t mapped[8192];static pipe_transfer transfer={160,1600};static unsigned unmaps;static bool failmap;
static void*map(pipe_context*,pipe_resource*,unsigned l,unsigned flags,const pipe_box*b,pipe_transfer**t){
 assert(flags==PIPE_MAP_WRITE);++calls;level=l;last=*b;if(failmap)return nullptr;*t=&transfer;return mapped;
}
static void unmap(pipe_context*,pipe_transfer*){++unmaps;memcpy(&pixel,mapped,4);}
static void clearbuf(pipe_context*,pipe_resource*,unsigned o,unsigned s,const void*p,int e){assert(e==4);++calls;offset=o;size=s;memcpy(&pixel,p,4);}
static void u_default_clear_texture(pipe_context*,pipe_resource*,unsigned l,const pipe_box*b,const void*p){++calls;level=l;last=*b;memcpy(&pixel,p,4);}
'''+'bool AgxD3d11ClearUavView(D3D10DDI_HDEVICE hDevice,D3D11DDI_HUNORDEREDACCESSVIEW hView,const FLOAT color[4],const D3D10_DDI_RECT *rects,UINT nRects)\n'+body(source,'AgxD3d11ClearUavView')+r'''
int main(){pipe_context p={clearbuf,nullptr,map,unmap};Device d={&p};pipe_resource r={2,32,16,99};AgxUnorderedAccessView v={&d,true,{&r,1,{}}};
 v.image.u.tex={1,2,4};float c[4]={17,0,0,0};
 // Different typed view/storage formats: packed float bits must survive.
 v.image.format=0;float f[4]={0.1f,0,0,0};D3D10_DDI_RECT one={0,0,1,1};
 memset(mapped,0xcd,sizeof(mapped));assert(AgxD3d11ClearUavView(&d,&v,f,&one,1));
 uint32_t exact;memcpy(&exact,mapped,4);assert(exact==0x3dcccccdu && unmaps==1);
 assert(mapped[8]==0xcd && mapped[160]==0xcd);
 memcpy(&exact,mapped+1600,4);assert(exact==0x3dcccccdu);
 failmap=true;assert(!AgxD3d11ClearUavView(&d,&v,f,&one,1));assert(unmaps==1);failmap=false;
 v.image.format=1;r.format=1;calls=0;
D3D10_DDI_RECT rects[]={{-2,3,5,20},{8,0,8,2},{20,0,30,10}};
 assert(AgxD3d11ClearUavView(&d,&v,c,rects,3));assert(calls==1&&level==1&&pixel==17);
 assert(last.x==0&&last.y==3&&last.width==5&&last.height==5&&last.z==2&&last.depth==3);
 r.target=PIPE_TEXTURE_3D;calls=0;assert(AgxD3d11ClearUavView(&d,&v,c,nullptr,0));assert(calls==1&&last.width==16&&last.height==8&&last.depth==3);
 r.target=PIPE_BUFFER;r.width0=256;r.height0=1;v.image.u.buf={28,52};calls=0;D3D10_DDI_RECT br={2,0,5,1};
 assert(AgxD3d11ClearUavView(&d,&v,c,&br,1));assert(calls==1&&offset==36&&size==12);
 v.typed=false;assert(!AgxD3d11ClearUavView(&d,&v,c,&br,1));
}
''')

if __name__=='__main__': unittest.main()
