#include <windows.h>
#include <wingdi.h>

/* d3d10umddi.h includes d3dkmddi.h, which expects this kernel-style type. */
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)

#include "direct_flip_contract.h"
#include "umd_internal.h"

#if defined(APPLE_AGX_UMD_ADMISSION_TRACE)
#define ADMISSION_UMD_TRACE(Text)                                             \
  OutputDebugStringW(L"AppleAgxUMD: " Text L"\n")
#else
#define ADMISSION_UMD_TRACE(Text) ((void)0)
#endif

static volatile LONG AdmissionUmdGenerationCounter;

static ULONG AdmissionUmdNextGeneration(VOID) {
  ULONG counter = (ULONG)InterlockedIncrement(&AdmissionUmdGenerationCounter);
  ULONG generation = ((GetCurrentProcessId() & 0xffffu) << 16) ^ counter;
  if (generation == 0u)
    generation = (ULONG)InterlockedIncrement(&AdmissionUmdGenerationCounter);
  return generation == 0u ? 1u : generation;
}

static SIZE_T APIENTRY AdmissionUmdCalcPrivateDeviceSize(
    D3D10DDI_HADAPTER Adapter,
    const D3D10DDIARG_CALCPRIVATEDEVICESIZE *Args);
static HRESULT APIENTRY AdmissionUmdCreateDevice(
    D3D10DDI_HADAPTER Adapter, D3D10DDIARG_CREATEDEVICE *Args);
static HRESULT APIENTRY AdmissionUmdCloseAdapter(D3D10DDI_HADAPTER Adapter);
static HRESULT APIENTRY AdmissionUmdGetSupportedVersions(
    D3D10DDI_HADAPTER Adapter, UINT32 *Entries, UINT64 *Versions);
static HRESULT APIENTRY AdmissionUmdGetCaps(
    D3D10DDI_HADAPTER Adapter, const D3D10_2DDIARG_GETCAPS *Caps);
static SIZE_T APIENTRY AdmissionUmdCalcPrivateResourceSize(
    D3D10DDI_HDEVICE Device,
    const D3D11DDIARG_CREATERESOURCE *CreateResource);
static SIZE_T APIENTRY AdmissionUmdCalcPrivateOpenedResourceSize(
    D3D10DDI_HDEVICE Device,
    const D3D10DDIARG_OPENRESOURCE *OpenResource);
static VOID APIENTRY AdmissionUmdCreateResource(
    D3D10DDI_HDEVICE Device,
    const D3D11DDIARG_CREATERESOURCE *CreateResource,
    D3D10DDI_HRESOURCE Resource, D3D10DDI_HRTRESOURCE RuntimeResource);
static VOID APIENTRY AdmissionUmdOpenResource(
    D3D10DDI_HDEVICE Device,
    const D3D10DDIARG_OPENRESOURCE *OpenResource,
    D3D10DDI_HRESOURCE Resource, D3D10DDI_HRTRESOURCE RuntimeResource);
static VOID APIENTRY AdmissionUmdDestroyResource(
    D3D10DDI_HDEVICE Device, D3D10DDI_HRESOURCE Resource);
static VOID APIENTRY AdmissionUmdCheckFormatSupport(
    D3D10DDI_HDEVICE Device, DXGI_FORMAT Format, UINT *FormatSupport);
static BOOL APIENTRY AdmissionUmdFlush(D3D10DDI_HDEVICE Device,
                                       UINT FlushFlags);
static VOID APIENTRY AdmissionUmdDestroyDevice(D3D10DDI_HDEVICE Device);
static VOID APIENTRY AdmissionUmdCheckDirectFlipSupport(
    D3D10DDI_HDEVICE Device, D3D10DDI_HRESOURCE CurrentResource,
    D3D10DDI_HRESOURCE CandidateResource, UINT Flags, BOOL *Supported);
static HRESULT APIENTRY AdmissionUmdPresent(DXGI_DDI_ARG_PRESENT *Args);
static HRESULT APIENTRY AdmissionUmdPresent1(DXGI_DDI_ARG_PRESENT1 *Args);
static HRESULT APIENTRY AdmissionUmdSetDisplayMode(
    DXGI_DDI_ARG_SETDISPLAYMODE *Args);
static HRESULT APIENTRY AdmissionUmdRotateResourceIdentities(
    DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *Args);

static ADMISSION_UMD_ADAPTER *AdmissionUmdAdapterFromHandle(
    D3D10DDI_HADAPTER Handle) {
  ADMISSION_UMD_ADAPTER *adapter =
      (ADMISSION_UMD_ADAPTER *)Handle.pDrvPrivate;
  return adapter != NULL && adapter->Magic == ADMISSION_UMD_ADAPTER_MAGIC
             ? adapter
             : NULL;
}

static ADMISSION_UMD_DEVICE *AdmissionUmdDeviceFromHandle(
    D3D10DDI_HDEVICE Handle) {
  ADMISSION_UMD_DEVICE *device =
      (ADMISSION_UMD_DEVICE *)Handle.pDrvPrivate;
  return device != NULL && device->Magic == ADMISSION_UMD_DEVICE_MAGIC
             ? device
             : NULL;
}

static ADMISSION_UMD_DEVICE *AdmissionUmdDeviceFromDxgi(
    DXGI_DDI_HDEVICE Handle) {
  ADMISSION_UMD_DEVICE *device =
      (ADMISSION_UMD_DEVICE *)(UINT_PTR)Handle;
  return device != NULL && device->Magic == ADMISSION_UMD_DEVICE_MAGIC
             ? device
             : NULL;
}

static ADMISSION_UMD_RESOURCE *AdmissionUmdResourceFromHandle(
    D3D10DDI_HRESOURCE Handle) {
  ADMISSION_UMD_RESOURCE *resource =
      (ADMISSION_UMD_RESOURCE *)Handle.pDrvPrivate;
  return resource != NULL && resource->Magic == ADMISSION_UMD_RESOURCE_MAGIC
             ? resource
             : NULL;
}

static ADMISSION_UMD_RESOURCE *AdmissionUmdResourceFromDxgi(
    DXGI_DDI_HRESOURCE Handle) {
  ADMISSION_UMD_RESOURCE *resource =
      (ADMISSION_UMD_RESOURCE *)(UINT_PTR)Handle;
  return resource != NULL && resource->Magic == ADMISSION_UMD_RESOURCE_MAGIC
             ? resource
             : NULL;
}

VOID AdmissionUmdSetError(ADMISSION_UMD_DEVICE *Device, HRESULT Error) {
  if (Device != NULL && Device->UserCallbacks != NULL &&
      Device->UserCallbacks->pfnSetErrorCb != NULL)
    Device->UserCallbacks->pfnSetErrorCb(Device->RuntimeCoreLayer, Error);
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

static BOOLEAN AdmissionUmdDescribePrimary(
    const D3D11DDIARG_CREATERESOURCE *CreateResource,
    ADMISSION_UMD_DIRECT_FLIP_RESOURCE *Description) {
  if (CreateResource == NULL || Description == NULL ||
      CreateResource->pMipInfoList == NULL ||
      CreateResource->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D ||
      CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
      CreateResource->pMipInfoList[0].TexelWidth != 2560u ||
      CreateResource->pMipInfoList[0].TexelHeight != 1600u ||
      CreateResource->MipLevels != 1u || CreateResource->ArraySize != 1u ||
      CreateResource->SampleDesc.Count != 1u ||
      CreateResource->SampleDesc.Quality != 0u ||
      (CreateResource->BindFlags & D3D10_DDI_BIND_PRESENT) == 0u ||
      CreateResource->pInitialDataUP != NULL)
    return FALSE;
  ZeroMemory(Description, sizeof(*Description));
  Description->Magic = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC;
  Description->Version = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION;
  if (!AdmissionAllocationDescribe(
          2560u, 1600u, 4u, (UINT)D3DKMDT_GDISURFACE_TEXTURE,
          (UINT)D3DDDIFMT_A8R8G8B8, 0u, &Description->Allocation))
    return FALSE;
  Description->SegmentId = 2u;
  Description->Linear = 1u;
  Description->Displayable = 1u;
  return TRUE;
}

static BOOLEAN AdmissionUmdResourceIsExact(
    const ADMISSION_UMD_RESOURCE *Resource) {
  return Resource != NULL && Resource->KernelAllocation != 0u &&
                 AdmissionUmdDirectFlipCompatible(
                     &Resource->DirectFlip, &Resource->DirectFlip, 0u,
                     (UINT)D3DDDIFMT_A8R8G8B8)
             ? TRUE
             : FALSE;
}

static HRESULT AdmissionUmdSubmitClear(
    ADMISSION_UMD_DEVICE *Device, ADMISSION_UMD_RESOURCE *Resource,
    const AGX_WIN32_CLEAR_REQUEST *Request) {
  D3DDDICB_RENDER render;
  APPLE_AGX_U32 commandBytes = 0u;
  APPLE_AGX_WIN32_ABI_RESULT build;
  HRESULT result;
  if (Device == NULL || !AdmissionUmdResourceIsExact(Resource) ||
      Request == NULL || Request->Generation != Device->Win32Generation ||
      Device->KernelCallbacks == NULL ||
      Device->KernelCallbacks->pfnRenderCb == NULL ||
      Device->KernelContext == NULL || Device->CommandBuffer == NULL ||
      Device->CommandBufferSize < sizeof(APPLE_AGX_WIN32_COMMAND_HEADER) ||
      Device->AllocationList == NULL || Device->AllocationListSize < 1u ||
      Device->PatchList == NULL)
    return E_INVALIDARG;
  build = AgxWin32TransportBuildClear(
      Request, Device->CommandBuffer, Device->CommandBufferSize,
      &commandBytes);
  if (build != AppleAgxWin32AbiSuccess)
    return E_INVALIDARG;
  ZeroMemory(&Device->AllocationList[0], sizeof(Device->AllocationList[0]));
  Device->AllocationList[0].hAllocation = Resource->KernelAllocation;
  Device->AllocationList[0].WriteOperation = 1u;
  ZeroMemory(&render, sizeof(render));
  render.CommandLength = commandBytes;
  render.CommandOffset = 0u;
  render.NumAllocations = 1u;
  render.NumPatchLocations = 0u;
  render.hContext = Device->KernelContext;
  result = Device->KernelCallbacks->pfnRenderCb(
      Device->RuntimeDevice.handle, &render);
  if (FAILED(result))
    return result;
  if (render.pNewCommandBuffer == NULL || render.NewCommandBufferSize == 0u ||
      render.pNewAllocationList == NULL || render.NewAllocationListSize == 0u ||
      render.pNewPatchLocationList == NULL ||
      render.NewPatchLocationListSize == 0u)
    return E_FAIL;
  Device->CommandBuffer = render.pNewCommandBuffer;
  Device->CommandBufferSize = render.NewCommandBufferSize;
  Device->AllocationList = render.pNewAllocationList;
  Device->AllocationListSize = render.NewAllocationListSize;
  Device->PatchList = render.pNewPatchLocationList;
  Device->PatchListSize = render.NewPatchLocationListSize;
  return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved) {
  UNREFERENCED_PARAMETER(Instance);
  UNREFERENCED_PARAMETER(Reason);
  UNREFERENCED_PARAMETER(Reserved);
  return TRUE;
}

HRESULT APIENTRY OpenAdapter10_2(
    D3D10DDIARG_OPENADAPTER *OpenAdapter) {
  ADMISSION_UMD_ADAPTER *adapter;
  D3D10_2DDI_ADAPTERFUNCS functions;
  D3DDDICB_QUERYADAPTERINFO query;
  HRESULT queryResult;
  ADMISSION_UMD_TRACE(L"OpenAdapter10_2 ENTER");
  if (OpenAdapter == NULL || OpenAdapter->pAdapterFuncs_2 == NULL ||
      OpenAdapter->pAdapterCallbacks == NULL)
    return E_INVALIDARG;
  adapter = (ADMISSION_UMD_ADAPTER *)HeapAlloc(
      GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
  if (adapter == NULL)
    return E_OUTOFMEMORY;
  adapter->Magic = ADMISSION_UMD_ADAPTER_MAGIC;
  adapter->RuntimeAdapter = OpenAdapter->hRTAdapter;
  adapter->Interface = OpenAdapter->Interface;
  adapter->Version = OpenAdapter->Version;
  adapter->Callbacks = OpenAdapter->pAdapterCallbacks;
  if (adapter->Callbacks->pfnQueryAdapterInfoCb == NULL) {
    HeapFree(GetProcessHeap(), 0u, adapter);
    return E_INVALIDARG;
  }
  ZeroMemory(&query, sizeof(query));
  query.pPrivateDriverData = &adapter->DeviceInfo;
  query.PrivateDriverDataSize = sizeof(adapter->DeviceInfo);
  queryResult = adapter->Callbacks->pfnQueryAdapterInfoCb(
      adapter->RuntimeAdapter.handle, &query);
  if (FAILED(queryResult) || !AgxWin32DeviceInfoValid(&adapter->DeviceInfo)) {
    HeapFree(GetProcessHeap(), 0u, adapter);
    return FAILED(queryResult) ? queryResult : E_FAIL;
  }
  ZeroMemory(&functions, sizeof(functions));
  functions.pfnCalcPrivateDeviceSize = AdmissionUmdCalcPrivateDeviceSize;
  functions.pfnCreateDevice = AdmissionUmdCreateDevice;
  functions.pfnCloseAdapter = AdmissionUmdCloseAdapter;
  functions.pfnGetSupportedVersions = AdmissionUmdGetSupportedVersions;
  functions.pfnGetCaps = AdmissionUmdGetCaps;
  *OpenAdapter->pAdapterFuncs_2 = functions;
  OpenAdapter->hAdapter.pDrvPrivate = adapter;
  return S_OK;
}

static SIZE_T APIENTRY AdmissionUmdCalcPrivateDeviceSize(
    D3D10DDI_HADAPTER Adapter,
    const D3D10DDIARG_CALCPRIVATEDEVICESIZE *Args) {
  return AdmissionUmdAdapterFromHandle(Adapter) != NULL && Args != NULL &&
                 Args->Interface == D3DWDDM1_3_DDI_INTERFACE_VERSION
             ? sizeof(ADMISSION_UMD_DEVICE)
             : 0u;
}

static HRESULT APIENTRY AdmissionUmdCreateDevice(
    D3D10DDI_HADAPTER Adapter, D3D10DDIARG_CREATEDEVICE *Args) {
  ADMISSION_UMD_ADAPTER *adapter = AdmissionUmdAdapterFromHandle(Adapter);
  ADMISSION_UMD_DEVICE *device;
  ADMISSION_WIN32_CONTEXT_CREATE win32Context;
  D3DDDICB_CREATECONTEXT createContext;
  D3DWDDM1_3DDI_DEVICEFUNCS *deviceFunctions;
  DXGI1_3_DDI_BASE_FUNCTIONS *dxgiFunctions;
  HRESULT result;
  ADMISSION_UMD_TRACE(L"CreateDevice ENTER");
  if (adapter == NULL || Args == NULL || Args->hDrvDevice.pDrvPrivate == NULL ||
      Args->Interface != D3DWDDM1_3_DDI_INTERFACE_VERSION ||
      Args->pWDDM1_3DeviceFuncs == NULL || Args->pKTCallbacks == NULL ||
      Args->pKTCallbacks->pfnCreateContextCb == NULL ||
      Args->pKTCallbacks->pfnDestroyContextCb == NULL ||
      Args->pKTCallbacks->pfnAllocateCb == NULL ||
      Args->pKTCallbacks->pfnDeallocateCb == NULL ||
      Args->pKTCallbacks->pfnRenderCb == NULL ||
      Args->p11UMCallbacks == NULL ||
      Args->DXGIBaseDDI.pDXGIBaseCallbacks == NULL ||
      Args->DXGIBaseDDI.pDXGIDDIBaseFunctions4 == NULL)
    return E_INVALIDARG;
  device = (ADMISSION_UMD_DEVICE *)Args->hDrvDevice.pDrvPrivate;
  ZeroMemory(device, sizeof(*device));
  device->Magic = ADMISSION_UMD_DEVICE_MAGIC;
  device->Adapter = adapter;
  device->RuntimeDevice = Args->hRTDevice;
  device->RuntimeCoreLayer = Args->hRTCoreLayer;
  device->KernelCallbacks = Args->pKTCallbacks;
  device->UserCallbacks = Args->p11UMCallbacks;
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

  deviceFunctions = Args->pWDDM1_3DeviceFuncs;
  ZeroMemory(deviceFunctions, sizeof(*deviceFunctions));
  deviceFunctions->pfnCalcPrivateResourceSize =
      AdmissionUmdCalcPrivateResourceSize;
  deviceFunctions->pfnCalcPrivateOpenedResourceSize =
      AdmissionUmdCalcPrivateOpenedResourceSize;
  deviceFunctions->pfnCreateResource = AdmissionUmdCreateResource;
  deviceFunctions->pfnOpenResource = AdmissionUmdOpenResource;
  deviceFunctions->pfnDestroyResource = AdmissionUmdDestroyResource;
  deviceFunctions->pfnCheckFormatSupport = AdmissionUmdCheckFormatSupport;
  deviceFunctions->pfnFlush = AdmissionUmdFlush;
  deviceFunctions->pfnDestroyDevice = AdmissionUmdDestroyDevice;
  deviceFunctions->pfnCheckDirectFlipSupport =
      AdmissionUmdCheckDirectFlipSupport;

  dxgiFunctions = Args->DXGIBaseDDI.pDXGIDDIBaseFunctions4;
  ZeroMemory(dxgiFunctions, sizeof(*dxgiFunctions));
  dxgiFunctions->pfnPresent = AdmissionUmdPresent;
  dxgiFunctions->pfnSetDisplayMode = AdmissionUmdSetDisplayMode;
  dxgiFunctions->pfnRotateResourceIdentities =
      AdmissionUmdRotateResourceIdentities;
  dxgiFunctions->pfnPresent1 = AdmissionUmdPresent1;
  return S_OK;
}

static HRESULT APIENTRY AdmissionUmdCloseAdapter(D3D10DDI_HADAPTER Adapter) {
  ADMISSION_UMD_ADAPTER *adapter = AdmissionUmdAdapterFromHandle(Adapter);
  if (adapter == NULL)
    return E_INVALIDARG;
  adapter->Magic = 0u;
  HeapFree(GetProcessHeap(), 0u, adapter);
  return S_OK;
}

static HRESULT APIENTRY AdmissionUmdGetSupportedVersions(
    D3D10DDI_HADAPTER Adapter, UINT32 *Entries, UINT64 *Versions) {
  ADMISSION_UMD_TRACE(L"GetSupportedVersions ENTER");
  if (AdmissionUmdAdapterFromHandle(Adapter) == NULL || Entries == NULL)
    return E_INVALIDARG;
  if (Versions != NULL && *Entries < 1u) {
    *Entries = 1u;
    return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
  }
  *Entries = 1u;
  if (Versions != NULL)
    Versions[0] = D3DWDDM1_3_DDI_SUPPORTED;
  return S_OK;
}

static HRESULT APIENTRY AdmissionUmdGetCaps(
    D3D10DDI_HADAPTER Adapter, const D3D10_2DDIARG_GETCAPS *Caps) {
  ADMISSION_UMD_TRACE(L"GetCaps ENTER");
  if (AdmissionUmdAdapterFromHandle(Adapter) == NULL || Caps == NULL ||
      Caps->pData == NULL)
    return E_INVALIDARG;
  switch (Caps->Type) {
  case D3D11DDICAPS_THREADING:
    if (Caps->DataSize != sizeof(D3D11DDI_THREADING_CAPS))
      return E_INVALIDARG;
    ((D3D11DDI_THREADING_CAPS *)Caps->pData)->Caps = 0u;
    return S_OK;
  case D3D11DDICAPS_3DPIPELINESUPPORT:
    if (Caps->DataSize != sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS))
      return E_INVALIDARG;
    ADMISSION_UMD_TRACE(L"GetCaps PIPELINE: no implemented level");
    ((D3D11DDI_3DPIPELINESUPPORT_CAPS *)Caps->pData)->Caps = 0u;
    return S_OK;
  default:
    return E_NOTIMPL;
  }
}

static SIZE_T APIENTRY AdmissionUmdCalcPrivateResourceSize(
    D3D10DDI_HDEVICE Device,
    const D3D11DDIARG_CREATERESOURCE *CreateResource) {
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE description;
  return AdmissionUmdDeviceFromHandle(Device) != NULL &&
                 AdmissionUmdDescribePrimary(CreateResource, &description)
             ? sizeof(ADMISSION_UMD_RESOURCE)
             : 0u;
}

static SIZE_T APIENTRY AdmissionUmdCalcPrivateOpenedResourceSize(
    D3D10DDI_HDEVICE Device,
    const D3D10DDIARG_OPENRESOURCE *OpenResource) {
  return AdmissionUmdDeviceFromHandle(Device) != NULL &&
                 OpenResource != NULL && OpenResource->NumAllocations == 1u
             ? sizeof(ADMISSION_UMD_RESOURCE)
             : 0u;
}

static VOID APIENTRY AdmissionUmdCreateResource(
    D3D10DDI_HDEVICE DeviceHandle,
    const D3D11DDIARG_CREATERESOURCE *CreateResource,
    D3D10DDI_HRESOURCE ResourceHandle,
    D3D10DDI_HRTRESOURCE RuntimeResource) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RESOURCE *resource =
      (ADMISSION_UMD_RESOURCE *)ResourceHandle.pDrvPrivate;
  D3DDDICB_ALLOCATE allocate;
  D3DDDI_ALLOCATIONINFO allocationInfo;
  ADMISSION_UMD_DIRECT_FLIP_RESOURCE description;
  ADMISSION_UMD_RETIREMENT *retirement;
  HRESULT result;
  if (device == NULL || resource == NULL ||
      RuntimeResource.handle == NULL ||
      !AdmissionUmdDescribePrimary(CreateResource, &description)) {
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  retirement = AdmissionUmdRetirementCreate();
  if (retirement == NULL) {
    AdmissionUmdSetError(device, E_OUTOFMEMORY);
    return;
  }
  ZeroMemory(resource, sizeof(*resource));
  ZeroMemory(&allocate, sizeof(allocate));
  ZeroMemory(&allocationInfo, sizeof(allocationInfo));
  allocationInfo.pPrivateDriverData = &description.Allocation;
  allocationInfo.PrivateDriverDataSize = sizeof(description.Allocation);
  allocationInfo.VidPnSourceId = 0u;
  allocationInfo.Flags.Primary = CreateResource->pPrimaryDesc != NULL;
  allocate.hResource = RuntimeResource.handle;
  allocate.NumAllocations = 1u;
  allocate.pAllocationInfo = &allocationInfo;
  result = device->KernelCallbacks->pfnAllocateCb(
      device->RuntimeDevice.handle, &allocate);
  if (FAILED(result) || allocationInfo.hAllocation == 0u) {
    AdmissionUmdRetirementFree(retirement);
    AdmissionUmdSetError(device, FAILED(result) ? result : E_FAIL);
    return;
  }
  resource->Magic = ADMISSION_UMD_RESOURCE_MAGIC;
  resource->RuntimeResource = RuntimeResource;
  resource->KernelAllocation = allocationInfo.hAllocation;
  resource->DirectFlip = description;
  retirement->RuntimeResource = RuntimeResource.handle;
  retirement->KernelResource = allocate.hKMResource;
  retirement->KernelAllocation = allocationInfo.hAllocation;
  retirement->Primary = CreateResource->pPrimaryDesc != NULL;
  retirement->Shared =
      (CreateResource->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED) != 0u;
  resource->Retirement = retirement;
}

static VOID APIENTRY AdmissionUmdOpenResource(
    D3D10DDI_HDEVICE DeviceHandle,
    const D3D10DDIARG_OPENRESOURCE *OpenResource,
    D3D10DDI_HRESOURCE ResourceHandle,
    D3D10DDI_HRTRESOURCE RuntimeResource) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RESOURCE *resource =
      (ADMISSION_UMD_RESOURCE *)ResourceHandle.pDrvPrivate;
  const D3DDDI_OPENALLOCATIONINFO *info;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  ADMISSION_UMD_RETIREMENT *retirement;
  if (device == NULL || resource == NULL || OpenResource == NULL ||
      RuntimeResource.handle == NULL ||
      OpenResource->NumAllocations != 1u ||
      OpenResource->pOpenAllocationInfo == NULL) {
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  retirement = AdmissionUmdRetirementCreate();
  if (retirement == NULL) {
    AdmissionUmdSetError(device, E_OUTOFMEMORY);
    return;
  }
  info = &OpenResource->pOpenAllocationInfo[0];
  description =
      (const ADMISSION_ALLOCATION_DESCRIPTION *)info->pPrivateDriverData;
  if (description == NULL ||
      info->PrivateDriverDataSize != sizeof(*description) ||
      !AdmissionAllocationDescriptionValid(description) ||
      description->Format != (UINT)D3DDDIFMT_A8R8G8B8 ||
      description->Width != 2560u || description->Height != 1600u ||
      description->Pitch != 10240u || description->BytesPerPixel != 4u ||
      description->Size != 0xfa0000ULL || info->hAllocation == 0u) {
    AdmissionUmdRetirementFree(retirement);
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  ZeroMemory(resource, sizeof(*resource));
  resource->Magic = ADMISSION_UMD_RESOURCE_MAGIC;
  resource->RuntimeResource = RuntimeResource;
  resource->KernelAllocation = info->hAllocation;
  resource->DirectFlip.Magic = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC;
  resource->DirectFlip.Version = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION;
  resource->DirectFlip.Allocation = *description;
  resource->DirectFlip.SegmentId = 2u;
  resource->DirectFlip.Linear = 1u;
  resource->DirectFlip.Displayable = 1u;
  retirement->RuntimeResource = RuntimeResource.handle;
  retirement->KernelResource = OpenResource->hKMResource.handle;
  retirement->KernelAllocation = info->hAllocation;
  retirement->Primary = FALSE;
  retirement->Shared = TRUE;
  resource->Retirement = retirement;
}

static VOID APIENTRY AdmissionUmdDestroyResource(
    D3D10DDI_HDEVICE DeviceHandle, D3D10DDI_HRESOURCE ResourceHandle) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RESOURCE *resource =
      AdmissionUmdResourceFromHandle(ResourceHandle);
  ADMISSION_UMD_RETIREMENT *retirement;
  HRESULT result;
  if (device == NULL || resource == NULL) {
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  retirement = resource->Retirement;
  ZeroMemory(resource, sizeof(*resource));
  if (retirement == NULL)
    return;
  if (retirement->Primary && !retirement->Shared) {
    result = AdmissionUmdRetirementDeallocate(&device->Retirement, retirement);
    if (FAILED(result)) {
      AdmissionUmdRetirementQueue(&device->Retirement, retirement);
      AdmissionUmdSetError(device, result);
      return;
    }
    AdmissionUmdRetirementFree(retirement);
  } else {
    AdmissionUmdRetirementQueue(&device->Retirement, retirement);
  }
}

static VOID APIENTRY AdmissionUmdCheckFormatSupport(
    D3D10DDI_HDEVICE DeviceHandle, DXGI_FORMAT Format,
    UINT *FormatSupport) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  if (device == NULL || FormatSupport == NULL) {
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  if (Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
    *FormatSupport = 0u;
    return;
  }
  *FormatSupport = D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET;
}

static BOOL APIENTRY AdmissionUmdFlush(D3D10DDI_HDEVICE DeviceHandle,
                                       UINT FlushFlags) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  UNREFERENCED_PARAMETER(FlushFlags);
  if (device == NULL)
    return FALSE;
  return AdmissionUmdRetirementDrain(&device->Retirement);
}

static VOID APIENTRY AdmissionUmdDestroyDevice(D3D10DDI_HDEVICE DeviceHandle) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RETIREMENT_FINALIZE_RESULT retirement;
  D3DDDICB_DESTROYCONTEXT destroyContext;
  if (device == NULL)
    return;
  AdmissionUmdRetirementFinalize(&device->Retirement, &retirement);
  if (retirement.Undeallocated != 0u) {
    device->LastRetirementError = retirement.LastError;
    device->RetirementErrorCount += retirement.Undeallocated;
    device->RetirementUndeallocated = retirement.Undeallocated;
    device->RetirementTerminal = TRUE;
    AdmissionUmdSetError(device, retirement.FirstError);
  }
  if (device->KernelContext != NULL && device->KernelCallbacks != NULL &&
      device->KernelCallbacks->pfnDestroyContextCb != NULL) {
    ZeroMemory(&destroyContext, sizeof(destroyContext));
    destroyContext.hContext = device->KernelContext;
    (void)device->KernelCallbacks->pfnDestroyContextCb(
        device->RuntimeDevice.handle, &destroyContext);
  }
  ZeroMemory(device, sizeof(*device));
}

static VOID APIENTRY AdmissionUmdCheckDirectFlipSupport(
    D3D10DDI_HDEVICE DeviceHandle, D3D10DDI_HRESOURCE CurrentHandle,
    D3D10DDI_HRESOURCE CandidateHandle, UINT Flags, BOOL *Supported) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RESOURCE *current =
      AdmissionUmdResourceFromHandle(CurrentHandle);
  ADMISSION_UMD_RESOURCE *candidate =
      AdmissionUmdResourceFromHandle(CandidateHandle);
  if (Supported == NULL) {
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  *Supported = device != NULL && current != NULL && candidate != NULL &&
                       AdmissionUmdDirectFlipCompatible(
                           &current->DirectFlip, &candidate->DirectFlip,
                           Flags, (UINT)D3DDDIFMT_A8R8G8B8)
                   ? TRUE
                   : FALSE;
}

static HRESULT AdmissionUmdSubmitPresent(ADMISSION_UMD_DEVICE *Device,
                                         ADMISSION_UMD_RESOURCE *Source,
                                         PVOID DxgiContext) {
  DXGIDDICB_PRESENT present;
  if (Device == NULL || !AdmissionUmdResourceIsExact(Source) ||
      Device->DxgiCallbacks == NULL ||
      Device->DxgiCallbacks->pfnPresentCb == NULL ||
      Device->KernelContext == NULL)
    return E_INVALIDARG;
  ZeroMemory(&present, sizeof(present));
  present.hSrcAllocation = Source->KernelAllocation;
  present.pDXGIContext = DxgiContext;
  present.hContext = Device->KernelContext;
  return Device->DxgiCallbacks->pfnPresentCb(
      Device->RuntimeDevice.handle, &present);
}

static HRESULT APIENTRY AdmissionUmdPresent(DXGI_DDI_ARG_PRESENT *Args) {
  ADMISSION_UMD_DEVICE *device;
  ADMISSION_UMD_RESOURCE *source;
  if (Args == NULL || Args->hDstResource != 0u ||
      Args->SrcSubResourceIndex != 0u || Args->Flags.Value != 0x2u ||
      Args->FlipInterval != DXGI_DDI_FLIP_INTERVAL_ONE)
    return E_INVALIDARG;
  device = AdmissionUmdDeviceFromDxgi(Args->hDevice);
  source = AdmissionUmdResourceFromDxgi(Args->hSurfaceToPresent);
  return AdmissionUmdSubmitPresent(device, source, Args->pDXGIContext);
}

static HRESULT APIENTRY AdmissionUmdPresent1(DXGI_DDI_ARG_PRESENT1 *Args) {
  ADMISSION_UMD_DEVICE *device;
  ADMISSION_UMD_RESOURCE *source;
  if (Args == NULL || Args->hDstResource != 0u ||
      Args->SurfacesToPresent != 1u || Args->phSurfacesToPresent == NULL ||
      Args->phSurfacesToPresent[0].SubResourceIndex != 0u ||
      Args->Flags.Value != 0x2u ||
      Args->FlipInterval != DXGI_DDI_FLIP_INTERVAL_ONE ||
      Args->Reserved != 0u || Args->DirtyRects != 0u)
    return E_INVALIDARG;
  device = AdmissionUmdDeviceFromDxgi(Args->hDevice);
  source = AdmissionUmdResourceFromDxgi(
      Args->phSurfacesToPresent[0].hSurface);
  return AdmissionUmdSubmitPresent(device, source, Args->pDXGIContext);
}

static HRESULT APIENTRY AdmissionUmdSetDisplayMode(
    DXGI_DDI_ARG_SETDISPLAYMODE *Args) {
  ADMISSION_UMD_DEVICE *device;
  ADMISSION_UMD_RESOURCE *resource;
  D3DDDICB_SETDISPLAYMODE setMode;
  if (Args == NULL || Args->SubResourceIndex != 0u)
    return E_INVALIDARG;
  device = AdmissionUmdDeviceFromDxgi(Args->hDevice);
  resource = AdmissionUmdResourceFromDxgi(Args->hResource);
  if (device == NULL || !AdmissionUmdResourceIsExact(resource) ||
      device->KernelCallbacks == NULL ||
      device->KernelCallbacks->pfnSetDisplayModeCb == NULL)
    return E_INVALIDARG;
  ZeroMemory(&setMode, sizeof(setMode));
  setMode.hPrimaryAllocation = resource->KernelAllocation;
  return device->KernelCallbacks->pfnSetDisplayModeCb(
      device->RuntimeDevice.handle, &setMode);
}

static HRESULT APIENTRY AdmissionUmdRotateResourceIdentities(
    DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *Args) {
  ADMISSION_UMD_DEVICE *device;
  ADMISSION_UMD_RESOURCE *first;
  D3DKMT_HANDLE saved;
  UINT index;
  if (Args == NULL || Args->Resources < 2u || Args->pResources == NULL)
    return E_INVALIDARG;
  device = AdmissionUmdDeviceFromDxgi(Args->hDevice);
  first = AdmissionUmdResourceFromDxgi(Args->pResources[0]);
  if (device == NULL || !AdmissionUmdResourceIsExact(first))
    return E_INVALIDARG;
  for (index = 1u; index < Args->Resources; ++index) {
    ADMISSION_UMD_RESOURCE *resource =
        AdmissionUmdResourceFromDxgi(Args->pResources[index]);
    if (resource == NULL ||
        !AdmissionUmdDirectFlipCompatible(
            &first->DirectFlip, &resource->DirectFlip, 0u,
            (UINT)D3DDDIFMT_A8R8G8B8))
      return E_INVALIDARG;
  }
  saved = first->KernelAllocation;
  for (index = 0u; index + 1u < Args->Resources; ++index) {
    ADMISSION_UMD_RESOURCE *to =
        AdmissionUmdResourceFromDxgi(Args->pResources[index]);
    ADMISSION_UMD_RESOURCE *from =
        AdmissionUmdResourceFromDxgi(Args->pResources[index + 1u]);
    to->KernelAllocation = from->KernelAllocation;
  }
  AdmissionUmdResourceFromDxgi(
      Args->pResources[Args->Resources - 1u])->KernelAllocation = saved;
  return S_OK;
}

#undef ADMISSION_UMD_TRACE
