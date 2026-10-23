"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

GenMips rejected legal non-BGRA SRVs and copied every staging row as width*4.
Compile the generated DDI and check typed per-level blits, exact storage bytes,
array/volume ranges and cleanup. WDK GenMips consumes an SRV; Gallium's
u_gen_mipmap.c defines typed filtering and slice/mip coordinates.
"""
from test_d3d10_copy_bc_reinterpret import SCRIPT, compile_run
import unittest


def generated_body(name):
    text = SCRIPT.read_text().split("'" + name + "','''", 1)[1]
    return text.split("''')", 1)[0]


class GenerateMipsFormats(unittest.TestCase):
    def test_actual_ddi_formats_targets_and_failure_cleanup(self):
        compile_run(r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <algorithm>
using UINT=unsigned; using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG -1
#define E_NOTIMPL -2
#define E_OUTOFMEMORY -3
#define E_FAIL -4
#define FAILED(s) ((s)<0)
#define D3D10_DDI_BIND_RENDER_TARGET 1
#define PIPE_BIND_SAMPLER_VIEW 2
#define PIPE_BIND_RENDER_TARGET 4
#define PIPE_USAGE_STAGING 1
#define PIPE_MAP_READ 1
#define PIPE_MAP_WRITE 2
#define PIPE_MASK_RGBA 15
#define PIPE_TEX_FILTER_LINEAR 1
#define DRM_FORMAT_MOD_APPLE_GPU_TILED 1
using pipe_format=unsigned;
enum { PIPE_BUFFER,PIPE_TEXTURE_1D,PIPE_TEXTURE_1D_ARRAY,PIPE_TEXTURE_2D,
 PIPE_TEXTURE_2D_ARRAY,PIPE_TEXTURE_3D,PIPE_TEXTURE_CUBE,PIPE_TEXTURE_CUBE_ARRAY };
enum {PIPE_FORMAT_NONE,PIPE_FORMAT_B8G8R8A8_UNORM,PIPE_FORMAT_B8G8R8A8_SRGB,
 PIPE_FORMAT_B8G8R8X8_UNORM,PIPE_FORMAT_B8G8R8X8_SRGB,RGBA8,RGBA16F,R11G11B10,RG16,R32F,SRGBA8,R8,RGBA32F,INTEGER,DEPTH,UNSUPPORTED};
static unsigned util_format_get_blocksize(unsigned f) {return f==RGBA16F?8:f==RGBA32F?16:f==R8?1:4;}
static unsigned util_format_get_blockwidth(unsigned) {return 1;}
static unsigned util_format_get_blockheight(unsigned) {return 1;}
static bool util_format_is_pure_integer(unsigned f) {return f==INTEGER;}
static bool util_format_is_depth_or_stencil(unsigned f) {return f==DEPTH;}
static unsigned u_minify(unsigned n,unsigned l) {return std::max(n>>l,1u);}
struct pipe_box { int x,y,z,width,height,depth; };
struct pipe_resource {unsigned target,format,width0,height0,depth0,array_size,nr_samples,nr_storage_samples,bind,usage,last_level;};
struct pipe_transfer {unsigned stride,layer_stride;};
struct pipe_blit_info {struct endpoint {pipe_resource* resource;unsigned format,level;pipe_box box;} src,dst;unsigned mask,filter;};
struct pipe_screen {pipe_resource*(*resource_create_with_modifiers)(pipe_screen*,const pipe_resource*,const uint64_t*,unsigned);
 bool(*is_format_supported)(pipe_screen*,unsigned,unsigned,unsigned,unsigned,unsigned);};
struct pipe_context {pipe_screen* screen;void*(*texture_map)(pipe_context*,pipe_resource*,unsigned,unsigned,const pipe_box*,pipe_transfer**);void(*blit)(pipe_context*,const pipe_blit_info*);};
struct Device {pipe_context* pipe;void* windows;};
struct Resource {Device* owner_device;unsigned bind_flags;};
struct pipe_sampler_view {pipe_resource*texture;unsigned format;struct {struct {unsigned first_level,last_level,first_layer,last_layer;}tex;}u;};
struct ShaderResourceView {Device*owner_device;pipe_sampler_view*handle;Resource*owner_resource;};
using D3D10DDI_HDEVICE=Device*;using D3D10DDI_HSHADERRESOURCEVIEW=ShaderResourceView*;
static Device* CastDevice(Device*d){return d;}static ShaderResourceView*CastShaderResourceView(ShaderResourceView*v){return v;}
static bool CheckPredicate(Device*){return true;}
static int error;static unsigned creates,maps,unmaps,releases,blits,failmap,element;
static pipe_resource staged;static pipe_transfer transfers[2];static uint8_t srcbytes[65536],dstbytes[65536];static pipe_box mappedbox;
static unsigned expected_first, expected_last, expected_layer,expected_count;
static void SetError(Device*,int e){error=e;}
static void AgxD3d10WindowsDiagnostic(const char*,int,const UINT*,unsigned){}
static int AgxD3d10WindowsFlushRetire(void*){return 0;}
static int AgxD3d10WindowsFlushStatus(void*){return 0;}
static bool supported(pipe_screen*,unsigned f,unsigned,unsigned,unsigned,unsigned bind){assert(bind==(PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW));return f!=UNSUPPORTED;}
static pipe_resource*create(pipe_screen*,const pipe_resource*i,const uint64_t*,unsigned){++creates;staged=*i;return &staged;}
static void*map(pipe_context*,pipe_resource*r,unsigned level,unsigned flags,const pipe_box*b,pipe_transfer**t){
 ++maps;if(failmap && maps==failmap)return nullptr;
 unsigned idx=flags==PIPE_MAP_READ?0:1;*t=&transfers[idx];
 (*t)->stride=b->width*element+(idx?19:11);(*t)->layer_stride=(*t)->stride*b->height+23;
 assert((*t)->layer_stride*b->depth<sizeof(srcbytes));
 if(idx){assert(r==&staged && level==0);memset(dstbytes,0xcd,sizeof(dstbytes));}
 else {mappedbox=*b;memset(srcbytes,0x37,sizeof(srcbytes));}
 return idx?dstbytes:srcbytes;
}
static void pipe_texture_unmap(pipe_context*,pipe_transfer*){++unmaps;}
static void pipe_resource_reference(pipe_resource**r,pipe_resource*){if(*r)++releases;*r=nullptr;}
static void blit(pipe_context*,const pipe_blit_info*b){
 ++blits;assert(b->dst.level==expected_first+blits && b->dst.level<=expected_last);
 assert(b->src.format==b->dst.format && b->filter==PIPE_TEX_FILTER_LINEAR && b->mask==PIPE_MASK_RGBA);
 assert(b->src.resource!=b->dst.resource && b->src.level==0);
 assert(b->dst.box.width==(int)u_minify(b->dst.resource->width0,b->dst.level));
 if(b->dst.resource->target==PIPE_TEXTURE_3D){assert(b->dst.box.depth==(int)u_minify(b->dst.resource->depth0,b->dst.level));}
 else {assert(b->src.box.z==(int)expected_layer && b->dst.box.z==(int)expected_layer);
 assert(b->src.box.depth==(int)expected_count && b->dst.box.depth==(int)expected_count);}
 for(int z=0;z<mappedbox.depth;++z)for(int y=0;y<mappedbox.height;++y){
 unsigned off=z*transfers[1].layer_stride+y*transfers[1].stride;
 for(unsigned x=0;x<(unsigned)mappedbox.width*element;++x)assert(dstbytes[off+x]==0x37);
 assert(dstbytes[off+mappedbox.width*element]==0xcd);
 }
}
static void GenMips(Device*hDevice,ShaderResourceView*hShaderResourceView){
''' + generated_body('GenMips') + r'''
}
int main(){
 pipe_screen s={create,supported};pipe_context p={&s,map,blit};Device d={&p,nullptr};Resource owner={&d,1};
 for(unsigned f: {PIPE_FORMAT_B8G8R8A8_UNORM,RGBA8,RGBA16F,R11G11B10,RG16,R32F,SRGBA8,R8,RGBA32F})
 for(unsigned target: {PIPE_TEXTURE_1D,PIPE_TEXTURE_1D_ARRAY,PIPE_TEXTURE_2D,PIPE_TEXTURE_2D_ARRAY,PIPE_TEXTURE_3D,PIPE_TEXTURE_CUBE,PIPE_TEXTURE_CUBE_ARRAY}){
 unsigned layers=target==PIPE_TEXTURE_CUBE?6:target==PIPE_TEXTURE_CUBE_ARRAY?18:target==PIPE_TEXTURE_1D_ARRAY||target==PIPE_TEXTURE_2D_ARRAY?5:1;
 pipe_resource tex={target,f,8,target==PIPE_TEXTURE_1D||target==PIPE_TEXTURE_1D_ARRAY?1u:8u,target==PIPE_TEXTURE_3D?8u:1u,layers,1,1,0,0,3};
 expected_first=1;expected_last=3;expected_layer=layers>6?6:layers==5?1:0;expected_count=layers>6?6:layers==5?3:layers;
 pipe_sampler_view srv={&tex,f,{{1,3,expected_layer,expected_layer+expected_count-1}}};ShaderResourceView v={&d,&srv,&owner};
 creates=maps=unmaps=releases=blits=failmap=0;error=0;element=util_format_get_blocksize(f);
 GenMips(&d,&v);assert(!error && blits==2 && creates==2 && releases==2 && maps==4 && unmaps==4);
 creates=maps=unmaps=releases=blits=0;failmap=2;GenMips(&d,&v);assert(error && !blits && unmaps==1 && releases==1);failmap=0;
 for(unsigned bad:{INTEGER,DEPTH,UNSUPPORTED}){srv.format=bad;error=0;creates=0;GenMips(&d,&v);assert(error && creates==0);}
 srv.format=f;srv.u.tex.last_level=1;error=0;GenMips(&d,&v);assert(!error && creates==0);
 }
}
''')

if __name__ == '__main__': unittest.main()
