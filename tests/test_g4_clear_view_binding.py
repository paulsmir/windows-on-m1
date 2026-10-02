"""EXP935: replay the actual projected clear against an unbound RTV.

Catches the observed valid ClearRenderTargetView -> E_NOTIMPL device failure,
and clearing/leaking the caller's existing MRT/depth framebuffer instead.
"""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def clear_body():
    values = [n.value for n in ast.walk(ast.parse(SCRIPT.read_text()))
              if isinstance(n, ast.Constant) and isinstance(n.value, str)
              and n.value.startswith('   Device *traceDevice = CastDevice(hDevice);')
              and 'clear-before' in n.value]
    assert len(values) == 1
    return values[0]


SHIM = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
enum { PIPE_TEXTURE_2D=1, PIPE_CLEAR_COLOR0=1, E_NOTIMPL=1 };
enum { PIPE_FORMAT_B8G8R8A8_UNORM=1, PIPE_FORMAT_B8G8R8A8_SRGB,
 PIPE_FORMAT_B8G8R8X8_UNORM,PIPE_FORMAT_B8G8R8X8_SRGB,
 PIPE_FORMAT_R8G8B8A8_UNORM,PIPE_FORMAT_R16G16B16A16_FLOAT,
 PIPE_FORMAT_R8_UNORM,PIPE_FORMAT_R16_FLOAT,PIPE_FORMAT_R32G32B32A32_FLOAT,
 PIPE_FORMAT_R10G10B10A2_UNORM,PIPE_FORMAT_R11G11B10_FLOAT,PIPE_FORMAT_B5G6R5_UNORM };
struct pipe_resource {unsigned target,format,nr_samples,array_size,last_level,width,height;float pixel;};
struct pipe_surface {pipe_resource *texture;unsigned format,level,first_layer,last_layer;};
struct pipe_framebuffer_state {unsigned width,height,layers,samples,nr_cbufs;pipe_surface cbufs[8],zsbuf;};
union pipe_color_union {float f[4];};
struct pipe_context {
 void (*clear_render_target)(pipe_context*,pipe_surface*,const pipe_color_union*,unsigned,unsigned,unsigned,unsigned,bool);
 void (*clear)(pipe_context*,unsigned,uint32_t,uint8_t,const void*,const pipe_color_union*,double,unsigned);
 void (*set_framebuffer_state)(pipe_context*,const pipe_framebuffer_state*);
 pipe_framebuffer_state active;
};
struct Device {pipe_context *pipe;pipe_framebuffer_state fb;void *windows;int error;};
static Device *CastDevice(Device *p){return p;}
static void AgxD3d10WindowsDiagnosticState(void*,const char*){}
static void SetError(Device *p,int e){p->error=e;}
#define LOG_UNSUPPORTED(x) ((void)0)
static unsigned pipe_surface_width(pipe_surface*s){return s->texture->width;}
static unsigned pipe_surface_height(pipe_surface*s){return s->texture->height;}
static unsigned util_format_linear(unsigned f){return f==PIPE_FORMAT_B8G8R8A8_SRGB?PIPE_FORMAT_B8G8R8A8_UNORM:f;}
static void bind(pipe_context*p,const pipe_framebuffer_state*f){p->active=*f;}
static unsigned clears;
static void native_clear(pipe_context*p,unsigned mask,uint32_t channels,uint8_t stencil,const void*scissor,const pipe_color_union*c,double,unsigned){
 assert(mask==PIPE_CLEAR_COLOR0 && channels==15 && stencil==0 && !scissor);
 assert(p->active.nr_cbufs==1 && !p->active.zsbuf.texture);
 auto *r=p->active.cbufs[0].texture;
 assert(p->active.width==r->width && p->active.height==r->height);
 r->pixel=c->f[1];clears++;
}
static void clear_view(Device*hDevice,pipe_surface*surface,pipe_color_union clear_color){
 auto *pipe=hDevice->pipe;
// ACTUAL_BODY
}
int main(){
 pipe_context pipe={};pipe.clear=native_clear;pipe.set_framebuffer_state=bind;
 Device d={};d.pipe=&pipe;
 pipe_resource target={PIPE_TEXTURE_2D,PIPE_FORMAT_B8G8R8A8_UNORM,1,1,0,2560,1600,-1};
 pipe_resource other={PIPE_TEXTURE_2D,PIPE_FORMAT_B8G8R8A8_UNORM,1,1,0,640,480,-2};
 pipe_resource depth=other;depth.pixel=-3;
 pipe_surface view={&target,target.format,0,0,0};pipe_color_union green={};green.f[1]=0.55f;
 // The real hardware reproducer: clear a valid view before OM binding.
 clear_view(&d,&view,green);
 assert(d.error==0 && clears==1 && target.pixel==0.55f);
 assert(pipe.active.nr_cbufs==0 && d.fb.nr_cbufs==0);
 // A different currently bound render target must not be cleared or lost.
 d.fb.width=640;d.fb.height=480;d.fb.nr_cbufs=2;
 d.fb.cbufs[0]={&other,other.format,0,0,0};d.fb.cbufs[1]=view;
 d.fb.zsbuf={&depth,depth.format,0,0,0};bind(&pipe,&d.fb);
 auto saved=d.fb;target.pixel=-1;
 clear_view(&d,&view,green);
 assert(d.error==0 && clears==2 && target.pixel==0.55f);
 assert(other.pixel==-2 && depth.pixel==-3);
 assert(memcmp(&pipe.active,&saved,sizeof(saved))==0);
 assert(memcmp(&d.fb,&saved,sizeof(saved))==0);
 // Invalid view descriptors still fail without modifying framebuffer state.
 view.level=1;target.pixel=-1;clear_view(&d,&view,green);
 assert(d.error==E_NOTIMPL && clears==2 && target.pixel==-1);
 assert(memcmp(&pipe.active,&saved,sizeof(saved))==0);
}
'''


class ClearViewBindingTests(unittest.TestCase):
    def test_actual_clear_handles_unbound_view_and_restores_mrt_depth(self):
        with tempfile.TemporaryDirectory(prefix='agx-clear-view-') as td:
            source=Path(td)/'clear.cpp';binary=Path(td)/'clear'
            source.write_text(SHIM.replace('// ACTUAL_BODY',clear_body()))
            subprocess.run([os.environ.get('CXX','clang++'),'-std=c++17',
                            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                            str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__ == '__main__':
    unittest.main()
