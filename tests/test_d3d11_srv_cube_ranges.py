"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

D3D11's cube wrapper already supports cube arrays and 2D-array views of cubes.
Its UINT multiplication let an overflowing NumCubes become a legal face count.
Compile the actual helper to preserve legal subviews and reject out-of-range
face/mip counts before creating a native sampler view (WDK TEXCUBE SRV fields).
"""
import unittest
from test_d3d10_copy_bc_reinterpret import ROOT,compile_run
from test_d3d11_buffer_rtv import body

SRC=ROOT/'drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp'

class SrvCubeRanges(unittest.TestCase):
    def test_actual_cube_family_subviews_and_count_overflow(self):
        compile_run(r'''
#include <cassert>
#include <cstring>
#include <initializer_list>
using UINT=unsigned;
#define FALSE 0
#define D3D10_DDI_BIND_SHADER_RESOURCE 1
#define D3D10DDIRESOURCE_TEXTURECUBE 4
#define D3D10DDIRESOURCE_TEXTURE2D 3
enum{PIPE_TEXTURE_CUBE,PIPE_TEXTURE_CUBE_ARRAY,PIPE_TEXTURE_2D_ARRAY};
enum pipe_format{PIPE_FORMAT_NONE,RGBA8};
struct pipe_resource{unsigned target,array_size,last_level;};
struct pipe_sampler_view{unsigned target;pipe_format format;struct{struct{UINT first_level,last_level,first_layer,last_layer;}tex;}u;};
struct pipe_context{pipe_sampler_view*(*create_sampler_view)(pipe_context*,pipe_resource*,const pipe_sampler_view*);};
struct Device{pipe_context*pipe;};
struct Resource{pipe_resource*resource;Device*owner_device;unsigned bind_flags,Format,sample_format,sample_level,sample_layer;};
struct ShaderResourceView{pipe_sampler_view*handle;Device*owner_device;Resource*owner_resource;};
struct D3D11DDIARG_CREATESHADERRESOURCEVIEW{Resource*hDrvResource;UINT ResourceDimension,Format;struct{UINT MostDetailedMip,MipLevels,First2DArrayFace,NumCubes;}TexCube;struct{UINT MostDetailedMip,MipLevels,FirstArraySlice,ArraySize;}Tex2D;};
using D3D10DDI_HDEVICE=Device*;using D3D10DDI_HSHADERRESOURCEVIEW=ShaderResourceView*;
static Device*CastDevice(Device*d){return d;}static Resource*CastResource(Resource*r){return r;}static ShaderResourceView*CastShaderResourceView(ShaderResourceView*v){return v;}
static bool AgxD3d10FormatViewCompatible(unsigned a,unsigned b,bool,bool){return a==b;}
static pipe_format FormatTranslate(unsigned f,bool){return (pipe_format)f;}
static pipe_format AgxD3d10LoweredTextureFormat(unsigned){return PIPE_FORMAT_NONE;}
static void u_sampler_view_default_template(pipe_sampler_view*d,pipe_resource*r,pipe_format f){*d={};d->target=r->target;d->format=f;}
static unsigned creates;static pipe_sampler_view last;
static pipe_sampler_view*create(pipe_context*,pipe_resource*,const pipe_sampler_view*d){++creates;last=*d;return &last;}
'''+'static bool srv_cube_family(D3D10DDI_HDEVICE hDevice,const D3D11DDIARG_CREATESHADERRESOURCEVIEW *args,D3D10DDI_HSHADERRESOURCEVIEW hView,bool *handled)\n'+body(SRC.read_text(),'srv_cube_family')+r'''
int main(){pipe_context p={create};Device d={&p};pipe_resource tex={PIPE_TEXTURE_CUBE_ARRAY,24,4};Resource r={&tex,&d,1,RGBA8,0,0,0};
 ShaderResourceView v={};D3D11DDIARG_CREATESHADERRESOURCEVIEW a={};a.hDrvResource=&r;a.Format=RGBA8;a.ResourceDimension=4;a.TexCube={1,3,6,2};bool handled=false;
 assert(srv_cube_family(&d,&a,&v,&handled)&&handled&&creates==1);
 assert(last.target==PIPE_TEXTURE_CUBE_ARRAY && last.u.tex.first_layer==6&&last.u.tex.last_layer==17&&last.u.tex.first_level==1&&last.u.tex.last_level==3);
 a.TexCube.NumCubes=1;a.TexCube.MipLevels=~0u;assert(srv_cube_family(&d,&a,&v,&handled));assert(last.target==PIPE_TEXTURE_CUBE&&last.u.tex.last_level==4);
 a.ResourceDimension=3;a.Tex2D={2,1,5,8};assert(srv_cube_family(&d,&a,&v,&handled));assert(last.target==PIPE_TEXTURE_2D_ARRAY&&last.u.tex.first_layer==5&&last.u.tex.last_layer==12);
 tex.target=PIPE_TEXTURE_CUBE;tex.array_size=6;a.Tex2D={0,2,1,3};assert(srv_cube_family(&d,&a,&v,&handled));assert(last.u.tex.last_layer==3);
 tex.target=PIPE_TEXTURE_CUBE_ARRAY;tex.array_size=24;a.ResourceDimension=4;a.TexCube={0,1,0,1};
 for(unsigned n:{0u,5u,0x80000001u,~0u}){a.TexCube.NumCubes=n;creates=0;assert(!srv_cube_family(&d,&a,&v,&handled)&&handled&&!creates&&!v.handle);}
 a.TexCube={4,2,0,1};assert(!srv_cube_family(&d,&a,&v,&handled));
 a.TexCube={0,1,20,1};assert(!srv_cube_family(&d,&a,&v,&handled));
}
''')

if __name__=='__main__':unittest.main()
