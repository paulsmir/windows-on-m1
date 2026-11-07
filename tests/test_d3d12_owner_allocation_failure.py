"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

D3D12 owner silently continued draws/dispatches after sampler, stream-output,
heap-batch allocation failures, and block copies used incomplete maps. Run
actual owner routines/admission regions with kth-allocation failures under
ASan/UBSan; require error propagation and release of partial allocations.
External Gallium callbacks are the fault-injection boundary, not a replacement
implementation of the owner admission logic.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
OWNER = ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'


def function(text, marker):
    start = text.index(marker)
    return text[start:text.index('\n}', start)+2]


PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using HRESULT=int; using UINT=unsigned;using LONGLONG=long long;
#define FALSE 0
static int diagnostics_enabled=1;
static int AdmissionUmdDiagnosticEnabled(){return diagnostics_enabled;}
static int AdmissionUmdMeasureEnabled(){return diagnostics_enabled;}
static LONGLONG clock_tick;
static LONGLONG qpc_now(){return ++clock_tick;}
#define S_OK 0
#define E_OUTOFMEMORY (-2)
#define E_INVALIDARG (-3)
#define DXGI_ERROR_DEVICE_HUNG (-4)

// GPUVA cause propagation is exercised by test_d3d12_pressure_error.
template<class T> static HRESULT owner_failure(T *, HRESULT fallback) { return fallback; }
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
#define AGX_D3D12_SAMPLER_SLOTS 4
#define AGX_D3D12_SRV_SLOTS 4
#define AGX_D3D12_UAV_SLOTS 4
#define AGX_D3D12_CBV_SLOTS 4
#define MESA_SHADER_VERTEX 0
#define MESA_SHADER_FRAGMENT 1
#define MESA_SHADER_COMPUTE 5
static unsigned nth, attempts, live, encoded, bound;
static bool faulted, launch_fault;
static void *allocate() {
 if (++attempts==nth) return nullptr;
 ++live; return malloc(8);
}
static void release(void *p) { if(p) {assert(live); --live;free(p);} }
struct pipe_resource {};
struct pipe_sampler_view {};
struct pipe_grid_info {unsigned work_dim,block[3],grid[3],indirect_offset;pipe_resource *indirect;};
struct pipe_context {
 void (*delete_sampler_state)(pipe_context *,void *)=[](pipe_context *,void *p){release(p);};
 void (*bind_sampler_states)(pipe_context *,int,unsigned,unsigned,void **)=[](pipe_context *,int,unsigned,unsigned,void **){++bound;};
 void (*bind_compute_state)(pipe_context *,void *)=[](pipe_context *,void *){};
 void (*launch_grid)(pipe_context *,pipe_grid_info *)=[](pipe_context *,pipe_grid_info *){++encoded;if(launch_fault)faulted=true;};
};
struct AGX_D3D12_BUFFER {void *Resource;uint64_t Bytes;};
struct AGX_D3D12_SAMPLER {unsigned Valid;};
struct AGX_D3D12_UAV {}; struct AGX_D3D12_CBV {};struct AGX_D3D12_SRV {};
struct AGX_D3D12_COMPUTE {void *State;unsigned Block[3];uint8_t SrvDimensions[4],UavDimensions[4];};
struct AGX_D3D12_INDIRECT {AGX_D3D12_BUFFER *Buffer,*CountBuffer;unsigned MaxCount,Stride;uint64_t Offset;};
struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingDispatches;AGX_D3D12_SAMPLER ComputeSamplerDesc[4];void *ComputeSamplers[4];LONGLONG DispatchTicks[4];unsigned TimedDispatches;};
struct OwnerLock {OwnerLock(AGX_D3D12_OWNER *){}};
static void *create_sampler(pipe_context *,const AGX_D3D12_SAMPLER *) {return allocate();}
static int AgxWin32AsahiContextFaulted(pipe_context *) {return faulted;}
static HRESULT bind_stage(pipe_context *,int,const AGX_D3D12_CBV *,const AGX_D3D12_SRV *,const AGX_D3D12_UAV *,pipe_sampler_view **,unsigned *,const uint8_t *,const uint8_t *,unsigned=4) {return S_OK;}
static void unbind_stage(pipe_context *,int,pipe_sampler_view **,unsigned) {}
static HRESULT bind_heaps(AGX_D3D12_OWNER *,bool) {return faulted?DXGI_ERROR_DEVICE_HUNG:S_OK;}
static void unbind_heaps(AGX_D3D12_OWNER *,bool) {}
static void validate_heap_refs(AGX_D3D12_OWNER *,bool) {}
'''


def dispatch_helpers(text):
    return (function(text,'static void *compute_sampler(')+
            function(text,'static void release_compute_samplers(')+
            function(text,'struct DispatchTiming {')+';\n')


class OwnerAllocationFailure(unittest.TestCase):
    def run_cpp(self, code):
        with tempfile.TemporaryDirectory(prefix='owner-fail-') as td:
            src=Path(td)/'test.cpp'; exe=Path(td)/'test'
            src.write_text(code)
            result=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_dispatch_kth_sampler_failure_never_launches(self):
        text=OWNER.read_text()
        self.run_cpp(PRELUDE+dispatch_helpers(text)+function(text,'extern "C" HRESULT AgxD3d12OwnerDispatch(')+r'''
int main() {
 for(nth=0;nth<=5;++nth) {
  faulted=nth==5;
  attempts=live=encoded=bound=0;
  pipe_context pipe;AGX_D3D12_OWNER owner={&pipe,0};
  AGX_D3D12_COMPUTE compute={&pipe,{1,1,1},{},{}};
  AGX_D3D12_SAMPLER samplers[4]={{1},{0},{1},{1}};
  AGX_D3D12_UAV uavs[4];AGX_D3D12_CBV cbvs[4];AGX_D3D12_SRV srvs[4];
  HRESULT hr=AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,1,1,1,nullptr);
  bool fail=nth>0&&nth<=3;
  assert(hr==(fail?E_OUTOFMEMORY:faulted?DXGI_ERROR_DEVICE_HUNG:S_OK));
  assert(encoded==((fail||faulted)?0u:1u));assert(owner.PendingDispatches==encoded);release_compute_samplers(&owner);assert(!live);
 }
 nth=attempts=live=encoded=0;faulted=false;launch_fault=true;
 pipe_context pipe;AGX_D3D12_OWNER owner={&pipe,0};
 AGX_D3D12_COMPUTE compute={&pipe,{1,1,1},{},{}};AGX_D3D12_SAMPLER samplers[4]={};
 AGX_D3D12_UAV uavs[4];AGX_D3D12_CBV cbvs[4];AGX_D3D12_SRV srvs[4];
 pipe_resource resource;AGX_D3D12_BUFFER buffer={&resource,64};
 AGX_D3D12_INDIRECT indirect={&buffer,nullptr,4,12,0};
 assert(AgxD3d12OwnerDispatch(&owner,&compute,uavs,cbvs,srvs,samplers,0,0,0,&indirect)==DXGI_ERROR_DEVICE_HUNG);
 assert(encoded==1&&!owner.PendingDispatches&&!live);
}
''')

    def test_draw_vertex_pixel_sampler_failure_is_reported(self):
        text=OWNER.read_text();draw=function(text,'extern "C" HRESULT AgxD3d12OwnerDraw(')
        # Real loop only. Binding/whole-draw cleanup is independently guarded
        # by the returned HRESULT and the existing release_samplers routine.
        region=draw[draw.index('unsigned vs_sampler_count'):draw.index('if (vs_sampler_count) pipe->bind_sampler_states')]
        self.run_cpp(PRELUDE+function(text,'static void release_samplers(')+r'''
struct Bind {AGX_D3D12_SAMPLER Samplers[4];};
struct Draw {Bind Vs,Ps;};
int main() {
 for(nth=0;nth<=9;++nth) {
  attempts=live=0;HRESULT hr=S_OK;pipe_context context,*pipe=&context;
  Draw value={};Draw *draw=&value;
  for(unsigned i=0;i<4;++i) draw->Vs.Samplers[i].Valid=draw->Ps.Samplers[i].Valid=1;
  void *vs_samplers[4]={},*ps_samplers[4]={};
'''+region+r'''
  assert(hr==((nth>0&&nth<=8)?E_OUTOFMEMORY:S_OK));
  release_samplers(pipe,vs_samplers,vs_sampler_count,ps_samplers,ps_sampler_count);assert(!live);
 }
}
''')

    def test_stream_output_failed_target_or_offset_is_reported(self):
        text=OWNER.read_text();draw=function(text,'extern "C" HRESULT AgxD3d12OwnerDraw(')
        region=draw[draw.index('struct pipe_stream_output_target *so[4]'):draw.rfind('\n', 0, draw.index('    const unsigned append')-1)]
        self.run_cpp(PRELUDE.replace('struct pipe_resource {};','struct pipe_resource {};\nstruct pipe_stream_output_target {pipe_resource *offset;};\nstruct pipe_box {int x,y,z,width,height,depth;};').replace('struct pipe_context {',r'''
struct pipe_context {
 pipe_stream_output_target *(*create_stream_output_target)(pipe_context *,pipe_resource *,unsigned,unsigned)=[](pipe_context *,pipe_resource *,unsigned,unsigned){
  auto *p=(pipe_stream_output_target *)allocate();if(p)p->offset=(pipe_resource *)allocate();return p;};
 void (*resource_copy_region)(pipe_context *,pipe_resource *,unsigned,unsigned,unsigned,unsigned,pipe_resource *,unsigned,const pipe_box *)=[](pipe_context *,pipe_resource *dst,unsigned,unsigned,unsigned,unsigned,pipe_resource *src,unsigned,const pipe_box *){assert(dst&&src);};
''')+r'''
static pipe_resource *AgxWin32AsahiStreamOutputOffset(pipe_stream_output_target *p){return p->offset;}
struct So {AGX_D3D12_BUFFER *Buffer,*Filled;uint64_t Offset,Size,FilledOffset;};
struct Draw {So So[4];};struct Graphics {bool Xfb;};
int main() {
 for(nth=0;nth<=9;++nth) {
  attempts=live=0;HRESULT hr=S_OK;pipe_context context,*pipe=&context;
  pipe_resource resource;AGX_D3D12_BUFFER buffer={&resource,64};
  Draw value={};Draw *draw=&value;Graphics g={true},*graphics=&g;
  for(auto &v:draw->So) {v.Buffer=v.Filled=&buffer;v.Size=64;}
'''+region+r'''
  assert(hr==((nth>0&&nth<=8)?E_OUTOFMEMORY:S_OK));
  for(auto *p:so)if(p){release(p->offset);release(p);}assert(!live);
 }
}
''')

    def test_reinterpret_map_failure_releases_partial_transfer(self):
        text=OWNER.read_text()
        prefix=PRELUDE[:PRELUDE.index('struct pipe_resource {};')]+r'''
#define PIPE_RESOURCE_FLAG_UNMAPPABLE 1
#define PIPE_FORMAT_NONE 0
#define PIPE_MAP_READ 1
#define PIPE_MAP_WRITE 2
struct pipe_resource {unsigned flags,format;};
struct pipe_box {int depth;};
struct pipe_transfer {unsigned stride,layer_stride;};
static unsigned char bytes[64];static bool partial;
struct pipe_context {
 void *(*texture_map)(pipe_context *,pipe_resource *,unsigned,unsigned,const pipe_box *,pipe_transfer **)=[](pipe_context *,pipe_resource *,unsigned,unsigned,const pipe_box *,pipe_transfer **t)->void * {
   *t=(pipe_transfer *)allocate();if(!*t)return nullptr;
   **t={8,8};return partial?nullptr:bytes;
 };
 void (*texture_unmap)(pipe_context *,pipe_transfer *)=[](pipe_context *,pipe_transfer *t){assert(t);release(t);};
};
static unsigned bc_staging_format(unsigned) {return 0;}
static void gpu_copy_blocks(pipe_context *,pipe_resource *,unsigned,const pipe_box &,pipe_resource *,unsigned,const pipe_box &) {}
'''
        self.run_cpp(prefix+function(text,'static HRESULT copy_reinterpret_blocks(')+r'''
int main() {
 for(unsigned mode=0;mode<4;++mode) {
  nth=mode<3?mode:0;partial=mode==3;attempts=live=0;
  pipe_context pipe;pipe_resource src={},dst={};pipe_box box={1};
  HRESULT hr=copy_reinterpret_blocks(&pipe,&dst,0,box,&src,0,box,1,8);
  assert(hr==(mode?E_OUTOFMEMORY:S_OK));assert(!live);
 }
}
''')

    def test_heap_allocation_failure_stops_before_binding_zero_handles(self):
        text=OWNER.read_text()
        prefix=PRELUDE[:PRELUDE.index('struct pipe_resource {};')]+r'''
using mesa_shader_stage=int;using APPLE_AGX_U16=uint16_t;
#define AGX_D3D12_HEAP_CS 5
#define AGX_D3D12_HEAP_STAGES 6
#define AGX_D3D12_HEAP_RANGES 1
#define DXBC_HEAP_SAMPLER_MAP_MAX 4
#define DXBC_SAMPLER_HANDLE_MASK 2047
#define DXBC_SAMPLER_MINMAX_SHIFT 11
#define DXBC_TIR_GS_HEAP_VIEWPORT_BYTES 32
#define DXBC_HEAP_TABLE_CB 14
#define DXBC_HEAP_MIRROR_CB 15
#define MESA_SHADER_GEOMETRY 2
static mesa_shader_stage heap_stages[6]={0,1,2,3,4,5};
struct pipe_resource {};
struct pipe_constant_buffer {const void *user_buffer;pipe_resource *buffer;unsigned buffer_size;};
struct pipe_context {
 void (*set_constant_buffer)(pipe_context *,int,unsigned,const pipe_constant_buffer *)=[](pipe_context *,int,unsigned,const pipe_constant_buffer *){++bound;};
};
struct AGX_D3D12_BUFFER {void *Resource;uint64_t Bytes;};
struct Sampler {uint8_t Packed[8];unsigned Valid,Bias;};
using AGX_D3D12_HEAP_SAMPLER=Sampler;
static void *GetProcessHeap(){return nullptr;}
static void *HeapAlloc(void *,unsigned,size_t n){return malloc(n);}
static void HeapFree(void *,unsigned,void *p){free(p);}
struct AGX_D3D12_HEAP_BINDING {
 AGX_D3D12_BUFFER *Heap;unsigned SamplerCount,TextureCount,WriteCount,First[4];
 Sampler *Samplers;AGX_D3D12_BUFFER *Textures,*Writes;
};
struct AGX_D3D12_OWNER {pipe_context *Context;AGX_D3D12_HEAP_BINDING PendingHeaps[6];uint32_t HeapTables[6][16];bool TirViewportValid;float TirViewport[4];};
static int AgxWin32AsahiBatchSamplerHandles(pipe_context *,int,const void *,unsigned,unsigned count,uint16_t *handles) {
 void *p=allocate();if(!p)return 0;release(p);for(unsigned i=0;i<count;++i)handles[i]=28;return 1;
}
static int AgxWin32AsahiContextFaulted(pipe_context *) {return faulted;}
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,unsigned) {}
static void AgxWin32AsahiBatchReadResources(pipe_context *,pipe_resource **,unsigned,int) {++encoded;}
static void AgxWin32AsahiBatchWriteResources(pipe_context *,pipe_resource **,unsigned,int) {++encoded;}
'''
        marker='static HRESULT bind_heaps(' if 'static HRESULT bind_heaps(' in text else 'static void bind_heaps('
        self.run_cpp(prefix+function(text,'static HRESULT bind_heap_samplers(')+function(text,marker)+r'''
int main() {
 for(nth=0;nth<=1;++nth) {
  attempts=live=bound=encoded=0;pipe_context pipe;AGX_D3D12_OWNER owner={};owner.Context=&pipe;
  Sampler samplers[1]={{{0},1,0}};AGX_D3D12_BUFFER resource={nullptr,0};
  owner.PendingHeaps[5].SamplerCount=1;owner.PendingHeaps[5].Samplers=samplers;
  owner.PendingHeaps[5].TextureCount=1;owner.PendingHeaps[5].Textures=&resource;
  bind_heaps(&owner,true);
  assert(bound==(nth?0u:1u));assert(encoded==(nth?0u:1u));assert(!live);
 }
}
''')

    def test_optional_stage_sampler_failure_releases_every_partial_stage(self):
        text=OWNER.read_text();draw=function(text,'extern "C" HRESULT AgxD3d12OwnerDraw(')
        region=draw[draw.index('  for (unsigned s = 0; stages &&'):draw.index('  /* FL 11_1: viewport 0')]
        prefix=PRELUDE+r'''
using mesa_shader_stage=int;
static void *GetProcessHeap(){return nullptr;}
static void HeapFree(void *,int,void *p){release(p);}
struct OptionalStage {
 int stage;bool bound;pipe_sampler_view *views[4];void *samplers[4];unsigned view_count,sampler_count;
};
const unsigned kOptionalStages=3;
struct Bind {AGX_D3D12_SAMPLER Samplers[4];AGX_D3D12_CBV Cbvs[4];AGX_D3D12_SRV Srvs[4];AGX_D3D12_UAV Uavs[4];unsigned SrvCount;};
'''
        self.run_cpp(prefix+function(text,'static void release_optional_stages(')+r'''
int main() {
 for(nth=0;nth<=13;++nth) {
  attempts=0;live=1;HRESULT hr=S_OK;pipe_context context,*pipe=&context;
  auto *stages=(OptionalStage *)calloc(3,sizeof(OptionalStage));assert(stages);
  Bind bindings[3]={};
  struct {int stage;void *shader;Bind *bind;const uint8_t *srv_dimensions,*uav_dimensions;} optional[3]={};
  for(unsigned i=0;i<3;++i){optional[i]={int(i),&context,&bindings[i],nullptr,nullptr};for(auto &s:bindings[i].Samplers)s.Valid=1;}
'''+region+r'''
  assert(hr==((nth>0&&nth<=12)?E_OUTOFMEMORY:S_OK));
  release_optional_stages(pipe,stages,true);assert(!live);
 }
}
''')

if __name__=='__main__':unittest.main()
