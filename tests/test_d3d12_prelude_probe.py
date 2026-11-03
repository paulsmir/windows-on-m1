"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The prelude probe must scope a statistics query over the actual render, and
must reject skipped/stale query results, even if pixels and compute pass.
"""
import unittest
from test_d3d12_rc_pair_probe import PROBE, function
import test_d3d12_rc_pair_probe as pair

class PreludeProbe(unittest.TestCase):
    build = pair.RcPairProbe.build

    def test_query_scope_and_exact_ia_counts(self):
        self.assertIn('static unsigned q3_bad_statistics(', PROBE.read_text())
        self.build(r'''
#include <assert.h>
typedef unsigned UINT;
typedef unsigned long long UINT64;
typedef int ID3D12GraphicsCommandList,ID3D12RootSignature,ID3D12Resource,ID3D12QueryHeap,D3D12_CPU_DESCRIPTOR_HANDLE;
typedef struct {UINT64 IAVertices,IAPrimitives,VSInvocations,GSInvocations,GSPrimitives,CInvocations,CPrimitives,PSInvocations,HSInvocations,DSInvocations,CSInvocations;} D3D12_QUERY_DATA_PIPELINE_STATISTICS;
#define D3D12_QUERY_TYPE_PIPELINE_STATISTICS 1
static int phase;
static void ID3D12GraphicsCommandList_BeginQuery(int *l,int *q,int t,UINT i) {assert(*l==1 && *q==2 && t==1 && i==0 && !phase);phase=1;}
static void q3_record_render(int *l,int *r,int h,int *v,UINT n) {assert(*l==1 && *r==3 && h==4 && *v==5 && n==17 && phase==1);phase=2;}
static void ID3D12GraphicsCommandList_EndQuery(int *l,int *q,int t,UINT i) {assert(*l==1 && *q==2 && t==1 && i==0 && phase==2);phase=3;}
''' + function('q3_record_prelude') + '\n' + function('q3_bad_statistics') + r'''
int main(void) {
 int l=1,q=2,r=3,v=5;
 q3_record_prelude(&l,&r,4,&v,17,&q,0);assert(phase==3);
 D3D12_QUERY_DATA_PIPELINE_STATISTICS s={68,34,68,0,0,34,34,69632,0,0,0};
 assert(!q3_bad_statistics(&s,17));
 UINT64 *p=(UINT64 *)&s;
 for(unsigned i=0;i<11;++i) {
   if(i==7) continue; /* PS count depends on rasterization, not the IA prelude. */
   ++p[i];assert(q3_bad_statistics(&s,17));--p[i];
 }
 D3D12_QUERY_DATA_PIPELINE_STATISTICS zero={0};
 assert(q3_bad_statistics(&zero,17));
 assert(q3_bad_statistics(&s,18));
}
''')

    def test_named_probe_wiring_and_delayed_resolve(self):
        s=PROBE.read_text()
        self.assertIn('{"Q3PC", q3_pc}',s)
        self.assertIn('{"Q3CP", q3_cp}',s)
        body=function('q3_sequence')
        self.assertLess(body.index('q3_submit_pair('),body.index('ResolveQueryData('))
        self.assertIn('q3_warm_prelude(',body)
        self.assertIn('q3_bad_statistics(',body)
        isolated=s[s.index('static int run_isolated('):]
        isolated=isolated[:isolated.index('\n}')+2]
        self.assertEqual(isolated.count('!strcmp(name, "Q3PC")'),2)
        self.assertEqual(isolated.count('!strcmp(name, "Q3CP")'),2)

    def test_exact_resource_warmup_is_serial_and_bounded(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Reusing an allocator before retirement, warming a different query slot,
        or matching stale warm query counts must not make the probe qualify RP.
        """
        self.build(r'''
#include <assert.h>
typedef unsigned UINT;
typedef unsigned long long UINT64;
typedef int HRESULT,ID3D12CommandQueue,ID3D12Fence,ID3D12GraphicsCommandList,ID3D12CommandAllocator,ID3D12PipelineState,ID3D12RootSignature,ID3D12Resource,ID3D12QueryHeap,D3D12_CPU_DESCRIPTOR_HANDLE;
#define S_OK 0
#define E_FAIL (-1)
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
#define NULL ((void*)0)
#define D3D12_RESOURCE_STATE_COMMON 0
#define D3D12_RESOURCE_STATE_UNORDERED_ACCESS 8
static unsigned runs, recorded, retired, reset, transitions, fail, killed, warmed;
static void ID3D12GraphicsCommandList_SetPipelineState(int *l,int *p) {assert(*l==1 && *p==4);}
static void q3_record_prelude(int *l,int *r,int h,int *v,UINT n,int *q,UINT i) {
 assert(*l==1 && *r==6 && h==10 && *v==8 && n==18 && *q==9 && i==0 && !recorded);recorded=1;
}
static void q3_record_compute(int *l,int *r,int *u,UINT n) {assert(*l==1 && *r==7 && *u==11 && n==99 && !recorded);recorded=1;}
static HRESULT run_list(int *q,int *f,int *l,UINT64 v,const char *name) {
 (void)name;assert(*q==2 && *f==3 && *l==1 && recorded && v==100+runs);++runs;
 if(runs==fail)return E_FAIL;
 retired=1;return S_OK;
}
static int GetCurrentProcess(void) {return 99;}
static void TerminateProcess(int p,unsigned c) {assert(p==99 && c==1);++killed;}
static HRESULT ID3D12CommandAllocator_Reset(int *a) {assert(*a==12 && retired);retired=recorded=0;reset=1;return S_OK;}
static HRESULT ID3D12GraphicsCommandList_Reset(int *l,int *a,int *p) {assert(*l==1 && *a==12 && reset);if(p)assert(*p==5);reset=0;return S_OK;}
static void transition(int *l,int *u,int b,int a) {assert(*l==1 && *u==11 && b==0 && a==8);++transitions;}
''' + function('q3_warm_prelude') + r'''
int main(void) {
 int l=1,q=2,f=3,p=4,c=5,r=6,cr=7,v=8,h=9,u=11,a=12;
 int *psos[]={&p,&c},*roots[]={&r,&cr};
 for(warmed=0;warmed<2;++warmed)for(fail=0;fail<=4;++fail) {
  runs=recorded=retired=reset=transitions=killed=0;
  HRESULT hr=q3_warm_prelude(&q,&f,&l,&a,psos,roots,10,&v,&u,&h,17,99,warmed,100);
  assert((hr==S_OK)==!fail);
  if(fail)assert(killed==1 && runs==fail);
  else assert(runs==4 && transitions==1+warmed && !recorded && !retired && !reset && !killed);
 }
}
''')
