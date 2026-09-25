#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include "umd_internal.h"

#ifdef APPLE_AGX_GPUVA_WINSYS

static D3DKMT_HANDLE allocation_handle(ADMISSION_UMD_DEVICE *device,
                                       uint64_t token) {
  D3DKMT_HANDLE handle = 0;
  if (!device || device->Magic != ADMISSION_UMD_DEVICE_MAGIC ||
      !token || device->ScreenClosing) return 0;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  for (UINT i = 0; i < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot = &device->ScreenBuffers[i];
    if (slot->Active && !slot->Transition && slot->Token == token) {
      handle = slot->KernelAllocation;
      break;
    }
  }
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  return handle;
}

static int reserve_va(void *context, uint64_t bytes, uint64_t minimum,
                      uint64_t maximum, uint64_t *va) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDI_RESERVEGPUVIRTUALADDRESS request = {};
  if (!device || !va || !bytes || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnReserveGpuVirtualAddressCb) return 0;
  request.hAdapter = 0;
  request.MinimumAddress = minimum;
  request.MaximumAddress = maximum;
  request.Size = bytes;
  HRESULT hr = device->KernelCallbacks->pfnReserveGpuVirtualAddressCb(
      device->RuntimeDevice.handle, &request);
  if (FAILED(hr) || !request.VirtualAddress) return 0;
  *va = request.VirtualAddress;
  return 1;
}

static int map_va(void *context, uint64_t token, uint64_t va,
                  uint64_t pages, unsigned protection, uint64_t *fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDI_MAPGPUVIRTUALADDRESS request = {};
  if (!device || !fence || !device->PagingQueue ||
      !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnMapGpuVirtualAddressCb) return 0;
  request.hAllocation = allocation_handle(device, token);
  if (!request.hAllocation) return 0;
  request.hPagingQueue = device->PagingQueue;
  request.BaseAddress = va;
  request.OffsetInPages = 0;
  request.SizeInPages = pages;
  request.Protection.Write = (protection & AGX_GPUVA_MAP_WRITE) != 0;
  request.Protection.Execute = (protection & AGX_GPUVA_MAP_EXECUTE) != 0;
  HRESULT hr = device->KernelCallbacks->pfnMapGpuVirtualAddressCb(
      device->RuntimeDevice.handle, &request);
  if (FAILED(hr) && hr != E_PENDING) return 0;
  if (request.VirtualAddress != va) return 3;
  *fence = request.PagingFenceValue;
  return hr == E_PENDING ? 2 : 1;
}

static int free_va(void *context, uint64_t va, uint64_t bytes) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDICB_FREEGPUVIRTUALADDRESS request = {};
  if (!device || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnFreeGpuVirtualAddressCb) return 0;
  request.BaseAddress = va;
  request.Size = bytes;
  return SUCCEEDED(device->KernelCallbacks->pfnFreeGpuVirtualAddressCb(
      device->RuntimeDevice.handle, &request));
}

static D3DKMT_HANDLE *translate_handles(ADMISSION_UMD_DEVICE *device,
                                        const uint64_t *tokens, unsigned count) {
  if (!device || !tokens || !count)
    return NULL;
  D3DKMT_HANDLE *handles = (D3DKMT_HANDLE *)HeapAlloc(
      GetProcessHeap(), 0, (SIZE_T)count * sizeof(*handles));
  if (!handles) return NULL;
  for (unsigned i = 0; i < count; ++i) {
    handles[i] = allocation_handle(device, tokens[i]);
    if (!handles[i]) {
      HeapFree(GetProcessHeap(), 0, handles);
      return NULL;
    }
  }
  return handles;
}

static int evict(void *context, const uint64_t *tokens, unsigned count) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  if (!device || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnEvictCb) return 0;
  D3DKMT_HANDLE *handles = translate_handles(device, tokens, count);
  if (!handles) return 0;
  D3DDDICB_EVICT request = {};
  request.NumAllocations = count;
  request.AllocationList = handles;
  HRESULT hr = device->KernelCallbacks->pfnEvictCb(
      device->RuntimeDevice.handle, &request);
  HeapFree(GetProcessHeap(), 0, handles);
  return SUCCEEDED(hr);
}

static int make_resident(void *context, const uint64_t *tokens,
                         unsigned count, uint64_t *fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  if (!device || !fence || !device->PagingQueue ||
      !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnMakeResidentCb) return 0;
  D3DKMT_HANDLE *handles = translate_handles(device, tokens, count);
  if (!handles) return 0;
  UINT *priorities = (UINT *)HeapAlloc(GetProcessHeap(), 0,
                                      (SIZE_T)count * sizeof(*priorities));
  if (!priorities) {
    HeapFree(GetProcessHeap(), 0, handles);
    return 0;
  }
  for (unsigned i = 0; i < count; ++i)
    priorities[i] = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  D3DDDI_MAKERESIDENT request = {};
  request.hPagingQueue = device->PagingQueue;
  request.NumAllocations = count;
  request.AllocationList = handles;
  request.PriorityList = priorities;
  HRESULT hr = device->KernelCallbacks->pfnMakeResidentCb(
      device->RuntimeDevice.handle, &request);
  int rollback = 1;
  if (request.NumAllocations && request.NumAllocations < count)
    rollback = evict(context, tokens, request.NumAllocations);
  HeapFree(GetProcessHeap(), 0, priorities);
  HeapFree(GetProcessHeap(), 0, handles);
  if (!rollback) {
    device->DrawTerminal = TRUE;
    return 3;
  }
  if ((FAILED(hr) && hr != E_PENDING) || request.NumAllocations != count)
    return 0;
  *fence = request.PagingFenceValue;
  return hr == E_PENDING ? 2 : 1;
}

static int wait_object(ADMISSION_UMD_DEVICE *device,
                       D3DKMT_HANDLE object, uint64_t fence) {
  D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU request = {};
  if (!device || !object || !fence || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnWaitForSynchronizationObjectFromCpuCb)
    return 0;
  request.ObjectCount = 1;
  request.ObjectHandleArray = &object;
  request.FenceValueArray = &fence;
  return SUCCEEDED(device->KernelCallbacks->pfnWaitForSynchronizationObjectFromCpuCb(
      device->RuntimeDevice.handle, &request));
}

static int wait_paging(void *context, uint64_t fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  return wait_object(device, device ? device->PagingSyncObject : 0, fence);
}

static int submit(void *context, const uint64_t *written,
                  unsigned written_count, uint64_t va, uint32_t bytes,
                  const void *private_data, uint32_t private_bytes,
                  uint64_t *fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  D3DDDICB_SUBMITCOMMAND request = {};
  D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 signal = {};
  if (!device || !fence || !device->KernelContext ||
      !device->RenderSyncObject || !device->KernelCallbacks ||
      !device->KernelCallbacks->pfnSubmitCommandCb ||
      !device->KernelCallbacks->pfnSignalSynchronizationObjectFromGpu2Cb ||
      device->NextRenderFence == UINT64_MAX ||
      written_count > D3DDDI_MAX_WRITTEN_PRIMARIES ||
      (written_count && !written)) return 0;
  AcquireSRWLockShared(&device->ScreenBufferLock);
  for (unsigned i = 0; i < written_count; ++i) {
    ADMISSION_UMD_SCREEN_BUFFER *slot = NULL;
    for (UINT j = 0; j < ADMISSION_UMD_SCREEN_BUFFER_LIMIT; ++j)
      if (device->ScreenBuffers[j].Active &&
          device->ScreenBuffers[j].Token == written[i]) {
        slot = &device->ScreenBuffers[j];
        break;
      }
    if (!slot || slot->Transition || !slot->KernelAllocation) {
      ReleaseSRWLockShared(&device->ScreenBufferLock);
      return 0;
    }
    if (slot->WrittenPrimary)
      request.WrittenPrimaries[request.NumPrimaries++] = slot->KernelAllocation;
  }
  ReleaseSRWLockShared(&device->ScreenBufferLock);
  request.Commands = va;
  request.CommandLength = bytes;
  request.BroadcastContextCount = 1;
  request.BroadcastContext[0] = device->KernelContext;
  request.pPrivateDriverData = (void *)private_data;
  request.PrivateDriverDataSize = private_bytes;
  request.RenderCBSequence = (UINT)InterlockedIncrement(&device->RenderCbSequence);
  if (FAILED(device->KernelCallbacks->pfnSubmitCommandCb(
      device->RuntimeDevice.handle, &request))) return 0;
  uint64_t next = device->NextRenderFence + 1;
  D3DKMT_HANDLE object = device->RenderSyncObject;
  HANDLE context_handle = device->KernelContext;
  signal.ObjectCount = 1;
  signal.ObjectHandleArray = &object;
  signal.BroadcastContextCount = 1;
  signal.BroadcastContextArray = &context_handle;
  signal.MonitoredFenceValueArray = &next;
  if (FAILED(device->KernelCallbacks->pfnSignalSynchronizationObjectFromGpu2Cb(
      device->RuntimeDevice.handle, &signal))) {
    device->DrawTerminal = TRUE;
    return 2; /* accepted submit, completion owner uncertain */
  }
  device->NextRenderFence = next;
  *fence = next;
  return 1;
}

static int wait_render(void *context, uint64_t fence) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)context;
  return wait_object(device, device ? device->RenderSyncObject : 0, fence);
}

#ifdef __cplusplus
extern "C"
#endif
const AGX_WIN32_GPUVA_OPS *AdmissionUmdGpuvaOperations(void) {
  static const AGX_WIN32_GPUVA_OPS operations = {
      reserve_va, map_va, free_va, make_resident, wait_paging,
      submit, wait_render, evict};
  return &operations;
}
#endif
