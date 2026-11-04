"""Cross-process cache pressure must reclaim only proven idle released storage."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_g3_private_scene_cache_lru import function

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'
PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "apple_agx_g3_private_pool.h"
typedef unsigned UINT, ULONG; typedef unsigned long long ULONGLONG;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY;
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
enum {ADMISSION_G3_PRIVATE_STAT_PRESSURE=3};
typedef struct { int GpuvaG3Closing; } CONTEXT;
typedef struct SCENE {struct SCENE *Next; CONTEXT *Context;
 unsigned Cached,Queued,Submitting,Quarantined,Reported,ReleaseRequested;
 ULONGLONG CachedAt; APPLE_AGX_G3_PRIVATE_EXTENT Extent; int Released;
} ADMISSION_G3_PRIVATE_SCENE;
typedef struct STATE {LIST_ENTRY Processes; ULONGLONG PrivateStats[16];
 ULONG PrivatePressure[12]; APPLE_AGX_G3_PRIVATE_POOL PrivatePool;} ADMISSION_G3_STATE;
/* L2 step 1: the per-process lock; a victim's is only tried (G held). */
typedef struct {int Held;} KGUARDED_MUTEX;
static BOOLEAN KeTryToAcquireGuardedMutex(KGUARDED_MUTEX *m) { if(m->Held) return FALSE; m->Held=1; return TRUE; }
static void KeReleaseGuardedMutex(KGUARDED_MUTEX *m) { assert(m->Held); m->Held=0; }
typedef struct PROC {LIST_ENTRY Link; ADMISSION_G3_STATE *State;
 ADMISSION_G3_PRIVATE_SCENE *PrivateScenes; int Poisoned; ULONG OsProcessId;
 struct {int Uncertain,JobInFlight,LeaseToken,Created; ULONGLONG ProcessId,RootIpa;} Graph;
 KGUARDED_MUTEX Lock;
} ADMISSION_G3_PROCESS;
typedef struct {int unused;} ADMISSION_BACKEND_MEMORY_VIEW;
static int refuse_release;
static BOOLEAN AdmissionG3PrivateReleaseScene(ADMISSION_G3_PROCESS *p,
 ADMISSION_G3_PRIVATE_SCENE *s,ADMISSION_BACKEND_MEMORY_VIEW *v) {
 (void)v; assert(p->Lock.Held); if(refuse_release) return FALSE;
 assert(!p->Poisoned && !p->Graph.Uncertain && !p->Graph.JobInFlight && !p->Graph.LeaseToken);
 assert(s->Cached && s->Reported && s->ReleaseRequested && !s->Queued && !s->Submitting && !s->Quarantined);
 assert(AppleAgxG3PrivateFree(&p->State->PrivatePool,p->Graph.ProcessId,&s->Extent));
 ADMISSION_G3_PRIVATE_SCENE **link=&p->PrivateScenes;
 while(*link!=s) link=&(*link)->Next;
 *link=s->Next;s->Released=1;return TRUE;
}
@@FUNCTIONS@@
static ADMISSION_G3_STATE st;
int main(void) {
 ADMISSION_G3_PROCESS p[6]={0}; ADMISSION_G3_PRIVATE_SCENE s[6]={0};
 CONTEXT ctx={0}; ADMISSION_BACKEND_MEMORY_VIEW view={0};
 APPLE_AGX_G3_PRIVATE_EXTENT fill[6]={0},wanted={0}; APPLE_AGX_G3_PRIVATE_POOL_STATS failure;
 st.Processes.Flink=&p[0].Link; st.Processes.Blink=&p[5].Link;
 for(unsigned i=0;i<6;i++) {
  p[i].State=&st; p[i].Graph.ProcessId=i+1; p[i].Graph.Created=1;p[i].Graph.RootIpa=0x10000u*(i+1);
  p[i].OsProcessId=100+i;
  p[i].Link.Flink=i==5?&st.Processes:&p[i+1].Link;
  p[i].Link.Blink=i?&p[i-1].Link:&st.Processes;
  if(i) {
   assert(AppleAgxG3PrivateAllocate(&st.PrivatePool,i+1,128*0x10000u,&fill[i]));
   assert(AppleAgxG3PrivateAllocate(&st.PrivatePool,i+1,(i==5?20:85)*0x10000u,&s[i].Extent));
   s[i].Context=&ctx;s[i].Cached=s[i].Reported=s[i].ReleaseRequested=1;s[i].CachedAt=i;
   p[i].PrivateScenes=&s[i];
  }
 }
 /* 4*213+148=1000; requester24; real physical pool is exactly full. */
 assert(AppleAgxG3PrivateAllocate(&st.PrivatePool,1,24*0x10000u,&fill[0]));
 AppleAgxG3PrivatePoolStats(&st.PrivatePool,1,&failure);
 assert(failure.GlobalUnits==1024 && !failure.LargestFreeUnits && failure.OwnerUnits==24);
 assert(!AppleAgxG3PrivateAllocate(&st.PrivatePool,1,0x10000u,&wanted));
 p[1].Graph.JobInFlight=1;p[2].Graph.LeaseToken=1;p[3].Poisoned=1;s[4].Queued=1;
 p[0].Lock.Held=1; /* the requester's escape holds its own lock */
 /* L2: an owner busy in its own parse is skipped, never awaited. */
 p[5].Lock.Held=1;
 assert(!AdmissionG3PrivateEvictPressure(&p[0],&view,0x10000u,&failure));
 assert(!s[5].Released && p[5].Lock.Held && !st.PrivatePressure[1]);
 p[5].Lock.Held=0;
 /* Only owner6 is idle and safe; caller owns no cached scene. */
 assert(AdmissionG3PrivateEvictPressure(&p[0],&view,0x10000u,&failure));
 assert(!p[5].Lock.Held);
 assert(s[5].Released && !s[1].Released && !s[2].Released && !s[3].Released && !s[4].Released);
 assert(AppleAgxG3PrivateAllocate(&st.PrivatePool,1,0x10000u,&wanted));
 assert(st.PrivatePressure[1]==1 && st.PrivatePressure[2]==100 && st.PrivatePressure[3]==105);
 /* No foreign eviction for quota refusal, even if other cache is safe. */
 p[1].Graph.JobInFlight=0; failure.OwnerUnits=APPLE_AGX_G3_PROCESS_UNITS;
 assert(!AdmissionG3PrivateEvictPressure(&p[0],&view,0x10000u,&failure));assert(!s[1].Released);
 failure.OwnerUnits=25;
 /* An allocator hard size refusal is not physical pressure. */
 assert(!AdmissionG3PrivateEvictPressure(&p[0],&view,9u<<20,&failure));assert(!s[1].Released);
 refuse_release=1;
 assert(!AdmissionG3PrivateEvictPressure(&p[0],&view,0x10000u,&failure));assert(!s[1].Released);
 refuse_release=0;p[1].Graph.Uncertain=1;
 assert(!AdmissionG3PrivateEvictPressure(&p[0],&view,0x10000u,&failure));
 /* Contiguous allocation requires more than the remaining19-unit run.
  * Reclaim one85-unit scene, then the32-unit request must succeed. */
 p[1].Graph.Uncertain=0;
 AppleAgxG3PrivatePoolStats(&st.PrivatePool,1,&failure);
 assert(failure.LargestFreeUnits==19 && !AppleAgxG3PrivateAllocate(&st.PrivatePool,1,32*0x10000u,&wanted));
 assert(AdmissionG3PrivateEvictPressure(&p[0],&view,32*0x10000u,&failure));
 assert(s[1].Released && AppleAgxG3PrivateAllocate(&st.PrivatePool,1,32*0x10000u,&wanted));
 /* Quarantine, submission and nonexistent graph cannot be reclaimed. */
 p[2].Graph.LeaseToken=0;s[2].Submitting=1;s[4].Queued=0;s[4].Quarantined=1;
 failure.LargestFreeUnits=0;
 assert(!AdmissionG3PrivateEvictPressure(&p[0],&view,0x10000u,&failure));
 s[2].Submitting=0;p[2].Graph.Created=0;
 assert(!AdmissionG3PrivateEvictPressure(&p[0],&view,0x10000u,&failure));
 puts("PASS");
}
'''

class GlobalPrivatePressure(unittest.TestCase):
    def test_full_pool_reclaims_foreign_cache_without_violating_owner_holds(self):
        text = SRC.read_text()
        funcs = function(text, 'AdmissionG3PrivateEvictCached')
        pressure = function(text, 'AdmissionG3PrivateEvictPressure')
        if not pressure:
            pressure = '''static BOOLEAN AdmissionG3PrivateEvictPressure(ADMISSION_G3_PROCESS *p,
 ADMISSION_BACKEND_MEMORY_VIEW *v,UINT bytes,const APPLE_AGX_G3_PRIVATE_POOL_STATS *s) {
 (void)bytes;(void)s;return AdmissionG3PrivateEvictCached(p,v); }'''
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp)/'pressure.c'; exe = Path(tmp)/'pressure'
            source.write_text(PROGRAM.replace('@@FUNCTIONS@@', funcs+'\n'+pressure))
            build = subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I',str(ROOT/'drivers/apple-agx/shared/include'),
                str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            run = subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
