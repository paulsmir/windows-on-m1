#include "agx_kmt_gpuva_bridge.h"
#include <string.h>

#define AGX_KMT_GPUVA_MAGIC 0x56474b41u /* 'AKGV' */
#define AGX_KMT_GPUVA_MAX_ALLOCATIONS 8u
#define KMT_STATUS_PENDING ((NTSTATUS)0x00000103L)
#define KMT_STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000dL)
#define KMT_STATUS_NO_MEMORY ((NTSTATUS)0xc0000017L)
#define KMT_STATUS_DEVICE_REMOVED ((NTSTATUS)0xc00002b6L)
#define KMT_STATUS_GRAPHICS_NO_VIDEO_MEMORY ((NTSTATUS)0xc01e0100L)

HRESULT AgxKmtGpuvaResult(NTSTATUS Status) {
  /* The runtime reports a queued paging operation (MakeResident, Map) as
   * E_PENDING with a paging fence; umd_* waits on exactly that value. */
  if (Status == KMT_STATUS_PENDING) return E_PENDING;
  if (Status >= 0) return S_OK;
  switch (Status) {
  case KMT_STATUS_INVALID_PARAMETER: return E_INVALIDARG;
  case KMT_STATUS_NO_MEMORY:
  case KMT_STATUS_GRAPHICS_NO_VIDEO_MEMORY: return E_OUTOFMEMORY;
  case KMT_STATUS_DEVICE_REMOVED: return D3DDDIERR_DEVICEREMOVED;
  default: return HRESULT_FROM_NT(Status);
  }
}

static AGX_KMT_GPUVA_BRIDGE *bridge(HANDLE handle) {
  AGX_KMT_GPUVA_BRIDGE *b = (AGX_KMT_GPUVA_BRIDGE *)handle;
  return b && b->Magic == AGX_KMT_GPUVA_MAGIC ? b : NULL;
}

static HRESULT done(AGX_KMT_GPUVA_BRIDGE *b, AGX_KMT_GPUVA_OP op,
                    NTSTATUS status) {
  HRESULT result = AgxKmtGpuvaResult(status);
  ++b->Receipt.Calls[op];
  if (FAILED(result)) {
    ++b->Receipt.Failures[op];
    b->Receipt.LastFailedOp = op;
    b->Receipt.LastFailedStatus = status;
  }
  return result;
}

static HRESULT refuse(AGX_KMT_GPUVA_BRIDGE *b, AGX_KMT_GPUVA_OP op) {
  if (!b) return E_INVALIDARG;
  return done(b, op, KMT_STATUS_INVALID_PARAMETER);
}

/* Kernel context handles travel through HANDLE fields of the callback
 * structures; anything wider than a D3DKMT_HANDLE is not one of ours. */
static BOOL kmt_handle(HANDLE value, D3DKMT_HANDLE *out) {
  if ((UINT_PTR)value > MAXUINT32) return FALSE;
  *out = (D3DKMT_HANDLE)(UINT_PTR)value;
  return TRUE;
}

static BOOL kmt_handles(const HANDLE *values, UINT count, D3DKMT_HANDLE *out) {
  for (UINT i = 0; i < count; ++i)
    if (!kmt_handle(values[i], &out[i])) return FALSE;
  return TRUE;
}

static HRESULT APIENTRY query_adapter(HANDLE adapter,
                                      const D3DDDICB_QUERYADAPTERINFO *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(adapter);
  D3DKMT_QUERYADAPTERINFO request;
  if (!b || !args || !args->pPrivateDriverData || !args->PrivateDriverDataSize)
    return refuse(b, AgxKmtGpuvaQueryAdapter);
  ZeroMemory(&request, sizeof(request));
  request.hAdapter = b->AdapterHandle;
  request.Type = KMTQAITYPE_UMDRIVERPRIVATE;
  request.pPrivateDriverData = args->pPrivateDriverData;
  request.PrivateDriverDataSize = args->PrivateDriverDataSize;
  return done(b, AgxKmtGpuvaQueryAdapter, b->Kmt.QueryAdapterInfo(&request));
}

static HRESULT APIENTRY create_context(HANDLE device,
                                       D3DDDICB_CREATECONTEXTVIRTUAL *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_CREATECONTEXTVIRTUAL request;
  HRESULT result;
  if (!b || !args) return refuse(b, AgxKmtGpuvaCreateContext);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.NodeOrdinal = args->NodeOrdinal;
  request.EngineAffinity = args->EngineAffinity;
  request.Flags = args->Flags;
  request.pPrivateDriverData = args->pPrivateDriverData;
  request.PrivateDriverDataSize = args->PrivateDriverDataSize;
  request.ClientHint = D3DKMT_CLIENTHINT_OPENGL;
  result = done(b, AgxKmtGpuvaCreateContext,
                b->Kmt.CreateContextVirtual(&request));
  if (SUCCEEDED(result)) args->hContext = (HANDLE)(UINT_PTR)request.hContext;
  return result;
}

static HRESULT APIENTRY destroy_context(HANDLE device,
                                        const D3DDDICB_DESTROYCONTEXT *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_DESTROYCONTEXT request;
  ZeroMemory(&request, sizeof(request));
  if (!b || !args || !kmt_handle(args->hContext, &request.hContext) ||
      !request.hContext)
    return refuse(b, AgxKmtGpuvaDestroyContext);
  return done(b, AgxKmtGpuvaDestroyContext, b->Kmt.DestroyContext(&request));
}

static HRESULT APIENTRY create_paging_queue(HANDLE device,
                                            D3DDDICB_CREATEPAGINGQUEUE *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_CREATEPAGINGQUEUE request;
  HRESULT result;
  if (!b || !args) return refuse(b, AgxKmtGpuvaCreatePagingQueue);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.Priority = args->Priority;
  request.PhysicalAdapterIndex = args->PhysicalAdapterIndex;
  result = done(b, AgxKmtGpuvaCreatePagingQueue,
                b->Kmt.CreatePagingQueue(&request));
  if (SUCCEEDED(result)) {
    args->hPagingQueue = request.hPagingQueue;
    args->hSyncObject = request.hSyncObject;
    args->FenceValueCPUVirtualAddress = request.FenceValueCPUVirtualAddress;
  }
  return result;
}

static HRESULT APIENTRY destroy_paging_queue(HANDLE device,
                                             const D3DDDI_DESTROYPAGINGQUEUE *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DDDI_DESTROYPAGINGQUEUE request;
  if (!b || !args || !args->hPagingQueue)
    return refuse(b, AgxKmtGpuvaDestroyPagingQueue);
  request = *args;
  return done(b, AgxKmtGpuvaDestroyPagingQueue,
              b->Kmt.DestroyPagingQueue(&request));
}

static HRESULT APIENTRY create_sync(HANDLE device,
                                    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_CREATESYNCHRONIZATIONOBJECT2 request;
  HRESULT result;
  if (!b || !args) return refuse(b, AgxKmtGpuvaCreateSync);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.Info = args->Info;
  result = done(b, AgxKmtGpuvaCreateSync,
                b->Kmt.CreateSynchronizationObject2(&request));
  if (SUCCEEDED(result)) {
    /* Info is in/out: a monitored fence returns its CPU and GPU VAs. */
    args->Info = request.Info;
    args->hSyncObject = request.hSyncObject;
  }
  return result;
}

static HRESULT APIENTRY destroy_sync(HANDLE device,
    const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_DESTROYSYNCHRONIZATIONOBJECT request;
  if (!b || !args || !args->hSyncObject) return refuse(b, AgxKmtGpuvaDestroySync);
  request.hSyncObject = args->hSyncObject;
  return done(b, AgxKmtGpuvaDestroySync,
              b->Kmt.DestroySynchronizationObject(&request));
}

static HRESULT APIENTRY allocate(HANDLE device, D3DDDICB_ALLOCATE *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DDDI_ALLOCATIONINFO2 info[AGX_KMT_GPUVA_MAX_ALLOCATIONS];
  D3DKMT_CREATEALLOCATION request;
  HRESULT result;
  /* No runtime resources exist without the D3D runtime: every ICD
   * allocation is a standalone device allocation (umd_win32_screen.c). */
  if (!b || !args || args->hResource || !args->pAllocationInfo ||
      !args->NumAllocations || args->NumAllocations > AGX_KMT_GPUVA_MAX_ALLOCATIONS)
    return refuse(b, AgxKmtGpuvaAllocate);
  ZeroMemory(info, sizeof(info));
  for (UINT i = 0; i < args->NumAllocations; ++i) {
    const D3DDDI_ALLOCATIONINFO *in = &args->pAllocationInfo[i];
    if (in->Flags.Value & ~3u) return refuse(b, AgxKmtGpuvaAllocate);
    info[i].pSystemMem = in->pSystemMem;
    info[i].pPrivateDriverData = in->pPrivateDriverData;
    info[i].PrivateDriverDataSize = in->PrivateDriverDataSize;
    info[i].VidPnSourceId = in->VidPnSourceId;
    info[i].Flags.Primary = in->Flags.Primary;
    info[i].Flags.Stereo = in->Flags.Stereo;
  }
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.pPrivateDriverData = args->pPrivateDriverData;
  request.PrivateDriverDataSize = args->PrivateDriverDataSize;
  request.NumAllocations = args->NumAllocations;
  request.pAllocationInfo2 = info;
  result = done(b, AgxKmtGpuvaAllocate, b->Kmt.CreateAllocation2(&request));
  /* Handles are returned even when the call fails part-way, so the caller
   * can roll back exactly what was created (umd_win32_screen.c:620). */
  for (UINT i = 0; i < args->NumAllocations; ++i)
    args->pAllocationInfo[i].hAllocation = info[i].hAllocation;
  args->hKMResource = request.hResource;
  return result;
}

static HRESULT APIENTRY deallocate(HANDLE device, const D3DDDICB_DEALLOCATE *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_DESTROYALLOCATION2 request;
  if (!b || !args || args->hResource || !args->HandleList || !args->NumAllocations)
    return refuse(b, AgxKmtGpuvaDeallocate);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.phAllocationList = args->HandleList;
  request.AllocationCount = args->NumAllocations;
  return done(b, AgxKmtGpuvaDeallocate, b->Kmt.DestroyAllocation2(&request));
}

static HRESULT APIENTRY reserve_va(HANDLE device,
                                   D3DDDI_RESERVEGPUVIRTUALADDRESS *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DDDI_RESERVEGPUVIRTUALADDRESS request;
  HRESULT result;
  if (!b || !args || !args->Size) return refuse(b, AgxKmtGpuvaReserve);
  request = *args;
  /* The runtime fills the adapter (umd_gpuva_windows.c:217 passes 0). */
  request.hAdapter = b->AdapterHandle;
  result = done(b, AgxKmtGpuvaReserve, b->Kmt.ReserveGpuVirtualAddress(&request));
  args->VirtualAddress = request.VirtualAddress;
  args->PagingFenceValue = request.PagingFenceValue;
  return result;
}

static HRESULT APIENTRY map_va(HANDLE device, D3DDDI_MAPGPUVIRTUALADDRESS *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DDDI_MAPGPUVIRTUALADDRESS request;
  HRESULT result;
  if (!b || !args || !args->hPagingQueue || !args->hAllocation)
    return refuse(b, AgxKmtGpuvaMap);
  request = *args;
  result = done(b, AgxKmtGpuvaMap, b->Kmt.MapGpuVirtualAddress(&request));
  args->VirtualAddress = request.VirtualAddress;
  args->PagingFenceValue = request.PagingFenceValue;
  return result;
}

static HRESULT APIENTRY free_va(HANDLE device,
                                const D3DDDICB_FREEGPUVIRTUALADDRESS *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_FREEGPUVIRTUALADDRESS request;
  if (!b || !args || !args->Size) return refuse(b, AgxKmtGpuvaFree);
  ZeroMemory(&request, sizeof(request));
  request.hAdapter = b->AdapterHandle;
  request.BaseAddress = args->BaseAddress;
  request.Size = args->Size;
  return done(b, AgxKmtGpuvaFree, b->Kmt.FreeGpuVirtualAddress(&request));
}

static HRESULT APIENTRY make_resident(HANDLE device, D3DDDI_MAKERESIDENT *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DDDI_MAKERESIDENT request;
  HRESULT result;
  if (!b || !args || !args->hPagingQueue || !args->AllocationList ||
      !args->NumAllocations)
    return refuse(b, AgxKmtGpuvaMakeResident);
  request = *args;
  result = done(b, AgxKmtGpuvaMakeResident, b->Kmt.MakeResident(&request));
  args->PagingFenceValue = request.PagingFenceValue;
  args->NumBytesToTrim = request.NumBytesToTrim;
  return result;
}

static HRESULT APIENTRY evict(HANDLE device, D3DDDICB_EVICT *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_EVICT request;
  HRESULT result;
  if (!b || !args || !args->AllocationList || !args->NumAllocations)
    return refuse(b, AgxKmtGpuvaEvict);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.NumAllocations = args->NumAllocations;
  request.AllocationList = args->AllocationList;
  request.Flags = args->Flags;
  result = done(b, AgxKmtGpuvaEvict, b->Kmt.Evict(&request));
  args->NumBytesToTrim = request.NumBytesToTrim;
  return result;
}

static HRESULT APIENTRY wait_cpu(HANDLE device,
    const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU request;
  if (!b || !args || !args->ObjectCount || !args->ObjectHandleArray ||
      !args->FenceValueArray)
    return refuse(b, AgxKmtGpuvaWaitCpu);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.ObjectCount = args->ObjectCount;
  request.ObjectHandleArray = args->ObjectHandleArray;
  request.FenceValueArray = args->FenceValueArray;
  request.hAsyncEvent = args->hAsyncEvent;
  request.Flags = args->Flags;
  return done(b, AgxKmtGpuvaWaitCpu,
              b->Kmt.WaitForSynchronizationObjectFromCpu(&request));
}

static HRESULT APIENTRY submit(HANDLE device, const D3DDDICB_SUBMITCOMMAND *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_SUBMITCOMMAND request;
  if (!b || !args || !args->BroadcastContextCount ||
      args->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT ||
      args->NumPrimaries > D3DDDI_MAX_WRITTEN_PRIMARIES ||
      (args->Flags.Value & ~1u))
    return refuse(b, AgxKmtGpuvaSubmit);
  ZeroMemory(&request, sizeof(request));
  if (!kmt_handles(args->BroadcastContext, args->BroadcastContextCount,
                   request.BroadcastContext))
    return refuse(b, AgxKmtGpuvaSubmit);
  request.Commands = args->Commands;
  request.CommandLength = args->CommandLength;
  request.Flags.NullRendering = args->Flags.NullRendering;
  request.BroadcastContextCount = args->BroadcastContextCount;
  request.pPrivateDriverData = args->pPrivateDriverData;
  request.PrivateDriverDataSize = args->PrivateDriverDataSize;
  request.NumPrimaries = args->NumPrimaries;
  for (UINT i = 0; i < args->NumPrimaries; ++i)
    request.WrittenPrimaries[i] = args->WrittenPrimaries[i];
  return done(b, AgxKmtGpuvaSubmit, b->Kmt.SubmitCommand(&request));
}

static HRESULT APIENTRY signal_gpu2(HANDLE device,
    const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 request;
  D3DKMT_HANDLE contexts[D3DDDI_MAX_BROADCAST_CONTEXT];
  if (!b || !args || !args->ObjectCount || !args->ObjectHandleArray ||
      !args->BroadcastContextCount || !args->BroadcastContextArray ||
      args->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT ||
      !kmt_handles(args->BroadcastContextArray, args->BroadcastContextCount,
                   contexts))
    return refuse(b, AgxKmtGpuvaSignalGpu2);
  ZeroMemory(&request, sizeof(request));
  request.ObjectCount = args->ObjectCount;
  request.ObjectHandleArray = args->ObjectHandleArray;
  request.Flags = args->Flags;
  request.BroadcastContextCount = args->BroadcastContextCount;
  request.BroadcastContextArray = contexts;
  /* FenceValue / CpuEventHandle / MonitoredFenceValueArray share the
   * 64-byte Reserved union in both structures. */
  C_ASSERT(sizeof(request.Reserved) == sizeof(args->Reserved));
  memcpy(request.Reserved, args->Reserved, sizeof(request.Reserved));
  return done(b, AgxKmtGpuvaSignalGpu2,
              b->Kmt.SignalSynchronizationObjectFromGpu2(&request));
}

static HRESULT APIENTRY signal2(HANDLE device,
    const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 request;
  if (!b || !args || !args->ObjectCount ||
      args->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED ||
      args->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
    return refuse(b, AgxKmtGpuvaSignal2);
  ZeroMemory(&request, sizeof(request));
  if (!kmt_handle(args->hContext, &request.hContext) ||
      !kmt_handles(args->BroadcastContext, args->BroadcastContextCount,
                   request.BroadcastContext))
    return refuse(b, AgxKmtGpuvaSignal2);
  request.ObjectCount = args->ObjectCount;
  for (UINT i = 0; i < args->ObjectCount; ++i)
    request.ObjectHandleArray[i] = args->ObjectHandleArray[i];
  request.Flags = args->Flags;
  request.BroadcastContextCount = args->BroadcastContextCount;
  if (args->Flags.EnqueueCpuEvent) request.CpuEventHandle = args->CpuEventHandle;
  else request.Fence.FenceValue = args->FenceValue;
  return done(b, AgxKmtGpuvaSignal2,
              b->Kmt.SignalSynchronizationObject2(&request));
}

static HRESULT APIENTRY lock(HANDLE device, D3DDDICB_LOCK *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_LOCK2 request;
  D3DDDICB_LOCKFLAGS rest;
  HRESULT result;
  if (!b || !args || !args->hAllocation || args->NumPages || args->pPages ||
      args->PrivateDriverData)
    return refuse(b, AgxKmtGpuvaLock);
  /* Lock2 always maps the whole allocation with no options; the umd_* code
   * only asks for LockEntire with ReadOnly/WriteOnly hints. */
  rest = args->Flags;
  rest.LockEntire = 0; rest.ReadOnly = 0; rest.WriteOnly = 0;
  if (rest.Value) return refuse(b, AgxKmtGpuvaLock);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.hAllocation = args->hAllocation;
  result = done(b, AgxKmtGpuvaLock, b->Kmt.Lock2(&request));
  if (SUCCEEDED(result)) args->pData = request.pData;
  return result;
}

static HRESULT APIENTRY unlock(HANDLE device, const D3DDDICB_UNLOCK *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  if (!b || !args || !args->NumAllocations || !args->phAllocations)
    return refuse(b, AgxKmtGpuvaUnlock);
  for (UINT i = 0; i < args->NumAllocations; ++i) {
    D3DKMT_UNLOCK2 request;
    HRESULT result;
    request.hDevice = b->DeviceHandle;
    request.hAllocation = args->phAllocations[i];
    result = done(b, AgxKmtGpuvaUnlock, b->Kmt.Unlock2(&request));
    if (FAILED(result)) return result;
  }
  return S_OK;
}

static HRESULT APIENTRY escape(HANDLE adapter, const D3DDDICB_ESCAPE *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(adapter);
  D3DKMT_ESCAPE request;
  if (!b || !args || !args->pPrivateDriverData || !args->PrivateDriverDataSize ||
      (args->hDevice && bridge(args->hDevice) != b))
    return refuse(b, AgxKmtGpuvaEscape);
  ZeroMemory(&request, sizeof(request));
  if (!kmt_handle(args->hContext, &request.hContext))
    return refuse(b, AgxKmtGpuvaEscape);
  request.hAdapter = b->AdapterHandle;
  request.hDevice = args->hDevice ? b->DeviceHandle : 0;
  request.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
  request.Flags = args->Flags;
  request.pPrivateDriverData = args->pPrivateDriverData;
  request.PrivateDriverDataSize = args->PrivateDriverDataSize;
  return done(b, AgxKmtGpuvaEscape, b->Kmt.Escape(&request));
}

static HRESULT APIENTRY set_priority(HANDLE device, D3DDDICB_SETPRIORITY *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_SETALLOCATIONPRIORITY request;
  if (!b || !args || args->hResource || !args->HandleList ||
      !args->NumAllocations || !args->pPriorities)
    return refuse(b, AgxKmtGpuvaSetPriority);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.phAllocationList = args->HandleList;
  request.AllocationCount = args->NumAllocations;
  request.pPriorities = args->pPriorities;
  return done(b, AgxKmtGpuvaSetPriority, b->Kmt.SetAllocationPriority(&request));
}

static HRESULT APIENTRY query_residency(HANDLE device,
                                        const D3DDDICB_QUERYRESIDENCY *args) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(device);
  D3DKMT_QUERYALLOCATIONRESIDENCY request;
  C_ASSERT(sizeof(D3DDDI_RESIDENCYSTATUS) == sizeof(D3DKMT_ALLOCATIONRESIDENCYSTATUS));
  if (!b || !args || args->hResource || !args->HandleList ||
      !args->NumAllocations || !args->pResidencyStatus)
    return refuse(b, AgxKmtGpuvaQueryResidency);
  ZeroMemory(&request, sizeof(request));
  request.hDevice = b->DeviceHandle;
  request.phAllocationList = args->HandleList;
  request.AllocationCount = args->NumAllocations;
  request.pResidencyStatus = (D3DKMT_ALLOCATIONRESIDENCYSTATUS *)args->pResidencyStatus;
  return done(b, AgxKmtGpuvaQueryResidency,
              b->Kmt.QueryAllocationResidency(&request));
}

static VOID APIENTRY set_error(D3D10DDI_HRTCORELAYER core, HRESULT error) {
  AGX_KMT_GPUVA_BRIDGE *b = bridge(core.handle);
  if (!b) return;
  ++b->Receipt.Calls[AgxKmtGpuvaSetError];
  ++b->Receipt.RuntimeErrors;
  b->Receipt.LastRuntimeError = error;
}

/* Store through FARPROC: GetProcAddress returns a generic function pointer. */
#define LOAD_THUNK(field, name) \
  ((*(FARPROC *)&Thunks->field = GetProcAddress(gdi, name)) != NULL)

BOOL AgxKmtThunksLoad(AGX_KMT_THUNKS *Thunks) {
  HMODULE gdi = GetModuleHandleW(L"gdi32.dll");
  if (!Thunks) return FALSE;
  ZeroMemory(Thunks, sizeof(*Thunks));
  if (!gdi) gdi = LoadLibraryW(L"gdi32.dll");
  if (!gdi) return FALSE;
  return LOAD_THUNK(QueryAdapterInfo, "D3DKMTQueryAdapterInfo") &&
      LOAD_THUNK(CreateContextVirtual, "D3DKMTCreateContextVirtual") &&
      LOAD_THUNK(DestroyContext, "D3DKMTDestroyContext") &&
      LOAD_THUNK(CreatePagingQueue, "D3DKMTCreatePagingQueue") &&
      LOAD_THUNK(DestroyPagingQueue, "D3DKMTDestroyPagingQueue") &&
      LOAD_THUNK(CreateSynchronizationObject2, "D3DKMTCreateSynchronizationObject2") &&
      LOAD_THUNK(DestroySynchronizationObject, "D3DKMTDestroySynchronizationObject") &&
      LOAD_THUNK(CreateAllocation2, "D3DKMTCreateAllocation2") &&
      LOAD_THUNK(DestroyAllocation2, "D3DKMTDestroyAllocation2") &&
      LOAD_THUNK(ReserveGpuVirtualAddress, "D3DKMTReserveGpuVirtualAddress") &&
      LOAD_THUNK(MapGpuVirtualAddress, "D3DKMTMapGpuVirtualAddress") &&
      LOAD_THUNK(FreeGpuVirtualAddress, "D3DKMTFreeGpuVirtualAddress") &&
      LOAD_THUNK(MakeResident, "D3DKMTMakeResident") &&
      LOAD_THUNK(Evict, "D3DKMTEvict") &&
      LOAD_THUNK(WaitForSynchronizationObjectFromCpu,
                 "D3DKMTWaitForSynchronizationObjectFromCpu") &&
      LOAD_THUNK(SubmitCommand, "D3DKMTSubmitCommand") &&
      LOAD_THUNK(SignalSynchronizationObjectFromGpu2,
                 "D3DKMTSignalSynchronizationObjectFromGpu2") &&
      LOAD_THUNK(SignalSynchronizationObject2, "D3DKMTSignalSynchronizationObject2") &&
      LOAD_THUNK(Lock2, "D3DKMTLock2") &&
      LOAD_THUNK(Unlock2, "D3DKMTUnlock2") &&
      LOAD_THUNK(Escape, "D3DKMTEscape") &&
      LOAD_THUNK(SetAllocationPriority, "D3DKMTSetAllocationPriority") &&
      LOAD_THUNK(QueryAllocationResidency, "D3DKMTQueryAllocationResidency");
}

#undef LOAD_THUNK

HRESULT AgxKmtGpuvaBridgeInitialize(AGX_KMT_GPUVA_BRIDGE *Bridge,
    const AGX_KMT_THUNKS *Thunks, D3DKMT_HANDLE Adapter, D3DKMT_HANDLE Device) {
  D3DDDI_DEVICECALLBACKS *d;
  if (!Bridge || !Thunks || !Adapter || !Device) return E_INVALIDARG;
  ZeroMemory(Bridge, sizeof(*Bridge));
  Bridge->Magic = AGX_KMT_GPUVA_MAGIC;
  Bridge->Kmt = *Thunks;
  Bridge->AdapterHandle = Adapter;
  Bridge->DeviceHandle = Device;
  Bridge->AdapterCallbacks.pfnQueryAdapterInfoCb = query_adapter;
  d = &Bridge->DeviceCallbacks;
  d->pfnAllocateCb = allocate;
  d->pfnDeallocateCb = deallocate;
  d->pfnSetPriorityCb = set_priority;
  d->pfnQueryResidencyCb = query_residency;
  d->pfnLockCb = lock;
  d->pfnUnlockCb = unlock;
  d->pfnEscapeCb = escape;
  d->pfnDestroyContextCb = destroy_context;
  d->pfnCreateSynchronizationObject2Cb = create_sync;
  d->pfnDestroySynchronizationObjectCb = destroy_sync;
  d->pfnSignalSynchronizationObject2Cb = signal2;
  d->pfnMakeResidentCb = make_resident;
  d->pfnEvictCb = evict;
  d->pfnWaitForSynchronizationObjectFromCpuCb = wait_cpu;
  d->pfnCreatePagingQueueCb = create_paging_queue;
  d->pfnDestroyPagingQueueCb = destroy_paging_queue;
  d->pfnReserveGpuVirtualAddressCb = reserve_va;
  d->pfnMapGpuVirtualAddressCb = map_va;
  d->pfnFreeGpuVirtualAddressCb = free_va;
  d->pfnCreateContextVirtualCb = create_context;
  d->pfnSubmitCommandCb = submit;
  d->pfnSignalSynchronizationObjectFromGpu2Cb = signal_gpu2;
  Bridge->CoreCallbacks.pfnSetErrorCb = set_error;
  return S_OK;
}
