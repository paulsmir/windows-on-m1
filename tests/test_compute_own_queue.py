"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Q2c H0 (Wom1ComputeOwnQueue): the KMD-synthesized compute work queue gets its
own Asahi Queue identity (NotifierList, Notifier + Threshold, GpuContextData;
Asahi queue/mod.rs Queue::new). Catches: (1) any firmware byte that changes
with the switch off (golden hashes captured on f4512ee84), (2) B objects that
differ from Asahi's initial values, (3) a compute QueueInfo still pointing at
the render Queue's notifier list or GPU context, (4) a compute build that
touches notifier 11 / threshold 12, (5) render notifications counted into the
compute threshold or vice versa (a lost or deferred firmware notification),
(6) a cancelled flight that leaves either threshold raised, (7) a switch read
after InitializeComputeQueue (B never initialised). Runs the real builder.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from test_render_job_depth2_caps_rc98 import SHARED, SHARED_SOURCES

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'

# Captured with the switch-off scenario below on f4512ee84 (before Q2c).
GOLDEN = {
    'init': '21dcaebdc5b2acd0',
    'f80': '699c2455a2f2107e',
    'f81pub': '843e5e248ca4e2c2',
    'f81cancel': 'a44c73a568da8995',
    'f81': '843e5e248ca4e2c2',
    'f82': '73327594b16ac8c3',
    'f83': 'e9cf65b79560d205',
    'direct': 'bd1ee5770402f927',
}

HELPERS = r'''
#include <stdio.h>
static unsigned q2c_fail_flush;
static APPLE_AGX_BOOL q2c_flush(void *c,const void *p,APPLE_AGX_U32 n) {
  (void)c;(void)p;(void)n; return !q2c_fail_flush; }
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
static APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *q2c_owner;
/* fails the flush of the compute Queue's threshold word only */
static APPLE_AGX_BOOL q2c_flush_b_fails(void *c,const void *p,APPLE_AGX_U32 n) {
  (void)c;(void)n;
  return p!=(const unsigned char *)q2c_owner->Objects[39].CpuAddress+
      APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET;
}
#endif
static unsigned long long q2c_hash(const APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o) {
  unsigned long long h=1469598103934665603ULL, k; unsigned i;
  for(i=0;i<APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;++i)
    for(k=0;k<o->Objects[i].Length;++k) {
      h^=((const unsigned char *)o->Objects[i].CpuAddress)[k]; h*=1099511628211ULL; }
  return h;
}
static void q2c_states(APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o,int prepared) {
  unsigned i;
  for(i=0;i<o->ObjectCount;++i) {
    o->Objects[i].State=prepared ? AppleAgxMemoryPrepared : AppleAgxMemoryGpuMapped;
    o->Objects[i].GpuVirtualAddress=prepared ? 0 : o->VirtualAddresses[i];
  }
}
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
#define Q2C_OWN_MAX 1u
#define Q2C_SET(v) (owner.ComputeOwnQueue=(v))
#else
#define Q2C_OWN_MAX 0u
#define Q2C_SET(v) ((void)(v))
#endif
'''

SCENARIO = r'''
  {
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER q_saved=owner;
    APPLE_AGX_BACKEND_JOB_IMAGE q_job=staged_job;
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *q_lay=AppleAgxRenderTemplateObjectLayouts();
    unsigned q_ec=read_u32(arena+q_lay[12].ArenaOffset), q_i, q_own;
    unsigned char *q_backup[40], *q_init[40];
#define RBUILD(init) AppleAgxRenderSharedMemoryBuildActiveJob(&owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,init,&bindings,&staged_job,active_objects,&active_job)
#define SET_EC(v) do { unsigned q_v=(v); memcpy(arena+q_lay[12].ArenaOffset,&q_v,4); } while(0)
#define MARK(name) do { if(!q_own) printf("Q2C %s %016llx\n",name,q2c_hash(&owner)); } while(0)
    for(q_i=0;q_i<40;++q_i) {
      q_backup[q_i]=malloc(owner.Objects[q_i].Length); q_init[q_i]=malloc(owner.Objects[q_i].Length);
      assert(q_backup[q_i] && q_init[q_i]);
      memcpy(q_backup[q_i],owner.Objects[q_i].CpuAddress,owner.Objects[q_i].Length);
    }
    for(q_own=0;q_own<=Q2C_OWN_MAX;++q_own) {
      APPLE_AGX_RENDER_COMPUTE_INPUT ci; APPLE_AGX_RENDER_COMPUTE_OUTPUT co;
      unsigned char *t_a=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
      unsigned char *sc=(unsigned char *)owner.Objects[39].CpuAddress;
      unsigned char *n11=(unsigned char *)owner.Objects[11].CpuAddress+owner.ObjectOffsets[11];
      unsigned char *info=(unsigned char *)owner.Objects[36].CpuAddress;
      unsigned char snap11[0xb0];
      (void)sc;(void)n11;(void)info;(void)snap11;
      for(q_i=0;q_i<40;++q_i)
        memcpy(owner.Objects[q_i].CpuAddress,q_backup[q_i],owner.Objects[q_i].Length);
      owner=q_saved; staged_job=q_job; SET_EC(q_ec);
      memset(&ci,0,sizeof(ci)); memset(&co,0,sizeof(co));
      ci.CdmStreamBase=0x1500400000ULL;ci.CdmStreamBytes=72u;ci.Counter=7u;
      ci.UscExecutionBase=0x1100000000ULL;ci.VmSlot=2u;ci.EventNumber=2u;
      ci.StampValue=200u;ci.EventSequence=11u;ci.ClientSequence=5u;
      ci.PreemptionGpuAddress=0x1500100000ULL;
      Q2C_SET(q_own);
      owner.NotifierFlight=1; owner.NotifierFlush=q2c_flush; owner.NotifierFlushContext=0;
      q2c_states(&owner,1);
      assert(AppleAgxRenderSharedMemoryInitializeComputeQueue(&owner));
      q2c_states(&owner,0);
      MARK("init");
      if(!q_own)
        for(q_i=0;q_i<40;++q_i)
          memcpy(q_init[q_i],owner.Objects[q_i].CpuAddress,owner.Objects[q_i].Length);
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      if(q_own) {
        static const unsigned char ctx_default[APPLE_AGX_GPU_CONTEXT_DATA_BYTES]={
            [0x00]=0xff,[0x01]=0xff,[0x05]=1,[0x1e]=0xff,[0x23]=2};
        APPLE_AGX_U64 sva=owner.VirtualAddresses[39];
        unsigned char *L=sc+APPLE_AGX_COMPUTE_OWN_LIST_OFFSET;
        unsigned char *T=sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET;
        unsigned char *C=sc+APPLE_AGX_COMPUTE_OWN_CONTEXT_OFFSET;
        unsigned char *N=sc+APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET;
        APPLE_AGX_U64 k;
        /* NotifierList: list_head.prev 0, .next = &list_head, unkptr_10 0 */
        assert(read_u64(L)==0 && read_u64(L+8)==sva+APPLE_AGX_COMPUTE_OWN_LIST_OFFSET &&
               read_u64(L+16)==0);
        /* the template's JobList (object 23) has the same shape */
        assert(read_u64(arena+q_lay[23].ArenaOffset)==0 &&
               read_u64(arena+q_lay[23].ArenaOffset+16)==0);
        assert(read_u64(T)==0); /* Threshold (u64) */
        assert(memcmp(C,ctx_default,sizeof(ctx_default))==0);
        assert(memcmp(C,arena+q_lay[0].ArenaOffset,APPLE_AGX_GPU_CONTEXT_DATA_BYTES)==0);
        /* Notifier {threshold, generation = queue id, cur_count 0, unk_10
         * 0x50, state default, unk_buf all-0xff}: the template notifier with
         * its own threshold pointer and generation + 1. */
        assert(read_u64(N)==sva+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET);
        assert(read_u32(N+8)==read_u32(n11+8)+APPLE_AGX_COMPUTE_OWN_GENERATION_DELTA);
        assert(read_u32(N+0xc)==0 && read_u32(N+0x10)==0x50u);
        for(k=0x14;k<0xa8;++k) assert(N[k]==0);
        for(k=0xa8;k<0xb0;++k) assert(N[k]==0xff);
        assert(memcmp(N+0xc,arena+q_lay[11].ArenaOffset+0xc,APPLE_AGX_G13_NOTIFIER_BYTES-0xc)==0);
        /* QueueInfo: notifier_list (+0x10) and gpu_context (+0xa4) */
        assert(read_u64(info+APPLE_AGX_QUEUE_INFO_NOTIFIER_LIST_OFFSET)==
               sva+APPLE_AGX_COMPUTE_OWN_LIST_OFFSET);
        assert(read_u64(info+APPLE_AGX_QUEUE_INFO_GPU_CONTEXT_OFFSET)==
               sva+APPLE_AGX_COMPUTE_OWN_CONTEXT_OFFSET);
        /* every other byte as with the switch off */
        for(q_i=0;q_i<40;++q_i) {
          const unsigned char *now=owner.Objects[q_i].CpuAddress;
          for(k=0;k<owner.Objects[q_i].Length;++k) {
            if(q_i==36 && ((k>=0x10 && k<0x18) || (k>=0xa4 && k<0xac))) continue;
            if(q_i==39 && k>=APPLE_AGX_COMPUTE_OWN_FIRST_OFFSET &&
               k<APPLE_AGX_COMPUTE_OWN_END_OFFSET) continue;
            assert(now[k]==q_init[q_i][k]);
          }
        }
      }
#endif
      /* flight 80: a render and its compute (the prelude / MIXED_BATCH shape) */
      assert(AppleAgxRenderFlightBegin(&owner,80));
      assert(RBUILD(1));
      ci.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,ci.StampValue,1);
      memcpy(snap11,n11,sizeof(snap11));
      {
        unsigned a_before=read_u32(t_a);
        assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
        assert(read_u32(t_a)==a_before); /* flight staging: nothing published */
      }
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      if(q_own) {
        APPLE_AGX_U64 sva=owner.VirtualAddresses[39];
        unsigned w=APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(co.JobWordSet,APPLE_AGX_COMPUTE_WORK_OFFSET);
        unsigned m=APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(co.JobWordSet,APPLE_AGX_COMPUTE_MICROSEQUENCE_OFFSET);
        assert(co.NotifierThreshold==1); /* B counts compute only */
        assert(memcmp(snap11,n11,sizeof(snap11))==0);
        assert(read_u32(sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET)==0);
        assert(read_u64(sc+w+0x14)==sva+APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET);
        assert(read_u32(sc+m+0x2c)==read_u32(n11+8)+APPLE_AGX_COMPUTE_OWN_GENERATION_DELTA);
        assert(read_u64(sc+m+0x164)==sva+APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET+
               APPLE_AGX_G13_NOTIFIER_BUFFER_OFFSET);
        assert(owner.Flights[0].HasCompute && owner.Flights[0].HasRender);
      }
#endif
      if(!q_own) assert(co.NotifierThreshold==q_ec+1);
      assert(AppleAgxRenderFlightPublish(&owner,80));
      AppleAgxRenderFlightVisible(&owner,80);
      MARK("f80");
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      if(q_own) assert(read_u32(t_a)==q_ec &&
                       read_u32(sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET)==1);
#endif
      if(!q_own) assert(read_u32(t_a)==q_ec+1);
      /* flight 81: compute only, published while 80 is live */
      assert(AppleAgxRenderFlightBegin(&owner,81));
      ci.StampValue=201u;
      ci.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,ci.StampValue,1);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
      assert(AppleAgxRenderFlightPublish(&owner,81));
      MARK("f81pub");
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      if(q_own) assert(read_u32(t_a)==q_ec && co.NotifierThreshold==2 &&
                       read_u32(sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET)==2);
#endif
      if(!q_own) assert(read_u32(t_a)==q_ec+2);
      assert(AppleAgxRenderFlightCancel(&owner,81));
      MARK("f81cancel");
      assert(owner.NotifierComputeCount==1);
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      if(q_own) assert(read_u32(t_a)==q_ec &&
                       read_u32(sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET)==1);
#endif
      if(!q_own) assert(read_u32(t_a)==q_ec+1);
      assert(AppleAgxRenderFlightBegin(&owner,81));
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
      assert(AppleAgxRenderFlightPublish(&owner,81));
      AppleAgxRenderFlightVisible(&owner,81);
      AppleAgxRenderFlightRetire(&owner,80);
      AppleAgxRenderFlightRetire(&owner,81);
      MARK("f81");
      /* flight 82: a steady-state render only */
      staged_job.TaExpectedStamp+=0x100; staged_job.D3ExpectedStamp+=0x100;
      SET_EC(q_ec+2);
      assert(AppleAgxRenderFlightBegin(&owner,82));
      assert(RBUILD(0));
      assert(AppleAgxRenderFlightPublish(&owner,82));
      AppleAgxRenderFlightVisible(&owner,82);
      AppleAgxRenderFlightRetire(&owner,82);
      MARK("f82");
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      if(q_own) assert(read_u32(t_a)==q_ec+2 && /* A counts renders only */
                       read_u32(sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET)==2);
#endif
      if(!q_own) assert(read_u32(t_a)==q_ec+2+2);
      /* flight 83: a render queue reinitialisation with its compute */
      staged_job.TaExpectedStamp+=0x100; staged_job.D3ExpectedStamp+=0x100;
      SET_EC(q_ec);
      assert(AppleAgxRenderFlightBegin(&owner,83));
      assert(RBUILD(1));
      ci.StampValue=202u;
      ci.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,ci.StampValue,1);
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
      assert(AppleAgxRenderFlightPublish(&owner,83));
      AppleAgxRenderFlightVisible(&owner,83);
      AppleAgxRenderFlightRetire(&owner,83);
      MARK("f83");
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      /* the render epoch re-seeds A only; the compute queue keeps counting */
      if(q_own) assert(read_u32(t_a)==q_ec && owner.NotifierComputeCount==3 &&
                       read_u32(sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET)==3);
#endif
      if(!q_own) assert(read_u32(t_a)==q_ec+1);
      /* the direct (NotifierFlight = 0) path */
      owner.NotifierFlight=0;
      ci.StampValue=203u; ci.JobWordSet=0;
      assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
      staged_job.TaExpectedStamp+=0x100; staged_job.D3ExpectedStamp+=0x100;
      SET_EC(q_ec+2);
      assert(RBUILD(0));
      MARK("direct");
#ifdef APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET
      if(q_own) assert(read_u32(t_a)==q_ec+2 &&
                       read_u32(sc+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET)==4);
#endif
      if(!q_own) assert(read_u32(t_a)==q_ec+2+2);
    }
    for(q_i=0;q_i<40;++q_i) {
      memcpy(owner.Objects[q_i].CpuAddress,q_backup[q_i],owner.Objects[q_i].Length);
      free(q_backup[q_i]); free(q_init[q_i]);
    }
    owner=q_saved; staged_job=q_job; SET_EC(q_ec);
#undef RBUILD
#undef SET_EC
#undef MARK
  }
'''

# Own-queue only: N1 deferral per queue, refusal of N2, flush failure and
# cancel restoring both thresholds, compute queue lifetime.
OWN_ONLY = r'''
  {
    APPLE_AGX_RENDER_SHARED_MEMORY_OWNER q_saved=owner;
    APPLE_AGX_BACKEND_JOB_IMAGE q_job=staged_job;
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *q_lay=AppleAgxRenderTemplateObjectLayouts();
    unsigned q_ec=read_u32(arena+q_lay[12].ArenaOffset), q_i;
    unsigned char *q_backup[40];
    unsigned char *t_a=(unsigned char *)owner.Objects[12].CpuAddress+owner.ObjectOffsets[12];
    unsigned char *t_b=(unsigned char *)owner.Objects[39].CpuAddress+APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET;
    APPLE_AGX_RENDER_COMPUTE_INPUT ci; APPLE_AGX_RENDER_COMPUTE_OUTPUT co;
#define RBUILD(init) AppleAgxRenderSharedMemoryBuildActiveJob(&owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,init,&bindings,&staged_job,active_objects,&active_job)
    for(q_i=0;q_i<40;++q_i) {
      q_backup[q_i]=malloc(owner.Objects[q_i].Length); assert(q_backup[q_i]);
      memcpy(q_backup[q_i],owner.Objects[q_i].CpuAddress,owner.Objects[q_i].Length);
    }
    memset(&ci,0,sizeof(ci)); memset(&co,0,sizeof(co));
    ci.CdmStreamBase=0x1500400000ULL;ci.CdmStreamBytes=72u;ci.Counter=7u;
    ci.UscExecutionBase=0x1100000000ULL;ci.VmSlot=2u;ci.EventNumber=2u;
    ci.StampValue=300u;ci.EventSequence=11u;ci.ClientSequence=5u;
    ci.PreemptionGpuAddress=0x1500100000ULL;
    q2c_owner=&owner;
    owner.ComputeOwnQueue=1; owner.NotifierFlight=1; owner.OwnNotify=1;
    owner.NotifierFlush=q2c_flush; owner.NotifierFlushContext=0;
    owner.NotifierComputeCount=5; owner.NotifierComputeStamp=99;
    q2c_states(&owner,1);
    assert(AppleAgxRenderSharedMemoryInitializeComputeQueue(&owner));
    q2c_states(&owner,0);
    /* a new compute queue lifetime starts at zero notifications */
    assert(owner.NotifierComputeCount==0 && owner.NotifierComputeStamp==0);
    /* N2 shares the render queue's notifier list: never with Q2c */
    owner.TwoNotifiers=1;
    assert(!AppleAgxRenderFlightBegin(&owner,90));
    owner.TwoNotifiers=0;
    /* head 90: render only (A) */
    assert(AppleAgxRenderFlightBegin(&owner,90));
    assert(RBUILD(1));
    assert(AppleAgxRenderFlightPublish(&owner,90));
    AppleAgxRenderFlightVisible(&owner,90);
    assert(read_u32(t_a)==q_ec && read_u32(t_b)==0);
    /* successor 91: compute only (B). N1 does not defer it: no live
     * flight on B, so its notification is its own at once. */
    assert(AppleAgxRenderFlightBegin(&owner,91));
    ci.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,ci.StampValue,1);
    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
    assert(AppleAgxRenderFlightPublish(&owner,91));
    AppleAgxRenderFlightVisible(&owner,91);
    assert(read_u32(t_b)==1 && read_u32(t_a)==q_ec && owner.OwnNotifyDeferred==0);
    AppleAgxRenderFlightRetire(&owner,90);
    /* 92: compute while 91 is live on B: deferred on B only */
    assert(AppleAgxRenderFlightBegin(&owner,92));
    ci.StampValue=301u;
    ci.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,ci.StampValue,1);
    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
    assert(AppleAgxRenderFlightPublish(&owner,92));
    assert(read_u32(t_b)==1 && owner.OwnNotifyDeferred==1);
    assert(AppleAgxRenderFlightCancel(&owner,92)); /* deferred: nothing to restore */
    assert(read_u32(t_b)==1 && owner.NotifierComputeCount==1);
    assert(AppleAgxRenderFlightBegin(&owner,92));
    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
    assert(AppleAgxRenderFlightPublish(&owner,92));
    AppleAgxRenderFlightVisible(&owner,92);
    assert(read_u32(t_b)==1 && owner.OwnNotifyDeferred==2);
    AppleAgxRenderFlightRetire(&owner,91); /* releases 92's B threshold */
    assert(read_u32(t_b)==2 && read_u32(t_a)==q_ec && owner.OwnNotifyReleased==1);
    AppleAgxRenderFlightRetire(&owner,92);
    /* 93: render + compute; a failed B flush leaves the flight published;
     * cancel restores both thresholds. */
    owner.OwnNotify=0;
    staged_job.TaExpectedStamp+=0x100; staged_job.D3ExpectedStamp+=0x100;
    { unsigned v=q_ec+2; memcpy(arena+q_lay[12].ArenaOffset,&v,4); }
    assert(AppleAgxRenderFlightBegin(&owner,93));
    assert(RBUILD(0));
    ci.StampValue=302u;
    ci.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,ci.StampValue,1);
    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
    q2c_fail_flush=1;
    assert(!AppleAgxRenderFlightPublish(&owner,93)); /* A first: B never written */
    assert(read_u32(t_a)==q_ec+2 && read_u32(t_b)==2);
    assert(!AppleAgxRenderFlightCancel(&owner,93)); /* uncertain restoration */
    assert(owner.BuildingFlight && !AppleAgxRenderFlightBegin(&owner,94));
    q2c_fail_flush=0;
    assert(AppleAgxRenderFlightCancel(&owner,93));
    assert(read_u32(t_a)==q_ec && read_u32(t_b)==2);
    assert(owner.NotifierComputeCount==2 && owner.NotifierRenderCount==q_ec);
    /* both written, B's flush fails: cancel restores both */
    assert(AppleAgxRenderFlightBegin(&owner,93));
    assert(RBUILD(0));
    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
    owner.NotifierFlush=q2c_flush_b_fails;
    assert(!AppleAgxRenderFlightPublish(&owner,93));
    assert(read_u32(t_a)==q_ec+2 && read_u32(t_b)==3);
    owner.NotifierFlush=q2c_flush;
    assert(AppleAgxRenderFlightCancel(&owner,93));
    assert(read_u32(t_a)==q_ec && read_u32(t_b)==2);
    assert(owner.NotifierComputeCount==2 && owner.NotifierRenderCount==q_ec);
    /* a corrupted B staging value is refused, never published */
    assert(AppleAgxRenderFlightBegin(&owner,93));
    assert(RBUILD(0));
    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&ci,&co));
    owner.Flights[0].EventCountB=1;
    assert(!AppleAgxRenderFlightPublish(&owner,93));
    assert(read_u32(t_a)==q_ec && read_u32(t_b)==2);
    assert(AppleAgxRenderFlightCancel(&owner,93));
    { unsigned v=q_ec; memcpy(arena+q_lay[12].ArenaOffset,&v,4); }
    /* a render rebind (retained activation) keeps the compute Queue's count */
    assert(AppleAgxRenderSharedMemoryBindRelocationObjects(&owner,arena,
        APPLE_AGX_EXP208_ARENA_BYTES,objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
    assert(owner.NotifierComputeCount==2 && owner.NotifierRenderCount==0 && read_u32(t_b)==2);
    for(q_i=0;q_i<40;++q_i) {
      memcpy(owner.Objects[q_i].CpuAddress,q_backup[q_i],owner.Objects[q_i].Length);
      free(q_backup[q_i]);
    }
    owner=q_saved; staged_job=q_job;
#undef RBUILD
  }
'''

LAYOUT = r'''
  {
    /* B objects lie in the sidecar's free tail: after gpu_buf (Asahi
     * workqueue.rs 0x2c18 bytes), above every job word of both sets and
     * inside the 0x10000-byte object; disjoint and aligned. */
    static const unsigned o[4]={APPLE_AGX_COMPUTE_OWN_LIST_OFFSET,
        APPLE_AGX_COMPUTE_OWN_THRESHOLD_OFFSET,APPLE_AGX_COMPUTE_OWN_CONTEXT_OFFSET,
        APPLE_AGX_COMPUTE_OWN_NOTIFIER_OFFSET};
    static const unsigned n[4]={APPLE_AGX_NOTIFIER_LIST_BYTES,APPLE_AGX_THRESHOLD_BYTES,
        APPLE_AGX_GPU_CONTEXT_DATA_BYTES,APPLE_AGX_G13_NOTIFIER_BYTES};
    unsigned a,b;
    assert(APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET+APPLE_AGX_COMPUTE_GPU_BUFFER_BYTES==0xec18u);
    assert(APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(1u,APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET)+
           APPLE_AGX_G13_TIMESTAMP_BLOCK_BYTES<=APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET);
    for(a=0;a<4;++a) {
      assert(o[a]>=APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET+APPLE_AGX_COMPUTE_GPU_BUFFER_BYTES);
      assert(o[a]>=APPLE_AGX_COMPUTE_OWN_FIRST_OFFSET &&
             o[a]+n[a]<=APPLE_AGX_COMPUTE_OWN_END_OFFSET);
      assert(APPLE_AGX_COMPUTE_OWN_END_OFFSET<=0x10000u && (o[a]&0x3fu)==0u);
      for(b=0;b<a;++b) assert(o[a]+n[a]<=o[b] || o[b]+n[b]<=o[a]);
    }
  }
'''


def build_and_run(exercise, needle='  arena[layouts[0].ArenaOffset] = 0x5au;'):
    source = (SHARED / 'tests/apple_agx_render_shared_memory_test.c').read_text()
    assert source.count('int main(void)') == 1 and source.count(needle) == 1
    source = source.replace('int main(void)', HELPERS + 'int main(void)')
    source = source.replace(needle, exercise + needle, 1)
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / 'test.c'
        c.write_text(source)
        binary = Path(d) / 'test'
        subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                        '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-ftrivial-auto-var-init=pattern',
                        '-I', str(SHARED / 'include'), str(c),
                        *[str(SHARED / 'src' / n) for n in SHARED_SOURCES],
                        '-o', str(binary)], check=True)
        out = subprocess.run([str(binary)], check=True, stdout=subprocess.PIPE, text=True)
    return dict(re.findall(r'^Q2C (\S+) ([0-9a-f]{16})$', out.stdout, re.M))


class ComputeOwnQueue(unittest.TestCase):
    def test_switch_off_bytes_match_golden_and_switch_on_semantics(self):
        hashes = build_and_run(SCENARIO)
        self.assertEqual(hashes, GOLDEN)

    def test_own_queue_flights_per_queue(self):
        build_and_run(OWN_ONLY)

    def test_own_queue_layout_in_sidecar_tail(self):
        build_and_run(LAYOUT)


if __name__ == '__main__':
    unittest.main()
