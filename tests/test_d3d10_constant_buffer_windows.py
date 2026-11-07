"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

D3D11.1 SetConstantBuffers1 accepts 0..4096 constants, including a zero-sized
window. Whole binding of a larger buffer exposes only its first 4096 constants.
Out-of-buffer offsets return zeros, not wrapped offsets into earlier memory.
The actual generated setter is compiled; only device/pipe callbacks are replaced.
"""
from test_d3d10_copy_bc_reinterpret import SCRIPT, compile_run
import unittest


class ConstantBufferWindows(unittest.TestCase):
    def test_actual_setter_binds_the_intersection_or_unbinds(self):
        text = SCRIPT.read_text()
        start = text.index('void\nAgxSetConstantBuffersRange(')
        body = text[start:text.index('\n}\n', start)+3]
        compile_run(r'''
#include <cassert>
#include <cstdint>
#include <algorithm>
#define MIN2(a,b) std::min(a,b)
#define D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT 14
#define PIPE_MAX_CONSTANT_BUFFERS 16
#define PIPE_BUFFER 1
#define PIPE_BIND_CONSTANT_BUFFER 1
#define PIPE_BIND_SHADER_IMAGE 2
#define E_NOTIMPL -1
typedef unsigned UINT, ULONG;
typedef unsigned long long ULONGLONG;
enum mesa_shader_stage { MESA_SHADER_VERTEX,MESA_SHADER_FRAGMENT,MESA_SHADER_GEOMETRY,
 MESA_SHADER_COMPUTE,MESA_SHADER_TESS_CTRL,MESA_SHADER_TESS_EVAL };
struct pipe_resource { unsigned target,bind,width0; };
struct pipe_constant_buffer { pipe_resource *buffer; unsigned buffer_offset,buffer_size; };
struct pipe_context { void (*set_constant_buffer)(pipe_context*,mesa_shader_stage,unsigned,const pipe_constant_buffer*); };
struct Device { pipe_context *pipe; void *windows; };
struct Resource { bool constant_buffer; Device *owner_device; ULONGLONG owner_cookie;
 ULONG device_generation; pipe_resource *resource; UINT logical_bytes; };
typedef Device* D3D10DDI_HDEVICE;
typedef Resource* D3D10DDI_HRESOURCE;
static Device *CastDevice(Device *d) { return d; }
static Resource *CastResource(Resource *r) { return r; }
static bool AgxD3d10WindowsIdentity(void*,ULONGLONG *o,ULONG *g) { *o=7;*g=9;return true; }
static int error;
static void SetError(Device*,int e) { error=e; }
static unsigned calls; static bool empty; static pipe_constant_buffer last;
static void bind(pipe_context*,mesa_shader_stage,unsigned,const pipe_constant_buffer *cb) {
 ++calls;empty=!cb;if(cb)last=*cb;
}
''' + body + r'''
int main() {
 pipe_context p={bind};Device d={&p,nullptr};pipe_resource backing={PIPE_BUFFER,1,131072};
 Resource r={true,&d,7,9,&backing,131072};Resource *resources[]={&r};
 for (unsigned stage=0;stage<6;++stage) {
   error=0; calls=0;
   AgxSetConstantBuffersRange((mesa_shader_stage)stage,&d,0,1,resources,nullptr,nullptr);
   assert(!error && calls==1 && !empty && last.buffer_offset==0 && last.buffer_size==65536);
   UINT first=0,count=0;
   AgxSetConstantBuffersRange((mesa_shader_stage)stage,&d,0,1,resources,&first,&count);
   assert(!error && empty);
   first=4096;count=4096;
   AgxSetConstantBuffersRange((mesa_shader_stage)stage,&d,0,1,resources,&first,&count);
   assert(!error && !empty && last.buffer_offset==65536 && last.buffer_size==65536);
   first=8176;count=32;
   AgxSetConstantBuffersRange((mesa_shader_stage)stage,&d,0,1,resources,&first,&count);
   assert(!error && !empty && last.buffer_size==256);
   first=0x10000000u;count=16;
   AgxSetConstantBuffersRange((mesa_shader_stage)stage,&d,0,1,resources,&first,&count);
   assert(!error && empty);
   first=1;calls=0;
   AgxSetConstantBuffersRange((mesa_shader_stage)stage,&d,0,1,resources,&first,&count);
   assert(error && calls==0);
   error=0;first=0;count=4112;
   AgxSetConstantBuffersRange((mesa_shader_stage)stage,&d,0,1,resources,&first,&count);
   assert(error && calls==0);
 }
}
''')


if __name__ == '__main__': unittest.main()
