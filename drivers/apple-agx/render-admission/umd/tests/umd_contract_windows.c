#include <windows.h>
#include <wingdi.h>

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)

#include "direct_flip_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/umd.c"

typedef struct _TEST_STATE {
  unsigned int Failures;
  unsigned int CreateContextCalls;
  unsigned int DestroyContextCalls;
  unsigned int AllocateCalls;
  unsigned int DeallocateCalls;
  unsigned int SetErrorCalls;
  unsigned int FailDeallocations;
  HANDLE LastAllocateResource;
  HANDLE DeallocateResources[8];
  HRESULT DeallocateResults[8];
} TEST_STATE;

static TEST_STATE State;
static unsigned char CommandBuffer[4096];
static D3DDDI_ALLOCATIONLIST AllocationList[16];
static D3DDDI_PATCHLOCATIONLIST PatchList[16];

#define CHECK(value)                                                          \
  do {                                                                        \
    if (!(value)) {                                                           \
      fprintf(stderr, "CHECK_FAIL line=%u expression=%s\n",                 \
              (unsigned int)__LINE__, #value);                                \
      ++State.Failures;                                                       \
    }                                                                         \
  } while (0)

static HRESULT APIENTRY TestCreateContext(HANDLE Device,
                                          D3DDDICB_CREATECONTEXT *Create) {
  (void)Device;
  ++State.CreateContextCalls;
  Create->hContext = (HANDLE)(UINT_PTR)0x200u;
  Create->pCommandBuffer = CommandBuffer;
  Create->CommandBufferSize = sizeof(CommandBuffer);
  Create->pAllocationList = AllocationList;
  Create->AllocationListSize = ARRAYSIZE(AllocationList);
  Create->pPatchLocationList = PatchList;
  Create->PatchLocationListSize = ARRAYSIZE(PatchList);
  return S_OK;
}

static HRESULT APIENTRY TestDestroyContext(
    HANDLE Device, const D3DDDICB_DESTROYCONTEXT *Destroy) {
  (void)Device;
  ++State.DestroyContextCalls;
  CHECK(Destroy != NULL);
  CHECK(Destroy != NULL && Destroy->hContext == (HANDLE)(UINT_PTR)0x200u);
  return S_OK;
}

static HRESULT APIENTRY TestAllocate(HANDLE Device,
                                     D3DDDICB_ALLOCATE *Allocate) {
  (void)Device;
  ++State.AllocateCalls;
  State.LastAllocateResource = Allocate->hResource;
  CHECK(Allocate->NumAllocations == 1u);
  CHECK(Allocate->pAllocationInfo != NULL);
  Allocate->hKMResource = 0x700u + State.AllocateCalls;
  if (Allocate->pAllocationInfo != NULL)
    Allocate->pAllocationInfo[0].hAllocation = 0x800u + State.AllocateCalls;
  return S_OK;
}

static HRESULT APIENTRY TestDeallocate(
    HANDLE Device, const D3DDDICB_DEALLOCATE *Deallocate) {
  HRESULT result = S_OK;
  unsigned int index = State.DeallocateCalls++;
  (void)Device;
  CHECK(index < ARRAYSIZE(State.DeallocateResources));
  if (index < ARRAYSIZE(State.DeallocateResources))
    State.DeallocateResources[index] = Deallocate->hResource;
  CHECK(Deallocate->hResource != NULL);
  CHECK(Deallocate->NumAllocations == 0u);
  CHECK(Deallocate->HandleList == NULL);
  if (State.FailDeallocations != 0u) {
    --State.FailDeallocations;
    result = E_FAIL;
  }
  if (index < ARRAYSIZE(State.DeallocateResults))
    State.DeallocateResults[index] = result;
  return result;
}

static VOID APIENTRY TestSetError(D3D10DDI_HRTCORELAYER CoreLayer,
                                  HRESULT Error) {
  CHECK(CoreLayer.handle == (VOID *)(UINT_PTR)0x102u);
  CHECK(FAILED(Error));
  ++State.SetErrorCalls;
}

static void initialize_create_resource(
    D3D11DDIARG_CREATERESOURCE *Create,
    D3D10DDI_MIPINFO *Mip,
    DXGI_DDI_PRIMARY_DESC *Primary) {
  memset(Create, 0, sizeof(*Create));
  memset(Mip, 0, sizeof(*Mip));
  memset(Primary, 0, sizeof(*Primary));
  Mip->TexelWidth = 2560u;
  Mip->TexelHeight = 1600u;
  Mip->TexelDepth = 1u;
  Mip->PhysicalWidth = 2560u;
  Mip->PhysicalHeight = 1600u;
  Mip->PhysicalDepth = 1u;
  Create->pMipInfoList = Mip;
  Create->ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
  Create->Usage = D3D10_DDI_USAGE_DEFAULT;
  Create->BindFlags = D3D10_DDI_BIND_PRESENT;
  Create->Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  Create->SampleDesc.Count = 1u;
  Create->MipLevels = 1u;
  Create->ArraySize = 1u;
  Create->pPrimaryDesc = Primary;
}

static void initialize_open_resource(
    D3D10DDIARG_OPENRESOURCE *Open,
    D3DDDI_OPENALLOCATIONINFO *Info,
    ADMISSION_ALLOCATION_DESCRIPTION *Description,
    D3DKMT_HANDLE AllocationHandle,
    D3DKMT_HANDLE ResourceHandle) {
  memset(Open, 0, sizeof(*Open));
  memset(Info, 0, sizeof(*Info));
  memset(Description, 0, sizeof(*Description));
  CHECK(AdmissionAllocationDescribe(
      2560u, 1600u, 4u, (unsigned int)D3DKMDT_GDISURFACE_TEXTURE,
      (unsigned int)D3DDDIFMT_A8R8G8B8, 0u, Description));
  Info->hAllocation = AllocationHandle;
  Info->pPrivateDriverData = Description;
  Info->PrivateDriverDataSize = sizeof(*Description);
  Open->NumAllocations = 1u;
  Open->pOpenAllocationInfo = Info;
  Open->hKMResource.handle = ResourceHandle;
}

static D3D10DDI_HRESOURCE create_primary(
    D3DWDDM1_3DDI_DEVICEFUNCS *Functions, D3D10DDI_HDEVICE Device,
    D3D10DDI_HRTRESOURCE RuntimeResource) {
  D3D11DDIARG_CREATERESOURCE create;
  D3D10DDI_MIPINFO mip;
  DXGI_DDI_PRIMARY_DESC primary;
  D3D10DDI_HRESOURCE resource;
  SIZE_T bytes;
  initialize_create_resource(&create, &mip, &primary);
  bytes = Functions->pfnCalcPrivateResourceSize(Device, &create);
  CHECK(bytes != 0u);
  resource.pDrvPrivate = calloc(1u, bytes);
  CHECK(resource.pDrvPrivate != NULL);
  if (resource.pDrvPrivate != NULL)
    Functions->pfnCreateResource(Device, &create, resource, RuntimeResource);
  return resource;
}

static D3D10DDI_HRESOURCE open_resource(
    D3DWDDM1_3DDI_DEVICEFUNCS *Functions, D3D10DDI_HDEVICE Device,
    D3D10DDI_HRTRESOURCE RuntimeResource, D3DKMT_HANDLE AllocationHandle,
    D3DKMT_HANDLE ResourceHandle) {
  D3D10DDIARG_OPENRESOURCE open;
  D3DDDI_OPENALLOCATIONINFO info;
  ADMISSION_ALLOCATION_DESCRIPTION description;
  D3D10DDI_HRESOURCE resource;
  SIZE_T bytes;
  initialize_open_resource(&open, &info, &description, AllocationHandle,
                           ResourceHandle);
  bytes = Functions->pfnCalcPrivateOpenedResourceSize(Device, &open);
  CHECK(bytes != 0u);
  resource.pDrvPrivate = calloc(1u, bytes);
  CHECK(resource.pDrvPrivate != NULL);
  if (resource.pDrvPrivate != NULL)
    Functions->pfnOpenResource(Device, &open, resource, RuntimeResource);
  return resource;
}

int main(void) {
  D3DDDI_ADAPTERCALLBACKS adapterCallbacks;
  D3D10_2DDI_ADAPTERFUNCS adapterFunctions;
  D3D10DDIARG_OPENADAPTER openAdapter;
  D3D11DDI_3DPIPELINESUPPORT_CAPS pipelineCaps;
  D3D10_2DDIARG_GETCAPS getCaps;
  D3D10DDIARG_CALCPRIVATEDEVICESIZE calculateDevice;
  D3DDDI_DEVICECALLBACKS kernelCallbacks;
  D3D11DDI_CORELAYER_DEVICECALLBACKS userCallbacks;
  DXGI_DDI_BASE_CALLBACKS dxgiCallbacks;
  DXGI1_3_DDI_BASE_FUNCTIONS dxgiFunctions;
  D3DWDDM1_3DDI_DEVICEFUNCS deviceFunctions;
  D3D10DDIARG_CREATEDEVICE createDevice;
  D3D10DDI_HDEVICE device;
  D3D10DDI_HRTRESOURCE primaryRuntime;
  D3D10DDI_HRTRESOURCE openRuntime;
  D3D10DDI_HRTRESOURCE retryRuntime;
  D3D10DDI_HRESOURCE primary;
  D3D10DDI_HRESOURCE opened;
  D3D10DDI_HRESOURCE retry;
  SIZE_T deviceBytes;
  UINT formatSupport;
  UINT32 versionCount;
  UINT64 version;
  unsigned int errorsBefore;
  unsigned int deallocationsBefore;
  ADMISSION_UMD_DEVICE *deviceState;

  memset(&State, 0, sizeof(State));
  memset(&adapterCallbacks, 0, sizeof(adapterCallbacks));
  memset(&adapterFunctions, 0, sizeof(adapterFunctions));
  memset(&openAdapter, 0, sizeof(openAdapter));
  openAdapter.hRTAdapter.handle = (VOID *)(UINT_PTR)0x100u;
  openAdapter.Interface = D3DWDDM1_3_DDI_INTERFACE_VERSION;
  openAdapter.Version = 0u;
  openAdapter.pAdapterCallbacks = &adapterCallbacks;
  openAdapter.pAdapterFuncs_2 = &adapterFunctions;
  CHECK(OpenAdapter10_2(&openAdapter) == S_OK);

  versionCount = 1u;
  version = 0u;
  CHECK(adapterFunctions.pfnGetSupportedVersions(
            openAdapter.hAdapter, &versionCount, &version) == S_OK);
  CHECK(versionCount == 1u);
  CHECK(version == D3DWDDM1_3_DDI_SUPPORTED);

  memset(&pipelineCaps, 0xff, sizeof(pipelineCaps));
  memset(&getCaps, 0, sizeof(getCaps));
  getCaps.Type = D3D11DDICAPS_3DPIPELINESUPPORT;
  getCaps.pData = &pipelineCaps;
  getCaps.DataSize = sizeof(pipelineCaps);
  CHECK(adapterFunctions.pfnGetCaps(openAdapter.hAdapter, &getCaps) == S_OK);
  CHECK(pipelineCaps.Caps == 0u);

  memset(&calculateDevice, 0, sizeof(calculateDevice));
  calculateDevice.Interface = D3DWDDM1_3_DDI_INTERFACE_VERSION;
  calculateDevice.Version = 0u;
  deviceBytes = adapterFunctions.pfnCalcPrivateDeviceSize(
      openAdapter.hAdapter, &calculateDevice);
  CHECK(deviceBytes != 0u);
  device.pDrvPrivate = calloc(1u, deviceBytes);
  CHECK(device.pDrvPrivate != NULL);

  memset(&kernelCallbacks, 0, sizeof(kernelCallbacks));
  kernelCallbacks.pfnAllocateCb = TestAllocate;
  kernelCallbacks.pfnDeallocateCb = TestDeallocate;
  kernelCallbacks.pfnCreateContextCb = TestCreateContext;
  kernelCallbacks.pfnDestroyContextCb = TestDestroyContext;
  memset(&userCallbacks, 0, sizeof(userCallbacks));
  userCallbacks.pfnSetErrorCb = TestSetError;
  memset(&dxgiCallbacks, 0, sizeof(dxgiCallbacks));
  memset(&dxgiFunctions, 0, sizeof(dxgiFunctions));
  memset(&deviceFunctions, 0, sizeof(deviceFunctions));
  memset(&createDevice, 0, sizeof(createDevice));
  createDevice.hRTDevice.handle = (VOID *)(UINT_PTR)0x101u;
  createDevice.Interface = D3DWDDM1_3_DDI_INTERFACE_VERSION;
  createDevice.Version = 0u;
  createDevice.pKTCallbacks = &kernelCallbacks;
  createDevice.pWDDM1_3DeviceFuncs = &deviceFunctions;
  createDevice.hDrvDevice = device;
  createDevice.DXGIBaseDDI.pDXGIBaseCallbacks = &dxgiCallbacks;
  createDevice.DXGIBaseDDI.pDXGIDDIBaseFunctions4 = &dxgiFunctions;
  createDevice.hRTCoreLayer.handle = (VOID *)(UINT_PTR)0x102u;
  createDevice.p11UMCallbacks = &userCallbacks;
  CHECK(adapterFunctions.pfnCreateDevice(openAdapter.hAdapter,
                                         &createDevice) == S_OK);
  CHECK(State.CreateContextCalls == 1u);
  deviceState = (ADMISSION_UMD_DEVICE *)device.pDrvPrivate;

  errorsBefore = State.SetErrorCalls;
  formatSupport = 0xffffffffu;
  deviceFunctions.pfnCheckFormatSupport(
      device, DXGI_FORMAT_R8G8B8A8_UNORM, &formatSupport);
  CHECK(formatSupport == 0u);
  CHECK(State.SetErrorCalls == errorsBefore);

  primaryRuntime.handle = (VOID *)(UINT_PTR)0x300u;
  primary = create_primary(&deviceFunctions, device, primaryRuntime);
  CHECK(State.AllocateCalls == 1u);
  CHECK(State.LastAllocateResource == primaryRuntime.handle);
  if (primary.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, primary);
  CHECK(State.DeallocateCalls == 1u);
  CHECK(State.DeallocateResources[0] == primaryRuntime.handle);
  free(primary.pDrvPrivate);

  openRuntime.handle = (VOID *)(UINT_PTR)0x400u;
  opened = open_resource(&deviceFunctions, device, openRuntime, 0x900u, 0xa00u);
  deallocationsBefore = State.DeallocateCalls;
  if (opened.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, opened);
  CHECK(State.DeallocateCalls == deallocationsBefore);
  CHECK(deviceFunctions.pfnFlush != NULL);
  errorsBefore = State.SetErrorCalls;
  deviceState->Retirement.HasImmediateCommands = TRUE;
  if (deviceFunctions.pfnFlush != NULL)
    CHECK(!deviceFunctions.pfnFlush(device, 0u));
  CHECK(State.DeallocateCalls == deallocationsBefore);
  CHECK(State.SetErrorCalls == errorsBefore + 1u);
  deviceState->Retirement.HasImmediateCommands = FALSE;
  if (deviceFunctions.pfnFlush != NULL)
    CHECK(deviceFunctions.pfnFlush(device, 0u));
  CHECK(State.DeallocateCalls == deallocationsBefore + 1u);
  CHECK(State.DeallocateResources[deallocationsBefore] == openRuntime.handle);
  free(opened.pDrvPrivate);

  retryRuntime.handle = (VOID *)(UINT_PTR)0x500u;
  retry = open_resource(&deviceFunctions, device, retryRuntime, 0x901u, 0xa01u);
  if (retry.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, retry);
  State.FailDeallocations = 1u;
  deallocationsBefore = State.DeallocateCalls;
  errorsBefore = State.SetErrorCalls;
  if (deviceFunctions.pfnFlush != NULL)
    CHECK(!deviceFunctions.pfnFlush(device, 0u));
  CHECK(State.DeallocateCalls == deallocationsBefore + 1u);
  CHECK(State.SetErrorCalls == errorsBefore + 1u);
  free(retry.pDrvPrivate);

  deviceFunctions.pfnDestroyDevice(device);
  CHECK(State.DeallocateCalls == deallocationsBefore + 2u);
  CHECK(State.DeallocateResources[deallocationsBefore + 1u] ==
        retryRuntime.handle);
  CHECK(State.DestroyContextCalls == 1u);
  free(device.pDrvPrivate);
  CHECK(adapterFunctions.pfnCloseAdapter(openAdapter.hAdapter) == S_OK);
  return State.Failures == 0u ? 0 : (int)State.Failures;
}
