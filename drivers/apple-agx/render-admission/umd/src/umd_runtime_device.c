#include <windows.h>
#include <wingdi.h>
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

VOID AdmissionUmdSetError(ADMISSION_UMD_DEVICE *Device, HRESULT Error) {
  if (Device != NULL && Device->SetErrorCallback != NULL)
    Device->SetErrorCallback(Device->RuntimeCoreLayer, Error);
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
  D3DDDICB_CREATECONTEXT createContext;
  PFND3D10DDI_SETERROR_CB errorCallback;
  HRESULT result;
  if (device == NULL || adapter == NULL ||
      adapter->Magic != ADMISSION_UMD_ADAPTER_MAGIC || Args == NULL ||
      Args->pKTCallbacks == NULL ||
      Args->pKTCallbacks->pfnCreateContextCb == NULL ||
      Args->pKTCallbacks->pfnDestroyContextCb == NULL ||
      Args->pKTCallbacks->pfnAllocateCb == NULL ||
      Args->pKTCallbacks->pfnDeallocateCb == NULL ||
      Args->pKTCallbacks->pfnLockCb == NULL ||
      Args->pKTCallbacks->pfnUnlockCb == NULL ||
      Args->pKTCallbacks->pfnSignalSynchronizationObject2Cb == NULL ||
      Args->pKTCallbacks->pfnRenderCb == NULL ||
      Args->DXGIBaseDDI.pDXGIBaseCallbacks == NULL)
    return E_INVALIDARG;
  if (Args->Interface == D3D10_0_DDI_INTERFACE_VERSION) {
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
  result = device->KernelCallbacks->pfnCreateContextCb(
      device->RuntimeDevice.handle, &createContext);
  if (FAILED(result)) {
    ZeroMemory(device, sizeof(*device));
    return result;
  }
  device->KernelContext = createContext.hContext;
  if (device->KernelContext == NULL || createContext.pCommandBuffer == NULL ||
      createContext.CommandBufferSize == 0u ||
      createContext.pAllocationList == NULL ||
      createContext.AllocationListSize == 0u ||
      createContext.pPatchLocationList == NULL ||
      createContext.PatchLocationListSize == 0u) {
    D3DDDICB_DESTROYCONTEXT destroyContext;
    ZeroMemory(&destroyContext, sizeof(destroyContext));
    destroyContext.hContext = device->KernelContext;
    if (device->KernelContext != NULL)
      (void)device->KernelCallbacks->pfnDestroyContextCb(
          device->RuntimeDevice.handle, &destroyContext);
    ZeroMemory(device, sizeof(*device));
    return E_FAIL;
  }
  device->CommandBuffer = createContext.pCommandBuffer;
  device->CommandBufferSize = createContext.CommandBufferSize;
  device->AllocationList = createContext.pAllocationList;
  device->AllocationListSize = createContext.AllocationListSize;
  device->PatchList = createContext.pPatchLocationList;
  device->PatchListSize = createContext.PatchLocationListSize;
  result = AdmissionUmdScreenInitialize(device);
  if (FAILED(result)) {
    D3DDDICB_DESTROYCONTEXT destroyContext;
    ZeroMemory(&destroyContext, sizeof(destroyContext));
    destroyContext.hContext = device->KernelContext;
    (void)device->KernelCallbacks->pfnDestroyContextCb(
        device->RuntimeDevice.handle, &destroyContext);
    ZeroMemory(device, sizeof(*device));
    return result;
  }

  return S_OK;
}

VOID AdmissionUmdRuntimeDeviceFinalize(ADMISSION_UMD_DEVICE *device) {
  ADMISSION_UMD_RETIREMENT_FINALIZE_RESULT retirement;
  D3DDDICB_DESTROYCONTEXT destroyContext;
  HRESULT screenResult;
  HRESULT terminalError = S_OK;
  ULONG screenUndeallocated = 0u;
  if (device == NULL)
    return;
  screenResult = AdmissionUmdScreenFinalize(device, &screenUndeallocated);
  if (FAILED(screenResult) || screenUndeallocated != 0u)
    terminalError = FAILED(screenResult) ? screenResult : E_FAIL;
  AdmissionUmdRetirementFinalize(&device->Retirement, &retirement);
  if (retirement.Undeallocated != 0u) {
    device->LastRetirementError = retirement.LastError;
    device->RetirementErrorCount += retirement.Undeallocated;
    device->RetirementUndeallocated = retirement.Undeallocated;
    device->RetirementTerminal = TRUE;
    if (SUCCEEDED(terminalError))
      terminalError = retirement.FirstError;
  }
  if (device->KernelContext != NULL && device->KernelCallbacks != NULL &&
      device->KernelCallbacks->pfnDestroyContextCb != NULL) {
    ZeroMemory(&destroyContext, sizeof(destroyContext));
    destroyContext.hContext = device->KernelContext;
    (void)device->KernelCallbacks->pfnDestroyContextCb(
        device->RuntimeDevice.handle, &destroyContext);
  }
  if (FAILED(terminalError))
    AdmissionUmdSetError(device, terminalError);
  ZeroMemory(device, sizeof(*device));
}
