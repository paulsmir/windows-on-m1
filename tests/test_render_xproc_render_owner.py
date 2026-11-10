"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
K1 (Wom1CrossProcessRenderPairs) lets a render of another process be built
behind a running managed render as a SECOND live buffer manager. The owner
then holds two live managers at once, and the bugs this catches are the ones
that corrupt a running job or a firmware-owned buffer manager:

 * the second build overwrites the first job's record (key, slot, fence,
   counter) or any byte of the first job's slot or work objects;
 * a terminal of one job (success or fault, either order, exact duplicate)
   releases, poisons, unbinds or reads the Stats of the OTHER job's slot
   (the caller's object array names the slot of the LAST build);
 * a third job, an InitBM, a bind, an eviction or a restore happens while two
   managers are live;
 * a state the rule cannot prove (key not bound, every slot bound, poisoned
   slot, pending fence of another job, state not resident) is resolved
   instead of refused;
 * the successor's work commands carry the first job's VM slot (context_id)
   or buffer-manager slot instead of their own;
 * with the switch off, or with the switch on and one key only, anything
   differs from the existing general-manager behaviour;
 * the KMD completion callback (the production AdmissionSaveG4Manager, sliced
   from backend_platform_windows.c) flushes and remembers the Stats of the
   slot the last build bound instead of the completed fence's own slot, so
   the completion record of the first job of a pair carries the second job's
   TVB page and overflow counts.

Production C under ASan/UBSan with pattern stack init.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_render_manager_slots_rc124 import SHARED, NAMES, FIXTURE, ROOT
from test_render_manager_deferred import CALLBACK
from test_depth2_general_manager import BODY as GENERAL_BODY

HELPERS = r'''
#include "apple_agx_render_template_vm_slot.h"
__attribute__((unused)) static unsigned k1_u32(const unsigned char *p) {
  return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
__attribute__((unused)) static unsigned long long k1_u64(const unsigned char *p) {
  return (unsigned long long)k1_u32(p) | ((unsigned long long)k1_u32(p + 4) << 32);
}
'''

# Shared prologue: four keys with their own VM slots, key 0 bound in slot 1
# and key 1 in slot 2, both resident and idle. mo = manager objects, jo = the
# per-job work objects that carry context_id and the manager slot.
PROLOGUE = r'''
    {
    static const unsigned mo[4] = {1u, 20u, 21u, 22u};
    static const unsigned jo[4] = {15u, 17u, 18u, 19u};
    unsigned char *mobj[4][4];
    unsigned char *data = NULL;
    unsigned mslot[4] = {0u, 0u, 0u, 0u};
    unsigned fence = 1000u;
    APPLE_AGX_U32 previous_vm = 0u;
    (void)data; (void)jo;
#define K1_Q(q) AppleAgxRenderManagerOtherSlotRefusal(&owner,&m[q],&k[q])
/* as the KMD does: the template arena is retargeted to the job's VM slot
 * before the build copies it */
#define K1_BUILD(q,init,initbm,f) \
    (AppleAgxRenderTemplateRetargetVmSlot(arena,APPLE_AGX_EXP208_ARENA_BYTES,k[q].VmSlot,&previous_vm) && \
     SL_BUILD(q,init,initbm,f))
#define K1_REFUSED(q,init,initbm,f,code) do { assert(!K1_BUILD(q,init,initbm,f)); \
    assert(owner.LastBuildRefusal == APPLE_AGX_RENDER_BUILD_REFUSAL_##code); } while (0)
#define K1_BIND(q,init) do { assert(K1_BUILD(q,init,1,fence)); mslot[q] = owner.ManagerSlot; \
    for (i = 0; i < 4u; ++i) mobj[q][i] = active_objects[mo[i]].Data; \
    assert(AppleAgxRenderManagerSave(&owner,&m[q],fence,active_objects)); fence += 2u; } while (0)
/* the six context_id fields of a render without InitBM, and the three
 * buffer-manager slot fields, of the job whose objects 15/17/18/19 are j */
#define K1_VM(j,v) do { assert(k1_u32((j)[2] + 0x00cu) == (v)); assert(k1_u32((j)[3] + 0x00cu) == (v)); \
    assert(k1_u32((j)[0] + 0x04cu) == (v)); assert(k1_u32((j)[0] + 0x284u) == (v)); \
    assert(k1_u32((j)[1] + 0x034u) == (v)); assert(k1_u32((j)[1] + 0x22cu) == (v)); } while (0)
#define K1_MSLOT(j,s) do { assert(k1_u64((j)[3] + 0x1cu) == (s)); assert(k1_u32((j)[0] + 0x58u) == (s)); \
    assert(k1_u64((j)[1] + 0x40u) == (s)); } while (0)
/* every reference of the job to the buffer-manager Info names va */
#define K1_INFO(j,va) do { assert(k1_u64((j)[1] + 0x14u) == (va)); assert(k1_u64((j)[1] + 0x214u) == (va)); \
    assert(k1_u64((j)[2] + 0x28u) == (va)); assert(k1_u64((j)[3] + 0x24u) == (va)); \
    assert(k1_u64((j)[0] + 0x24cu) == (va)); } while (0)
    assert(AppleAgxRenderManagerDrain(&owner));
    memset(m, 0, sizeof(m));
    for (q = 0; q < 4u; ++q) k[q].VmSlot = 5u + q;
    owner.ManagerDepth2 = 1;
    K1_BIND(0, 1);                 /* queue initialisation forgets every binding */
    K1_BIND(1, 0);
    assert(mslot[0] == 1u && mslot[1] == 2u);
    for (i = 0; i < 4u; ++i) assert(mobj[0][i] != mobj[1][i]);
'''

EPILOGUE = r'''
    assert(!owner.ManagerFence && !owner.ManagerOtherFence);
    owner.ManagerOtherSlot = 0;
    owner.ManagerDepth2 = 0;
    assert(AppleAgxRenderManagerDrain(&owner));
    owner.ManagerSaveOnEvict = 0;
    owner.ManagerSlotCount = 0;
#undef K1_Q
#undef K1_BUILD
#undef K1_REFUSED
#undef K1_BIND
#undef K1_VM
#undef K1_MSLOT
#undef K1_INFO
    }
#undef SL_SOURCE
#undef SL_BUILD
#undef SL_FIRMWARE
#undef SL_U32
#undef SL_U64
#undef SL_SLOT_FIELDS
  }
'''

TWO_SLOT = r'''
    /* switch off (default): a render of another key behind a live manager
     * is refused exactly as before */
    assert(K1_BUILD(0,0,0,fence));
    assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_UNPROVEN);
    K1_REFUSED(1,0,0,fence + 1u,MANAGER_FENCE);
    assert(!AppleAgxRenderManagerFenceObject(&owner,fence,22u,&data) && data == NULL);
    assert(AppleAgxRenderManagerSave(&owner,&m[0],fence,active_objects));
    fence += 2u;
    owner.ManagerOtherSlot = 1;
    assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_UNPROVEN); /* no live manager */

    /* both observation orders x success/success, fault/success,
     * success/fault, fault/fault; every terminal is delivered twice */
    for (unsigned order = 0; order < 2u; ++order) for (unsigned faults = 0; faults < 4u; ++faults) {
      unsigned a, b, sa, sb, ca, cb, jsize[4], msize[4];
      unsigned char *ja[4], *jb[4], *jcopy[4], before[4][188];
      APPLE_AGX_U64 ia, ib;
      for (q = 0; q < 2u; ++q)
        if (AppleAgxRenderManagerNeedsBind(&owner,&k[q])) K1_BIND(q, 0);
      a = fence; b = fence + 1u; fence += 2u;
      /* A: the first live manager (key 0) */
      assert(K1_BUILD(0,0,0,a));
      sa = owner.ManagerSlot; ca = owner.ManagerBuildCounter; ia = active_objects[1].GpuVa;
      assert(sa == mslot[0] && owner.ManagerSlotLive && owner.ManagerFence == a);
      for (i = 0; i < 4u; ++i) {
        assert(active_objects[mo[i]].Data == mobj[0][i]);
        data = NULL;
        assert(AppleAgxRenderManagerFenceObject(&owner,a,mo[i],&data) && data == mobj[0][i]);
        ja[i] = active_objects[jo[i]].Data; jsize[i] = active_objects[jo[i]].Size;
        jcopy[i] = malloc(jsize[i]); assert(jcopy[i]); memcpy(jcopy[i], ja[i], jsize[i]);
        msize[i] = active_objects[mo[i]].Size; assert(msize[i] <= 188u);
        memcpy(before[i], mobj[0][i], msize[i]);
      }
      K1_VM(ja, k[0].VmSlot); K1_MSLOT(ja, sa); K1_INFO(ja, ia);
      cb = k1_u32(mobj[1][2]);
      assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_OK);
      /* a second manager is never bound or initialised, and never takes
       * the first one's fence */
      K1_REFUSED(1,0,1,b,MANAGER_FENCE);
      K1_REFUSED(1,1,1,b,MANAGER_FENCE);
      K1_REFUSED(1,0,0,a,MANAGER_FENCE);
      /* B: the second live manager (key 1), in its own slot */
      assert(K1_BUILD(1,0,0,b));
      sb = owner.ManagerOtherIndex; ib = active_objects[1].GpuVa;
      assert(sb == mslot[1] && sb != sa && ib != ia);
      assert(owner.ManagerOtherFence == b && owner.ManagerOtherBuildCounter == cb);
      assert(!owner.LastBuildRestored);
      /* the first live manager's record is not this job's */
      assert(owner.ManagerFence == a && owner.ManagerSlot == sa && owner.ManagerSlotLive &&
             owner.ManagerBuildCounter == ca && !owner.ManagerSecondFence && !owner.ManagerPoisoned);
      assert(owner.ManagerKey.Owner == k[0].Owner && owner.ManagerKey.VmSlot == k[0].VmSlot &&
             owner.ManagerKey.RootIpa == k[0].RootIpa);
      assert(m[0].PendingFence == a && m[1].PendingFence == b);
      assert(owner.SlotBound[sa] && owner.SlotBound[sb]);
      for (i = 0; i < 4u; ++i) {
        assert(active_objects[mo[i]].Data == mobj[1][i]);
        data = NULL;
        assert(AppleAgxRenderManagerFenceObject(&owner,b,mo[i],&data) && data == mobj[1][i]);
        assert(AppleAgxRenderManagerFenceObject(&owner,a,mo[i],&data) && data == mobj[0][i]);
        jb[i] = active_objects[jo[i]].Data;
        assert(jb[i] != ja[i]);
        /* not one byte of the running job or of its slot changed */
        assert(memcmp(ja[i], jcopy[i], jsize[i]) == 0);
        assert(memcmp(mobj[0][i], before[i], msize[i]) == 0);
      }
      K1_VM(jb, k[1].VmSlot); K1_MSLOT(jb, sb); K1_INFO(jb, ib);
      K1_VM(ja, k[0].VmSlot); K1_MSLOT(ja, sa); K1_INFO(ja, ia);
      assert(k1_u32(mobj[1][2]) == cb + 1u);   /* slot B's Counter, its sole manager mutation */
      /* two live managers: no third user anywhere, nothing is drained */
      assert(!AppleAgxRenderManagerCanShare(&owner,&m[0],&k[0]));
      assert(!AppleAgxRenderManagerCanShare(&owner,&m[1],&k[1]));
      K1_REFUSED(0,0,0,9001u,MANAGER_FENCE);
      K1_REFUSED(1,0,0,9001u,MANAGER_FENCE);
      K1_REFUSED(2,0,1,9001u,MANAGER_FENCE);
      assert(K1_Q(0) == APPLE_AGX_RENDER_MANAGER_OTHER_PENDING);
      assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_PENDING);
      assert(K1_Q(2) == APPLE_AGX_RENDER_MANAGER_OTHER_PENDING);
      assert(!AppleAgxRenderManagerDrain(&owner));
      assert(owner.ManagerOtherFence == b && owner.ManagerFence == a);
      /* a terminal names its own state and fence; a running first manager
       * is never an unpublished build */
      assert(!AppleAgxRenderManagerSave(&owner,&m[0],b,active_objects));
      assert(!AppleAgxRenderManagerSave(&owner,&m[1],a,active_objects));
      assert(!AppleAgxRenderManagerSave(&owner,&m[1],9001u,active_objects));
      assert(!AppleAgxRenderManagerAbort(&owner,&m[0],b));
      assert(!AppleAgxRenderManagerAbort(&owner,&m[1],a));
      assert(!AppleAgxRenderManagerCancelBuild(&owner,&m[0],a,active_objects));
      assert(!AppleAgxRenderManagerCancelBuild(&owner,&m[0],b,active_objects));
      assert(!AppleAgxRenderManagerCancelBuild(&owner,&m[1],a,active_objects));
      assert(m[0].PendingFence == a && m[1].PendingFence == b &&
             owner.ManagerOtherFence == b && owner.ManagerFence == a);
      /* the firmware's Stats of the two slots differ */
      memset(mobj[0][3], (int)(0xa0u + faults), 64u);
      memset(mobj[1][3], (int)(0xb0u + order), 64u);
      for (unsigned at = 0; at < 2u; ++at) {
        unsigned who = at ^ order;              /* 0 = A (key 0), 1 = B (key 1) */
        unsigned f = who ? b : a, rest = who ? a : b;
        unsigned bad = (faults >> who) & 1u;
        for (unsigned retry = 0; retry < 2u; ++retry) {
          if (bad) assert(AppleAgxRenderManagerAbort(&owner,&m[who],f));
          else assert(AppleAgxRenderManagerSave(&owner,&m[who],f,active_objects));
          assert(!m[who].PendingFence && m[who].SavedFence == f);
          assert((m[who].Valid != 0) == !bad && (m[who].Resident != 0) == !bad);
          assert((AppleAgxRenderManagerNeedsBind(&owner,&k[who]) != 0) == (bad != 0));
          /* Stats of the job's OWN slot, whichever slot the array names */
          if (!bad) assert(memcmp(m[who].Stats, mobj[who][3], 64u) == 0 &&
                           m[who].Stats[0] == (unsigned char)(who ? 0xb0u + order : 0xa0u + faults));
          if (!at) {
            /* the other job is THE live manager now, untouched by this
             * terminal: not released, not poisoned, not unbound */
            assert(owner.ManagerFence == rest && !owner.ManagerOtherFence &&
                   !owner.ManagerSecondFence && !owner.ManagerPoisoned);
            assert(owner.ManagerSlot == mslot[!who] && owner.ManagerSlotLive);
            assert(owner.ManagerBuildCounter == (who ? ca : cb));
            assert(owner.ManagerKey.Owner == k[!who].Owner && owner.ManagerKey.VmSlot == k[!who].VmSlot);
            assert(m[!who].PendingFence == rest && owner.SlotBound[mslot[!who]]);
            assert(!AppleAgxRenderManagerDrain(&owner));
            data = NULL;
            assert(AppleAgxRenderManagerFenceObject(&owner,rest,22u,&data) && data == mobj[!who][3]);
            assert(!AppleAgxRenderManagerFenceObject(&owner,f,22u,&data));
            assert(AppleAgxRenderManagerCanShare(&owner,&m[!who],&k[!who]));
          } else {
            assert(!owner.ManagerFence && !owner.ManagerOtherFence && !owner.ManagerSecondFence);
          }
        }
      }
      /* late exact duplicates of both terminals change nothing */
      for (q = 0; q < 2u; ++q) {
        unsigned f = q ? b : a;
        if ((faults >> q) & 1u) assert(AppleAgxRenderManagerAbort(&owner,&m[q],f));
        else assert(AppleAgxRenderManagerSave(&owner,&m[q],f,active_objects));
        assert(!owner.ManagerFence && !owner.ManagerOtherFence && !m[q].PendingFence &&
               m[q].SavedFence == f && (m[q].Valid != 0) == !((faults >> q) & 1u));
      }
      for (i = 0; i < 4u; ++i) free(jcopy[i]);
    }

    /* unpublished second manager: the cancel undoes its Counter only */
    {
      unsigned a, b, cb;
      for (q = 0; q < 2u; ++q)
        if (AppleAgxRenderManagerNeedsBind(&owner,&k[q])) K1_BIND(q, 0);
      a = fence; b = fence + 1u; fence += 4u;
      assert(K1_BUILD(0,0,0,a));
      cb = k1_u32(mobj[1][2]);
      assert(K1_BUILD(1,0,0,b));
      assert(k1_u32(mobj[1][2]) == cb + 1u);
      assert(AppleAgxRenderManagerCancelBuild(&owner,&m[1],b,active_objects));
      assert(k1_u32(mobj[1][2]) == cb && !owner.ManagerOtherFence && !m[1].PendingFence);
      assert(owner.ManagerFence == a && m[0].PendingFence == a && owner.ManagerSlot == mslot[0] &&
             owner.ManagerSlotLive && !owner.ManagerPoisoned);
      assert(m[1].Valid && m[1].Resident && !AppleAgxRenderManagerNeedsBind(&owner,&k[1]));
      assert(!AppleAgxRenderManagerCancelBuild(&owner,&m[1],b,active_objects));
      assert(owner.ManagerFence == a);
      /* and it can be built again */
      assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_OK);
      assert(K1_BUILD(1,0,0,b + 2u));
      assert(k1_u32(mobj[1][2]) == cb + 1u && owner.ManagerOtherFence == b + 2u);
      memset(mobj[0][3], 0x5a, 64u); memset(mobj[1][3], 0x6b, 64u);
      assert(active_objects[22].Data == mobj[1][3]);
      assert(AppleAgxRenderManagerSave(&owner,&m[0],a,active_objects));
      assert(m[0].Stats[0] == 0x5a && m[0].Stats[63] == 0x5a);
      assert(AppleAgxRenderManagerSave(&owner,&m[1],b + 2u,active_objects));
      assert(m[1].Stats[0] == 0x6b && m[1].Stats[63] == 0x6b);
    }

    /* fail-closed terms: never resolved by a bind, an eviction or a restore */
    {
      unsigned a = fence, c;
      fence += 4u;
      assert(K1_BUILD(0,0,0,a));
      /* key 2 is not bound and slot 3 is free: a bind needs InitBM */
      assert(K1_Q(2) == APPLE_AGX_RENDER_MANAGER_OTHER_UNBOUND);
      K1_REFUSED(2,0,0,a + 1u,MANAGER_FENCE);
      K1_REFUSED(2,0,1,a + 1u,MANAGER_FENCE);
      assert(!owner.SlotBound[3] && AppleAgxRenderManagerNeedsBind(&owner,&k[2]));
      /* the same key is the shared-user path, never the other-slot one */
      assert(K1_Q(0) == APPLE_AGX_RENDER_MANAGER_OTHER_PENDING);
      assert(K1_BUILD(0,0,0,a + 1u));
      assert(owner.ManagerSecondFence == a + 1u && !owner.ManagerOtherFence);
      /* two users of the live slot: a pending fence of another job */
      assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_PENDING);
      K1_REFUSED(1,0,0,a + 3u,MANAGER_FENCE);
      /* a fault poisons the live slot until every user is terminal */
      assert(AppleAgxRenderManagerAbort(&owner,&m[0],a));
      assert(owner.ManagerPoisoned && owner.ManagerFence == a + 1u);
      assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_POISONED);
      K1_REFUSED(1,0,0,a + 3u,MANAGER_FENCE);
      assert(AppleAgxRenderManagerAbort(&owner,&m[0],a + 1u));
      assert(!owner.ManagerFence && !owner.ManagerOtherFence);
      /* the dropped job's invalid terminal record never joins */
      c = fence; fence += 2u;
      assert(K1_BUILD(1,0,0,c));
      assert(K1_Q(0) == APPLE_AGX_RENDER_MANAGER_OTHER_POISONED);
      K1_REFUSED(0,0,0,c + 1u,MANAGER_FENCE);
      K1_REFUSED(0,0,1,c + 1u,MANAGER_FENCE);
      assert(AppleAgxRenderManagerNeedsBind(&owner,&k[0]) && !m[0].Valid);
      assert(AppleAgxRenderManagerSave(&owner,&m[1],c,active_objects));
      /* every managed slot bound: key 3 would need an eviction */
      K1_BIND(0, 0);
      K1_BIND(2, 0);
      assert(mslot[0] == 1u && mslot[1] == 2u && mslot[2] == 3u);
      c = fence; fence += 2u;
      assert(K1_BUILD(0,0,0,c));
      assert(K1_Q(3) == APPLE_AGX_RENDER_MANAGER_OTHER_ALL_BOUND);
      K1_REFUSED(3,0,0,c + 1u,MANAGER_FENCE);
      K1_REFUSED(3,0,1,c + 1u,MANAGER_FENCE);
      for (q = 0; q < 3u; ++q)
        assert(owner.SlotBound[mslot[q]] && !AppleAgxRenderManagerNeedsBind(&owner,&k[q]));
      assert(AppleAgxRenderManagerNeedsBind(&owner,&k[3]));
      /* any bound, resident, idle key can be the second manager */
      assert(K1_Q(2) == APPLE_AGX_RENDER_MANAGER_OTHER_OK);
      assert(K1_BUILD(2,0,0,c + 1u));
      assert(owner.ManagerOtherIndex == 3u && owner.ManagerSlot == 1u);
      assert(AppleAgxRenderManagerSave(&owner,&m[2],c + 1u,active_objects));
      assert(AppleAgxRenderManagerSave(&owner,&m[0],c,active_objects));
      /* a bound slot whose state was evicted (not resident) is not proven:
       * no restore while a job is live. The drain itself proves that the
       * second manager's terminals left dirty fences the eviction accepts. */
      assert(AppleAgxRenderManagerDrain(&owner));
      assert(m[1].Valid && !m[1].Resident && !AppleAgxRenderManagerNeedsBind(&owner,&k[1]));
      c = fence; fence += 2u;
      assert(K1_BUILD(0,0,0,c));
      assert(K1_Q(1) == APPLE_AGX_RENDER_MANAGER_OTHER_UNPROVEN);
      K1_REFUSED(1,0,0,c + 1u,MANAGER_FENCE);
      assert(AppleAgxRenderManagerSave(&owner,&m[0],c,active_objects));
    }
'''

# The production completion callback over two live managers. The runtime is a
# mock that names the fixture's owner; the function text is the KMD's.
PLATFORM = ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'
SAVE_MOCK = r'''
#include "apple_agx_backend_runtime.h"
typedef unsigned char BOOLEAN;
#define TRUE 1
#define FALSE 0
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {
  APPLE_AGX_RENDER_MANAGER_STATE *G4Manager;
  unsigned G4Native, BoundFence;
} ADMISSION_BACKEND_IMAGE;
typedef struct { ADMISSION_BACKEND_IMAGE BackendImage; } K1_ADAPTER;
typedef struct {
  K1_ADAPTER *Adapter;
  struct { APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *Owner; } Initdata;
  struct { unsigned TaComplete, D3Complete; } Backend;
  struct {
    unsigned char (*FlushForCpu)(void *, const void *, unsigned);
    void (*MemoryBarrier)(void *);
  } TransportIo;
  APPLE_AGX_EXP208_RELOCATION_OBJECT *QueueObjects;
  unsigned AheadReleasedFence;
  APPLE_AGX_RENDER_MANAGER_STATE *AheadManager;
  BOOLEAN AheadG4Native, ManagerSlots;
  unsigned char BuildDirectReads;
  unsigned SavedStatsFence; unsigned char *SavedStatsData;
} ADMISSION_PLATFORM_RUNTIME;
#define AdmissionJobSubSaved 6
static unsigned k1_saved;
static void AdmissionJobTimingSubWindows(ADMISSION_PLATFORM_RUNTIME *r, unsigned f, int s) {
  assert(r && f && s == AdmissionJobSubSaved); ++k1_saved; }
static unsigned k1_flushes, k1_barriers;
static const void *k1_flushed;
static unsigned char k1_flush(void *c, const void *p, unsigned n) {
  assert(c && p && n == 64u); ++k1_flushes; k1_flushed = p; return 1; }
static void k1_barrier(void *c) { assert(c); ++k1_barriers; }
/* the runtime embeds the owner; here it names the fixture's */
#define RenderSharedMemory Owner[0]
@SAVE@
#undef RenderSharedMemory
'''

SAVE_GLUE = r'''
    {
      K1_ADAPTER ad;
      ADMISSION_PLATFORM_RUNTIME rt;
      unsigned a, b;
      memset(&ad, 0, sizeof(ad)); memset(&rt, 0, sizeof(rt));
      rt.Adapter = &ad; rt.Initdata.Owner = &owner; rt.QueueObjects = active_objects;
      rt.TransportIo.FlushForCpu = k1_flush; rt.TransportIo.MemoryBarrier = k1_barrier;
      rt.Backend.TaComplete = rt.Backend.D3Complete = 1u;
      rt.ManagerSlots = TRUE; rt.BuildDirectReads = (unsigned char)owner.DirectReads;
      assert(owner.ManagerSlotCount == APPLE_AGX_RENDER_MANAGER_SLOTS);
      ad.BackendImage.G4Native = 1u;

      /* switch off (default): the bound slot's Stats, nothing remembered */
      a = fence; fence += 2u;
      assert(K1_BUILD(0,0,0,a));
      ad.BackendImage.G4Manager = &m[0]; ad.BackendImage.BoundFence = a;
      memset(mobj[0][3], 0x51, 64u);
      assert(AdmissionSaveG4Manager(&rt, a, AppleAgxBackendCompletionSuccess));
      assert(k1_flushes == 1u && k1_barriers == 1u && k1_saved == 1u);
      assert(k1_flushed == active_objects[22].Data && k1_flushed == mobj[0][3]);
      assert(!rt.SavedStatsFence && !rt.SavedStatsData && m[0].Stats[0] == 0x51u);

      owner.ManagerOtherSlot = 1;
      for (unsigned order = 0; order < 2u; ++order) {
        a = fence; b = fence + 1u; fence += 2u;
        assert(K1_BUILD(0,0,0,a));
        assert(K1_BUILD(1,0,0,b));
        /* the array and the image name the LAST build: job B, slot B */
        assert(active_objects[22].Data == mobj[1][3] && mobj[0][3] != mobj[1][3]);
        ad.BackendImage.G4Manager = &m[1]; ad.BackendImage.BoundFence = b;
        rt.AheadReleasedFence = a; rt.AheadManager = &m[0]; rt.AheadG4Native = TRUE;
        memset(mobj[0][3], (int)(0xa0u + order), 64u);
        memset(mobj[1][3], (int)(0xb0u + order), 64u);
        for (unsigned at = 0; at < 2u; ++at) {
          unsigned who = at ^ order;          /* 0 = A, 1 = B */
          unsigned f = who ? b : a;
          k1_flushes = 0u; k1_flushed = NULL;
          assert(AdmissionSaveG4Manager(&rt, f, AppleAgxBackendCompletionSuccess));
          /* Stats only (save-on-evict), of this fence's OWN slot */
          assert(k1_flushes == 1u && k1_flushed == mobj[who][3]);
          assert(rt.SavedStatsFence == f && rt.SavedStatsData == mobj[who][3]);
          assert(m[who].Stats[0] == (unsigned char)((who ? 0xb0u : 0xa0u) + order));
          assert(!m[who].PendingFence && m[who].Valid && m[who].Resident);
          /* a retried completion keeps what the first one remembered */
          assert(AdmissionSaveG4Manager(&rt, f, AppleAgxBackendCompletionSuccess));
          assert(rt.SavedStatsFence == f && rt.SavedStatsData == mobj[who][3]);
        }
        assert(!owner.ManagerFence && !owner.ManagerOtherFence);
        rt.AheadReleasedFence = 0u;
      }
    }
'''

# The architect's third test in isolation: the successor's work commands
# carry their OWN VM slot and buffer-manager slot, the running job's keep
# theirs.
SUCCESSOR = r'''
    owner.ManagerOtherSlot = 1;
    {
      unsigned a = fence, b = fence + 1u;
      unsigned char *ja[4], *jb[4];
      APPLE_AGX_U64 ia, ib;
      assert(k[0].VmSlot == 5u && k[1].VmSlot == 6u);
      assert(K1_BUILD(0,0,0,a));
      for (i = 0; i < 4u; ++i) ja[i] = active_objects[jo[i]].Data;
      ia = active_objects[1].GpuVa;
      K1_VM(ja, 5u); K1_MSLOT(ja, 1u); K1_INFO(ja, ia);
      assert(K1_BUILD(1,0,0,b));
      assert(previous_vm == 5u);   /* the arena was the running job's */
      for (i = 0; i < 4u; ++i) jb[i] = active_objects[jo[i]].Data;
      ib = active_objects[1].GpuVa;
      assert(ib != ia);
      K1_VM(jb, 6u); K1_MSLOT(jb, 2u); K1_INFO(jb, ib);
      K1_VM(ja, 5u); K1_MSLOT(ja, 1u); K1_INFO(ja, ia);
      /* the buffer-manager Info of each slot points at its own
       * BlockControl and Counter */
      assert(k1_u64(mobj[0][0] + 0x40u) != k1_u64(mobj[1][0] + 0x40u));
      assert(k1_u64(mobj[0][0] + 0x64u) != k1_u64(mobj[1][0] + 0x64u));
      assert(AppleAgxRenderManagerSave(&owner,&m[1],b,active_objects));
      assert(AppleAgxRenderManagerSave(&owner,&m[0],a,active_objects));
    }
'''


def run(case, body, helpers=''):
    fixture = FIXTURE[:FIXTURE.index('    /* 2: key 0 again:')]
    fixture = fixture.replace('    owner.ManagerSlotCount = 4u;', '''
    owner.ManagerSlotCount = 4u;
    owner.ManagerSaveOnEvict = 1;
    owner.ManagerEvict = evict;
    owner.ManagerEvictContext = m;
''') + body
    source = (SHARED/'tests/apple_agx_render_shared_memory_test.c').read_text()
    source = source.replace('int main(', HELPERS + CALLBACK + helpers + '\nint main(', 1)
    marker = '  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =='
    case.assertEqual(source.count(marker), 1)
    source = source.replace(marker, fixture + marker)
    names = NAMES + ['render_template_vm_slot']
    with tempfile.TemporaryDirectory() as d:
        p = Path(d); (p/'test.c').write_text(source)
        result = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
            '-Werror', '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined',
            '-I', str(SHARED/'include'), str(p/'test.c'),
            *[str(SHARED/'src'/('apple_agx_'+n+'.c')) for n in names],
            '-o', str(p/'test')], capture_output=True, text=True)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(p/'test')], capture_output=True, text=True)
        case.assertEqual(result.returncode, 0, result.stdout + result.stderr)


def save_mock(platform):
    start = platform.index('static APPLE_AGX_BACKEND_BOOL AdmissionSaveG4Manager(')
    end = platform.index('{', start) + 1
    depth = 1
    while depth:
        depth += (platform[end] == '{') - (platform[end] == '}')
        end += 1
    return SAVE_MOCK.replace('@SAVE@', platform[start:end])


class CrossProcessRenderOwner(unittest.TestCase):
    def test_two_live_managers_both_terminal_orders_and_faults(self):
        run(self, PROLOGUE + TWO_SLOT + EPILOGUE)

    def test_successor_carries_its_own_vm_slot_and_manager_slot(self):
        run(self, PROLOGUE + SUCCESSOR + EPILOGUE)

    def test_kmd_completion_flushes_and_remembers_the_fence_own_stats(self):
        run(self, PROLOGUE + SAVE_GLUE + EPILOGUE, save_mock(PLATFORM.read_text()))
        # the completion capture runs after the Save and reads what it kept
        text = PLATFORM.read_text()
        done = text[text.index('static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete('):]
        self.assertLess(done.index('AdmissionSaveG4Manager(runtime, Fence, Status)'),
                        done.index('AdmissionGpuCompletionCaptureWindows(runtime, Fence, Status)'))
        capture = text[text.index('static VOID AdmissionGpuCompletionCaptureWindows('):]
        capture = capture[:capture.index('\n}\n')]
        self.assertIn('runtime->SavedStatsFence == fence && runtime->SavedStatsData != NULL\n'
                      '                ? runtime->SavedStatsData : stats->Data;', capture)
        self.assertEqual(capture.count('stats->Data'), 1)
        self.assertEqual(text.count('runtime->SavedStatsData='), 1)

    def test_one_key_behaves_as_the_general_manager_with_the_switch_on(self):
        # the existing general-manager scenario, unchanged, with K1 armed
        self.assertTrue(GENERAL_BODY.lstrip().startswith('owner.ManagerDepth2 = 1;'))
        run(self, '\n    owner.ManagerOtherSlot = 1;' + GENERAL_BODY)


if __name__ == '__main__':
    unittest.main()
