#include <assert.h>
#include <string.h>
#include "apple_agx_render_shared_memory.h"
#include "apple_agx_g13_queue_provider.h"
#define RTL_NUMBER_OF(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {
  APPLE_AGX_RENDER_MANAGER_STATE *G4Manager;
  APPLE_AGX_RENDER_MANAGER_KEY G4ManagerKey;
  unsigned G4Native,BoundFence;
} ADMISSION_BACKEND_IMAGE;
typedef struct { ADMISSION_BACKEND_IMAGE BackendImage; } ADAPTER;
typedef struct {
  ADAPTER *Adapter;
  struct { APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory; } Initdata;
  struct { APPLE_AGX_G13_QUEUE_PROVIDER QueueProvider; } Provider;
  struct { unsigned TaComplete,D3Complete; } Backend;
  struct {
    unsigned char (*FlushForCpu)(void *,const void *,unsigned);
    void (*MemoryBarrier)(void *);
  } TransportIo;
  APPLE_AGX_EXP208_RELOCATION_OBJECT QueueObjects[76];
} ADMISSION_PLATFORM_RUNTIME;
static unsigned flushes,fail_flush,barriers;
static unsigned char Flush(void *c,const void *p,unsigned n) {
  assert(c && p && n);return ++flushes!=fail_flush;
}
static void Barrier(void *c) { assert(c);++barriers; }
@PREPARE@
@SAVE@
int main(void) {
  ADMISSION_PLATFORM_RUNTIME runtime={0};ADAPTER adapter={0};
  APPLE_AGX_RENDER_MANAGER_STATE state={0};
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
  assert(!AdmissionSaveG4Manager(&runtime,77u));
  runtime.Backend.TaComplete=1;
  assert(!AdmissionSaveG4Manager(&runtime,77u));
  assert(flushes==0 && !state.Valid && state.PendingFence==77u);
  runtime.Backend.D3Complete=1;
  assert(!AdmissionSaveG4Manager(&runtime,78u));
  fail_flush=2;
  assert(!AdmissionSaveG4Manager(&runtime,77u));
  assert(!state.Valid && state.PendingFence==77u && barriers==0);
  flushes=0;fail_flush=0;
  assert(AdmissionSaveG4Manager(&runtime,77u));
  assert(flushes==4 && barriers==1 && state.Valid && state.Info[0]==42u);
  assert(!state.PendingFence && !runtime.Initdata.RenderSharedMemory.ManagerFence);
  assert(AdmissionSaveG4Manager(&runtime,77u)); /* completion retry */
  return 0;
}
