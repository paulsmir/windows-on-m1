#include <assert.h>
#include <string.h>
#include <stddef.h>
#include "apple_agx_render_shared_memory.h"
#include "apple_agx_g13_queue_provider.h"
typedef unsigned char BOOLEAN;
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
static unsigned KeGetCurrentIrql(void) { return PASSIVE_LEVEL; }
typedef struct _LIST_ENTRY { struct _LIST_ENTRY *Flink, *Blink; } LIST_ENTRY, *PLIST_ENTRY;
#define CONTAINING_RECORD(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
static void ExAcquireFastMutex(unsigned *lock) { assert(!*lock); *lock=1; }
static void ExReleaseFastMutex(unsigned *lock) { assert(*lock); *lock=0; }
typedef struct { unsigned Lock; LIST_ENTRY Processes; } ADMISSION_G3_STATE;
typedef struct { LIST_ENTRY Link; struct { unsigned long long ProcessId; } Graph;
    APPLE_AGX_RENDER_MANAGER_STATE FirmwareManager; } ADMISSION_G3_PROCESS;
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {
  APPLE_AGX_RENDER_MANAGER_STATE *G4Manager;
  APPLE_AGX_RENDER_MANAGER_KEY G4ManagerKey;
  unsigned G4Native,BoundFence;
} ADMISSION_BACKEND_IMAGE;
typedef struct { ADMISSION_BACKEND_IMAGE BackendImage; void *GpuvaG3State; } ADAPTER;
typedef ADAPTER ADMISSION_CONTEXT;
typedef struct {
  ADAPTER *Adapter;
  struct { APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory; } Initdata;
  struct { APPLE_AGX_G13_QUEUE_PROVIDER QueueProvider; } Provider;
  APPLE_AGX_BACKEND_RUNTIME Backend;
  struct {
    unsigned char (*FlushForCpu)(void *,const void *,unsigned);
    void (*MemoryBarrier)(void *);
  } TransportIo;
  APPLE_AGX_EXP208_RELOCATION_OBJECT QueueObjects[76];
  unsigned AheadReleasedFence;
  APPLE_AGX_RENDER_MANAGER_STATE *AheadManager;
  BOOLEAN AheadG4Native, ManagerSlots;
  unsigned char BuildDirectReads; /* rc83 Wom1BuildDirectReads */
  unsigned SavedStatsFence; unsigned char *SavedStatsData; /* K1: unset here */
} ADMISSION_PLATFORM_RUNTIME;
/* rc83 receipt v3: the save's sub-interval stamp */
#define AdmissionJobSubSaved 6
static unsigned saved_stamps;
static void AdmissionJobTimingSubWindows(ADMISSION_PLATFORM_RUNTIME *r, unsigned f, int s) {
  assert(r && f && s == AdmissionJobSubSaved); ++saved_stamps; }
static unsigned flushes,fail_flush,barriers;
static unsigned char Flush(void *c,const void *p,unsigned n) {
  assert(c && p && n);return ++flushes!=fail_flush;
}
static void Barrier(void *c) { assert(c);++barriers; }
@STORE@
@EVICT@
@CANCEL@
@PREPARE@
@SAVE@
int main(void) {
  ADMISSION_PLATFORM_RUNTIME runtime={0};ADAPTER adapter={0};
  ADMISSION_G3_STATE registry={0};
  ADMISSION_G3_PROCESS process={0};
#define state process.FirmwareManager
  registry.Processes.Flink=registry.Processes.Blink=&process.Link;
  process.Link.Flink=process.Link.Blink=&registry.Processes;
  process.Graph.ProcessId=7;
  adapter.GpuvaG3State=&registry;
  unsigned char storage[4][188]={{0}};
  unsigned indices[]={1u,20u,21u,22u};
  runtime.Adapter=&adapter;
  runtime.TransportIo.FlushForCpu=Flush;runtime.TransportIo.MemoryBarrier=Barrier;
  assert(AdmissionPrepareG4Manager(&runtime)); /* legacy */
  adapter.BackendImage.G4Manager=&state;
  adapter.BackendImage.G4Native=1;
  adapter.BackendImage.G4ManagerKey.Owner=7;
  adapter.BackendImage.G4ManagerKey.Generation=11;
  adapter.BackendImage.G4ManagerKey.RootIpa=0x20000000ULL;
  runtime.Provider.QueueProvider.Runtime.BufferManagerInitialized=1;
  assert(!AdmissionPrepareG4Manager(&runtime)); /* not created */
  assert(runtime.Provider.QueueProvider.Runtime.BufferManagerInitialized);
  runtime.Provider.QueueProvider.Phase=AppleAgxG13QueueProviderCreated;
  assert(AdmissionPrepareG4Manager(&runtime));
  assert(!runtime.Provider.QueueProvider.Runtime.BufferManagerInitialized);
  state.PendingFence=77u;
  assert(!AdmissionPrepareG4Manager(&runtime));
  runtime.Initdata.RenderSharedMemory.ManagerFence=77u;
  runtime.Initdata.RenderSharedMemory.ManagerKey=adapter.BackendImage.G4ManagerKey;
  adapter.BackendImage.BoundFence=77u;
  for(unsigned i=0;i<4u;++i) {
    runtime.QueueObjects[indices[i]].Data=storage[i];
    runtime.QueueObjects[indices[i]].Size=i?64u:188u;
  }
  storage[0][0]=42u;
  assert(!AdmissionSaveG4Manager(&runtime,77u,AppleAgxBackendCompletionSuccess));
  runtime.Backend.TaComplete=1;
  assert(!AdmissionSaveG4Manager(&runtime,77u,AppleAgxBackendCompletionSuccess));
  assert(flushes==0 && !state.Valid && state.PendingFence==77u);
  runtime.Backend.D3Complete=1;
  assert(!AdmissionSaveG4Manager(&runtime,78u,AppleAgxBackendCompletionSuccess));
  fail_flush=2;
  assert(!AdmissionSaveG4Manager(&runtime,77u,AppleAgxBackendCompletionSuccess));
  assert(!state.Valid && state.PendingFence==77u && barriers==0);
  flushes=0;fail_flush=0;
  assert(AdmissionSaveG4Manager(&runtime,77u,AppleAgxBackendCompletionSuccess));
  assert(flushes==4 && barriers==1 && state.Valid && state.Info[0]==42u);
  assert(!state.PendingFence && !runtime.Initdata.RenderSharedMemory.ManagerFence);
  assert(AdmissionSaveG4Manager(&runtime,77u,AppleAgxBackendCompletionSuccess)); /* completion retry */
  /* q1: only Stats is synchronized/copied on the hot completion path. */
  runtime.ManagerSlots=1;
  runtime.Initdata.RenderSharedMemory.ManagerSlotCount=4;
  runtime.Initdata.RenderSharedMemory.ManagerSaveOnEvict=1;
  runtime.Initdata.RenderSharedMemory.ManagerEvict=AdmissionEvictG4Manager;
  runtime.Initdata.RenderSharedMemory.ManagerEvictContext=&runtime;
  runtime.Initdata.RenderSharedMemory.ManagerSlot=1;
  state.PendingFence=88;
  runtime.Initdata.RenderSharedMemory.ManagerFence=88;
  adapter.BackendImage.BoundFence=88;
  storage[0][0]=99; storage[3][0]=81; flushes=barriers=0;
  assert(AdmissionSaveG4Manager(&runtime,88u,AppleAgxBackendCompletionSuccess));
  assert(state.Resident && state.Info[0]==42 && state.Stats[0]==81);
  assert(flushes==1 && barriers==1);
  fail_flush=2; flushes=barriers=0;
  assert(!AdmissionEvictG4Manager(&runtime,&state.Key,88,runtime.QueueObjects));
  assert(state.Resident && state.Info[0]==42 && barriers==0);
  fail_flush=0; flushes=0;
  assert(AdmissionEvictG4Manager(&runtime,&state.Key,88,runtime.QueueObjects));
  assert(!state.Resident && state.Info[0]==99 && flushes==4 && barriers==1);
  /* A built job refused before atomic Run3d admission leaves Ready, with
   * no accepted provider fence. A failed RunTa is Failed/uncertain. */
  state.PendingFence=98;
  runtime.Initdata.RenderSharedMemory.ManagerFence=98;
  runtime.Initdata.RenderSharedMemory.ManagerSlotLive=0;
  runtime.Initdata.RenderSharedMemory.SlotBound[1]=1;
  runtime.Backend.Phase=AppleAgxBackendRuntimeFailed;
  assert(!AdmissionCancelUnpublishedManager(&runtime,98));
  assert(state.PendingFence==98);
  runtime.Backend.Phase=AppleAgxBackendRuntimeReady;
  runtime.Provider.QueueProvider.PendingFence=98;
  assert(!AdmissionCancelUnpublishedManager(&runtime,98));
  runtime.Provider.QueueProvider.PendingFence=0;
  {
    FAKE_BACKEND fake={0};
    APPLE_AGX_BACKEND_IO io=BackendIo(&fake);
    APPLE_AGX_BACKEND_SUBMISSION submission=Submission(&fake);
    submission.Submission.Fence=98;
    AppleAgxBackendRuntimeInitialize(&runtime.Backend,3);
    assert(AppleAgxBackendRuntimeStart(&runtime.Backend,&io)==AppleAgxBackendRuntimeResultOk);
    fake.FailRun3d=1;
    assert(AppleAgxBackendRuntimeSubmit(&runtime.Backend,&submission)==AppleAgxBackendRuntimeResultQueueFailed);
    assert(state.PendingFence==98 && runtime.Initdata.RenderSharedMemory.ManagerFence==98);
    assert(AdmissionCancelUnpublishedManager(&runtime,98));
    assert(AppleAgxBackendRuntimeStop(&runtime.Backend)==AppleAgxBackendRuntimeResultOk);
  }
  runtime.Backend.TaComplete=runtime.Backend.D3Complete=1;
  assert(!state.PendingFence && !runtime.Initdata.RenderSharedMemory.ManagerFence);
  assert(state.Valid && !state.Resident && state.Info[0]==99);
  /* Recovered/dropped work sets both completion flags in BackendRuntime.
   * It must retire without making partially executed manager state valid. */
  state.PendingFence=99;
  runtime.Initdata.RenderSharedMemory.ManagerFence=99;
  runtime.Initdata.RenderSharedMemory.SlotBound[1]=1;
  runtime.Initdata.RenderSharedMemory.SlotDirtyFence[1]=88;
  adapter.BackendImage.BoundFence=99;
  flushes=0;
  assert(AdmissionSaveG4Manager(&runtime,99,AppleAgxBackendCompletionDropped));
  assert(!state.Valid && !state.Resident && !state.PendingFence);
  assert(!runtime.Initdata.RenderSharedMemory.ManagerFence && !flushes);
  assert(!runtime.Initdata.RenderSharedMemory.SlotBound[1]);
  assert(!runtime.Initdata.RenderSharedMemory.SlotDirtyFence[1]);
  assert(AdmissionSaveG4Manager(&runtime,99,AppleAgxBackendCompletionDropped));
  /* Remove the process: eviction resolves by key and does not dereference
   * the removed entry, nor store a snapshot into another owner's state. */
  registry.Processes.Flink=registry.Processes.Blink=&registry.Processes;
  state.Resident=1; state.Info[0]=17;
  assert(AdmissionEvictG4Manager(&runtime,&state.Key,88,runtime.QueueObjects));
  assert(state.Resident && state.Info[0]==17 && !registry.Lock);
  adapter.GpuvaG3State=NULL;
  assert(AdmissionEvictG4Manager(&runtime,&state.Key,88,runtime.QueueObjects));
  return 0;
}
