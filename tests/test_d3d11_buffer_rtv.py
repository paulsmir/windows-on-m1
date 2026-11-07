"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

A D3D buffer RTV has FirstElement/NumElements (WDK d3d10umddi.h), not
texture array slices. Creation refused all such views, including legal
INDEX|RT buffers; clears must address only the typed element range.
Compile the actual projected creation function, with reference ownership
and bounds checks observable at its native boundary.
"""
import ast
from pathlib import Path
import unittest
from test_d3d10_copy_bc_reinterpret import SCRIPT, ROOT, compile_run


def projected(name):
    rel = 'src/gallium/frontends/d3d10umd/' + name
    text = (ROOT / '.local/reference/mesa' / rel).read_text()
    for node in ast.walk(ast.parse(SCRIPT.read_text())):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'change':
            if isinstance(node.args[0], ast.Constant) and node.args[0].value == rel:
                for old, new in ast.literal_eval(node.args[2]):
                    assert old in text, old[:80]
                    text = text.replace(old, new)
    return text


def body(text, name):
    start = text.index('\n' + name + '(')
    start = text.index('{', start)
    end = text.index('\n}', start)
    return text[start:end+2]


PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
using UINT=unsigned;using UINT64=uint64_t;
#define LOG_ENTRYPOINT()
#define LOG_UNSUPPORTED(x)
#define ASSERT(x) assert(x)
#define FALSE 0
#define E_NOTIMPL -1
#define PIPE_BUFFER 0
#define PIPE_FORMAT_NONE 0
#define D3D10_DDI_BIND_RENDER_TARGET 1
#define D3D10_DDI_BIND_PRESENT 2
#define PIPE_BIND_RENDER_TARGET 1
#define DXGI_FORMAT_UNKNOWN 0
#define D3D10DDIRESOURCE_BUFFER 1
#define D3D10DDIRESOURCE_TEXTURE1D 2
#define D3D10DDIRESOURCE_TEXTURE2D 3
#define D3D10DDIRESOURCE_TEXTURE3D 4
#define D3D10DDIRESOURCE_TEXTURECUBE 5
enum pipe_format { NONE,RGBA8,R32F };
struct pipe_resource {unsigned target,width0;int refs;};
struct pipe_surface {pipe_resource*texture;pipe_format format;unsigned level,first_layer,last_layer;};
struct pipe_screen {bool(*is_format_supported)(pipe_screen*,pipe_format,unsigned,unsigned,unsigned,unsigned);};
struct pipe_context {pipe_screen*screen;};struct Device {pipe_context*pipe;};
struct RenderTargetView;
struct Resource {Device*owner_device;bool presentation;unsigned bind_flags,Format,logical_bytes;pipe_resource*resource;RenderTargetView*first_presentation_rtv;};
struct RenderTargetView {pipe_surface surface;Resource*owner_resource;RenderTargetView*next_presentation;UINT buffer_first_element,buffer_num_elements;};
struct Create {Resource*hDrvResource;unsigned Format,ResourceDimension;struct {UINT FirstElement,NumElements;}Buffer;
 struct {UINT MipSlice,FirstArraySlice,ArraySize;}Tex1D,Tex2D,TexCube;struct {UINT MipSlice,FirstW,WSize;}Tex3D;};
static Device*CastDevice(Device*d){return d;}static Resource*CastResource(Resource*r){return r;}
static RenderTargetView*CastRenderTargetView(RenderTargetView*v){return v;}
static pipe_resource*CastPipeResource(Resource*r){return r->resource;}
static bool AgxD3d10FormatViewCompatible(unsigned r,unsigned v,bool,bool){return r==v;}
static pipe_format FormatTranslate(unsigned f,bool){return (pipe_format)f;}
static pipe_format AgxD3d10LoweredTextureFormat(unsigned){return NONE;}
static unsigned util_format_get_blocksize(pipe_format){return 4;}
static unsigned util_format_get_blockwidth(pipe_format){return 1;}
static unsigned util_format_get_blockheight(pipe_format){return 1;}
static bool util_format_is_depth_or_stencil(pipe_format){return false;}
static int error;
static void SetError(Device*,int e){error=e;}
static void pipe_resource_reference(pipe_resource**dst,pipe_resource*src){if(*dst)--(*dst)->refs;*dst=src;if(src)++src->refs;}
static bool supported(pipe_screen*,pipe_format f,unsigned,unsigned,unsigned,unsigned){return f!=NONE;}
'''


class BufferRtv(unittest.TestCase):
    def test_actual_create_typed_buffer_view_range(self):
        compile_run(PREFIX + 'static void create(Device*hDevice,const Create*pCreateRenderTargetView,RenderTargetView*hRenderTargetView)\n' + body(projected('OutputMerger.cpp'),'CreateRenderTargetView') + r'''
int main(){pipe_screen s={supported};pipe_context p={&s};Device d={&p};pipe_resource b={PIPE_BUFFER,1024,1};
 Resource r={&d,false,1,DXGI_FORMAT_UNKNOWN,1024,&b,nullptr};RenderTargetView v={};Create a={};
 a.hDrvResource=&r;a.Format=R32F;a.ResourceDimension=D3D10DDIRESOURCE_BUFFER;a.Buffer={7,13};
 create(&d,&a,&v);assert(!error && b.refs==2 && v.surface.texture==&b);
 assert(v.buffer_first_element==7 && v.buffer_num_elements==13);
 pipe_resource_reference(&v.surface.texture,nullptr);
 for(auto range: {UINT64(0),UINT64(1)<<32, (UINT64(250)<<32)|7, (UINT64(~0u)<<32)|2}){
  a.Buffer.FirstElement=range>>32;a.Buffer.NumElements=range;error=0;
  create(&d,&a,&v);assert(error && b.refs==1);
 }
}
''')

if __name__ == '__main__': unittest.main()
