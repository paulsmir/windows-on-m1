#include <windows.h>
#include <wingdi.h>
#include <stdio.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#if defined(__cplusplus)
extern "C" {
#endif
#include "umd_internal.h"
#if defined(__cplusplus)
}
#endif

/* Shared Windows device ownership; no adapter exports, DDI tables or caps. */
static volatile LONG AdmissionUmdGenerationCounter;

static ULONG AdmissionUmdNextGeneration(VOID) {
  ULONG counter = (ULONG)InterlockedIncrement(&AdmissionUmdGenerationCounter);
  ULONG generation = ((GetCurrentProcessId() & 0xffffu) << 16) ^ counter;
  if (generation == 0u)
    generation = (ULONG)InterlockedIncrement(&AdmissionUmdGenerationCounter);
  return generation == 0u ? 1u : generation;
}

/* Opt-in, process-local diagnostics for standard-runtime admission. Never
 * change the caller's last-error state or any graphics result. */
BOOL AdmissionUmdDiagnosticEnabled(VOID) {
  DWORD saved = GetLastError();
  WCHAR path[MAX_PATH], only[2];
  DWORD refusalOnly = GetEnvironmentVariableW(L"APPLE_AGX_UMD_REFUSALS_ONLY",only,2);
  DWORD length = GetEnvironmentVariableW(
      L"APPLE_AGX_UMD_TRACE_FILE", path, ARRAYSIZE(path));
  SetLastError(saved);
  return length != 0u && length < ARRAYSIZE(path) &&
      !(refusalOnly == 1u && only[0] == L'1');
}

VOID AdmissionUmdDiagnostic(PCSTR Stage, HRESULT Status,
                            const UINT *Values, UINT Count) {
  static volatile LONG records;
  DWORD saved = GetLastError();
  WCHAR path[MAX_PATH], only[2];
  char line[512];
  HANDLE file = INVALID_HANDLE_VALUE;
  DWORD length, written;
  int used;
  UINT i;
  if (Stage == NULL || Count > 16u || (Count != 0u && Values == NULL))
    goto done;
  if (GetEnvironmentVariableW(L"APPLE_AGX_UMD_REFUSALS_ONLY",only,2)==1u &&
      only[0]==L'1' && strncmp(Stage,"reject-",7u)!=0 &&
      strncmp(Stage,"measure-",8u)!=0) goto done;
  length = GetEnvironmentVariableW(L"APPLE_AGX_UMD_TRACE_FILE", path,
                                    ARRAYSIZE(path));
  /* Refusals must not disappear when successful startup chatter consumes
   * the normal 128-record budget. Capture remains opt-in and run-bounded. */
  if (length == 0u || length >= ARRAYSIZE(path) ||
      (strncmp(Stage,"reject-",7u) != 0 &&
       strncmp(Stage,"measure-",8u) != 0 &&
       (InterlockedCompareExchange(&records, 0, 0) >= 128 ||
        InterlockedIncrement(&records) > 128)))
    goto done;
  used = _snprintf_s(line, sizeof(line), _TRUNCATE,
      "%s hr=0x%08lx pid=%lu tid=%lu", Stage, (ULONG)Status,
      GetCurrentProcessId(), GetCurrentThreadId());
  if (used < 0) goto done;
  for (i = 0u; i < Count; ++i) {
    int added = _snprintf_s(line + used, sizeof(line) - (SIZE_T)used,
                           _TRUNCATE, " %08x", Values[i]);
    if (added < 0) goto done;
    used += added;
  }
  if ((SIZE_T)used + 1u >= sizeof(line)) goto done;
  line[used++] = '\n';
  file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                     NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (file != INVALID_HANDLE_VALUE)
    (void)WriteFile(file, line, (DWORD)used, &written, NULL);
 done:
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  SetLastError(saved);
}

static BOOL AdmissionUmdPresentMeasureSample(ULONG Count) {
  return Count != 0u && (Count & (Count - 1u)) == 0u;
}

VOID AdmissionUmdPresentMeasure(UINT Kind, HRESULT Status,
                                const UINT *Values, UINT Count) {
  static volatile LONG counts[AdmissionUmdMeasureCount];
  static volatile LONG failures[AdmissionUmdMeasureCount];
  static const char *const names[AdmissionUmdMeasureCount] = {
    "measure-native-device", "measure-native-present-entry",
    "measure-native-present-return", "measure-native-blt-entry",
    "measure-native-blt-return", "measure-native-flush-stage",
    "measure-native-submit-entry", "measure-native-submit-return",
    "measure-present-callback-enter", "measure-present-callback-return",
    "measure-legacy-present-entry", "measure-legacy-present1-entry"
  };
  UINT receipt[16];
  ULONG seen;
  UINT index;
  if (Kind >= AdmissionUmdMeasureCount || Count > 14u ||
      (Count != 0u && Values == NULL)) return;
  seen = (ULONG)InterlockedIncrement(&counts[Kind]);
  if (!AdmissionUmdPresentMeasureSample(seen) &&
      !(FAILED(Status) &&
        InterlockedCompareExchange(&failures[Kind], 1, 0) == 0)) return;
  receipt[0] = Kind;
  receipt[1] = seen;
  for (index = 0u; index < Count; ++index) receipt[index + 2u] = Values[index];
  AdmissionUmdDiagnostic(names[Kind], Status, receipt, Count + 2u);
}

VOID AdmissionUmdSetError(ADMISSION_UMD_DEVICE *Device, HRESULT Error) {
  AdmissionUmdDiagnostic("runtime-set-error", Error, NULL, 0u);
  if (Device != NULL && Device->SetErrorCallback != NULL)
    Device->SetErrorCallback(Device->RuntimeCoreLayer, Error);
}

BOOL AdmissionUmdNextRenderSequence(
    ADMISSION_UMD_DEVICE *Device, UINT *Sequence) {
  LONG next;
  if (Sequence != NULL)
    *Sequence = 0u;
  if (Device == NULL || Sequence == NULL ||
      Device->Magic != ADMISSION_UMD_DEVICE_MAGIC)
    return FALSE;
  /* Threading caps are zero: this is the single-threaded sequence range.
   * Interlocked increment also makes callback reentry observationally unique. */
  next = InterlockedIncrement(&Device->RenderCbSequence);
  if (next <= 0) {
    Device->DrawTerminal = TRUE;
    Device->LastScreenError = E_FAIL;
    return FALSE;
  }
  *Sequence = (UINT)next;
  return TRUE;
}

static HRESULT APIENTRY AdmissionUmdDeallocateResource(
    void *Context, HANDLE RuntimeResource) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  D3DDDICB_DEALLOCATE deallocate;
  if (device == NULL || RuntimeResource == NULL ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnDeallocateCb == NULL)
    return E_INVALIDARG;
  ZeroMemory(&deallocate, sizeof(deallocate));
  deallocate.hResource = RuntimeResource;
  return device->KernelCallbacks->pfnDeallocateCb(
      device->RuntimeDevice.handle, &deallocate);
}

static VOID APIENTRY AdmissionUmdReportResourceError(
    void *Context, HRESULT Error) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  if (device != NULL) {
    device->LastRetirementError = Error;
    ++device->RetirementErrorCount;
  }
  AdmissionUmdSetError(device, Error);
}

HRESULT AdmissionUmdRuntimeAdapterInitialize(
    ADMISSION_UMD_ADAPTER *Adapter, const D3D10DDIARG_OPENADAPTER *Args) {
  ADMISSION_UMD_ADAPTER candidate;
  D3DDDICB_QUERYADAPTERINFO query;
  HRESULT result;
  if (Adapter == NULL || Args == NULL || Args->pAdapterCallbacks == NULL ||
      Args->pAdapterCallbacks->pfnQueryAdapterInfoCb == NULL)
    return E_INVALIDARG;
  ZeroMemory(&candidate, sizeof(candidate));
  candidate.Magic = ADMISSION_UMD_ADAPTER_MAGIC;
  candidate.RuntimeAdapter = Args->hRTAdapter;
  candidate.Interface = Args->Interface;
  candidate.Version = Args->Version;
  candidate.Callbacks = Args->pAdapterCallbacks;
  ZeroMemory(&query, sizeof(query));
  query.pPrivateDriverData = &candidate.DeviceInfo;
  query.PrivateDriverDataSize = sizeof(candidate.DeviceInfo);
  result = candidate.Callbacks->pfnQueryAdapterInfoCb(
      candidate.RuntimeAdapter.handle, &query);
  if (FAILED(result) || !AgxWin32DeviceInfoValid(&candidate.DeviceInfo))
    return FAILED(result) ? result : E_FAIL;
  *Adapter = candidate;
  return S_OK;
}

HRESULT AdmissionUmdRuntimeDeviceInitialize(
    ADMISSION_UMD_DEVICE *device, ADMISSION_UMD_ADAPTER *adapter,
    const D3D10DDIARG_CREATEDEVICE *Args) {
  ADMISSION_WIN32_CONTEXT_CREATE win32Context;
#ifdef APPLE_AGX_GPUVA_WINSYS
  D3DDDICB_CREATECONTEXTVIRTUAL createContext;
#else
  D3DDDICB_CREATECONTEXT createContext;
#endif
  PFND3D10DDI_SETERROR_CB errorCallback;
  HRESULT result;
  if (device == NULL || adapter == NULL ||
      adapter->Magic != ADMISSION_UMD_ADAPTER_MAGIC || Args == NULL ||
      Args->pKTCallbacks == NULL ||
#ifdef APPLE_AGX_GPUVA_WINSYS
      Args->pKTCallbacks->pfnCreateContextVirtualCb == NULL ||
      Args->pKTCallbacks->pfnCreateSynchronizationObject2Cb == NULL ||
      Args->pKTCallbacks->pfnDestroySynchronizationObjectCb == NULL ||
      Args->pKTCallbacks->pfnReserveGpuVirtualAddressCb == NULL ||
      Args->pKTCallbacks->pfnMapGpuVirtualAddressCb == NULL ||
      Args->pKTCallbacks->pfnFreeGpuVirtualAddressCb == NULL ||
      Args->pKTCallbacks->pfnSubmitCommandCb == NULL ||
      Args->pKTCallbacks->pfnSignalSynchronizationObjectFromGpu2Cb == NULL ||
#else
      Args->pKTCallbacks->pfnCreateContextCb == NULL ||
#endif
      Args->pKTCallbacks->pfnDestroyContextCb == NULL ||
      Args->pKTCallbacks->pfnAllocateCb == NULL ||
      Args->pKTCallbacks->pfnDeallocateCb == NULL ||
      Args->pKTCallbacks->pfnLockCb == NULL ||
      Args->pKTCallbacks->pfnUnlockCb == NULL ||
      Args->pKTCallbacks->pfnSetPriorityCb == NULL ||
      Args->pKTCallbacks->pfnQueryResidencyCb == NULL ||
      Args->pKTCallbacks->pfnSignalSynchronizationObject2Cb == NULL ||
      Args->pKTCallbacks->pfnMakeResidentCb == NULL ||
      Args->pKTCallbacks->pfnEvictCb == NULL ||
      Args->pKTCallbacks->pfnWaitForSynchronizationObjectFromCpuCb == NULL ||
      Args->pKTCallbacks->pfnCreatePagingQueueCb == NULL ||
      Args->pKTCallbacks->pfnDestroyPagingQueueCb == NULL ||
#ifndef APPLE_AGX_GPUVA_WINSYS
      Args->pKTCallbacks->pfnRenderCb == NULL ||
#endif
      Args->DXGIBaseDDI.pDXGIBaseCallbacks == NULL)
    return E_INVALIDARG;
  if (Args->Interface == D3D10_0_DDI_INTERFACE_VERSION ||
      Args->Interface == D3D10_0_x_DDI_INTERFACE_VERSION ||
      Args->Interface == D3D10_0_7_DDI_INTERFACE_VERSION) {
    if (Args->pUMCallbacks == NULL)
      return E_INVALIDARG;
    errorCallback = Args->pUMCallbacks->pfnSetErrorCb;
  } else if (Args->Interface == D3DWDDM1_3_DDI_INTERFACE_VERSION) {
    if (Args->p11UMCallbacks == NULL)
      return E_INVALIDARG;
    errorCallback = Args->p11UMCallbacks->pfnSetErrorCb;
  } else {
    return E_INVALIDARG;
  }
  ZeroMemory(device, sizeof(*device));
  device->Magic = ADMISSION_UMD_DEVICE_MAGIC;
  device->Adapter = adapter;
  device->RuntimeDevice = Args->hRTDevice;
  device->RuntimeCoreLayer = Args->hRTCoreLayer;
  device->KernelCallbacks = Args->pKTCallbacks;
  device->SetErrorCallback = errorCallback;
  device->DxgiCallbacks = Args->DXGIBaseDDI.pDXGIBaseCallbacks;
  device->Win32Generation = AdmissionUmdNextGeneration();
  AdmissionUmdRetirementInitialize(
      &device->Retirement, device, AdmissionUmdDeallocateResource,
      AdmissionUmdReportResourceError);
  ZeroMemory(&createContext, sizeof(createContext));
  ZeroMemory(&win32Context, sizeof(win32Context));
  win32Context.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
  win32Context.Version = ADMISSION_WIN32_CONTEXT_VERSION;
  win32Context.Bytes = sizeof(win32Context);
  win32Context.Generation = device->Win32Generation;
  createContext.NodeOrdinal = 0u;
  createContext.EngineAffinity = 1u;
  createContext.pPrivateDriverData = &win32Context;
  createContext.PrivateDriverDataSize = sizeof(win32Context);
#ifdef APPLE_AGX_GPUVA_WINSYS
  result = device->KernelCallbacks->pfnCreateContextVirtualCb(
      device->RuntimeDevice.handle, &createContext);
  AdmissionUmdDiagnostic("g4-create-context-virtual-cb", result, NULL, 0u);
#else
  result = device->KernelCallbacks->pfnCreateContextCb(
      device->RuntimeDevice.handle, &createContext);
#endif
  if (FAILED(result)) {
    ZeroMemory(device, sizeof(*device));
    return result;
  }
  device->KernelContext = createContext.hContext;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if (device->KernelContext == NULL) {
#else
  if (device->KernelContext == NULL || createContext.pCommandBuffer == NULL ||
      createContext.CommandBufferSize == 0u ||
      createContext.pAllocationList == NULL ||
      createContext.AllocationListSize == 0u ||
      createContext.pPatchLocationList == NULL ||
      createContext.PatchLocationListSize == 0u) {
#endif
    D3DDDICB_DESTROYCONTEXT destroyContext;
    ZeroMemory(&destroyContext, sizeof(destroyContext));
    destroyContext.hContext = device->KernelContext;
    if (device->KernelContext != NULL)
      (void)device->KernelCallbacks->pfnDestroyContextCb(
          device->RuntimeDevice.handle, &destroyContext);
    ZeroMemory(device, sizeof(*device));
    return E_FAIL;
  }
#ifndef APPLE_AGX_GPUVA_WINSYS
  device->CommandBuffer = createContext.pCommandBuffer;
  device->CommandBufferSize = createContext.CommandBufferSize;
  device->AllocationList = createContext.pAllocationList;
  device->AllocationListSize = createContext.AllocationListSize;
  device->PatchList = createContext.pPatchLocationList;
  device->PatchListSize = createContext.PatchLocationListSize;
#endif
  {
    D3DDDICB_CREATEPAGINGQUEUE pagingQueue;
    ZeroMemory(&pagingQueue, sizeof(pagingQueue));
    pagingQueue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    pagingQueue.PhysicalAdapterIndex = 0u;
    result = device->KernelCallbacks->pfnCreatePagingQueueCb(
        device->RuntimeDevice.handle, &pagingQueue);
#ifdef APPLE_AGX_GPUVA_WINSYS
    {
      UINT values[5] = { (UINT)pagingQueue.Priority,
          pagingQueue.PhysicalAdapterIndex, pagingQueue.hPagingQueue != 0u,
          pagingQueue.hSyncObject != 0u,
          pagingQueue.FenceValueCPUVirtualAddress != NULL };
      AdmissionUmdDiagnostic("g4-create-paging-queue-cb", result,
                             values, ARRAYSIZE(values));
    }
#endif
    if (FAILED(result) || pagingQueue.hPagingQueue == 0u ||
        pagingQueue.hSyncObject == 0u ||
        pagingQueue.FenceValueCPUVirtualAddress == NULL) {
      D3DDDICB_DESTROYCONTEXT destroyContext;
      ZeroMemory(&destroyContext, sizeof(destroyContext));
      destroyContext.hContext = device->KernelContext;
      (void)device->KernelCallbacks->pfnDestroyContextCb(
          device->RuntimeDevice.handle, &destroyContext);
      ZeroMemory(device, sizeof(*device));
      return FAILED(result) ? result : E_FAIL;
    }
    device->PagingQueue = pagingQueue.hPagingQueue;
    device->PagingSyncObject = pagingQueue.hSyncObject;
    device->PagingFenceAddress =
        (volatile UINT64 *)pagingQueue.FenceValueCPUVirtualAddress;
  }
#ifdef APPLE_AGX_GPUVA_WINSYS
  {
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 renderFence;
    ZeroMemory(&renderFence, sizeof(renderFence));
    renderFence.Info.Type = D3DDDI_MONITORED_FENCE;
    renderFence.Info.MonitoredFence.InitialFenceValue = 0;
    renderFence.Info.MonitoredFence.EngineAffinity = 1;
    result = device->KernelCallbacks->pfnCreateSynchronizationObject2Cb(
        device->RuntimeDevice.handle, &renderFence);
    {
      UINT values[4] = { (UINT)renderFence.Info.Type,
          renderFence.Info.MonitoredFence.EngineAffinity,
          renderFence.hSyncObject != 0u,
          renderFence.Info.MonitoredFence.FenceValueCPUVirtualAddress != NULL };
      AdmissionUmdDiagnostic("g4-create-render-fence-cb", result,
                             values, ARRAYSIZE(values));
    }
    if (FAILED(result) || !renderFence.hSyncObject ||
        !renderFence.Info.MonitoredFence.FenceValueCPUVirtualAddress) {
      D3DDDI_DESTROYPAGINGQUEUE destroyQueue = {};
      D3DDDICB_DESTROYCONTEXT destroyContext = {};
      destroyQueue.hPagingQueue = device->PagingQueue;
      destroyContext.hContext = device->KernelContext;
      (void)device->KernelCallbacks->pfnDestroyPagingQueueCb(
          device->RuntimeDevice.handle, &destroyQueue);
      (void)device->KernelCallbacks->pfnDestroyContextCb(
          device->RuntimeDevice.handle, &destroyContext);
      ZeroMemory(device, sizeof(*device));
      return FAILED(result) ? result : E_FAIL;
    }
    device->RenderSyncObject = renderFence.hSyncObject;
    device->RenderFenceAddress =
        (volatile UINT64 *)renderFence.Info.MonitoredFence.FenceValueCPUVirtualAddress;
  }
#endif
  result = AdmissionUmdScreenInitialize(device);
#ifdef APPLE_AGX_GPUVA_WINSYS
  AdmissionUmdDiagnostic("g4-screen-initialize", result, NULL, 0u);
#endif
  if (FAILED(result)) {
    D3DDDICB_DESTROYCONTEXT destroyContext;
    D3DDDI_DESTROYPAGINGQUEUE destroyQueue;
    ZeroMemory(&destroyQueue, sizeof(destroyQueue));
    destroyQueue.hPagingQueue = device->PagingQueue;
    (void)device->KernelCallbacks->pfnDestroyPagingQueueCb(
        device->RuntimeDevice.handle, &destroyQueue);
#ifdef APPLE_AGX_GPUVA_WINSYS
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT destroyFence = {};
    destroyFence.hSyncObject = device->RenderSyncObject;
    (void)device->KernelCallbacks->pfnDestroySynchronizationObjectCb(
        device->RuntimeDevice.handle, &destroyFence);
#endif
    ZeroMemory(&destroyContext, sizeof(destroyContext));
    destroyContext.hContext = device->KernelContext;
    (void)device->KernelCallbacks->pfnDestroyContextCb(
        device->RuntimeDevice.handle, &destroyContext);
    ZeroMemory(device, sizeof(*device));
    return result;
  }

  return S_OK;
}

HRESULT AdmissionUmdRuntimeDeviceDestroyKernelContext(
    ADMISSION_UMD_DEVICE *device, BOOL *destroyed) {
  D3DDDICB_DESTROYCONTEXT destroyContext;
  HANDLE liveContext;
  HRESULT result;
  if(destroyed) *destroyed=FALSE;
  if(!device || !destroyed || device->Magic!=ADMISSION_UMD_DEVICE_MAGIC)
    return E_INVALIDARG;
  if(!device->KernelContext) { *destroyed=TRUE;return S_OK; }
  if(!device->KernelCallbacks || !device->KernelCallbacks->pfnDestroyContextCb)
    return E_INVALIDARG;
  liveContext=device->KernelContext;
  ZeroMemory(&destroyContext,sizeof(destroyContext));
  destroyContext.hContext=liveContext;
  result=device->KernelCallbacks->pfnDestroyContextCb(
      device->RuntimeDevice.handle,&destroyContext);
  if(FAILED(result)) return result;
  device->QuiescedKernelContext=liveContext;
  device->KernelContextQuiesced=TRUE;
  device->KernelContext=NULL;
  device->CommandBuffer=NULL;device->CommandBufferSize=0;
  device->AllocationList=NULL;device->AllocationListSize=0;
  device->PatchList=NULL;device->PatchListSize=0;
  *destroyed=TRUE;
  return S_OK;
}

HRESULT AdmissionUmdRuntimeDeviceFinalize(ADMISSION_UMD_DEVICE *device,
                                           BOOL *consumed) {
  ADMISSION_UMD_RETIREMENT_FINALIZE_RESULT retirement;
  HRESULT screenResult;
  HRESULT terminalError = S_OK;
  ULONG screenUndeallocated = 0u;
  BOOL contextDestroyed=FALSE;
  if (consumed != NULL)
    *consumed = FALSE;
  if (device == NULL || consumed == NULL ||
      device->Magic != ADMISSION_UMD_DEVICE_MAGIC)
    return E_INVALIDARG;
  screenResult = AdmissionUmdScreenFinalize(device, &screenUndeallocated);
  if (FAILED(screenResult) || screenUndeallocated != 0u)
    return FAILED(screenResult) ? screenResult : E_FAIL;
  AdmissionUmdRetirementFinalize(&device->Retirement, &retirement);
  if (retirement.Undeallocated != 0u) {
    device->LastRetirementError = retirement.LastError;
    device->RetirementErrorCount += retirement.Undeallocated;
    device->RetirementUndeallocated = retirement.Undeallocated;
    device->RetirementTerminal = TRUE;
    if (SUCCEEDED(terminalError))
      terminalError = retirement.FirstError;
  }
  screenResult=AdmissionUmdRuntimeDeviceDestroyKernelContext(device,&contextDestroyed);
  if(FAILED(screenResult) || !contextDestroyed)
    return FAILED(screenResult)?screenResult:E_FAIL;
  if(device->DrawSubmission || device->NativeBatchTransaction)
    return HRESULT_FROM_WIN32(ERROR_BUSY);
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(device->RenderSyncObject) {
    D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT destroyFence = {};
    destroyFence.hSyncObject = device->RenderSyncObject;
    screenResult = device->KernelCallbacks->pfnDestroySynchronizationObjectCb(
        device->RuntimeDevice.handle, &destroyFence);
    if(FAILED(screenResult)) return screenResult;
    device->RenderSyncObject = 0;
    device->RenderFenceAddress = NULL;
  }
#endif
  if(device->PagingQueue) {
    D3DDDI_DESTROYPAGINGQUEUE destroyQueue;
    ZeroMemory(&destroyQueue, sizeof(destroyQueue));
    destroyQueue.hPagingQueue=device->PagingQueue;
    screenResult=device->KernelCallbacks->pfnDestroyPagingQueueCb(
        device->RuntimeDevice.handle,&destroyQueue);
    if(FAILED(screenResult)) return screenResult;
    device->PagingQueue=0u;
    device->PagingSyncObject=0u;
    device->PagingFenceAddress=NULL;
  }
  device->QuiescedKernelContext=NULL;
  device->KernelContextQuiesced=FALSE;
  if (FAILED(terminalError))
    AdmissionUmdSetError(device, terminalError);
  ZeroMemory(device, sizeof(*device));
  *consumed = TRUE;
  return terminalError;
}
