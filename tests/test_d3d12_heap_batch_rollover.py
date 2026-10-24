"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A later stage's sampler rollover must not strand earlier CB14 handles and BO
references in a submitted batch. Exercise actual owner binding and native
sampler selection, with allocation/flush callbacks as the platform boundary.
Deduplication must preserve stage-local metadata and room for native samplers.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body
from test_d3d12_owner_allocation_failure import function

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'

HEADER = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define AGX_SAMPLER_HEAP_SIZE 8u
#define MESA_SHADER_STAGES 6u
#define MESA_SHADER_COMPUTE 5u
#define APPLE_AGX_U16 uint16_t
struct agx_sampler_packed {uint8_t bytes[8];};
struct agx_bo {struct agx_sampler_packed entries[8];};
struct agx_batch {struct {struct agx_bo *bo;unsigned count;} sampler_heap;unsigned reads,writes;};
struct pipe_resource {unsigned id;};
struct pipe_constant_buffer {const void *user_buffer;struct pipe_resource *buffer;unsigned buffer_size;};
struct pipe_context {void *screen;void (*set_constant_buffer)(struct pipe_context *,int,unsigned,const struct pipe_constant_buffer *);};
struct shader {bool uses_bindless_samplers;};
struct agx_context {struct pipe_context base;bool any_faults;struct {unsigned valid_samplers;struct shader *shader;} stage[6];};
struct agx_device {unsigned unused;};
#define agx_context(p) ((struct agx_context *)(p))
#ifdef __cplusplus
extern "C" {
#endif
struct agx_device *agx_device(void *);
struct agx_batch *heap_batch(struct agx_context *,int);
void agx_flush_batch_for_reason(struct agx_context *,struct agx_batch *,const char *);
void *agx_bo_map(struct agx_bo *);
unsigned agx_sampler_heap_add(struct agx_device *,void *,struct agx_sampler_packed *);
void AgxWin32AsahiMarkContextFault(struct agx_context *,unsigned);
int AgxWin32AsahiBatchSamplerHandles(struct pipe_context *,int,const void *,unsigned,unsigned,uint16_t *);
#ifdef __cplusplus
}
#endif
'''

PROGRAM = r'''
#include "fixture.h"
#include <cstdlib>
using HRESULT=int;using UINT=unsigned;using mesa_shader_stage=int;
#define S_OK 0
#define E_OUTOFMEMORY (-2)
#define E_INVALIDARG (-3)
#define DXGI_ERROR_DEVICE_HUNG (-4)
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
#define AGX_D3D12_HEAP_CS 5u
#define AGX_D3D12_HEAP_STAGES 6u
#define AGX_D3D12_HEAP_RANGES 1u
#define DXBC_HEAP_SAMPLER_MAP_MAX 16u
#define DXBC_SAMPLER_HANDLE_MASK 2047u
#define DXBC_SAMPLER_MINMAX_SHIFT 11u
#define DXBC_TIR_GS_HEAP_VIEWPORT_BYTES 80u
#define DXBC_HEAP_TABLE_CB 14u
#define DXBC_HEAP_MIRROR_CB 15u
#define MESA_SHADER_GEOMETRY 2
struct AGX_D3D12_BUFFER {void *Resource;uint64_t Bytes;};
struct AGX_D3D12_HEAP_SAMPLER {uint8_t Packed[8];uint16_t Bias,Valid;};
struct AGX_D3D12_HEAP_BINDING {
 const AGX_D3D12_BUFFER *Heap;uint32_t First[1][4];unsigned Ranges;
 const AGX_D3D12_BUFFER *Textures;unsigned TextureCount;
 const AGX_D3D12_BUFFER *Writes;unsigned WriteCount;
 const AGX_D3D12_HEAP_SAMPLER *Samplers;unsigned SamplerCount;
};
struct AGX_D3D12_OWNER {pipe_context *Context;AGX_D3D12_HEAP_BINDING PendingHeaps[6];
 uint32_t HeapTables[6][24];bool TirViewportValid;float TirViewport[4];};
static const mesa_shader_stage heap_stages[6]={0,1,2,3,4,5};
static struct agx_device dev;
static agx_batch batches[3];static agx_bo bos[3];
static unsigned current,flushes,sets,allocation_calls,live,fail_alloc;
static bool fail_flush,fail_map,fail_add;
static uint32_t uploaded[6][24];
static void *GetProcessHeap(){return nullptr;}
static void *HeapAlloc(void *,unsigned,size_t n) {
 if(++allocation_calls==fail_alloc)return nullptr;
 void *p=malloc(n);if(p)++live;return p;
}
static void HeapFree(void *,unsigned,void *p){if(p){assert(live);--live;free(p);}}
extern "C" {
struct agx_device *agx_device(void *) {return &dev;}
struct agx_batch *heap_batch(struct agx_context *c,int) {return c->any_faults?nullptr:&batches[current];}
void agx_flush_batch_for_reason(struct agx_context *c,struct agx_batch *,const char *) {
 ++flushes;assert(current<2);++current;if(fail_flush)c->any_faults=true;
}
void *agx_bo_map(struct agx_bo *b){return fail_map?nullptr:b->entries;}
unsigned agx_sampler_heap_add(struct agx_device *,void *,struct agx_sampler_packed *p) {
 if(fail_add)return 65535;
 agx_batch &b=batches[current];assert(b.sampler_heap.count<8);
 b.sampler_heap.bo=&bos[current];bos[current].entries[b.sampler_heap.count]=*p;
 return b.sampler_heap.count++;
}
void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned){c->any_faults=true;}
}
static int AgxWin32AsahiContextFaulted(pipe_context *p){return agx_context(p)->any_faults;}
static void AdmissionUmdDiagnostic(const char *,HRESULT,const UINT *,unsigned){}
static void AgxWin32AsahiBatchReadResources(pipe_context *,pipe_resource **r,unsigned n,int){
 for(unsigned i=0;i<n;++i)batches[current].reads|=1u<<r[i]->id;
}
static void AgxWin32AsahiBatchWriteResources(pipe_context *,pipe_resource **r,unsigned n,int){
 for(unsigned i=0;i<n;++i)batches[current].writes|=1u<<r[i]->id;
}
static void set_cb(pipe_context *,int stage,unsigned slot,const pipe_constant_buffer *b){
 ++sets;if(slot==14)memcpy(uploaded[stage],b->user_buffer,b->buffer_size);
}
@@OWNER@@
int main(int argc,char **argv){
 assert(argc==2);unsigned mode=atoi(argv[1]);agx_context ctx={};ctx.base.set_constant_buffer=set_cb;
 AGX_D3D12_OWNER o={};o.Context=&ctx.base;
 pipe_resource resources[3]={{0},{1},{2}};
 AGX_D3D12_BUFFER rb[3]={{&resources[0],65536},{&resources[1],65536},{&resources[2],65536}};
 AGX_D3D12_HEAP_SAMPLER ss[3][16]={};
 unsigned count=mode==1?10u:3u;
 for(unsigned s=0;s<3;++s)for(unsigned i=0;i<count;++i){
  ss[s][i].Packed[0]=(uint8_t)(20+(mode==1?i%3:i));
  ss[s][i].Bias=(uint16_t)(s+2);ss[s][i].Valid=(uint16_t)(1u|(s<<8));
 }
 unsigned stage0=mode==2?5:0,stage1=mode==2?5:1;
 for(unsigned s=stage0;s<=stage1;++s){
  auto &h=o.PendingHeaps[s];h.Heap=&rb[s==5?0:s];
  h.Samplers=ss[s==5?0:s];h.SamplerCount=count;
  h.Textures=&rb[s==5?0:s];h.TextureCount=1;
  h.Writes=&rb[s==5?0:s];h.WriteCount=1;
 }
 // Poison unused stage: compute must never consume graphics and vice versa.
 o.PendingHeaps[mode==2?0:5].SamplerCount=100000;
 batches[0].sampler_heap.bo=&bos[0];batches[0].sampler_heap.count=5;
 for(unsigned i=0;i<5;++i)bos[0].entries[i].bytes[0]=(uint8_t)(80+i);
 shader bindless={true},ordinary={false};
 if(mode==3){ctx.stage[0].valid_samplers=3u;ctx.stage[0].shader=&bindless;} // two native insertions after preparation
 if(mode==4){fail_flush=true;batches[0].sampler_heap.count=7;}
 if(mode==5)fail_map=true;
 if(mode==6)fail_add=true;
 if(mode==7)fail_alloc=1;
 if(mode==8)fail_alloc=2;
 if(mode==9){ // No samplers still registers heap resources.
  o.PendingHeaps[0].SamplerCount=o.PendingHeaps[1].SamplerCount=0;
 }
 if(mode==10){ // More unique descriptors than a fresh native heap can fit.
  for(unsigned i=0;i<9;++i)ss[0][i].Packed[0]=(uint8_t)i;
  o.PendingHeaps[0].SamplerCount=9;
 }
 if(mode==11){ctx.stage[0].valid_samplers=0x3fu;ctx.stage[0].shader=&bindless;} // 3+6 cannot fit even when fresh
 if(mode==12 || mode==13){
  batches[0].sampler_heap.count=0;
  count=8;
  for(unsigned s=0;s<2;++s){o.PendingHeaps[s].SamplerCount=count;
   for(unsigned i=0;i<count;++i){ss[s][i].Packed[0]=(uint8_t)i;ss[s][i].Bias=(uint16_t)(s+2);ss[s][i].Valid=(uint16_t)(1u|(s<<8));}}
  ctx.stage[0].valid_samplers=1u;ctx.stage[0].shader=mode==13?&ordinary:nullptr;
 }
 HRESULT hr=bind_heaps(&o,mode==2);
 bool failure=mode>=4 && mode<=8 || mode==10 || mode==11;
 if(failure){assert(FAILED(hr));assert(!sets);assert(!batches[current].reads && !batches[current].writes);}
 else {
  assert(hr==S_OK);
  unsigned expected=mode==2?1u:3u;
  assert(batches[current].reads==expected && batches[current].writes==expected);
  for(unsigned s=stage0;mode!=9 && s<=stage1;++s)for(unsigned i=0;i<count;++i){
   unsigned k=s==5?0:s;uint32_t w=uploaded[s][4+i];unsigned handle=w&2047u;
   assert(handle>=28 && handle-28<batches[current].sampler_heap.count);
   assert(bos[current].entries[handle-28].bytes[0]==ss[k][i].Packed[0]);
   assert((w>>16)==ss[k][i].Bias && ((w>>11)&31u)==k);
  }
  if(mode==3)assert(batches[current].sampler_heap.count+2<=8);
 }
 assert(!live);
}
'''

class HeapBatchRollover(unittest.TestCase):
    def test_all_stage_handles_and_resources_survive_rollover(self):
        owner = (WINSYS/'agx_d3d12_windows.cpp').read_text()
        scene = (WINSYS/'agx_win32_asahi_scene.c').read_text()
        helpers = ''
        if 'static HRESULT bind_heap_samplers(' in owner:
            helpers = function(owner, 'static HRESULT bind_heap_samplers(')
        native = ''
        if 'heap_sampler_headroom(' in scene:
            native = function(scene, 'static unsigned heap_sampler_headroom(')
        native += body(scene, 'AgxWin32AsahiBatchSamplerHandles')
        with tempfile.TemporaryDirectory(prefix='heap-rollover-') as directory:
            p = Path(directory)
            (p/'fixture.h').write_text(HEADER)
            (p/'native.c').write_text('#include "fixture.h"\n'+native)
            (p/'owner.cpp').write_text(PROGRAM.replace('@@OWNER@@', helpers+function(owner, 'static HRESULT bind_heaps(')))
            for command in [
                ['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-c',str(p/'native.c'),'-o',str(p/'native.o')],
                ['clang++','-std=c++17','-Wall','-Wextra','-Wno-unused-function','-Wno-logical-op-parentheses','-Werror','-fsanitize=address,undefined',str(p/'owner.cpp'),str(p/'native.o'),'-o',str(p/'replay')]]:
                result = subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            for mode in range(14):
                with self.subTest(mode=mode):
                    run = subprocess.run([str(p/'replay'),str(mode)],capture_output=True,text=True)
                    self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__ == '__main__':
    unittest.main()
