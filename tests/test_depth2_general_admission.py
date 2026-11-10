"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Distinct scenes sharing a live rc124 key must be admitted only in general mode,
with an explicitly enabled typed barrier. The platform's actual manager lookup
must reject any changed key component, pending second user, or unknown binding.
"""
import unittest
from test_render_job_depth2_exp2047 import (build_run, SHARED, SHARED_SOURCES,
    WORKER, section, MANAGER, RENDER)

class GeneralAdmission(unittest.TestCase):
    def test_typed_classes_and_resident_pair(self):
        source = r'''
#include <assert.h>
#include "render_job_window.h"
int main(void) {
  ADMISSION_JOB_WINDOW_KICK_STATE s = {0};
  s.Enabled=1; s.InFlight=1; s.NextPrebound=1; s.QueuedFence=12;
  s.Running.Fence=11; s.Next.Fence=12;
  s.Running.Kind=1; s.Next.Kind=1; s.Running.Process=s.Next.Process=17;
  s.Running.SceneId=20; s.Next.SceneId=21;
  s.Capabilities=ADMISSION_JOB_WINDOW_CAPS_RC98 | ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
  assert(AdmissionJobWindowKickRefusal(&s)==58); /* legacy */
  s.General=1; s.Prelude=1; s.SameManagerSlot=1;
  s.KickClasses=AdmissionJobWindowGeneralClasses(0,0,0);
  assert(AdmissionJobWindowKickRefusal(&s)==64); /* R/R not proven */
  s.KickClasses=AdmissionJobWindowGeneralClasses(0,1,0);
  assert(!AdmissionJobWindowKickRefusal(&s));
  s.SameManagerSlot=0; assert(AdmissionJobWindowKickRefusal(&s)==58);
  s.SameManagerSlot=1;
  for (unsigned a=1;a<=3;++a) for (unsigned b=1;b<=3;++b) {
    s.Running.Kind=a; s.Next.Kind=b;
    unsigned barrier=AdmissionJobWindowBarrier(a,b);
    s.KickClasses=AdmissionJobWindowGeneralClasses(0,0,0);
    assert(AdmissionJobWindowKickRefusal(&s)==(barrier==3 ? 0u:64u));
    s.KickClasses=AdmissionJobWindowGeneralClasses(1,1,1);
    assert(!AdmissionJobWindowKickRefusal(&s));
  }
  s.PreemptionPhase=1; assert(AdmissionJobWindowKickRefusal(&s)==4);
  s.PreemptionPhase=0; s.Next.Process++; assert(AdmissionJobWindowKickRefusal(&s)==55);
  return 0;
}
'''
        ran=build_run(source,[RENDER/'src/render_job_window.c'])
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_platform_checks_complete_live_key(self):
        helper=section(WORKER.read_text(),'static BOOLEAN AdmissionJobWindowManagerNeedsIdle(')+'\n}\n'
        source=MANAGER.replace('/*HELPER*/',helper)
        source=source.replace('  puts("EXP2051_MANAGER_OK");',r'''
  owner.ManagerDepth2=owner.ManagerSaveOnEvict=1;
  owner.ManagerEvict=evict;
  owner.ManagerSlotCount=4; owner.ManagerSlot=1;
  owner.SlotBound[1]=1; owner.SlotKey[1]=a;
  manager.Key=a; manager.PendingFence=owner.ManagerFence;
  assert(!AdmissionJobWindowManagerNeedsIdle(&owner,1,1,&manager,&a,&previous,&b));
  for (unsigned i=0;i<12;++i) {
    b=a;
    if(i==0) b.Owner++;
    else if(i==1) b.Generation++;
    else if(i==2) b.RootIpa++;
    else if((i-3)%3==0) b.Backing[(i-3)/3].Va++;
    else if((i-3)%3==1) b.Backing[(i-3)/3].Bytes++;
    else b.Backing[(i-3)/3].Reserved++;
    assert(AdmissionJobWindowManagerNeedsIdle(&owner,1,1,&manager,&b,&previous,&a));
  }
  owner.ManagerSecondFence=8;
  assert(AdmissionJobWindowManagerNeedsIdle(&owner,1,1,&manager,&a,&previous,&a));
''')
        source=source.replace('int main(void)', '''static APPLE_AGX_BOOL evict(void *c, const APPLE_AGX_RENDER_MANAGER_KEY *k,
    APPLE_AGX_U32 f, const APPLE_AGX_EXP208_RELOCATION_OBJECT *o) {
    (void)c;(void)k;(void)f;(void)o;return 1;
}
int main(void)''')
        ran=build_run(source,[SHARED/'src'/n for n in SHARED_SOURCES])
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_prelude_and_compute_on_compute_default_on(self):
        # EXP2129-2133: both default on; only an explicit REG_DWORD 0 opts out.
        text=(RENDER/'src/backend_platform_windows.c').read_text()
        for name in ('Wom1Depth2Prelude','Wom1Depth2ComputeOnCompute'):
            self.assertIn('!AdmissionPlatformSwitchDisabled(Context, L"%s")'%name,text)
            self.assertNotIn('AdmissionPlatformSwitchEnabled(Context, L"%s")'%name,text)

    def test_actual_switches_reach_retained_queue_config(self):
        text=WORKER.read_text()
        start=text.index('  runtime->ManagerSlots =')
        end=text.index('  RtlZeroMemory(&runtime->RcCurrent',start)
        body=text[start:end]
        source=r'''
#include <assert.h>
#include <string.h>
#include <wchar.h>
#include "render_job_window.h"
#include "apple_agx_render_shared_memory.h"
#define TRUE 1
#define FALSE 0
#define ADMISSION_JOB_AHEAD_BUILD 1
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
static unsigned switches;
static unsigned mask(const wchar_t *name) {
 const wchar_t *names[]={L"Wom1ManagerSlots",L"Wom1JobDepth2",L"Wom1JobDepth2Caps",
 L"Wom1Depth2General",L"Wom1Depth2TaOnCompute",L"Wom1Depth2TaOn3d",
 L"Wom1Depth2ComputeOnCompute",L"Wom1RcDepth2",L"Wom1JobDepth2Kick",
 L"Wom1JobDepth2KickOn3d",L"Wom1ManagerSaveOnEvict",L"Wom1RcReceipt",L"Wom1Depth2Prelude",L"Wom1JobDiagnostics"};
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i)
  if(!wcscmp(name,names[i])) return 1u<<i;
 return 0;
}
static unsigned char AdmissionPlatformSwitchEnabled(void *c,const wchar_t *n) {
 (void)c;return (switches & mask(n)) != 0;
}
static unsigned char AdmissionPlatformSwitchDisabled(void *c,const wchar_t *n) {
 /* Explicit zero for absent mask bits preserves this legacy fixture. */
 (void)c;return wcscmp(n,L"Wom1JobBuildAhead") && !(switches & mask(n));
}
static APPLE_AGX_BOOL AdmissionEvictG4Manager(void *c,const APPLE_AGX_RENDER_MANAGER_KEY *k,
 APPLE_AGX_U32 f,const APPLE_AGX_EXP208_RELOCATION_OBJECT *o) {
 (void)c;(void)k;(void)f;(void)o;return 1;
}
typedef struct {
 unsigned char ManagerSlots,RcDepth2,RcReceiptEnabled,RcExportPending,Depth2General,Depth2Prelude;
 unsigned char JobAhead,JobDepth2,JobDepth2Kick,JobDiagnostics;
 unsigned char FwRxDrain,PresentTsEnabled; /* 125bddd1, EXP2205 */
 unsigned char SubmitBracket; /* EXP2270 */
 unsigned char IrqCompletion; /* IRQ-completion step 2, default on */
 unsigned Depth2FallbackDrill,Depth2FallbackDrillCount; /* be8c6f7f */
 unsigned Depth2FallbackDrillBuilt,Depth2FallbackDrillBuiltCount; /* S4a drill */
 unsigned Depth2Capabilities,Depth2KickClasses;
 struct { APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory; } Initdata;
 struct { struct {APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG Config;} QueueProvider;} Provider;
 unsigned char CrossProcessPairs; /* C1: Wom1CrossProcessPairs, default off */
 unsigned char XprocLeadEnabled, XprocLeadWaive; /* C2a: Wom1CrossProcessPrivateLead, default off */
 ADMISSION_JOB_WINDOW_XPROC_RECEIPT XprocReceipt; unsigned long long XprocExportQpc;
} Runtime;
typedef struct { unsigned G3RetainedSlots, G3CrossProcessPairs, SubmitRequeue; } Adapter; /* V1 Wom1G3RetainedSlots */
static unsigned char AdmissionPlatformDwordValue(void *c,const wchar_t *n,unsigned *v) {
 (void)c;(void)n;(void)v;return 0;
}
static void configure_on(Runtime *runtime,Adapter *Context) {
'''+body+r'''
}
static Adapter adapter;
static void configure(Runtime *runtime,void *unused) { (void)unused; configure_on(runtime,&adapter); }
int main(void) {
 Runtime r={0}; r.Initdata.RenderSharedMemory.NotifierFlight=1;
 switches=15; configure(&r,0);
 assert(r.Depth2General && r.JobDepth2Kick && !r.RcReceiptEnabled && !r.JobDiagnostics);
 switches=15|(1u<<13);configure(&r,0);
 assert(r.JobDiagnostics && !r.RcReceiptEnabled && r.JobDepth2Kick);
 switches=15;configure(&r,0);assert(!r.JobDiagnostics);
 /* Functional modes must not silently turn on per-poll diagnostics. */
 switches=15|(1u<<7); configure(&r,0); assert(!r.RcReceiptEnabled);
 switches=15|(1u<<11); configure(&r,0); assert(r.RcReceiptEnabled);
 switches=(1u<<7); configure(&r,0); assert(!r.JobDepth2 && !r.RcReceiptEnabled);
 switches=(1u<<11); configure(&r,0); assert(!r.JobDepth2 && r.RcReceiptEnabled);
 switches=15; configure(&r,0);
 assert(!r.Depth2Prelude);
 assert(r.Initdata.RenderSharedMemory.ManagerDepth2 && r.Initdata.RenderSharedMemory.ManagerSaveOnEvict);
 assert(r.Provider.QueueProvider.Config.AbortFaultSuffix);
 assert(r.Depth2KickClasses==(1u<<3));
 for(unsigned i=0;i<4;++i) {
  memset(&r,0,sizeof(r));r.Initdata.RenderSharedMemory.NotifierFlight=1;
  switches=15u & ~(1u<<i);configure(&r,0);
  assert(!r.Depth2General && !r.Provider.QueueProvider.Config.AbortFaultSuffix);
 }
 memset(&r,0,sizeof(r));switches=15;configure(&r,0);assert(!r.Depth2General);
 memset(&r,0,sizeof(r));r.Initdata.RenderSharedMemory.NotifierFlight=1;
 switches=127;configure(&r,0);assert(r.Depth2KickClasses==30);
 switches=127|(1u<<12);configure(&r,0);assert(r.Depth2Prelude);
 switches=127;configure(&r,0);assert(!r.Depth2Prelude); /* no stale opt-in */
 for(unsigned i=0;i<4;++i) {
  switches=(15u & ~(1u<<i)) | (1u<<12);configure(&r,0);
  assert(!r.Depth2General && !r.Depth2Prelude);
 }
 /* Broad legacy knobs cannot bypass the Q6 typed mask. */
 switches=15|(1u<<8)|(1u<<9);configure(&r,0);assert(r.Depth2KickClasses==(1u<<3));
 return 0;
}
'''
        ran=build_run(source,[RENDER/'src/render_job_window.c'])
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
        # Configuration happens after provider preparation but before Create.
        self.assertLess(end,text.index('  backend_result = AppleAgxBackendRuntimeStart(',end))
