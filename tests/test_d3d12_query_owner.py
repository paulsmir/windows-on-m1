"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The owner accepted mismatched resolve types (including an out-of-bounds binary
slot on a timestamp heap). Resolve must validate before emitting work, maintain
D3D12's eleven UINT64 field order/stride, queue GPU results and preserve high
bits, offsets and neighboring bytes. Never-used slots retain defensive zero
fill; D3D12 does not define results for queries without EndQuery.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import ROOT

PRELUDE='#include "'+str(ROOT/'tests/d3d12_wait_disabled_fixture.h')+'"\n'+r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
using UINT=unsigned;using UINT64=uint64_t;using SIZE_T=size_t;using HRESULT=int;
#define S_OK 0
#define E_INVALIDARG (-1)
#define E_OUTOFMEMORY (-2)
#define E_FAIL (-3)
#define DXGI_ERROR_DEVICE_HUNG (-4)
#define HEAP_ZERO_MEMORY 1
#define PIPE_QUERY_WAIT 1
#define PIPE_QUERY_TYPE_U64 3
#define PIPE_QUERY_PIPELINE_STATISTICS_SINGLE 10
#define PIPE_QUERY_TIMESTAMP 8
#define PIPE_QUERY_OCCLUSION_COUNTER 0
#define PIPE_QUERY_OCCLUSION_PREDICATE 1
static void *GetProcessHeap(){return nullptr;}
static void *HeapAlloc(void *,unsigned,size_t n){return calloc(1,n);}
static void HeapFree(void *,unsigned,void *p){free(p);}
struct pipe_resource {uint64_t data[64];};
struct pipe_query {unsigned type,index;uint64_t result;};
struct operation {pipe_query *query;pipe_resource *dst;unsigned at;};
static std::vector<operation> gpu;
static bool faulted, fault_on_emit;
struct pipe_context {
 pipe_query *(*create_query)(pipe_context *,unsigned,unsigned)=[](pipe_context *,unsigned type,unsigned index){return new pipe_query{type,index,0};};
 void (*destroy_query)(pipe_context *,pipe_query *)=[](pipe_context *,pipe_query *q){delete q;};
 bool (*begin_query)(pipe_context *,pipe_query *)=[](pipe_context *,pipe_query *q){q->result=0;return true;};
 bool (*end_query)(pipe_context *,pipe_query *)=[](pipe_context *,pipe_query *q){q->result=0x100000000ull+q->index+1;return true;};
 void (*get_query_result_resource)(pipe_context *,pipe_query *,unsigned,unsigned,int,pipe_resource *,unsigned)=[](pipe_context *,pipe_query *q,unsigned flags,unsigned type,int index,pipe_resource *dst,unsigned at){assert(flags==PIPE_QUERY_WAIT&&type==PIPE_QUERY_TYPE_U64&&index==0);gpu.push_back({q,dst,at});if(fault_on_emit)faulted=true;};
 void (*clear_buffer)(pipe_context *,pipe_resource *,unsigned,unsigned,const void *,unsigned)=[](pipe_context *,pipe_resource *dst,unsigned at,unsigned bytes,const void *zero,unsigned size){assert(bytes==8&&size==8&&*(const uint64_t *)zero==0);gpu.push_back({nullptr,dst,at});};
};
struct AGX_D3D12_OWNER {pipe_context *Context;unsigned PendingDispatches;};
struct AGX_D3D12_BUFFER {void *Resource;uint64_t Bytes;};
struct OwnerLock {OwnerLock(AGX_D3D12_OWNER *){}};
static int AgxWin32AsahiContextFaulted(pipe_context *) {return faulted;}
static HRESULT owner_failure(AGX_D3D12_OWNER *,HRESULT hr){return hr;}
static bool owner_stats_no_ps(){return false;} /* STATS-NOPS diagnostic: default off */
static void owner_stats_no_ps_receipt(unsigned){}
static void run_gpu(){for(auto &op:gpu){uint64_t v=op.query?op.query->result:0; if(op.query&&op.query->type==PIPE_QUERY_OCCLUSION_PREDICATE)v=uint32_t(v)!=0;memcpy((char *)op.dst->data+op.at,&v,8);}gpu.clear();}
'''


class QueryOwner(unittest.TestCase):
    def replay(self, main):
        text=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        begin=text.index('struct AGX_D3D12_QUERY_HEAP {')
        end=text.index('/* C7c:',begin)
        with tempfile.TemporaryDirectory(prefix='query-owner-') as td:
            p=Path(td);src=p/'test.cpp';src.write_text(PRELUDE+text[begin:end]+main)
            build=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(src),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

    def test_resolve_type_refused_before_any_gpu_work(self):
        self.replay(r'''
int main(){pipe_context pipe;AGX_D3D12_OWNER owner{&pipe,0};pipe_resource r={};AGX_D3D12_BUFFER dst{&r,sizeof(r)};AGX_D3D12_QUERY_HEAP *h=nullptr;
 assert(AgxD3d12OwnerCreateQueryHeap(&owner,1,1,&h)==S_OK);
 assert(AgxD3d12OwnerResolveQueries(&owner,h,1,0,1,&dst,0)==E_INVALIDARG);assert(gpu.empty());
 assert(AgxD3d12OwnerResolveQueries(&owner,h,99,0,1,&dst,0)==E_INVALIDARG);assert(gpu.empty());
 AgxD3d12OwnerDestroyQueryHeap(&owner,h);return 0;}
''')

    def test_statistic_order_multiple_queries_offset_and_u64_transport(self):
        self.replay(r'''
int main(){pipe_context pipe;AGX_D3D12_OWNER owner{&pipe,0};pipe_resource r;memset(&r,0xcc,sizeof(r));AGX_D3D12_BUFFER dst{&r,sizeof(r)};AGX_D3D12_QUERY_HEAP *h=nullptr;
 assert(AgxD3d12OwnerCreateQueryHeap(&owner,2,3,&h)==S_OK);
 for(unsigned i=1;i<3;i++){assert(AgxD3d12OwnerQuery(&owner,h,3,i,1)==S_OK);assert(AgxD3d12OwnerQuery(&owner,h,3,i,0)==S_OK);}
 assert(AgxD3d12OwnerResolveQueries(&owner,h,3,1,2,&dst,8)==S_OK);
 assert(gpu.size()==22 && owner.PendingDispatches==1);
 assert(r.data[1]==0xccccccccccccccccull);run_gpu();
 for(unsigned i=0;i<2;i++)for(unsigned v=0;v<11;v++)assert(r.data[1+i*11+v]==0x100000001ull+v);
 assert(r.data[0]==0xccccccccccccccccull && r.data[23]==0xccccccccccccccccull);
 assert(AgxD3d12OwnerResolveQueries(&owner,h,3,0,1,&dst,0)==S_OK);run_gpu();for(unsigned i=0;i<11;i++)assert(r.data[i]==0);
 assert(AgxD3d12OwnerResolveQueries(&owner,h,3,2,2,&dst,0)==E_INVALIDARG);
 assert(AgxD3d12OwnerResolveQueries(&owner,h,3,1,1,&dst,1)==E_INVALIDARG);
 assert(AgxD3d12OwnerResolveQueries(&owner,h,3,1,1,&dst,sizeof(r)-8)==E_INVALIDARG);
 assert(gpu.empty());AgxD3d12OwnerDestroyQueryHeap(&owner,h);return 0;}
''')

    def test_resolve_failure_is_reported_without_a_cpu_fallback(self):
        self.replay(r'''
int main(){pipe_context pipe;AGX_D3D12_OWNER owner{&pipe,0};pipe_resource r={};AGX_D3D12_BUFFER dst{&r,sizeof(r)};AGX_D3D12_QUERY_HEAP *h=nullptr;
 assert(AgxD3d12OwnerCreateQueryHeap(&owner,1,1,&h)==S_OK);
 assert(AgxD3d12OwnerQuery(&owner,h,2,0,0)==S_OK);fault_on_emit=true;
 assert(AgxD3d12OwnerResolveQueries(&owner,h,2,0,1,&dst,0)==DXGI_ERROR_DEVICE_HUNG);assert(gpu.size()==1 && faulted);
 AgxD3d12OwnerDestroyQueryHeap(&owner,h);return 0;}
''')

if __name__=='__main__':unittest.main()
