"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The prelude probe must execute counted indirect work, reject skipped/stale
query results and reject a late successor even when all output values pass.
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
typedef int ID3D12GraphicsCommandList,ID3D12RootSignature,ID3D12Resource,ID3D12QueryHeap,ID3D12CommandSignature,D3D12_CPU_DESCRIPTOR_HANDLE;
typedef struct {UINT64 IAVertices,IAPrimitives,VSInvocations,GSInvocations,GSPrimitives,CInvocations,CPrimitives,PSInvocations,HSInvocations,DSInvocations,CSInvocations;} D3D12_QUERY_DATA_PIPELINE_STATISTICS;
#define D3D12_QUERY_TYPE_PIPELINE_STATISTICS 1
static int phase;
static void ID3D12GraphicsCommandList_BeginQuery(int *l,int *q,int t,UINT i) {assert(*l==1 && *q==2 && t==1 && i==0 && !phase);phase=1;}
static void q3_bind_render(int *l,int *r,int h,int *v) {assert(*l==1 && *r==3 && h==4 && *v==5 && phase==1);phase=2;}
static void ID3D12GraphicsCommandList_ExecuteIndirect(int *l,int *s,UINT max,int *v,UINT64 off,int *c,UINT64 co) {
 assert(*l==1 && *s==6 && max==2 && *v==5 && off==2048 && c==v && co==2080 && phase==2);phase=3;
}
static void ID3D12GraphicsCommandList_EndQuery(int *l,int *q,int t,UINT i) {assert(*l==1 && *q==2 && t==1 && i==0 && phase==3);phase=4;}
''' + function('q3_record_prelude') + '\n' + function('q3_bad_statistics') + r'''
int main(void) {
 int l=1,q=2,r=3,v=5,sig=6;
 q3_record_prelude(&l,&r,4,&v,&sig,&q,0);assert(phase==4);
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
typedef int HRESULT,ID3D12CommandQueue,ID3D12Fence,ID3D12GraphicsCommandList,ID3D12CommandAllocator,ID3D12PipelineState,ID3D12RootSignature,ID3D12Resource,ID3D12QueryHeap,ID3D12CommandSignature,D3D12_CPU_DESCRIPTOR_HANDLE;
#define S_OK 0
#define E_FAIL (-1)
#define FAILED(h) ((h)<0)
#define SUCCEEDED(h) ((h)>=0)
#define NULL ((void*)0)
#define D3D12_RESOURCE_STATE_COMMON 0
#define D3D12_RESOURCE_STATE_UNORDERED_ACCESS 8
static unsigned runs, recorded, retired, reset, transitions, fail, killed, warmed, arguments;
static HRESULT q3_set_prelude_arguments(int *v,UINT n) {assert(*v==8 && !recorded && n==(arguments ? 17u:18u));++arguments;return S_OK;}
static void ID3D12GraphicsCommandList_SetPipelineState(int *l,int *p) {assert(*l==1 && *p==4);}
static void q3_record_prelude(int *l,int *r,int h,int *v,int *s,int *q,UINT i) {
 assert(*l==1 && *r==6 && h==10 && *v==8 && *s==13 && arguments==1 && *q==9 && i==0 && !recorded);recorded=1;
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
 int l=1,q=2,f=3,p=4,c=5,r=6,cr=7,v=8,h=9,u=11,a=12,sig=13;
 int *psos[]={&p,&c},*roots[]={&r,&cr};
 for(warmed=0;warmed<2;++warmed)for(fail=0;fail<=4;++fail) {
  runs=recorded=retired=reset=transitions=killed=arguments=0;
  HRESULT hr=q3_warm_prelude(&q,&f,&l,&a,psos,roots,10,&v,&u,&h,&sig,17,99,warmed,100);
  assert((hr==S_OK)==!fail);
  if(fail)assert(killed==1 && runs==fail);
  else assert(arguments==2 && runs==4 && transitions==1+warmed && !recorded && !retired && !reset && !killed);
 }
}
''')

    def test_arguments_are_counted_and_updates_preserve_other_upload_data(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Count ignored, wrong stride, or overwriting vertex/sentinel data would
        make the intended GPU-stream trigger or exact-output check invalid.
        """
        self.assertIn('static HRESULT q3_set_prelude_arguments(', PROBE.read_text())
        self.build(r'''
#include <assert.h>
#include <string.h>
typedef unsigned UINT;typedef int HRESULT,ID3D12Resource;
#define S_OK 0
#define E_FAIL (-1)
#define FAILED(h) ((h)<0)
static unsigned char data[65536];static unsigned mode,unmaps;
static HRESULT ID3D12Resource_Map(int *v,UINT sub,void *range,void **p) {
 assert(*v==1 && !sub && !range);*p=mode==2?NULL:data;return mode==1?E_FAIL:S_OK;
}
static void ID3D12Resource_Unmap(int *v,UINT sub,void *r) {assert(*v==1 && !sub && !r);++unmaps;}
'''+function('q3_set_prelude_arguments')+r'''
int main(void) {
 int v=1;
 for(mode=0;mode<3;++mode) {
  memset(data,0xa5,sizeof(data));unmaps=0;
  assert((q3_set_prelude_arguments(&v,262144)==S_OK)==!mode);
  assert(unmaps==(!mode));
  if(mode)continue;
  UINT expected[]={4,262144,0,0,4,1,0,0,1};
  assert(!memcmp(data+2048,expected,sizeof(expected)));
  for(UINT i=0;i<sizeof(data);++i)if(i<2048 || i>=2084)assert(data[i]==0xa5);
 }
}
''')

    def test_pending_oracle_rejects_exp2104_and_ambiguous_records(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        EXP2104 had correct pixels and a real prelude, but its successor arrived
        after the render retired. Do not label that as topology PASS again.
        """
        self.assertIn('static int q3_prelude_pending(', PROBE.read_text())
        header=PROBE.with_name('agx_d3d12_probe_completion.h')
        self.build('#include "'+str(header)+'"\n'+r'''
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
typedef unsigned UINT;typedef unsigned long long UINT64;
'''+function('q3_prelude_pending')+r'''
int main(void) {
 Q3_GPU_COMPLETION_QUERY q={0};q.Frequency=24000000;q.Count=2;q.Cursor=q.Newest=3;
 Q3_GPU_COMPLETION *r=&q.Record[0],*c=&q.Record[1];
 *r=(Q3_GPU_COMPLETION){.Sequence=2,.ProcessId=5652,.Fence=7170,.Kind=1,
 .SubmitQpc=1588716634,.CompleteQpc=1588735339,.PreludeStart=2377553361,.PreludeEnd=2377553590};
 *c=(Q3_GPU_COMPLETION){.Sequence=3,.ProcessId=5652,.Fence=7188,.Kind=2,
 .SubmitQpc=1589067457,.CompleteQpc=1589089068};
 assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));
 c->SubmitQpc=r->CompleteQpc-1;assert(q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));
 c->SubmitQpc=r->CompleteQpc;assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));
 c->SubmitQpc=r->CompleteQpc-1;
 r->PreludeEnd=0;assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));r->PreludeEnd=r->PreludeStart+1;
 q.Lost=1;assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));q.Lost=0;
 q.MissingTiming=1;assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));q.MissingTiming=0;
 q.MissingTiming=7;assert(q3_prelude_pending(&q,1,7,5652,1582355505,1589115974,0));q.MissingTiming=0;
 q.Reset=1;assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));q.Reset=0;
 c->Status=1;assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));c->Status=0;
 q.Record[2]=*r;q.Record[2].Sequence=4;q.Count=3;
 assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));
 q.Record[2].ProcessId=99;assert(q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,0));q.Count=2;
 c->SubmitQpc=r->SubmitQpc-1;c->CompleteQpc=r->CompleteQpc+1;
 assert(q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,1));
 r->SubmitQpc=c->CompleteQpc;assert(!q3_prelude_pending(&q,1,0,5652,1582355505,1589115974,1));
}
''')

    def test_prelude_does_not_use_stale_render_calibration(self):
        body=function('q3_sequence')
        self.assertIn('prelude=counted-indirect',body)
        self.assertIn('q3_prelude_pending(',body)
        self.assertIn('ID3D12Device_CreateCommandSignature(',body)
        self.assertIn('ID3D12CommandSignature_Release(',body)
        self.assertLess(body.index('prelude && mode != 3'),body.index('q3_calibrate_render('))
        self.assertIn('instances = 262144;',body)
