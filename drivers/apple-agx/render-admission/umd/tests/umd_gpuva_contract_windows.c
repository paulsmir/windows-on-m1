#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include "../src/umd_internal.h"
#include "apple_agx_g3_copy_abi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static UINT submit_diagnostics, signal_diagnostics;
VOID AdmissionUmdDiagnostic(PCSTR stage, HRESULT status,
                            const UINT *values, UINT count) {
  if (strcmp(stage, "g4-submit-command-cb") == 0) {
    assert(status == S_OK && values != NULL && count == 4u);
    ++submit_diagnostics;
  } else if (strcmp(stage, "g4-signal-render-fence") == 0) {
    assert(status == S_OK && values == NULL && count == 0u);
    ++signal_diagnostics;
  }
}

typedef struct {
  UINT reserve, map, wait_map, resident, wait_resident;
  UINT submit, signal, wait_render, evict, free_va;
  UINT64 next_va;
  UINT failed;
  UINT uploads, downloads, locks, unlocks;
  UINT Failure, Persistent, DisplayableDirect, Uncached, Round;
  BYTE staging[2][131072], canonical[2][131072];
} G4_FIXTURE;

static HRESULT APIENTRY TestReserve(HANDLE handle,
    D3DDDI_RESERVEGPUVIRTUALADDRESS *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(request->Size != 0x20000 || request->MinimumAddress != 0x10000 ||
     request->MaximumAddress != (1ULL << 39)) f->failed = 1;
  request->VirtualAddress = f->next_va;
  f->next_va += 0x20000;
  ++f->reserve;
  return S_OK;
}
static HRESULT APIENTRY TestMap(HANDLE handle,
    D3DDDI_MAPGPUVIRTUALADDRESS *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(request->hPagingQueue != 1 || request->SizeInPages != 32 ||
     request->OffsetInPages != 0 || request->Protection.Write != 1 ||
     request->hAllocation != (f->map ? 100u : 99u)) f->failed = 1;
  request->VirtualAddress = request->BaseAddress;
  request->PagingFenceValue = 3;
  ++f->map;
  return E_PENDING;
}
static HRESULT APIENTRY TestFree(HANDLE handle,
    const D3DDDICB_FREEGPUVIRTUALADDRESS *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(request->Size != 0x20000 || f->evict != 2) f->failed = 1;
  ++f->free_va;
  return S_OK;
}
static HRESULT APIENTRY TestResident(HANDLE handle,
    D3DDDI_MAKERESIDENT *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(f->wait_map != 2 || request->hPagingQueue != 1 ||
     request->NumAllocations != 2 || request->AllocationList[0] != 99 ||
     request->AllocationList[1] != 100) f->failed = 1;
  request->PagingFenceValue = 5;
  ++f->resident;
  return E_PENDING;
}
static HRESULT APIENTRY TestWait(HANDLE handle,
    const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(request->ObjectCount != 1) f->failed = 1;
  if(request->ObjectHandleArray[0] == 2 &&
     request->FenceValueArray[0] == 3) ++f->wait_map;
  else if(request->ObjectHandleArray[0] == 2 &&
          request->FenceValueArray[0] == 5) ++f->wait_resident;
  else if(request->ObjectHandleArray[0] == 4 &&
          (request->FenceValueArray[0] == 2*f->Round+1 || request->FenceValueArray[0] == 2*f->Round+2)) ++f->wait_render;
  else f->failed = 1;
  if(f->Failure==1 && f->wait_render) return E_FAIL;
  return S_OK;
}
static HRESULT APIENTRY TestSubmit(HANDLE handle,
    const D3DDDICB_SUBMITCOMMAND *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  const BYTE *data = (const BYTE *)request->pPrivateDriverData;
  if(f->DisplayableDirect && request->NumPrimaries != 1u)
    fprintf(stderr,"WRITTEN_PRIMARY_RED expected=1 actual=%u\n",request->NumPrimaries);
  if(f->wait_resident != f->Round+1 || request->Commands != 0x20000 ||
     request->CommandLength != 64 || request->BroadcastContextCount != 1 ||
     request->BroadcastContext[0] != (HANDLE)3 ||
     request->NumPrimaries != f->DisplayableDirect ||
     (f->DisplayableDirect && request->WrittenPrimaries[0] != 100) ||
     f->uploads != (f->DisplayableDirect ? 2 : 4)*(f->Round+1) ||
     f->downloads != (f->DisplayableDirect ? 0 : 2*f->Round) ||
     request->PrivateDriverDataSize != 4 ||
     data[0] != 0xa1 || data[3] != 0xd4) f->failed = 1;
  assert(f->canonical[0][19] == 0x71 && f->canonical[1][53] == (f->Round ? 0xa5 : 0x29));
  if(f->Round) assert(f->canonical[1][54]==0xf3 && f->canonical[1][55]==0x78 && f->canonical[1][98316]==0x58);
  f->canonical[1][53]=0xa5; f->canonical[1][54]=0xf3; f->canonical[1][98316]=0x58;
  ++f->submit;
  return S_OK;
}
static HRESULT APIENTRY TestSignal(HANDLE handle,
    const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(f->submit != f->Round+1 || request->ObjectCount != 1 ||
     request->ObjectHandleArray[0] != 4 ||
     request->MonitoredFenceValueArray[0] != f->signal + 1) f->failed = 1;
  if(f->signal%2) assert(f->downloads == (f->DisplayableDirect ? 0 : 2*(f->Round+1)) &&
      f->unlocks == (f->DisplayableDirect ? f->Round+1 : (f->Persistent ? 2u : 3u)*(f->Round+1)) &&
      (f->DisplayableDirect ? f->staging[1][53] == 0x29 : f->staging[1][53] == 0xa5));
  ++f->signal;
  return S_OK;
}
static HRESULT APIENTRY TestEvict(HANDLE handle,D3DDDICB_EVICT *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(f->wait_render != 2*(f->Round+1) || request->NumAllocations != 2 ||
     request->AllocationList[0] != 99 || request->AllocationList[1] != 100)
    f->failed = 1;
  ++f->evict;
  return S_OK;
}

static HRESULT APIENTRY CopyLock(HANDLE handle,D3DDDICB_LOCK *q) {
  G4_FIXTURE *f=(G4_FIXTURE *)handle;
  assert(q->hAllocation == 199 || q->hAllocation == 200);
  q->pData=f->staging[q->hAllocation-199]; ++f->locks; return S_OK;
}
static HRESULT APIENTRY CopyUnlock(HANDLE handle,const D3DDDICB_UNLOCK *q) {
  G4_FIXTURE *f=(G4_FIXTURE *)handle;
  assert(q->NumAllocations == 1 && (q->phAllocations[0] == 199 || q->phAllocations[0] == 200));
  if(f->Failure==3 && f->downloads) return E_FAIL;
  ++f->unlocks; return S_OK;
}
static HRESULT APIENTRY CopyEscape(HANDLE adapter,const D3DDDICB_ESCAPE *q) {
  G4_FIXTURE *f=(G4_FIXTURE *)adapter;
  assert(q->hDevice==adapter && q->hContext==(HANDLE)3 && q->Flags.Value==0);
  auto *p=(APPLE_AGX_G3_COPY_REQUEST *)q->pPrivateDriverData;
  assert(q->PrivateDriverDataSize==sizeof(*p) && p->Magic==APPLE_AGX_G3_COPY_MAGIC);
  assert(p->Allocation==99 || p->Allocation==100);
  UINT i=p->Allocation-99;
  assert(p->GpuVa==(i ? 0x40000ULL : 0x20000ULL));
  assert(f->wait_resident==f->Round+1);
  if(p->Operation==APPLE_AGX_G3_COPY_QUERY) {
    assert(!p->Offset && !p->TransferBytes && !p->ProcessGeneration && !p->MappingGeneration);
    p->ProcessGeneration=13; p->MappingGeneration=27; return S_OK;
  }
  assert(p->ProcessGeneration==13 && p->MappingGeneration==27);
  assert((p->Offset==0 && p->TransferBytes==65536) || (p->Offset==65536 && p->TransferBytes==32781));
  if(p->Operation==APPLE_AGX_G3_COPY_UPLOAD) {
    assert(f->submit==f->Round);memcpy(f->canonical[i]+p->Offset,p->Data,p->TransferBytes);++f->uploads;
  } else {
    assert(p->Operation==APPLE_AGX_G3_COPY_DOWNLOAD && f->wait_render==2*f->Round+1 && f->signal==2*f->Round+1);
    if(f->Failure==2) return E_FAIL;
    memcpy(p->Data,f->canonical[i]+p->Offset,p->TransferBytes);++f->downloads;
  }
  return S_OK;
}

static int ReleaseNativeMap(const void *key,const void *address,int commit) {
  auto *f=(G4_FIXTURE *)key;
  assert(address==f->staging[1]);
  if(commit) ++f->Uncached;
  return 1;
}

/* EscapeCb is adapter-scoped even though it is in DEVICECALLBACKS.
 * EXP855C passed the device as adapter and left request.hDevice NULL. */
static unsigned EscapeCalls;
static HRESULT EscapeResult;
static APPLE_AGX_G3_PRIVATE_REQUEST *EscapePayload;
static HANDLE EscapeAdapter, EscapeDevice, EscapeContext;
static HRESULT APIENTRY TestEscape(HANDLE adapter,
    const D3DDDICB_ESCAPE *request) {
  ++EscapeCalls;
  if(adapter != EscapeAdapter || !request ||
     request->hDevice != EscapeDevice || request->hContext != EscapeContext ||
     request->Flags.Value != 0u || request->pPrivateDriverData != EscapePayload ||
     request->PrivateDriverDataSize != sizeof(*EscapePayload)) {
    fprintf(stderr,"R140 escape adapter/device/context contract violation\n");
    return E_INVALIDARG;
  }
  EscapePayload->SceneId = 73;
  return EscapeResult;
}

static int test_private_escape(ADMISSION_UMD_DEVICE *device) {
  ADMISSION_UMD_ADAPTER adapter = {};
  D3DDDI_DEVICECALLBACKS callbacks = {};
  APPLE_AGX_G3_PRIVATE_REQUEST payload = {};
  const AGX_WIN32_GPUVA_OPS *ops = AdmissionUmdGpuvaOperations();
  int adapterIdentity, deviceIdentity, contextIdentity;
  EscapeCalls=0;
  EscapeAdapter = &adapterIdentity;
  EscapeDevice = &deviceIdentity;
  EscapeContext = &contextIdentity;
  EscapePayload = &payload;
  EscapeResult = S_OK;
  adapter.Magic = ADMISSION_UMD_ADAPTER_MAGIC;
  adapter.RuntimeAdapter.handle = EscapeAdapter;
  device->Magic = ADMISSION_UMD_DEVICE_MAGIC;
  device->Adapter = &adapter;
  device->RuntimeDevice.handle = EscapeDevice;
  device->KernelContext = EscapeContext;
  device->KernelCallbacks = &callbacks;
  callbacks.pfnEscapeCb = TestEscape;
  payload.Magic = APPLE_AGX_G3_PRIVATE_MAGIC;
  payload.Version = APPLE_AGX_G3_PRIVATE_VERSION;
  payload.Bytes = sizeof(payload);
  payload.Operation = APPLE_AGX_G3_PRIVATE_ACQUIRE;
  if(!ops->PrivateEscape(device,&payload) || EscapeCalls != 1 ||
     payload.SceneId != 73) return 0;
  payload.Operation = APPLE_AGX_G3_PRIVATE_RELEASE;
  assert(ops->PrivateEscape(device,&payload) && EscapeCalls == 2);
  EscapeResult = E_FAIL;
  assert(!ops->PrivateEscape(device,&payload) && EscapeCalls == 3);
  EscapeResult = S_OK;
  assert(!ops->PrivateEscape(NULL,&payload));
  assert(!ops->PrivateEscape(device,NULL));
  device->Magic = 0;
  assert(!ops->PrivateEscape(device,&payload));
  device->Magic = ADMISSION_UMD_DEVICE_MAGIC;
  device->Adapter = NULL;
  assert(!ops->PrivateEscape(device,&payload));
  device->Adapter = &adapter;
  adapter.Magic = 0;
  assert(!ops->PrivateEscape(device,&payload));
  adapter.Magic = ADMISSION_UMD_ADAPTER_MAGIC;
  adapter.RuntimeAdapter.handle = NULL;
  assert(!ops->PrivateEscape(device,&payload));
  adapter.RuntimeAdapter.handle = EscapeAdapter;
  device->RuntimeDevice.handle = NULL;
  assert(!ops->PrivateEscape(device,&payload));
  device->RuntimeDevice.handle = EscapeDevice;
  device->KernelContext = NULL;
  assert(!ops->PrivateEscape(device,&payload));
  device->KernelContext = EscapeContext;
  device->KernelCallbacks = NULL;
  assert(!ops->PrivateEscape(device,&payload));
  device->KernelCallbacks = &callbacks;
  callbacks.pfnEscapeCb = NULL;
  assert(!ops->PrivateEscape(device,&payload));
  callbacks.pfnEscapeCb = TestEscape;
  device->ScreenClosing = TRUE;
  assert(!ops->PrivateEscape(device,&payload));
  assert(EscapeCalls == 3);
  ZeroMemory(device,sizeof(*device));
  puts("R140 escape identity/round-trip/failure/guards: PASS");
  return 1;
}

static void run_copy_scenario(UINT failure,UINT persistent,UINT displayableDirect) {
  G4_FIXTURE fixture = {};
  ADMISSION_UMD_ADAPTER adapter = {};
  D3DDDI_DEVICECALLBACKS callbacks = {};
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)HeapAlloc(
      GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*device));
  AGX_WIN32_GPUVA_SPACE space = {};
  AGX_WIN32_GPUVA_BO command = {}, color = {};
  const AGX_WIN32_GPUVA_BO *references[2] = {&command, &color};
  const AGX_WIN32_GPUVA_BO *written[1] = {&color};
  const BYTE native_command[4] = {0xa1, 0xb2, 0xc3, 0xd4};
  UINT64 fence = 0;
  assert(device != NULL);
  assert(test_private_escape(device));
  submit_diagnostics=0;signal_diagnostics=0;
  fixture.Failure=failure;fixture.Persistent=persistent;
  fixture.DisplayableDirect=displayableDirect;
  callbacks.pfnReserveGpuVirtualAddressCb = TestReserve;
  callbacks.pfnMapGpuVirtualAddressCb = TestMap;
  callbacks.pfnFreeGpuVirtualAddressCb = TestFree;
  callbacks.pfnMakeResidentCb = TestResident;
  callbacks.pfnWaitForSynchronizationObjectFromCpuCb = TestWait;
  callbacks.pfnSubmitCommandCb = TestSubmit;
  callbacks.pfnSignalSynchronizationObjectFromGpu2Cb = TestSignal;
  callbacks.pfnEvictCb = TestEvict;
  callbacks.pfnLockCb=CopyLock; callbacks.pfnUnlockCb=CopyUnlock; callbacks.pfnEscapeCb=CopyEscape;
  adapter.Magic=ADMISSION_UMD_ADAPTER_MAGIC;adapter.RuntimeAdapter.handle=&fixture;device->Adapter=&adapter;
  fixture.staging[0][19]=0x71; fixture.staging[1][53]=0x29;
  if(displayableDirect) fixture.canonical[1][53]=0x29;
  fixture.next_va = 0x20000;
  device->Magic = ADMISSION_UMD_DEVICE_MAGIC;
  device->RuntimeDevice.handle = (HANDLE)&fixture;
  device->KernelCallbacks = &callbacks;
  device->KernelContext = (HANDLE)3;
  device->PagingQueue = 1;
  device->PagingSyncObject = 2;
  device->RenderSyncObject = 4;
  device->ScreenBuffers[0].Active = TRUE;
  device->ScreenBuffers[0].Token = 17;
  device->ScreenBuffers[0].KernelAllocation = 99;
  device->ScreenBuffers[1].Active = TRUE;
  device->ScreenBuffers[1].Token = 19;
  device->ScreenBuffers[1].KernelAllocation = 100;
  device->ScreenBuffers[1].WrittenPrimary = TRUE;
  for(UINT i=0;i<2;++i) {device->ScreenBuffers[i].StagingAllocation=199+i;device->ScreenBuffers[i].Bytes=98317;device->ScreenBuffers[i].Flags=i ? 15 : 7;}
  device->ScreenBuffers[1].Borrowed=TRUE;
  if(displayableDirect) {
    device->ScreenBuffers[1].Direct=TRUE;
    device->ScreenBuffers[1].StagingAllocation=0;
  }
  if(persistent) for(UINT i=0;i<2;++i) {
    device->ScreenBuffers[i].Mapped=TRUE;device->ScreenBuffers[i].LockedBase=fixture.staging[i];
    device->ScreenBuffers[i].NativeBo=&fixture;device->ScreenBuffers[i].NativeMapRelease=ReleaseNativeMap;
  }
  InitializeSRWLock(&device->ScreenBufferLock);
  assert(AgxWin32GpuvaInit(&space,AdmissionUmdGpuvaOperations(),device));
  assert(AgxWin32GpuvaBind(&space,&command,17,98317,0,AGX_GPUVA_MAP_WRITE));
  assert(AgxWin32GpuvaBind(&space,&color,19,98317,0,AGX_GPUVA_MAP_WRITE));
  if(failure) {
    assert(!AgxWin32GpuvaSubmit(&space,references,2,&command,64,
        written,1,native_command,sizeof(native_command),&fence));
    assert(!fence && device->DrawTerminal && space.Terminal && space.Held);
    assert(fixture.signal==1 && !fixture.evict);
    for(UINT i=0;i<2;++i) assert(device->ScreenBuffers[i].CopyHeld && device->ScreenBuffers[i].SubmissionHolds==1);
    assert(!AgxWin32GpuvaUnbind(&space,&color));
    free(space.Held);HeapFree(GetProcessHeap(),0,device);return;
  }
  for(fixture.Round=0;fixture.Round<2;++fixture.Round) {
  assert(AgxWin32GpuvaSubmit(&space,references,2,&command,64,
      written,1,
      native_command,sizeof(native_command),&fence));
  if(persistent) assert(fixture.Uncached==1 && device->ScreenBuffers[0].Mapped && !device->ScreenBuffers[1].Mapped);
  if(displayableDirect)
    assert(fixture.canonical[1][53]==0xa5 && fixture.staging[1][53]==0x29);
  else
    assert(fixture.staging[1][53]==0xa5 && fixture.staging[1][54]==0xf3);
  assert(fence == 2*(fixture.Round+1) && command.Va == 0x20000 && color.Va == 0x40000);
  assert(AgxWin32GpuvaRetire(&space,fence));
  if(displayableDirect) fixture.canonical[1][55]=0x78;
  else fixture.staging[1][55]=0x78;
  }
  assert(AgxWin32GpuvaUnbind(&space,&command));
  assert(AgxWin32GpuvaUnbind(&space,&color));
  assert(!fixture.failed && fixture.reserve == 2 && fixture.map == 2 &&
      fixture.wait_map == 2 && fixture.resident == 2 &&
      fixture.wait_resident == 2 && fixture.submit == 2 &&
      fixture.signal == 4 && fixture.wait_render == 4 &&
      fixture.evict == 2 && fixture.free_va == 2);
  assert(submit_diagnostics == 2 && signal_diagnostics == 4);
  HeapFree(GetProcessHeap(),0,device);
}
int main(void) {
  run_copy_scenario(0,0,0);run_copy_scenario(0,1,0);
  run_copy_scenario(1,0,0);run_copy_scenario(2,0,0);run_copy_scenario(3,0,0);
  run_copy_scenario(0,0,1);
  puts("umd_gpuva_contract_windows: PASS (temporary/persistent/import, internal wait/readback/unlock failure retention)");
  return 0;
}
