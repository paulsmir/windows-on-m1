"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

ClearRenderTargetView must clear its complete typed buffer view, independent
of framebuffer state; the old texture-only fallback rejected PIPE_BUFFER.
Compile the projected DDI to verify float/integer conversion, element offsets,
prefix/suffix preservation and that no framebuffer or texture clear is used.
"""
import unittest
from test_d3d11_buffer_rtv import projected, body
from test_d3d10_copy_bc_reinterpret import compile_run


PREFIX = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <climits>
#include <algorithm>
using UINT=unsigned;using FLOAT=float;
#define LOG_ENTRYPOINT()
#define LOG_UNSUPPORTED(x)
#define E_NOTIMPL -1
#define E_FAIL -2
#define PIPE_BUFFER 0
#define PIPE_CLEAR_COLOR0 1
#define TRUE 1
#define FALSE 0
union pipe_color_union {float f[4];int i[4];unsigned ui[4];};
enum pipe_format {R32F,R32UI,R32SI};
struct pipe_resource {unsigned target,nr_samples;};
struct pipe_surface {pipe_resource*texture;pipe_format format;unsigned first_layer,last_layer;};
struct pipe_framebuffer_state {unsigned width,height,nr_cbufs;pipe_surface cbufs[1];};
struct pipe_context {
 void(*clear_render_target)(pipe_context*,pipe_surface*,const pipe_color_union*,unsigned,unsigned,unsigned,unsigned,bool);
 void(*clear)(pipe_context*,unsigned,unsigned,unsigned,void*,pipe_color_union*,double,unsigned);
 void(*set_framebuffer_state)(pipe_context*,const pipe_framebuffer_state*);
 void(*clear_buffer)(pipe_context*,pipe_resource*,unsigned,unsigned,const void*,int);
};
struct Device {pipe_context*pipe;void*windows;pipe_framebuffer_state fb;};
struct RenderTargetView {pipe_surface surface;unsigned buffer_first_element,buffer_num_elements;};
static Device*CastDevice(Device*d){return d;}static pipe_context*CastPipeContext(Device*d){return d->pipe;}
static RenderTargetView*CastRenderTargetView(RenderTargetView*v){return v;}
static pipe_surface*CastPipeRenderTargetView(RenderTargetView*v){return &v->surface;}
static bool util_format_is_pure_integer(pipe_format f){return f!=R32F;}
static bool util_format_is_pure_sint(pipe_format f){return f==R32SI;}
static bool util_format_is_pure_uint(pipe_format f){return f==R32UI;}
static bool util_is_nan(float f){return std::isnan(f);}
static unsigned util_format_get_blocksize(pipe_format){return 4;}
static void util_format_pack_rgba(pipe_format,void*dst,const void*src,unsigned n){assert(n==1);memcpy(dst,src,4);}
static unsigned pipe_surface_width(pipe_surface*){return 64;}static unsigned pipe_surface_height(pipe_surface*){return 1;}
static bool predicate=true;static bool CheckPredicate(Device*){return predicate;}
static int error;static unsigned calls;static uint32_t bytes[64];
static void SetError(Device*,int e){error=e;}
static void AgxD3d10WindowsDiagnosticState(void*,const char*){}
static void clear(pipe_context*,pipe_resource*,unsigned offset,unsigned size,const void*pixel,int element){
 assert(element==4 && offset==28 && size==52);++calls;
 for(unsigned i=offset/4;i<(offset+size)/4;++i)memcpy(&bytes[i],pixel,4);
}
'''


class BufferRtvClear(unittest.TestCase):
    def test_actual_clear_ddi_typed_view_without_framebuffer(self):
        source = projected('OutputMerger.cpp')
        helper = ''
        if '\nAgxD3d11ClearBufferRtv(' in source:
            helper = 'bool AgxD3d11ClearBufferRtv(Device *device, RenderTargetView *view, const pipe_color_union *color, UINT first, UINT count)\n' + body(source, 'AgxD3d11ClearBufferRtv')
        compile_run(PREFIX + helper + '\nstatic void ClearRenderTargetView(Device*hDevice,RenderTargetView*hRenderTargetView,float*pColorRGBA)\n' + body(source,'ClearRenderTargetView') + r'''
int main(){pipe_context p={nullptr,nullptr,nullptr,clear};Device d={&p,nullptr,{}};pipe_resource r={PIPE_BUFFER,1};
 RenderTargetView v={{&r,R32F,0,0},7,13};
 float values[4]={0.75f,0,0,0};
 predicate=false;ClearRenderTargetView(&d,&v,values);assert(!calls && !error);predicate=true;
 for(pipe_format f:{R32F,R32UI,R32SI})for(float value:{0.75f,-3.0f,INFINITY,-INFINITY,NAN}){
  v.surface.format=f;values[0]=value;error=0;calls=0;std::fill(bytes,bytes+64,0xdeadbeefu);
  ClearRenderTargetView(&d,&v,values);assert(!error && calls==1);
  uint32_t expected;
  if(f==R32F)memcpy(&expected,&value,4);
  else if(f==R32UI)expected=!(value>0)?0:value>=4294967295.0f?UINT32_MAX:(unsigned)value;
  else {int signed_value=std::isnan(value)?0:value<=-2147483648.0f?INT32_MIN:value>=2147483647.0f?INT32_MAX:(int)value;memcpy(&expected,&signed_value,4);}
  for(unsigned i=0;i<64;++i)assert(bytes[i]==(i>=7&&i<20?expected:0xdeadbeefu));
 }
}
''')

if __name__ == '__main__': unittest.main()
