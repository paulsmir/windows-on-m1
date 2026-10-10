#include <windows.h>
#include <wingdi.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>
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

/* EXP1094: EXP1092 context-switch samples put ~3.5 % of DWM's composition
 * thread in RtlQueryEnvironmentVariable: every diagnostic call (several per
 * submission) looked up both trace variables. They are process start-up
 * configuration; read them once per process. */
static INIT_ONCE AdmissionUmdTraceOnce = INIT_ONCE_STATIC_INIT;
static WCHAR AdmissionUmdTracePath[MAX_PATH];
static BOOL AdmissionUmdTraceRefusalsOnly;

static BOOL CALLBACK AdmissionUmdTraceLoad(PINIT_ONCE Once, PVOID Parameter,
                                           PVOID *Context) {
  WCHAR only[2];
  DWORD length;
  UNREFERENCED_PARAMETER(Once);
  UNREFERENCED_PARAMETER(Parameter);
  UNREFERENCED_PARAMETER(Context);
  AdmissionUmdTraceRefusalsOnly =
      GetEnvironmentVariableW(L"APPLE_AGX_UMD_REFUSALS_ONLY", only, 2) == 1u &&
      only[0] == L'1';
  length = GetEnvironmentVariableW(L"APPLE_AGX_UMD_TRACE_FILE",
      AdmissionUmdTracePath, ARRAYSIZE(AdmissionUmdTracePath));
  if (length == 0u || length >= ARRAYSIZE(AdmissionUmdTracePath))
    AdmissionUmdTracePath[0] = L'\0';
  return TRUE;
}

/* The trace file path, or NULL when tracing is off. */
static PCWSTR AdmissionUmdTraceConfig(BOOL *RefusalsOnly) {
  (void)InitOnceExecuteOnce(&AdmissionUmdTraceOnce, AdmissionUmdTraceLoad,
                            NULL, NULL);
  *RefusalsOnly = AdmissionUmdTraceRefusalsOnly;
  return AdmissionUmdTracePath[0] != L'\0' ? AdmissionUmdTracePath : NULL;
}

/* Opt-in, process-local diagnostics for standard-runtime admission. Never
 * change the caller's last-error state or any graphics result. */
BOOL AdmissionUmdDiagnosticEnabled(VOID) {
  DWORD saved = GetLastError();
  BOOL refusalsOnly;
  PCWSTR path = AdmissionUmdTraceConfig(&refusalsOnly);
  SetLastError(saved);
  return path != NULL && !refusalsOnly;
}

/* EXP959 exhausted the normal startup budget before DWM's first failed
 * deallocation. Keep the failure receipt independent and bounded. */
static BOOL AdmissionUmdDiagnosticPermit(
    PCSTR Stage, HRESULT Status, volatile LONG *NormalRecords,
    volatile LONG *DeallocateFailures,
    volatile LONG *RetirementFailures) {
  if (strcmp(Stage, "umd-deallocate-failure") == 0 && FAILED(Status))
    return InterlockedIncrement(DeallocateFailures) <= 16;
  if (strcmp(Stage, "umd-retirement-failure") == 0 && FAILED(Status))
    return InterlockedIncrement(RetirementFailures) <= 16;
  if (strncmp(Stage, "reject-", 7u) == 0 ||
      strncmp(Stage, "measure-", 8u) == 0)
    return TRUE;
  /* EXP1131: DWM rebuilt its primaries after a Settings close (EXP1130) and
   * no runtime error or device lifetime line survived the startup budget.
   * Keep them on a separate bounded budget so a failing loop cannot flood. */
  if (strcmp(Stage, "runtime-set-error") == 0 ||
      strncmp(Stage, "device-", 7u) == 0) {
    static volatile LONG lifetimeRecords;
    return InterlockedIncrement(&lifetimeRecords) <= 64;
  }
  /* EXP1032 diagnostic: per-call DDI/slot trace, unbudgeted but opt-in per
   * process (APPLE_AGX_UMD_DDI_TRACE=1); never emitted otherwise. */
  if (strncmp(Stage, "ddi-", 4u) == 0) {
    static volatile LONG ddiTrace = -1;
    LONG cached = InterlockedCompareExchange(&ddiTrace, -1, -1);
    if (cached < 0) {
      WCHAR flag[2];
      DWORD saved = GetLastError();
      cached = GetEnvironmentVariableW(L"APPLE_AGX_UMD_DDI_TRACE", flag, 2) == 1u &&
          flag[0] == L'1';
      SetLastError(saved);
      InterlockedExchange(&ddiTrace, cached);
    }
    return cached == 1;
  }
  return InterlockedCompareExchange(NormalRecords, 0, 0) < 128 &&
         InterlockedIncrement(NormalRecords) <= 128;
}

/* EXP1078 context-switch trace: DWM's composition thread kept entering NTFS
 * and the Defender filter (create, query-name, cleanup) because every line
 * opened, appended and closed the trace file; measure-* lines are unbudgeted
 * (hundreds per second in DWM). Keep one append handle per process; writes
 * with FILE_APPEND_DATA stay atomic appends across processes. */
static HANDLE AdmissionUmdTraceHandle(PCWSTR Path) {
  static HANDLE cached;
  HANDLE handle = (HANDLE)InterlockedCompareExchangePointer(
      (PVOID volatile *)&cached, NULL, NULL);
  if (handle != NULL) return handle;
  handle = CreateFileW(Path, FILE_APPEND_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
  if (InterlockedCompareExchangePointer((PVOID volatile *)&cached, handle,
                                        NULL) != NULL) {
    CloseHandle(handle);
    handle = (HANDLE)InterlockedCompareExchangePointer(
        (PVOID volatile *)&cached, NULL, NULL);
  }
  return handle;
}

/* EXP1109: a ~9.8 ms DWM frame issued ~30 trace writes, each through the
 * file-system filter stack (EXP1108 sn1108). Lines collect in a per-process
 * buffer and are appended in one write when it would overflow, when the
 * oldest buffered line is 250 ms old, for a refusal or failure record, and at
 * process detach. Caller holds AdmissionUmdTraceLock. */
#define ADMISSION_UMD_TRACE_BUFFER_BYTES 65536u
#define ADMISSION_UMD_TRACE_FLUSH_MS 250
static SRWLOCK AdmissionUmdTraceLock = SRWLOCK_INIT;
static char AdmissionUmdTraceBuffer[ADMISSION_UMD_TRACE_BUFFER_BYTES];
static DWORD AdmissionUmdTraceUsed;
static LONGLONG AdmissionUmdTraceOldestQpc;

static VOID AdmissionUmdTraceWriteLocked(VOID) {
  BOOL refusalsOnly;
  PCWSTR path;
  HANDLE file;
  DWORD written;
  if (AdmissionUmdTraceUsed == 0u) return;
  path = AdmissionUmdTraceConfig(&refusalsOnly);
  file = path != NULL ? AdmissionUmdTraceHandle(path) : INVALID_HANDLE_VALUE;
  if (file != INVALID_HANDLE_VALUE)
    (void)WriteFile(file, AdmissionUmdTraceBuffer, AdmissionUmdTraceUsed,
                    &written, NULL);
  AdmissionUmdTraceUsed = 0u;
}

/* Also called from DllMain at process detach, where a terminated thread may
 * still own the lock: never wait for it. */
VOID AdmissionUmdDiagnosticFlush(VOID) {
  DWORD saved = GetLastError();
  if (TryAcquireSRWLockExclusive(&AdmissionUmdTraceLock)) {
    AdmissionUmdTraceWriteLocked();
    ReleaseSRWLockExclusive(&AdmissionUmdTraceLock);
  }
  SetLastError(saved);
}

static DWORD AdmissionUmdTraceSession(VOID) {
  static volatile LONG cached = -1;
  LONG value = InterlockedCompareExchange(&cached, -1, -1);
  if (value < 0) {
    DWORD session = MAXDWORD;
    (void)ProcessIdToSessionId(GetCurrentProcessId(), &session);
    value = (LONG)session;
    InterlockedExchange(&cached, value);
  }
  return (DWORD)value;
}

static VOID AdmissionUmdTraceAppend(const char *Line, DWORD Bytes,
                                    BOOL Urgent, LONGLONG Qpc) {
  LARGE_INTEGER frequency;
  (void)QueryPerformanceFrequency(&frequency);
  AcquireSRWLockExclusive(&AdmissionUmdTraceLock);
  if (AdmissionUmdTraceUsed + Bytes > ADMISSION_UMD_TRACE_BUFFER_BYTES)
    AdmissionUmdTraceWriteLocked();
  if (AdmissionUmdTraceUsed == 0u) AdmissionUmdTraceOldestQpc = Qpc;
  memcpy(AdmissionUmdTraceBuffer + AdmissionUmdTraceUsed, Line, Bytes);
  AdmissionUmdTraceUsed += Bytes;
  if (Urgent || Qpc - AdmissionUmdTraceOldestQpc >=
                    frequency.QuadPart * ADMISSION_UMD_TRACE_FLUSH_MS / 1000)
    AdmissionUmdTraceWriteLocked();
  ReleaseSRWLockExclusive(&AdmissionUmdTraceLock);
}

VOID AdmissionUmdDiagnostic(PCSTR Stage, HRESULT Status,
                            const UINT *Values, UINT Count) {
  static volatile LONG records;
  static volatile LONG deallocateFailures;
  static volatile LONG retirementFailures;
  DWORD saved = GetLastError();
  BOOL refusalsOnly;
  PCWSTR path;
  char line[512];
  int used;
  UINT i;
  LARGE_INTEGER diagnosticQpc = {0};
  DWORD diagnosticSession = MAXDWORD;
  if (Stage == NULL || Count > 16u || (Count != 0u && Values == NULL))
    goto done;
  path = AdmissionUmdTraceConfig(&refusalsOnly);
  if (path == NULL) goto done;
  if (refusalsOnly && strncmp(Stage,"reject-",7u)!=0 &&
      strncmp(Stage,"measure-",8u)!=0 &&
      strcmp(Stage,"runtime-set-error")!=0 &&
      strncmp(Stage,"device-",7u)!=0 &&
      !(strcmp(Stage,"umd-deallocate-failure")==0 && FAILED(Status)) &&
      !(strcmp(Stage,"umd-retirement-failure")==0 && FAILED(Status))) goto done;
  /* Refusals must not disappear when successful startup chatter consumes
   * the normal 128-record budget. Capture remains opt-in and run-bounded. */
  if (!AdmissionUmdDiagnosticPermit(Stage,Status,&records,&deallocateFailures,
                                    &retirementFailures))
    goto done;
  (void)QueryPerformanceCounter(&diagnosticQpc);
  diagnosticSession = AdmissionUmdTraceSession();
  used = _snprintf_s(line, sizeof(line), _TRUNCATE,
      "%s hr=0x%08lx pid=%lu tid=%lu session=%lu qpc=%lld", Stage, (ULONG)Status,
      GetCurrentProcessId(), GetCurrentThreadId(), diagnosticSession,
      diagnosticQpc.QuadPart);
  if (used < 0) goto done;
  for (i = 0u; i < Count; ++i) {
    int added = _snprintf_s(line + used, sizeof(line) - (SIZE_T)used,
                           _TRUNCATE, " %08x", Values[i]);
    if (added < 0) goto done;
    used += added;
  }
  if ((SIZE_T)used + 1u >= sizeof(line)) goto done;
  line[used++] = '\n';
  AdmissionUmdTraceAppend(line, (DWORD)used,
      strncmp(Stage, "reject-", 7u) == 0 || FAILED(Status),
      diagnosticQpc.QuadPart);
 done:
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
    "measure-legacy-present-entry", "measure-legacy-present1-entry",
    "measure-native-context-flush", "measure-native-flush-status",
    "measure-present-guard"
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

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

VOID AdmissionUmdSetError(ADMISSION_UMD_DEVICE *Device, HRESULT Error) {
  /* EXP1131: the caller's image offset names the failing DDI path. */
  UINT caller = (UINT)((ULONG_PTR)_ReturnAddress() - (ULONG_PTR)&__ImageBase);
  AdmissionUmdDiagnostic("runtime-set-error", Error, &caller, 1u);
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
    void *Context, const ADMISSION_UMD_RETIREMENT *Retirement) {
  ADMISSION_UMD_DEVICE *device = (ADMISSION_UMD_DEVICE *)Context;
  D3DDDICB_DEALLOCATE deallocate;
  D3DKMT_HANDLE allocation = 0u;
  HANDLE runtimeResource = Retirement == NULL ? NULL : Retirement->RuntimeResource;
  UINT receipt[3] = {(UINT)1u, (UINT)(ULONG_PTR)runtimeResource,
                     (UINT)((ULONGLONG)(ULONG_PTR)runtimeResource >> 32)};
  HRESULT result;
  if (device == NULL || Retirement == NULL || runtimeResource == NULL ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnDeallocateCb == NULL) {
    AdmissionUmdDiagnostic("umd-deallocate-failure", E_INVALIDARG, receipt,
                           ARRAYSIZE(receipt));
    return E_INVALIDARG;
  }
  ZeroMemory(&deallocate, sizeof(deallocate));
  if (Retirement->Origin == 1u && !Retirement->Primary &&
      !Retirement->Shared && Retirement->KernelResource == 0u &&
      Retirement->KernelAllocation != 0u) {
    allocation = Retirement->KernelAllocation;
    deallocate.NumAllocations = 1u;
    deallocate.HandleList = &allocation;
  } else {
    deallocate.hResource = runtimeResource;
  }
  result = device->KernelCallbacks->pfnDeallocateCb(
      device->RuntimeDevice.handle, &deallocate);
  if (FAILED(result)) {
    receipt[0] = 2u;
    AdmissionUmdDiagnostic("umd-deallocate-failure", result, receipt,
                           ARRAYSIZE(receipt));
  } else {
    AdmissionUmdScreenForgetAllocation(device, Retirement->KernelAllocation);
  }
  return result;
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
