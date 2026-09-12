#include "umd_asahi_owner.h"
static ADMISSION_UMD_DEVICE PoolDevice;
static ADMISSION_UMD_ADAPTER PoolAdapter;
static D3DDDI_DEVICECALLBACKS PoolCallbacks;
static void *PoolMemory;
static unsigned PoolCreates,PoolMaps,PoolUnlocks,PoolDeletes,PoolErrors;
static int PoolFailAllocation;
static unsigned PoolFailDeallocation;
static HRESULT APIENTRY PoolAllocate(HANDLE h,D3DDDICB_ALLOCATE *a) {
  const ADMISSION_WIN32_ALLOCATION_CREATE *desc=a->pAllocationInfo->pPrivateDriverData;
  (void)h;
  if(PoolFailAllocation) return E_OUTOFMEMORY;
  if(PoolMemory || a->NumAllocations!=1 || desc->Allocation.Size!=0x40000 ||
     desc->ClassId!=AgxWin32BufferClassEncoder) return E_INVALIDARG;
  PoolMemory=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)desc->Allocation.Size);
  if(!PoolMemory) return E_OUTOFMEMORY;
  ++PoolCreates; a->pAllocationInfo->hAllocation=0x701;
  /* Allocation must already reserve the UMD slot against callback reentry. */
  if(AdmissionUmdScreenBeginClose(&PoolDevice)!=HRESULT_FROM_WIN32(ERROR_BUSY)) ++PoolErrors;
  return S_OK;
}
static HRESULT APIENTRY PoolLock(HANDLE h,D3DDDICB_LOCK *a) {
  (void)h;
  if(a->hAllocation!=0x701 || !PoolMemory) return E_INVALIDARG;
  a->pData=PoolMemory; ++PoolMaps; return S_OK;
}
static HRESULT APIENTRY PoolUnlock(HANDLE h,const D3DDDICB_UNLOCK *a) {
  (void)h; (void)a; ++PoolUnlocks; return S_OK;
}
static HRESULT APIENTRY PoolDeallocate(HANDLE h,const D3DDDICB_DEALLOCATE *a) {
  (void)h;
  if(a->NumAllocations!=1 || a->HandleList[0]!=0x701 || !PoolMemory) return E_INVALIDARG;
  if(PoolFailDeallocation) { --PoolFailDeallocation; return E_FAIL; }
  HeapFree(GetProcessHeap(),0,PoolMemory); PoolMemory=NULL; ++PoolDeletes; return S_OK;
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
static unsigned TestAsahiNativePoolOwner(void) {
  AGX_WIN32_ASAHI_BACKEND backend={0};
  AGX_WIN32_ASAHI_OWNER_OPS ops;
  ADMISSION_UMD_ASAHI_OWNER owner={&PoolDevice,&backend};
  memset(&PoolDevice,0,sizeof(PoolDevice)); memset(&PoolAdapter,0,sizeof(PoolAdapter));
  memset(&PoolCallbacks,0,sizeof(PoolCallbacks));
  PoolErrors=PoolCreates=PoolMaps=PoolUnlocks=PoolDeletes=0;
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
  if(PoolCreates!=1 || PoolMaps!=1 || PoolUnlocks!=1 || PoolDeletes!=1 || PoolMemory || PoolDevice.NativeBackendCount)
    ++PoolErrors;
  return PoolErrors;
}
