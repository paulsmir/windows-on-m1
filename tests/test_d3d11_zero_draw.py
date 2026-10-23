"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

A zero instance or per-instance count produces no primitives and consumes
no index buffer/topology/state (D3D draw no-op contract). The generated
instanced DDIs validated those unused bindings first and set E_NOTIMPL.
Compile actual bodies; nonzero draws must still validate and submit.
"""
import unittest
from test_d3d10_copy_bc_reinterpret import compile_run
from test_d3d11_generate_mips_formats import generated_body

PREFIX=r'''
#include <cassert>
#include <cstddef>
using UINT=unsigned;using HRESULT=int;
#define E_NOTIMPL -1
#define FAILED(x) ((x)<0)
#define MESA_PRIM_COUNT 10
struct pipe_draw_info{unsigned index_size,mode;struct{void*resource;}index;unsigned instance_count,start_instance;bool primitive_restart;unsigned restart_index;};
struct pipe_draw_start_count_bias{unsigned start,count;int index_bias;};
struct pipe_context{void(*draw_vbo)(pipe_context*,pipe_draw_info*,unsigned,void*,pipe_draw_start_count_bias*,unsigned);};
struct Device{void*index_buffer;unsigned index_size,primitive;pipe_context*pipe;void*windows;unsigned restart_index,ib_offset;};
static unsigned resolves,submits,receipts;static int error;
static Device*CastDevice(Device*d){return d;}
static void SetError(Device*,int e){error=e;}
static bool AgxWin32AsahiContextDrawReceipt(pipe_context*){++receipts;return false;}
static int AgxD3d10WindowsFlushRetire(void*){return 0;}
static void ResolveState(Device*){++resolves;}
static void util_draw_init_info(pipe_draw_info*i){*i={};}
static void AgxD3d10NoTargetReceipt(Device*,unsigned,unsigned){}
static void draw(pipe_context*,pipe_draw_info*i,unsigned,void*,pipe_draw_start_count_bias*d,unsigned){assert(i->instance_count==2 && d->count==3);++submits;}
static void util_draw_arrays_instanced(pipe_context*,unsigned,unsigned,unsigned count,unsigned,unsigned instances){assert(count==3 && instances==2);++submits;}
'''


class ZeroDraw(unittest.TestCase):
    def check(self,name,count):
        indexed=name=='DrawIndexedInstanced'
        args='UINT '+count+',UINT InstanceCount,UINT Start'+('Index' if indexed else 'Vertex')+'Location,'+('int BaseVertexLocation,' if indexed else '')+'UINT StartInstanceLocation'
        tail=',0,0,0' if indexed else ',0,0'
        compile_run(PREFIX+'static void call(Device*hDevice,'+args+'){\n'+generated_body(name)+'\n}\n'+r'''
int main(){pipe_context p={draw};Device d={nullptr,0,MESA_PRIM_COUNT,&p,nullptr,0,0};
'''+'call(&d,0,2'+tail+');call(&d,3,0'+tail+');'+r'''
 assert(!error && !resolves && !submits && !receipts);
'''+'call(&d,3,2'+tail+');'+r'''
 assert(error && !resolves && !submits && !receipts);
 d.primitive=3;d.index_buffer=&d;d.index_size=4;error=0;
'''+'call(&d,3,2'+tail+');'+r'''
 assert(!error && resolves==1 && submits==1 && receipts==1);
}
''')

    def test_indexed_instanced_zero_does_not_use_index_or_topology(self):
        self.check('DrawIndexedInstanced','IndexCountPerInstance')

if __name__=='__main__': unittest.main()
