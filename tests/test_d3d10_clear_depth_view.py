"""D3D-G10a: ClearDepthStencilView follows the D3D10 contract.

Bring-up admitted only the depth view bound whole as the framebuffer's depth
target (2D, level 0, slice 0, four formats); a shadow map cleared before it
is bound -- the usual D3D order -- set E_NOTIMPL and removed the device.
Invariant: an unbound view of any level and slice range clears through a
depth-only framebuffer, one slice at a time, and the application's
framebuffer is restored; the view bound whole clears in the current batch;
stencil is cleared only where the format has it; a non-depth or buffer
view is refused without touching the framebuffer.
"""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def clear_body():
    values = [n.value for n in ast.walk(ast.parse(SCRIPT.read_text()))
              if isinstance(n, ast.Constant) and isinstance(n.value, str)
              and "struct pipe_surface *surface = CastPipeDepthStencilView(hDepthStencilView);" in n.value
              and "D3D-G10a" in n.value]
    assert len(values) == 1, len(values)
    return values[0]


SHIM = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
typedef unsigned UINT; typedef float FLOAT; typedef unsigned char UINT8;
enum { PIPE_BUFFER=0, PIPE_TEXTURE_2D=1 };
enum { PIPE_CLEAR_DEPTH=2, PIPE_CLEAR_STENCIL=4, E_NOTIMPL=1 };
enum { D3D10_DDI_CLEAR_DEPTH=1, D3D10_DDI_CLEAR_STENCIL=2 };
enum { PIPE_FORMAT_Z32_FLOAT=1, PIPE_FORMAT_Z24_UNORM_S8_UINT, PIPE_FORMAT_R8G8B8A8_UNORM };
struct pipe_resource {unsigned target,format,nr_samples,width,height;};
struct pipe_surface {pipe_resource *texture;unsigned format,level,first_layer,last_layer;};
struct pipe_framebuffer_state {unsigned width,height,layers,samples,nr_cbufs;pipe_surface cbufs[8],zsbuf;};
union pipe_color_union {float f[4];};
struct format_desc {unsigned format;};
static const format_desc *util_format_description(unsigned f){static format_desc d;d.format=f;return &d;}
static bool util_format_has_stencil(const format_desc *d){return d->format==PIPE_FORMAT_Z24_UNORM_S8_UINT;}
static bool util_format_is_depth_or_stencil(unsigned f){return f!=PIPE_FORMAT_R8G8B8A8_UNORM;}
struct pipe_context {
 void (*clear)(pipe_context*,unsigned,uint32_t,uint8_t,const void*,const pipe_color_union*,double,unsigned);
 void (*set_framebuffer_state)(pipe_context*,const pipe_framebuffer_state*);
 pipe_framebuffer_state active; unsigned binds;
};
struct Device {pipe_context *pipe;pipe_framebuffer_state fb;int error;};
static Device *CastDevice(Device *p){return p;}
static pipe_context *CastPipeContext(Device *p){return p->pipe;}
static pipe_surface *CastPipeDepthStencilView(pipe_surface *s){return s;}
static void SetError(Device *p,int e){p->error=e;}
#define LOG_UNSUPPORTED(x) ((void)0)
static unsigned pipe_surface_width(const pipe_surface*s){return s->texture->width>>s->level;}
static unsigned pipe_surface_height(const pipe_surface*s){return s->texture->height>>s->level;}
static void bind(pipe_context*p,const pipe_framebuffer_state*f){p->active=*f;p->binds++;}
static unsigned clears, last_mask; static double last_depth;
static void native_clear(pipe_context*p,unsigned mask,uint32_t,uint8_t,const void*,const pipe_color_union*,double depth,unsigned){
 assert(p->active.zsbuf.texture);
 assert(p->active.zsbuf.first_layer==p->active.zsbuf.last_layer);
 last_mask=mask;last_depth=depth;clears++;
}
static void clear_dsv(Device*hDevice,pipe_surface*hDepthStencilView,UINT Flags,FLOAT Depth,UINT8 Stencil){
// ACTUAL_BODY
}
int main(){
 pipe_context pipe={};pipe.clear=native_clear;pipe.set_framebuffer_state=bind;
 Device d={};d.pipe=&pipe;
 pipe_resource colour={PIPE_TEXTURE_2D,PIPE_FORMAT_R8G8B8A8_UNORM,1,1024,768};
 pipe_resource shadow={PIPE_TEXTURE_2D,PIPE_FORMAT_Z32_FLOAT,1,2048,2048};
 pipe_resource depth={PIPE_TEXTURE_2D,PIPE_FORMAT_Z24_UNORM_S8_UINT,1,1024,768};
 d.fb.width=1024;d.fb.height=768;d.fb.nr_cbufs=1;d.fb.cbufs[0]={&colour,colour.format,0,0,0};
 d.fb.zsbuf={&depth,depth.format,0,0,0};bind(&pipe,&d.fb);
 auto saved=d.fb;
 // An unbound shadow map (two slices, level 1): a depth-only clear per slice.
 pipe_surface shadow_view={&shadow,shadow.format,1,0,1};
 clear_dsv(&d,&shadow_view,D3D10_DDI_CLEAR_DEPTH|D3D10_DDI_CLEAR_STENCIL,2.0f,0);
 assert(d.error==0 && clears==2 && last_mask==PIPE_CLEAR_DEPTH && last_depth==1.0);
 assert(memcmp(&pipe.active,&saved,sizeof(saved))==0);
 // The bound view clears in the current batch (no framebuffer change).
 unsigned binds=pipe.binds;
 pipe_surface bound_view=d.fb.zsbuf;
 clear_dsv(&d,&bound_view,D3D10_DDI_CLEAR_DEPTH|D3D10_DDI_CLEAR_STENCIL,0.5f,7);
 assert(d.error==0 && clears==3 && last_mask==(PIPE_CLEAR_DEPTH|PIPE_CLEAR_STENCIL));
 assert(pipe.binds==binds);
 // A colour or buffer view is refused without touching the framebuffer.
 pipe_surface bad={&colour,colour.format,0,0,0};
 clear_dsv(&d,&bad,D3D10_DDI_CLEAR_DEPTH,0.0f,0);
 assert(d.error==E_NOTIMPL && clears==3 && pipe.binds==binds);
 d.error=0; pipe_resource buffer=shadow;buffer.target=PIPE_BUFFER;
 pipe_surface bad2={&buffer,buffer.format,0,0,0};
 clear_dsv(&d,&bad2,D3D10_DDI_CLEAR_DEPTH,0.0f,0);
 assert(d.error==E_NOTIMPL && clears==3);
 assert(memcmp(&pipe.active,&saved,sizeof(saved))==0);
}
'''


class ClearDepthViewTests(unittest.TestCase):
    def test_unbound_and_sliced_depth_views_clear(self):
        with tempfile.TemporaryDirectory(prefix="agx-clear-dsv-") as td:
            source = Path(td) / "clear.cpp"; binary = Path(td) / "clear"
            source.write_text(SHIM.replace("// ACTUAL_BODY", clear_body()))
            subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17",
                            "-fsanitize=address,undefined", str(source), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
