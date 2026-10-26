"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Repeated native proof must not walk unchanged backing, and cached success must
never survive a mutation of the native/logical identity or access contract.
"""
import unittest
import test_g4_native_allocation_guard as guard
from g3_vidmm_replay import body


class NativeReadinessGeneration(unittest.TestCase):
    def test_unchanged_generation_skips_page_walk_and_stale_token_reproves(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? RC45 walks every
        resident 4-KiB page on every submission, even when no mapping changed.
        The token is only a hint: zero/forged/stale tokens must get a real proof.
        """
        source = guard.KMD.read_text()
        funcs = '\n'.join(body(source, n) for n in (
            'AdmissionG3CopyPte', 'AdmissionGpuvaG3NativeQueryEscape'))
        funcs = funcs.replace('  ADMISSION_G3_TABLE_SHADOW *shadow;',
                              '  ++walks;\n  ADMISSION_G3_TABLE_SHADOW *shadow;', 1)
        fixture = guard.KMD_FIXTURE.replace('@@FUNCS@@', 'static unsigned walks;\n' + funcs)
        fixture = fixture[:fixture.index(' #define CHECK')] + r'''
 q.Version=3;
 #define CHECK() do {q.Ready=0;q.FailedVa=0;q.Reason=0;assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)==0);assert(q.Ready==1 && !refs && !locked);} while(0)
 CHECK();assert(q.Reserved2 && walks==16);
 ULONGLONG first=q.Reserved2;
 for(unsigned repeat=0;repeat<1000;++repeat) CHECK();
 assert(walks==16 && q.Reserved2==first);
 q.Reserved2=0;CHECK();assert(walks==32 && q.Reserved2!=first);
 q.Reserved2=first;CHECK();assert(walks==48);
 q.Reserved2=~0ULL;CHECK();assert(walks==64);
 // Caller access is still authenticated on a warm cache.
 opened.ReadOnly=1;q.Ready=0;q.Reason=0;q.FailedVa=0;
 assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)!=0 && walks==64);
 opened.ReadOnly=0;
 p.Contexts=NULL;
 assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)!=0 && walks==64);
 p.Contexts=&c;c.GpuvaG3Closing=1;
 assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)!=0 && walks==64);
 c.GpuvaG3Closing=0;c.GpuvaG3Poisoned=1;
 assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)!=0 && walks==64);
 c.GpuvaG3Poisoned=0;p.Graph.Created=0;
 assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)!=0 && walks==64);
 p.Graph.Created=1;p.Graph.Uncertain=1;
 assert(AdmissionGpuvaG3NativeQueryEscape(&a,&e)!=0 && walks==64);
}
'''
        guard.NativeAllocationGuard().compile_run(fixture, graph=True)

    def test_umd_returns_each_slot_token_without_skipping_escape(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A lost or
        misassociated UMD token makes every batch walk again; a local token
        must never suppress KMD authentication or a failed-probe retry.
        """
        from test_umd_async_render_completion import function
        fixture = guard.UMD_FIXTURE[:guard.UMD_FIXTURE.index('int main() {')]
        fixture = fixture.replace('  q->Ready=q->Allocation!=bad;',
            '  assert(q->Version==3);\n'
            '  assert(q->Reserved2==(escapes==1 ? 0u : 100u+q->Allocation));\n'
            '  q->Reserved2=100u+q->Allocation;\n'
            '  q->Ready=q->Allocation!=bad;')
        fixture += r"""int main() {
 (void)submit;(void)hold;
 CALLBACKS cbks={cb,signal_cb,escape_cb};ADAPTER a={ADMISSION_UMD_ADAPTER_MAGIC,{9}};
 ADMISSION_UMD_DEVICE d={};d.Adapter=&a;d.KernelCallbacks=&cbks;d.KernelContext=7;
 d.RuntimeDevice.handle=3;d.RenderSyncObject=8;uploads=1;bad=0;
 for(unsigned i=0;i<3;++i){auto &s=d.ScreenBuffers[i];s.Active=s.CopyHeld=true;
  s.KernelAllocation=i+1;s.CanonicalGpuVa=0x10000ull*(i+1);s.Bytes=65536;s.SubmissionHolds=1;}
 assert(native_held_ready(&d,0x10000) && escapes==1);
 assert(!d.ScreenBuffers[0].NativeReadyToken);
 assert(d.ScreenBuffers[1].NativeReadyToken==102 && d.ScreenBuffers[2].NativeReadyToken==103);
 for(unsigned i=0;i<100;++i) assert(native_held_ready(&d,0x10000));
 assert(escapes==101 && queries==202 && !allocations && !touches);
 escape_fail=true;assert(!native_held_ready(&d,0x10000) && escapes==102 && !touches);
}
"""
        funcs = '\n'.join(function(guard.UMD.read_text(), n) for n in ('native_held_ready','submit'))
        guard.NativeAllocationGuard().compile_run(fixture.replace('@@FUNCS@@',funcs),cpp=True)


class NativeReadinessMutation(unittest.TestCase):
    def fixture(self):
        source = guard.KMD.read_text()
        private = (guard.KMD.parent / 'gpuva_g3_private.h').read_text()
        helper = body(private.replace('static inline ', 'static '), 'AdmissionG3NativeInvalidatePtes')
        funcs = '\n'.join(body(source, n) for n in (
            'AdmissionG3CopyPte', 'AdmissionG3NativeInvalidate',
            'AdmissionGpuvaG3NativeQueryEscape', 'AdmissionGpuvaG3NativeQueryBatchEscape'))
        funcs = funcs.replace('  ADMISSION_G3_TABLE_SHADOW *shadow;',
                              '  ++walks;\n  ADMISSION_G3_TABLE_SHADOW *shadow;', 1)
        base = guard.KMD_FIXTURE[:guard.KMD_FIXTURE.index('static APPLE_AGX_GPUVA_G3_NODE root')]
        return base.replace('@@FUNCS@@', 'static unsigned walks;\n' + helper + '\n' + funcs)

    def test_real_graph_leaf_topology_and_reuse_invalidate_before_reproof(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A native leaf
        remap, write downgrade, unlink, root move or table reuse cannot leave
        an authenticated token hot; a different allocation's leaf must not
        force an unchanged allocation through the 4-KiB walk.
        """
        graph_test = (guard.ROOT / 'tests/apple_agx_gpuva_g3_graph_test.c').read_text()
        broker = graph_test[graph_test.index('struct fixture {'):graph_test.index('static unsigned long long update_visits;')]
        broker = broker.replace('static void release(', 'static void free_node(')
        fixture = self.fixture().replace('#include <assert.h>', '#include <assert.h>\n#include <stdlib.h>')
        cases = r'''
int main(void) {
 ADMISSION_G3_PROCESS p={0}; ADMISSION_G3_STATE state={0,&p};
 ADMISSION_CONTEXT a={0}; ADMISSION_RENDER_CONTEXT c={0}; ADMISSION_DEVICE d={0};
 ADMISSION_ALLOCATION_HANDLE allocation={0}; ADMISSION_G3_TABLE_SHADOW shadow={0};
 struct fixture f={0};f.bulk=1;
 APPLE_AGX_GPUVA_V5_CLIENT client;
 APPLE_AGX_GPUVA_V5_IO io={&f,write64,read64,write32,barrier,0};
 const ULONGLONG va=0x10000,ipa=0x20000000,ti=0x10008000;
 APPLE_AGX_GPUVA_G3_LOGICAL_PTE *ptes=calloc(8192,sizeof(*ptes));assert(ptes);
 a.Started=1;a.GpuvaG3State=&state;a.Interface.DxgkCbAcquireHandleData=acquire;
 a.Interface.DxgkCbReleaseHandleData=release;d.Object.Adapter=&a.ObjectAdapter;
 c.Object.Device=&d.Object;c.Win32Transport=1;p.Contexts=&c;
 allocation.Object.Magic=ADMISSION_ALLOCATION_OBJECT_MAGIC;allocation.Object.Description.Size=65536;
 opened.Magic=ADMISSION_OPEN_ALLOCATION_MAGIC;opened.RuntimeAllocation=42;
 opened.Device=&d;opened.Allocation=&allocation.Object;
 assert(AppleAgxGpuvaV5ClientInit(&client,&io));
 assert(AppleAgxGpuvaG3GraphInit(&p.Graph,&client,1,1,allocate,free_node,0));
 p.Graph.InvalidateNative=AdmissionG3NativeInvalidate;p.Graph.InvalidateContext=&p;
 assert(AppleAgxGpuvaG3GraphCreate(&p.Graph,0x10000000,false));
 assert(AppleAgxGpuvaG3GraphRegisterTable(&p.Graph,0x10004000,1));
 assert(AppleAgxGpuvaG3GraphRegisterTable(&p.Graph,ti,2));
 assert(AppleAgxGpuvaG3GraphUpdateParent(&p.Graph,0x10000000,0,0x10004000));
 assert(AppleAgxGpuvaG3GraphUpdateParent(&p.Graph,0x10004000,0,ti));
 shadow.BrokerIpa=ti;shadow.ResidentPtes=ptes;
 p.TableShadowBrokerBuckets[((ti>>14)^(ti>>22)^(ti>>30))&255]=&shadow;
 for(unsigned i=0;i<4;++i)assert(AppleAgxGpuvaG3GraphUpdateLeaf(&p.Graph,ti,4+i,ipa+i*16384,true));
 for(unsigned i=0;i<16;++i) {ptes[16+i].Flags=3;ptes[16+i].Allocation=(ULONG_PTR)&allocation;
  ptes[16+i].AllocationOffset=i*4096;ptes[16+i].GuestIpa=ptes[16+i].NativeIpa=ipa+i*4096;}
 APPLE_AGX_G3_NATIVE_QUERY q={0};q.Magic=APPLE_AGX_G3_NATIVE_QUERY_MAGIC;q.Version=3;
 q.Bytes=sizeof(q);q.Allocation=42;q.GpuVa=va;q.AllocationBytes=65536;q.Write=1;
 DXGKARG_ESCAPE e={&q,sizeof(q),{0},&p,&c,&d};
 #define RUN(ready) do {q.Ready=0;q.Reason=0;q.FailedVa=0;assert(!AdmissionGpuvaG3NativeQueryEscape(&a,&e));assert(q.Ready==(ready));assert(!refs && !locked);} while(0)
 RUN(1);assert(walks==16);ULONGLONG token=q.Reserved2;
 // Unrelated leaf and exact idempotent update preserve the proof.
 assert(AppleAgxGpuvaG3GraphUpdateLeaf(&p.Graph,ti,8,0x30000000,true));
 assert(AppleAgxGpuvaG3GraphUpdateLeaf(&p.Graph,ti,4,ipa,true));
 RUN(1);assert(walks==16 && q.Reserved2==token);
 // Write downgrade: native range validation refuses before logical walk.
 assert(AppleAgxGpuvaG3GraphUpdateLeaf(&p.Graph,ti,5,ipa+16384,false));
 RUN(0);assert(!q.Reserved2);
 q.Reserved2=token;q.Write=0;RUN(1);assert(walks==32 && q.Reserved2!=token);
 q.Write=1;RUN(0);
 assert(AppleAgxGpuvaG3GraphUpdateLeaf(&p.Graph,ti,5,ipa+16384,true));
 RUN(1);token=q.Reserved2;
 // New native IPA, unchanged logical provenance: never authorize stale identity.
 assert(AppleAgxGpuvaG3GraphUpdateLeaf(&p.Graph,ti,5,0x40000000,true));
 RUN(0);assert(q.Reason==APPLE_AGX_G3_NATIVE_QUERY_TRANSLATION && !q.Reserved2);
 assert(AppleAgxGpuvaG3GraphUpdateLeaf(&p.Graph,ti,5,ipa+16384,true));
 RUN(1);assert(q.Reserved2!=token);token=q.Reserved2;
 assert(AppleAgxGpuvaG3GraphUpdateParent(&p.Graph,0x10004000,0,0));
 RUN(0);assert(!q.Reserved2);
 assert(AppleAgxGpuvaG3GraphUpdateParent(&p.Graph,0x10004000,0,ti));
 q.Reserved2=token;walks=0;RUN(1);assert(walks==16);token=q.Reserved2;
 assert(AppleAgxGpuvaG3GraphRegisterTable(&p.Graph,0x1000c000,0));
 assert(AppleAgxGpuvaG3GraphBindRoot(&p.Graph,0x1000c000));RUN(0);
 assert(AppleAgxGpuvaG3GraphBindRoot(&p.Graph,0x10000000));
 q.Reserved2=token;walks=0;RUN(1);assert(walks==16);token=q.Reserved2;
 // Reusing a detached table at another level invalidates topology too.
 assert(AppleAgxGpuvaG3GraphRegisterTable(&p.Graph,0x10010000,2));
 assert(AppleAgxGpuvaG3GraphRegisterTable(&p.Graph,0x10010000,1));
 walks=0;RUN(1);assert(walks==16 && q.Reserved2!=token);
 // v2 batch reports actual hits and a zero-token full proof.
 struct {APPLE_AGX_G3_NATIVE_QUERY_BATCH b;APPLE_AGX_G3_NATIVE_QUERY q[2];} batch={0};
 batch.b.Magic=APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAGIC;batch.b.Version=2;
 batch.b.Bytes=sizeof(batch);batch.b.Count=2;
 q.Ready=0;q.Reason=0;q.FailedVa=0;batch.q[0]=batch.q[1]=q;batch.q[1].Reserved2=0;
 e.pPrivateDriverData=&batch;e.PrivateDriverDataSize=sizeof(batch);walks=0;
 assert(!AdmissionGpuvaG3NativeQueryBatchEscape(&a,&e) && batch.b.Ready);
 assert(batch.b.Reserved[0]==1 && batch.b.Reserved[1]==1 && walks==16);
 assert(AppleAgxGpuvaG3GraphDestroy(&p.Graph));free(ptes);
}
'''
        guard.NativeAllocationGuard().compile_run(fixture + broker + cases, graph=True)


    def test_destroy_identity_invalidates_all_process_caches(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Reuse of a freed
        allocation address must not inherit tokens in any process that opened
        it; unrelated owners must remain cached and keys never dereferenced.
        """
        fixture = self.fixture()
        fixture = fixture.replace('typedef unsigned UINT, ULONG;',
            'typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink,*Blink; } LIST_ENTRY,*PLIST_ENTRY;\n'
            'typedef unsigned UINT, ULONG;')
        fixture = fixture.replace('typedef struct { APPLE_AGX_GPUVA_G3_GRAPH Graph;',
                                  'typedef struct { LIST_ENTRY Link; APPLE_AGX_GPUVA_G3_GRAPH Graph;')
        fixture = fixture.replace('ADMISSION_G3_PROCESS *Process; } ADMISSION_G3_STATE;',
                                  'ADMISSION_G3_PROCESS *Process; LIST_ENTRY Processes; } ADMISSION_G3_STATE;')
        fixture += body(guard.KMD.read_text(),'AdmissionGpuvaG3InvalidateAllocation')
        fixture += r"""
int main(void) {
 (void)AdmissionG3NativeInvalidate;(void)acquire;(void)release;
 ADMISSION_CONTEXT a={0};ADMISSION_G3_STATE state={0};
 ADMISSION_G3_PROCESS p[2]={0};ULONGLONG changed[2],other[2];
 a.GpuvaG3State=&state;state.Processes.Flink=&p[0].Link;
 p[0].Link.Flink=&p[1].Link;p[1].Link.Flink=&state.Processes;
 for(unsigned i=0;i<2;++i){
  changed[i]=AppleAgxG3NativeCacheStore(&p[i].NativeReady,0x10,0x10000,65536,1);
  other[i]=AppleAgxG3NativeCacheStore(&p[i].NativeReady,0x20,0x20000,65536,1);
 }
 AdmissionGpuvaG3InvalidateAllocation(&a,(HANDLE)0x10);
 assert(!locked);
 for(unsigned i=0;i<2;++i){
  assert(!AppleAgxG3NativeCacheHit(&p[i].NativeReady,0x10,0x10000,65536,1,changed[i]));
  assert(AppleAgxG3NativeCacheHit(&p[i].NativeReady,0x20,0x20000,65536,1,other[i]));
  ULONGLONG fresh=AppleAgxG3NativeCacheStore(&p[i].NativeReady,0x10,0x10000,65536,1);
  assert(fresh!=changed[i]);
 }
 a.GpuvaG3State=0;AdmissionGpuvaG3InvalidateAllocation(&a,(HANDLE)0x10);
}
"""
        guard.NativeAllocationGuard().compile_run(fixture,graph=True)
        destroy = body((guard.KMD.parent/'allocation_windows.c').read_text(), 'AdmissionDestroyAllocationImpl')
        self.assertLess(destroy.index('AdmissionGpuvaG3InvalidateAllocation(context, allocation)'),
                        destroy.index('AdmissionAllocationDestroy(&allocation->Object)'))

    def test_cache_collisions_generation_exhaustion_and_access_keys_fail_closed(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Reused tokens,
        access changes and bounded-cache replacement cannot authorize another
        identity; token exhaustion must fall back to proofs rather than wrap.
        """
        source = r'''
#include <assert.h>
#include "apple_agx_g3_native_cache.h"
int main(void) {
 APPLE_AGX_G3_NATIVE_CACHE c={0};unsigned long long k[5],t[5];unsigned n=0;
 for(unsigned long long a=16;n<5;a+=16)
  if(AppleAgxG3NativeCacheBucket(a)==0)k[n++]=a;
 for(unsigned i=0;i<4;i++)t[i]=AppleAgxG3NativeCacheStore(&c,k[i],0x10000,65536,1);
 for(unsigned i=0;i<4;i++)assert(AppleAgxG3NativeCacheHit(&c,k[i],0x10000,65536,1,t[i]));
 assert(!AppleAgxG3NativeCacheHit(&c,k[0],0x10000,65536,0,t[0]));
 assert(!AppleAgxG3NativeCacheHit(&c,k[0],0x10000,65536,3,t[0]));
 assert(!AppleAgxG3NativeCacheHit(&c,k[0],0x20000,65536,1,t[0]));
 assert(!AppleAgxG3NativeCacheHit(&c,k[0],0x10000,4096,1,t[0]));
 t[4]=AppleAgxG3NativeCacheStore(&c,k[4],0x10000,65536,1);
 unsigned hits=0;for(unsigned i=0;i<4;i++)hits+=AppleAgxG3NativeCacheHit(&c,k[i],0x10000,65536,1,t[i]);
 assert(hits==3 && !AppleAgxG3NativeCacheHit(&c,k[0],0x10000,65536,1,t[4]));
 AppleAgxG3NativeCacheInvalidate(&c,k[4]);
 assert(!AppleAgxG3NativeCacheHit(&c,k[4],0x10000,65536,1,t[4]));
 AppleAgxG3NativeCacheInvalidateAll(&c);
 for(unsigned i=0;i<5;i++)assert(!AppleAgxG3NativeCacheHit(&c,k[i],0x10000,65536,1,t[i]));
 c.Clock=~0ULL-1;t[0]=AppleAgxG3NativeCacheStore(&c,k[0],0x10000,65536,1);assert(t[0]==~0ULL);
 assert(AppleAgxG3NativeCacheHit(&c,k[0],0x10000,65536,1,t[0]));
 assert(!AppleAgxG3NativeCacheStore(&c,k[1],0x10000,65536,1));
 AppleAgxG3NativeCacheInvalidateAll(&c);
 assert(!AppleAgxG3NativeCacheHit(&c,k[0],0x10000,65536,1,t[0]));
}
'''
        guard.NativeAllocationGuard().compile_run(source)
        guard.NativeAllocationGuard().compile_run(source.replace('c={0}', 'c={}'), cpp=True)

if __name__ == '__main__':
    unittest.main()
