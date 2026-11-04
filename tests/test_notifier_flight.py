"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A compute build currently raises a live render's shared notifier threshold before
any queue commit. Cancelled builds must not count notifications, or overwrite
another flight's commands, barriers or timestamps. Run the real shared builder.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from test_render_job_depth2_caps_rc98 import SHARED, SHARED_SOURCES

def retire_source():
    """Real retirement code for Windows completion fixtures with minimal WDK shims."""
    source=(SHARED/'src/apple_agx_render_shared_memory.c').read_text()
    return '#include "apple_agx_render_shared_memory.h"\n'+ '\n'.join(
        re.search(r'^(?:static )?void '+name+r'\(.*?^}',source,re.S|re.M)[0]
        for name in ('zero_bytes','AppleAgxRenderFlightRetire'))+'\n'


EXERCISE = r'''
    {
      unsigned char *threshold=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
      unsigned before=read_u32(threshold);
#ifdef APPLE_AGX_NOTIFIER_FLIGHT_COUNT
      owner.NotifierFlight=1;
      assert(AppleAgxRenderFlightBegin(&owner,77));
#endif
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(read_u32(threshold)==before); /* build is firmware-invisible */
#ifdef APPLE_AGX_NOTIFIER_FLIGHT_COUNT
      assert(output.NotifierThreshold==1);
      assert(AppleAgxRenderFlightCancel(&owner,77));
      assert(owner.NotifierComputeCount==0 && read_u32(threshold)==before);
      owner.NotifierFlight=0;
#endif
    }
'''

class NotifierFlight(unittest.TestCase):
    def test_depth_one_compute_bytes_with_caps_on_and_off(self):
        self.run_fixture(DEPTH_ONE)

    def test_failed_restore_and_counter_bounds(self):
        self.run_fixture(RESTORE)

    def test_render_parity_and_all_owned_objects(self):
        self.run_fixture(RENDER, '  arena[layouts[0].ArenaOffset] = 0x5au;')

    def test_two_notifiers_render_relocations(self):
        # N2: the second flight's render work commands and microsequences
        # point at notifier copy 1, and its threshold is its own count.
        self.run_fixture(RENDER_TWO, '  arena[layouts[0].ArenaOffset] = 0x5au;')

    def test_flight_bytes_and_lifetime(self):
        self.run_fixture(FLIGHTS)

    def test_build_does_not_publish(self):
        self.run_fixture(EXERCISE)

    def test_own_notify_defers_successor_threshold(self):
        # N1: a successor published while its head is unreported must not
        # raise the threshold (the head would notify only at the successor's
        # end, U4); the head's retirement writes it, a cancel never does.
        self.run_fixture(OWN_NOTIFY)

    def test_two_notifiers_alternate_with_own_thresholds(self):
        # N2: a successor flight signals the other notifier, whose threshold
        # counts only its jobs, so the head's threshold equals the head's
        # count (firmware notifies on equality, EXP2148); copy 1 is seeded
        # with its own threshold pointer and generation.
        self.run_fixture(TWO_NOTIFIERS)

    def run_fixture(self, exercise, needle=None):
        source=(SHARED/'tests/apple_agx_render_shared_memory_test.c').read_text()
        source=source.replace('int main(void)', 'static unsigned fail_flush;\nstatic APPLE_AGX_BOOL test_flush(void *ctx,const void *p,APPLE_AGX_U32 n) { (void)ctx;(void)p;(void)n;return !fail_flush; }\nint main(void)')
        source=source.replace('  FAKE_MEMORY fake;', '  FAKE_MEMORY fake; (void)test_flush;')
        needle=needle or '    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));'
        source=source.replace(needle,exercise+needle,1)
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'test.c'; c.write_text(source); binary=Path(d)/'test'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-ftrivial-auto-var-init=pattern',
                '-I',str(SHARED/'include'),str(c),
                *[str(SHARED/'src'/n) for n in SHARED_SOURCES],'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

TWO_NOTIFIERS = r'''
    {
      unsigned char *t0=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
      unsigned char *t1=t0-8;
      unsigned char *n0=(unsigned char *)owner.Objects[11].CpuAddress+owner.ObjectOffsets[11];
      unsigned char *n1=n0-0xb0;
      unsigned saved_set=input.JobWordSet, saved_stamp=input.StampValue, before=read_u32(t0);
      APPLE_AGX_RENDER_SHARED_MEMORY_OWNER saved_owner=owner;
      APPLE_AGX_U64 t1va=owner.VirtualAddresses[12]+owner.ObjectOffsets[12]-8;
      owner.TwoNotifiers=1;
      assert(AppleAgxRenderSharedMemoryBindRelocationObjects(&owner,arena,
          APPLE_AGX_EXP208_ARENA_BYTES,objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      assert(objects[11].GpuVa==owner.VirtualAddresses[11]+owner.ObjectOffsets[11]);
      assert(read_u32(t1)==0 && read_u32(n1+8)==read_u32(n0+8)+1u);
      assert(read_u32(n1)==(unsigned)t1va && read_u32(n1+4)==(unsigned)(t1va>>32));
      owner.NotifierFlight=1;
      input.PreemptionGpuAddress=0x1500100000ULL;
      memcpy(t0,&before,4);
      assert(AppleAgxRenderFlightBegin(&owner,77));
      assert(owner.Flights[0].Notifier==0);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(output.NotifierThreshold==1);
      assert(AppleAgxRenderFlightPublish(&owner,77));
      AppleAgxRenderFlightVisible(&owner,77);
      assert(read_u32(t0)==1 && read_u32(t1)==0);
      assert(AppleAgxRenderFlightBegin(&owner,78));
      assert(owner.Flights[1].Notifier==1);
      input.StampValue++;
      input.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,input.StampValue,1);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(output.NotifierThreshold==1); /* its own notifier's count */
      assert(AppleAgxRenderFlightPublish(&owner,78));
      assert(read_u32(t0)==1 && read_u32(t1)==1); /* head's threshold untouched */
      assert(AppleAgxRenderFlightCancel(&owner,78));
      assert(read_u32(t1)==0 && owner.NotifierTotal[1]==0 && owner.NotifierTotal[0]==1);
      assert(AppleAgxRenderFlightBegin(&owner,78));
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(AppleAgxRenderFlightPublish(&owner,78));
      AppleAgxRenderFlightVisible(&owner,78);
      AppleAgxRenderFlightRetire(&owner,77);
      assert(AppleAgxRenderFlightBegin(&owner,79)); /* 78 live on 1 -> 0 */
      assert(owner.Flights[0].Notifier==0);
      input.StampValue++;
      input.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,input.StampValue,1);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(AppleAgxRenderFlightPublish(&owner,79));
      assert(read_u32(t0)==2 && read_u32(t1)==1);
      assert(owner.NotifierUses[0]==2 && owner.NotifierUses[1]==2);
      owner=saved_owner; memcpy(t0,&before,4);
      input.JobWordSet=saved_set; input.StampValue=saved_stamp; input.PreemptionGpuAddress=0;
    }
'''

OWN_NOTIFY = r'''
    {
      unsigned char *threshold=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
      unsigned saved_set=input.JobWordSet, saved_stamp=input.StampValue;
      unsigned before=read_u32(threshold);
      APPLE_AGX_RENDER_SHARED_MEMORY_OWNER saved_owner=owner;
      owner.NotifierFlight=1; owner.OwnNotify=1;
      input.PreemptionGpuAddress=0x1500100000ULL;
      owner.NotifierFlush=test_flush; owner.NotifierFlushContext=0;
      assert(AppleAgxRenderFlightBegin(&owner,77));
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(AppleAgxRenderFlightPublish(&owner,77));
      assert(read_u32(threshold)==1 && owner.OwnNotifyDeferred==0);
      AppleAgxRenderFlightVisible(&owner,77);
      assert(AppleAgxRenderFlightBegin(&owner,78));
      input.StampValue++;
      input.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,input.StampValue,1);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(output.NotifierThreshold==2);
      assert(AppleAgxRenderFlightPublish(&owner,78));
      assert(read_u32(threshold)==1 && owner.OwnNotifyDeferred==1); /* deferred */
      assert(AppleAgxRenderFlightCancel(&owner,78));
      assert(read_u32(threshold)==1 && owner.NotifierComputeCount==1);
      assert(AppleAgxRenderFlightBegin(&owner,78));
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(AppleAgxRenderFlightPublish(&owner,78));
      AppleAgxRenderFlightVisible(&owner,78);
      assert(read_u32(threshold)==1);
      AppleAgxRenderFlightRetire(&owner,999); /* wrong fence releases nothing */
      assert(read_u32(threshold)==1 && owner.OwnNotifyReleased==0);
      fail_flush=1;
      AppleAgxRenderFlightRetire(&owner,77);  /* head reported: successor's own count */
      fail_flush=0;
      assert(read_u32(threshold)==2 && owner.OwnNotifyReleased==1);
      assert(owner.OwnNotifyFlushFailures==1 && !owner.Flights[1].Deferred);
      AppleAgxRenderFlightRetire(&owner,78);
      assert(read_u32(threshold)==2 && owner.OwnNotifyReleased==1);
      /* a successor published after its head retired writes at once */
      assert(AppleAgxRenderFlightBegin(&owner,79));
      input.StampValue++;
      input.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,input.StampValue,1);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(AppleAgxRenderFlightPublish(&owner,79));
      assert(read_u32(threshold)==3 && owner.OwnNotifyDeferred==2); /* incl. the cancelled one */
      AppleAgxRenderFlightVisible(&owner,79);
      AppleAgxRenderFlightRetire(&owner,79);
      owner=saved_owner; /* counts, sets and flights as before the block */
      memcpy(threshold,&before,4);
      input.JobWordSet=saved_set; input.StampValue=saved_stamp;
      input.PreemptionGpuAddress=0;
    }
'''

FLIGHTS = r'''
    {
      unsigned char *threshold=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
      unsigned before=read_u32(threshold), i;
      unsigned char saved[0x10000];
      APPLE_AGX_RENDER_BARRIER_INPUT barrier={0};
      APPLE_AGX_U64 va;
      input.PreemptionGpuAddress=0x1500100000ULL;
      input.Timestamps=1;
      owner.NotifierFlight=1;
      assert(AppleAgxRenderFlightBegin(&owner,77));
      assert(!AppleAgxRenderFlightBegin(&owner,78));
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(read_u32(threshold)==before && output.JobWordSet==0);
      assert(AppleAgxRenderFlightPublish(&owner,77));
      assert(read_u32(threshold)==1);
      AppleAgxRenderFlightVisible(&owner,77);
      memcpy(saved,owner.Objects[39].CpuAddress,sizeof(saved));
      assert(!AppleAgxRenderFlightCancel(&owner,77));
      assert(!AppleAgxRenderSharedMemoryInitializeComputeQueue(&owner));
      assert(!AppleAgxRenderSharedMemoryBindRelocationObjects(&owner,arena,
          APPLE_AGX_EXP208_ARENA_BYTES,objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      assert(AppleAgxRenderSharedMemoryDestroy(&owner)==AppleAgxRenderSharedMemoryResultReleaseFailed);
      assert(AppleAgxRenderFlightBegin(&owner,78));
      input.StampValue++;
      assert(!AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output)); /* A pinned */
      input.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,input.StampValue,1);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(output.JobWordSet==1 && output.NotifierThreshold==2);
      assert(read_u32(threshold)==1);
      barrier.WaitStampGpuAddress=AppleAgxRenderSharedMemoryFirmwareStampAddress(&owner,1);
      barrier.WaitValue=0x100; barrier.StampSelf=0x200; barrier.JobWordSet=1;
      assert(AppleAgxRenderSharedMemoryBuildLeadingBarrier(&owner,&barrier,1,&va));
      for(i=0;i<sizeof(saved);++i)
        if(i<0x9420 || i>=0xc000)
          assert(saved[i]==((unsigned char *)owner.Objects[39].CpuAddress)[i]);
      assert(AppleAgxRenderFlightPublish(&owner,78));
      assert(read_u32(threshold)==2);
      assert(AppleAgxRenderFlightCancel(&owner,78));
      assert(read_u32(threshold)==1 && owner.NotifierComputeCount==1);
      assert(AppleAgxRenderFlightBegin(&owner,78));
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      assert(output.NotifierThreshold==2); /* failed attempt did not count */
      assert(AppleAgxRenderFlightPublish(&owner,78));
      AppleAgxRenderFlightVisible(&owner,78);
      assert(!AppleAgxRenderFlightBegin(&owner,79));
      AppleAgxRenderFlightRetire(&owner,999); /* wrong fence frees nothing */
      assert(!AppleAgxRenderFlightBegin(&owner,79));
      AppleAgxRenderFlightRetire(&owner,77);
      assert(AppleAgxRenderFlightBegin(&owner,79));
      assert(owner.BuildingFlight==1);
      assert(AppleAgxRenderFlightCancel(&owner,79));
      AppleAgxRenderFlightRetire(&owner,77); /* duplicate cannot free 78 */
      assert(owner.Flights[1].Fence==78);
      AppleAgxRenderFlightStopped(&owner); /* caller proved synchronous stop */
      assert(!owner.Flights[0].Fence && !owner.Flights[1].Fence);
      for(i=0;i<owner.ObjectCount;++i) {
        owner.Objects[i].State=AppleAgxMemoryPrepared;
        owner.Objects[i].GpuVirtualAddress=0;
      }
      assert(AppleAgxRenderSharedMemoryInitializeComputeQueue(&owner));
      for(i=0;i<owner.ObjectCount;++i) {
        owner.Objects[i].State=AppleAgxMemoryGpuMapped;
        owner.Objects[i].GpuVirtualAddress=owner.VirtualAddresses[i];
      }
      owner.NotifierFlight=0;
      assert(AppleAgxRenderSharedMemoryBindRelocationObjects(&owner,arena,
          APPLE_AGX_EXP208_ARENA_BYTES,objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
      input.StampValue=9; input.Timestamps=0; input.PreemptionGpuAddress=0; input.JobWordSet=0;
    }
'''

RENDER = r'''
  {
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER old=owner;
    APPLE_AGX_BACKEND_JOB_IMAGE old_job=staged_job;
    APPLE_AGX_EXP208_RELOCATION_OBJECT first[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
    unsigned char *backup[40], *live[40], *legacy[40];
    unsigned char *threshold=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
    unsigned i, before=read_u32(threshold);
    for(i=0;i<40;++i) {
      backup[i]=malloc(owner.Objects[i].Length); live[i]=NULL;
      assert(backup[i]); memcpy(backup[i],owner.Objects[i].CpuAddress,owner.Objects[i].Length);
    }
#define RBUILD(init) AppleAgxRenderSharedMemoryBuildActiveJob(&owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,init,&bindings,&staged_job,active_objects,&active_job)
    assert(RBUILD(1));
    for(i=0;i<40;++i) {
      legacy[i]=malloc(owner.Objects[i].Length);assert(legacy[i]);
      memcpy(legacy[i],owner.Objects[i].CpuAddress,owner.Objects[i].Length);
      memcpy(owner.Objects[i].CpuAddress,backup[i],owner.Objects[i].Length);
    }
    owner=old;
    owner.NotifierFlight=1;
    assert(AppleAgxRenderFlightBegin(&owner,80));
    assert(RBUILD(1));
    assert(read_u32(threshold)==before);
    assert(active_objects[12].Data!=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12]);
    assert(AppleAgxRenderFlightPublish(&owner,80));
    for(i=0;i<40;++i) { /* at depth one every firmware byte matches */
      assert(memcmp(legacy[i],owner.Objects[i].CpuAddress,owner.Objects[i].Length)==0);
      free(legacy[i]);
    }
    AppleAgxRenderFlightVisible(&owner,80);
    memcpy(first,active_objects,sizeof(first));
    for(i=13;i<=35;++i) if(i<=19 || i>=28) {
      live[i]=malloc(first[i].Size);assert(live[i]);memcpy(live[i],first[i].Data,first[i].Size);
    }
    assert(AppleAgxRenderFlightBegin(&owner,81));
    assert(!RBUILD(1)); /* no queue init over live bytes */
    assert(!RBUILD(0)); /* same render parity is still held */
    staged_job.TaExpectedStamp+=0x100; staged_job.D3ExpectedStamp+=0x100;
    assert(RBUILD(0));
    for(i=13;i<=35;++i) if(live[i]) {
      assert(first[i].Data!=active_objects[i].Data);
      assert(memcmp(first[i].Data,live[i],first[i].Size)==0);
    }
    assert(first[12].Data!=active_objects[12].Data);
    assert(AppleAgxRenderFlightCancel(&owner,81));
    for(i=13;i<=35;++i) if(live[i]) {
      assert(memcmp(first[i].Data,live[i],first[i].Size)==0);free(live[i]);
    }
    AppleAgxRenderFlightRetire(&owner,80);
    assert(AppleAgxRenderFlightBegin(&owner,82));
    staged_job=old_job; assert(RBUILD(0)); /* exact completion permits reuse */
    assert(AppleAgxRenderFlightCancel(&owner,82));
    for(i=0;i<40;++i) { memcpy(owner.Objects[i].CpuAddress,backup[i],owner.Objects[i].Length);free(backup[i]); }
    owner=old; staged_job=old_job;
#undef RBUILD
  }
'''

RENDER_TWO = r'''
  {
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER old=owner;
    APPLE_AGX_BACKEND_JOB_IMAGE old_job=staged_job;
    unsigned char *backup[40];
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *lay=AppleAgxRenderTemplateObjectLayouts();
    unsigned char *t0=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
    APPLE_AGX_U64 n0=owner.VirtualAddresses[11]+owner.ObjectOffsets[11], n1=n0-0xb0;
    unsigned i, ec, saved_ec=read_u32(arena+lay[12].ArenaOffset);
    for(i=0;i<40;++i) {
      backup[i]=malloc(owner.Objects[i].Length);
      assert(backup[i]); memcpy(backup[i],owner.Objects[i].CpuAddress,owner.Objects[i].Length);
    }
#define RBUILD(init) AppleAgxRenderSharedMemoryBuildActiveJob(&owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,init,&bindings,&staged_job,active_objects,&active_job)
#define U64AT(o,off) ((APPLE_AGX_U64)read_u32(active_objects[o].Data+(off)) | ((APPLE_AGX_U64)read_u32(active_objects[o].Data+(off)+4)<<32))
    owner.NotifierFlight=1; owner.TwoNotifiers=1;
    assert(AppleAgxRenderFlightBegin(&owner,80));
    assert(RBUILD(1));
    ec=read_u32(arena+lay[12].ArenaOffset);
    assert(active_objects[11].GpuVa==n0 && U64AT(18,32)==n0 && U64AT(19,20)==n0);
    assert(U64AT(15,420)==n0+168 && U64AT(17,384)==n0+168);
    assert(AppleAgxRenderFlightPublish(&owner,80));
    assert(read_u32(t0)==ec && owner.NotifierTotal[0]==ec);
    AppleAgxRenderFlightVisible(&owner,80);
    assert(AppleAgxRenderFlightBegin(&owner,81) && owner.Flights[1].Notifier==1);
    staged_job.TaExpectedStamp+=0x100; staged_job.D3ExpectedStamp+=0x100;
    { unsigned v=ec+2; memcpy(arena+lay[12].ArenaOffset,&v,4); }
    assert(RBUILD(0));
    assert(active_objects[11].GpuVa==n1 && U64AT(18,32)==n1 && U64AT(19,20)==n1);
    assert(U64AT(15,420)==n1+168 && U64AT(17,384)==n1+168);
    assert(AppleAgxRenderFlightPublish(&owner,81));
    assert(read_u32(t0)==ec && read_u32(t0-8)==2 && owner.NotifierTotal[1]==2);
    memcpy(arena+lay[12].ArenaOffset,&saved_ec,4);
    for(i=0;i<40;++i) { memcpy(owner.Objects[i].CpuAddress,backup[i],owner.Objects[i].Length);free(backup[i]); }
    owner=old; staged_job=old_job;
#undef RBUILD
#undef U64AT
  }
'''

RESTORE = r'''
    {
      unsigned char *threshold=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
      unsigned before=read_u32(threshold);
      owner.NotifierFlight=1;owner.NotifierFlush=test_flush;
      assert(AppleAgxRenderFlightBegin(&owner,77));
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      fail_flush=1;
      assert(!AppleAgxRenderFlightPublish(&owner,77));
      assert(!AppleAgxRenderFlightCancel(&owner,77));
      assert(owner.BuildingFlight && !AppleAgxRenderFlightBegin(&owner,78));
      assert(!AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      fail_flush=0;
      assert(AppleAgxRenderFlightCancel(&owner,77));
      assert(read_u32(threshold)==before && owner.NotifierComputeCount==0);
      owner.NotifierComputeCount=~0u;
      assert(AppleAgxRenderFlightBegin(&owner,78));
      assert(!AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
      owner.Flights[0].EventCount=0;
      assert(!AppleAgxRenderFlightPublish(&owner,78)); /* no wrapping threshold */
      assert(read_u32(threshold)==before);
      assert(AppleAgxRenderFlightCancel(&owner,78));
      owner.NotifierComputeCount=0;owner.NotifierFlight=0;owner.NotifierFlush=NULL;
    }
'''

DEPTH_ONE = r'''
    {
      APPLE_AGX_RENDER_SHARED_MEMORY_OWNER initial=owner;
      APPLE_AGX_RENDER_COMPUTE_INPUT saved_input=input;
      unsigned char original[0x10000], before_bytes[0x10000], expected[0x10000];
      unsigned char *threshold=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
      unsigned original_count=read_u32(threshold),cap,n;
      memcpy(original,owner.Objects[39].CpuAddress,sizeof(original));
      input.PreemptionGpuAddress=0x1500100000ULL;input.Timestamps=1;
      for(cap=0;cap<2;++cap) for(n=0;n<4;++n) {
        APPLE_AGX_RENDER_SHARED_MEMORY_OWNER prior=owner;
        unsigned before=read_u32(threshold),want_set,want_count;
        memcpy(before_bytes,owner.Objects[39].CpuAddress,sizeof(before_bytes));
        input.StampValue=100+cap*4+n;
        input.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,input.StampValue,cap);
        assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
        want_set=output.JobWordSet;want_count=read_u32(threshold);
        memcpy(expected,owner.Objects[39].CpuAddress,sizeof(expected));
        owner=prior;memcpy(owner.Objects[39].CpuAddress,before_bytes,sizeof(before_bytes));
        memcpy(threshold,&before,4);
        owner.NotifierFlight=1;
        assert(AppleAgxRenderFlightBegin(&owner,input.StampValue));
        input.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,input.StampValue,cap);
        assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&input,&output));
        assert(output.JobWordSet==want_set);
        assert(read_u32(threshold)==before);
        assert(AppleAgxRenderFlightPublish(&owner,input.StampValue));
        assert(read_u32(threshold)==want_count);
        assert(memcmp(expected,owner.Objects[39].CpuAddress,sizeof(expected))==0);
        AppleAgxRenderFlightVisible(&owner,input.StampValue);
        AppleAgxRenderFlightRetire(&owner,input.StampValue);
        owner.NotifierFlight=0;
      }
      owner=initial;input=saved_input;
      memcpy(owner.Objects[39].CpuAddress,original,sizeof(original));
      memcpy(threshold,&original_count,4);
    }
'''
