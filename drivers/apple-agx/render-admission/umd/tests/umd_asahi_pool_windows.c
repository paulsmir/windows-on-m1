#include "umd_asahi_owner.h"
static ADMISSION_UMD_DEVICE PoolDevice;
static ADMISSION_UMD_ADAPTER PoolAdapter;
static D3DDDI_DEVICECALLBACKS PoolCallbacks;
static void *PoolMemory[8];
static D3DKMT_HANDLE PoolHandles[8];
static unsigned PoolNextHandle;
static unsigned PoolCreates,PoolMaps,PoolUnlocks,PoolDeletes,PoolErrors;
static int PoolFailAllocation;
static unsigned PoolFailDeallocation;
static HRESULT APIENTRY PoolAllocate(HANDLE h,D3DDDICB_ALLOCATE *a) {
  const ADMISSION_WIN32_ALLOCATION_CREATE *desc=a->pAllocationInfo->pPrivateDriverData;
  (void)h;
  if(PoolFailAllocation) return E_OUTOFMEMORY;
  if(a->NumAllocations!=1 || !desc->Allocation.Size || desc->Allocation.Size>0x100000 ||
     (desc->Allocation.Size&0x3fff) || desc->ClassId<AgxWin32BufferClassGeneral ||
     desc->ClassId>AgxWin32BufferClassEncoder) return E_INVALIDARG;
  unsigned slot;
  for(slot=0;slot<8 && PoolMemory[slot];++slot) {}
  if(slot==8) return E_OUTOFMEMORY;
  PoolMemory[slot]=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)desc->Allocation.Size);
  if(!PoolMemory[slot]) return E_OUTOFMEMORY;
  ++PoolCreates; PoolHandles[slot]=0x700+(++PoolNextHandle);
  a->pAllocationInfo->hAllocation=PoolHandles[slot];
  /* Allocation must already reserve the UMD slot against callback reentry. */
  if(AdmissionUmdScreenBeginClose(&PoolDevice)!=HRESULT_FROM_WIN32(ERROR_BUSY)) ++PoolErrors;
  return S_OK;
}
static HRESULT APIENTRY PoolLock(HANDLE h,D3DDDICB_LOCK *a) {
  (void)h;
  for(unsigned slot=0;slot<8;++slot) if(PoolMemory[slot] && a->hAllocation==PoolHandles[slot]) {
    a->pData=PoolMemory[slot]; ++PoolMaps; return S_OK;
  }
  return E_INVALIDARG;
}
static HRESULT APIENTRY PoolUnlock(HANDLE h,const D3DDDICB_UNLOCK *a) {
  (void)h; (void)a; ++PoolUnlocks; return S_OK;
}
static HRESULT APIENTRY PoolDeallocate(HANDLE h,const D3DDDICB_DEALLOCATE *a) {
  (void)h;
  if(a->NumAllocations!=1) return E_INVALIDARG;
  unsigned slot;
  for(slot=0;slot<8;++slot) if(PoolMemory[slot] && a->HandleList[0]==PoolHandles[slot]) break;
  if(slot==8) return E_INVALIDARG;
  if(PoolFailDeallocation) { --PoolFailDeallocation; return E_FAIL; }
  HeapFree(GetProcessHeap(),0,PoolMemory[slot]); PoolMemory[slot]=NULL; ++PoolDeletes; return S_OK;
}
static void PoolHolds(void *context,int active) {
  ADMISSION_UMD_ASAHI_OWNER *c=context;
  if(active==2) { PoolFailAllocation=1; return; }
  if(active==3) { PoolFailAllocation=0; return; }
  if(active==4) { PoolFailDeallocation=1; return; }
  AcquireSRWLockExclusive(&c->Device->ScreenBufferLock);
  for(unsigned i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b=&c->Device->ScreenBuffers[i];
    if(b->Active && b->NativeBackend==c->Backend) b->SubmissionHolds=active?1:0;
  }
  ReleaseSRWLockExclusive(&c->Device->ScreenBufferLock);
}
unsigned AgxWin32AsahiPoolTest(AGX_WIN32_SCREEN *,const AGX_WIN32_ASAHI_OWNER_OPS *,
    void *,AGX_WIN32_ASAHI_BACKEND *,void (*)(void *,int));
unsigned AgxWin32AsahiPipelineTest(AGX_WIN32_SCREEN *,const AGX_WIN32_ASAHI_OWNER_OPS *,
    void *,AGX_WIN32_ASAHI_BACKEND *);
unsigned AgxWin32AsahiStateDirtyZeroTest(AGX_WIN32_SCREEN *,
    const AGX_WIN32_ASAHI_OWNER_OPS *,void *,AGX_WIN32_ASAHI_BACKEND *);
static unsigned TestAsahiNativePoolOwner(void) {
  AGX_WIN32_ASAHI_BACKEND backend={0};
  AGX_WIN32_ASAHI_OWNER_OPS ops;
  ADMISSION_UMD_ASAHI_OWNER owner={&PoolDevice,&backend};
  memset(&PoolDevice,0,sizeof(PoolDevice)); memset(&PoolAdapter,0,sizeof(PoolAdapter));
  memset(&PoolCallbacks,0,sizeof(PoolCallbacks));
  PoolErrors=PoolCreates=PoolMaps=PoolUnlocks=PoolDeletes=0;
  PoolNextHandle=0; memset(PoolMemory,0,sizeof(PoolMemory)); memset(PoolHandles,0,sizeof(PoolHandles));
  PoolFailAllocation=0;
  PoolFailDeallocation=0;
  PoolDevice.Magic=ADMISSION_UMD_DEVICE_MAGIC; PoolDevice.Win32Generation=27;
  PoolDevice.Adapter=&PoolAdapter; PoolDevice.KernelCallbacks=&PoolCallbacks;
  PoolAdapter.DeviceInfo.Magic=AGX_WIN32_DEVICE_INFO_MAGIC;
  PoolAdapter.DeviceInfo.Version=AGX_WIN32_DEVICE_INFO_VERSION;
  PoolAdapter.DeviceInfo.Bytes=sizeof(PoolAdapter.DeviceInfo);
  PoolAdapter.DeviceInfo.BootGeneration=9; PoolAdapter.DeviceInfo.GpuGeneration=13;
  PoolAdapter.DeviceInfo.GpuVariant=AgxWin32GpuG13G;
  PoolAdapter.DeviceInfo.PageBytes=0x4000; PoolAdapter.DeviceInfo.ClassCount=3;
  for(unsigned i=0;i<3;++i) {
    PoolAdapter.DeviceInfo.Classes[i].ClassId=i+1;
    PoolAdapter.DeviceInfo.Classes[i].MinimumAlignment=0x4000;
    PoolAdapter.DeviceInfo.Classes[i].MaximumBytes=0x100000;
    PoolAdapter.DeviceInfo.Classes[i].Flags=i==0?0xf:0x6;
  }
  PoolCallbacks.pfnAllocateCb=PoolAllocate; PoolCallbacks.pfnLockCb=PoolLock;
  PoolCallbacks.pfnUnlockCb=PoolUnlock; PoolCallbacks.pfnDeallocateCb=PoolDeallocate;
  if(AdmissionUmdScreenInitialize(&PoolDevice)!=S_OK) return 1;
  AdmissionUmdAsahiOwnerOperations(&ops);
  PoolErrors+=AgxWin32AsahiPoolTest(&PoolDevice.Screen,&ops,&owner,&backend,PoolHolds);
  PoolErrors+=AgxWin32AsahiPipelineTest(&PoolDevice.Screen,&ops,&owner,&backend);
  /* Original pool/pipeline seven plus dedicated root and reserve BOs. */
  unsigned expectedCreates=9;
#ifdef ADMISSION_UMD_NATIVE_STATE_TEST
  /* Opt-in until the real state emitter runtime closure is linked. */
  PoolErrors+=AgxWin32AsahiStateDirtyZeroTest(&PoolDevice.Screen,&ops,&owner,&backend);
  ++expectedCreates;
#endif
  printf("NATIVE_OWNER_BALANCE: creates=%u maps=%u unlocks=%u deletes=%u expected=%u backends=%u\n",
      PoolCreates,PoolMaps,PoolUnlocks,PoolDeletes,expectedCreates,PoolDevice.NativeBackendCount);
  if(PoolCreates!=expectedCreates || PoolMaps!=expectedCreates ||
     PoolUnlocks!=expectedCreates || PoolDeletes!=expectedCreates || PoolDevice.NativeBackendCount)
    ++PoolErrors;
  for(unsigned i=0;i<8;++i) if(PoolMemory[i]) ++PoolErrors;
  return PoolErrors;
}
