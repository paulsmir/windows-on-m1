#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include "../src/umd_internal.h"
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
} G4_FIXTURE;

static HRESULT APIENTRY TestReserve(HANDLE handle,
    D3DDDI_RESERVEGPUVIRTUALADDRESS *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(request->Size != 0x10000 || request->MinimumAddress != 0x10000 ||
     request->MaximumAddress != (1ULL << 39)) f->failed = 1;
  request->VirtualAddress = f->next_va;
  f->next_va += 0x20000;
  ++f->reserve;
  return S_OK;
}
static HRESULT APIENTRY TestMap(HANDLE handle,
    D3DDDI_MAPGPUVIRTUALADDRESS *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(request->hPagingQueue != 1 || request->SizeInPages != 16 ||
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
  if(request->Size != 0x10000 || f->evict != 1) f->failed = 1;
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
          request->FenceValueArray[0] == 1) ++f->wait_render;
  else f->failed = 1;
  return S_OK;
}
static HRESULT APIENTRY TestSubmit(HANDLE handle,
    const D3DDDICB_SUBMITCOMMAND *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  const BYTE *data = (const BYTE *)request->pPrivateDriverData;
  if(f->wait_resident != 1 || request->Commands != 0x20000 ||
     request->CommandLength != 64 || request->BroadcastContextCount != 1 ||
     request->BroadcastContext[0] != (HANDLE)3 ||
     request->NumPrimaries != 1 || request->WrittenPrimaries[0] != 100 ||
     request->PrivateDriverDataSize != 4 ||
     data[0] != 0xa1 || data[3] != 0xd4) f->failed = 1;
  ++f->submit;
  return S_OK;
}
static HRESULT APIENTRY TestSignal(HANDLE handle,
    const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(f->submit != 1 || request->ObjectCount != 1 ||
     request->ObjectHandleArray[0] != 4 ||
     request->MonitoredFenceValueArray[0] != 1) f->failed = 1;
  ++f->signal;
  return S_OK;
}
static HRESULT APIENTRY TestEvict(HANDLE handle,D3DDDICB_EVICT *request) {
  G4_FIXTURE *f = (G4_FIXTURE *)handle;
  if(f->wait_render != 1 || request->NumAllocations != 2 ||
     request->AllocationList[0] != 99 || request->AllocationList[1] != 100)
    f->failed = 1;
  ++f->evict;
  return S_OK;
}

int main(void) {
  G4_FIXTURE fixture = {};
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
  callbacks.pfnReserveGpuVirtualAddressCb = TestReserve;
  callbacks.pfnMapGpuVirtualAddressCb = TestMap;
  callbacks.pfnFreeGpuVirtualAddressCb = TestFree;
  callbacks.pfnMakeResidentCb = TestResident;
  callbacks.pfnWaitForSynchronizationObjectFromCpuCb = TestWait;
  callbacks.pfnSubmitCommandCb = TestSubmit;
  callbacks.pfnSignalSynchronizationObjectFromGpu2Cb = TestSignal;
  callbacks.pfnEvictCb = TestEvict;
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
  InitializeSRWLock(&device->ScreenBufferLock);
  assert(AgxWin32GpuvaInit(&space,AdmissionUmdGpuvaOperations(),device));
  assert(AgxWin32GpuvaBind(&space,&command,17,0x4000,0,AGX_GPUVA_MAP_WRITE));
  assert(AgxWin32GpuvaBind(&space,&color,19,0x4000,0,AGX_GPUVA_MAP_WRITE));
  assert(AgxWin32GpuvaSubmit(&space,references,2,&command,64,
      written,1,
      native_command,sizeof(native_command),&fence));
  assert(fence == 1 && command.Va == 0x20000 && color.Va == 0x40000);
  assert(AgxWin32GpuvaRetire(&space,fence));
  assert(AgxWin32GpuvaUnbind(&space,&command));
  assert(AgxWin32GpuvaUnbind(&space,&color));
  assert(!fixture.failed && fixture.reserve == 2 && fixture.map == 2 &&
      fixture.wait_map == 2 && fixture.resident == 1 &&
      fixture.wait_resident == 1 && fixture.submit == 1 &&
      fixture.signal == 1 && fixture.wait_render == 1 &&
      fixture.evict == 1 && fixture.free_va == 2);
  assert(submit_diagnostics == 1 && signal_diagnostics == 1);
  HeapFree(GetProcessHeap(),0,device);
  puts("umd_gpuva_contract_windows: PASS");
  return 0;
}
