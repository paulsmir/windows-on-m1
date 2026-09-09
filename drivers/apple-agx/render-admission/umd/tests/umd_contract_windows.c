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
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
#include "agx_win32_pipe_screen.h"
#endif

typedef struct _TEST_STATE {
  unsigned int Failures;
  unsigned int CreateContextCalls;
  unsigned int DestroyContextCalls;
  unsigned int AllocateCalls;
  unsigned int DeallocateCalls;
  unsigned int SetErrorCalls;
  unsigned int FailAllocations;
  unsigned int FailDeallocations;
  unsigned int CreatedKernelResources;
  unsigned int ReleasedKernelResources;
  unsigned int OutstandingKernelResources;
  unsigned int ActiveDdi;
  BOOL RuntimeTerminal;
  HRESULT LastSetError;
  HANDLE LastAllocateResource;
  HANDLE DeallocateResources[16];
  HRESULT DeallocateResults[16];
  HRESULT SetErrors[8];
  unsigned int SetErrorDdis[8];
  unsigned int ContextGeneration;
  unsigned int RenderCalls;
  unsigned int QueryAdapterCalls;
  unsigned int InternalAllocateCalls;
  unsigned int InternalDeallocateCalls;
  unsigned int LockCalls;
  unsigned int UnlockCalls;
  unsigned int SignalCompletionCalls;
  unsigned int FailCompletionSignals;
  BOOL AutoCompleteFence;
  D3DKMT_HANDLE InternalAllocation;
  D3DKMT_HANDLE LastLockedAllocation;
  D3DKMT_HANDLE LastUnlockedAllocation;
  HANDLE LastCompletionEvent;
  ADMISSION_ALLOCATION_DESCRIPTION InternalDescription;
  unsigned char RenderCommand[128];
  AGX_WIN32_CLEAR_REQUEST *MutatedRequest;
} TEST_STATE;

enum {
  TEST_DDI_NONE = 0u,
  TEST_DDI_FLUSH = 1u,
  TEST_DDI_DESTROY_DEVICE = 2u,
  TEST_DDI_CREATE_RESOURCE = 3u,
};

static TEST_STATE State;
static unsigned char CommandBuffer[4096];
static unsigned char NextCommandBuffer[4096];
static D3DDDI_ALLOCATIONLIST AllocationList[16];
static D3DDDI_ALLOCATIONLIST NextAllocationList[16];
static D3DDDI_PATCHLOCATIONLIST PatchList[16];
static D3DDDI_PATCHLOCATIONLIST NextPatchList[16];
static unsigned char InternalAllocationData[0x8000];

#define CHECK(value)                                                          \
  do {                                                                        \
    if (!(value)) {                                                           \
      fprintf(stderr, "CHECK_FAIL line=%u expression=%s\n",                 \
              (unsigned int)__LINE__, #value);                                \
      ++State.Failures;                                                       \
    }                                                                         \
  } while (0)

static HRESULT APIENTRY TestQueryAdapterInfo(
    HANDLE Adapter, const D3DDDICB_QUERYADAPTERINFO *Query) {
  AGX_WIN32_DEVICE_INFO *info;
  CHECK(Adapter == (HANDLE)(UINT_PTR)0x100u);
  CHECK(Query != NULL && Query->pPrivateDriverData != NULL &&
        Query->PrivateDriverDataSize == sizeof(AGX_WIN32_DEVICE_INFO));
  if (Query == NULL || Query->pPrivateDriverData == NULL ||
      Query->PrivateDriverDataSize != sizeof(AGX_WIN32_DEVICE_INFO))
    return E_INVALIDARG;
  ++State.QueryAdapterCalls;
  info = (AGX_WIN32_DEVICE_INFO *)Query->pPrivateDriverData;
  memset(info, 0, sizeof(*info));
  info->Magic = AGX_WIN32_DEVICE_INFO_MAGIC;
  info->Version = AGX_WIN32_DEVICE_INFO_VERSION;
  info->Bytes = sizeof(*info);
  info->BootGeneration = 9u;
  info->GpuGeneration = 13u;
  info->GpuVariant = AgxWin32GpuG13G;
  info->PageBytes = 0x4000u;
  info->ClassCount = 3u;
  info->Classes[0] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassGeneral, 0x4000u, 0x01000000ULL,
      AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite |
          AppleAgxWin32BufferGpuRead | AppleAgxWin32BufferGpuWrite};
  info->Classes[1] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassShader, 0x4000u, 0x01000000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead};
  info->Classes[2] = (AGX_WIN32_BUFFER_CLASS_INFO){
      AgxWin32BufferClassEncoder, 0x4000u, 0x01000000ULL,
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead};
  return S_OK;
}

static HRESULT APIENTRY TestCreateContext(HANDLE Device,
                                          D3DDDICB_CREATECONTEXT *Create) {
  (void)Device;
  ++State.CreateContextCalls;
  CHECK(Create->pPrivateDriverData != NULL);
  CHECK(Create->PrivateDriverDataSize == sizeof(ADMISSION_WIN32_CONTEXT_CREATE));
  if (Create->pPrivateDriverData != NULL &&
      Create->PrivateDriverDataSize == sizeof(ADMISSION_WIN32_CONTEXT_CREATE)) {
    const ADMISSION_WIN32_CONTEXT_CREATE *context =
        (const ADMISSION_WIN32_CONTEXT_CREATE *)Create->pPrivateDriverData;
    CHECK(context->Magic == ADMISSION_WIN32_CONTEXT_MAGIC);
    CHECK(context->Version == ADMISSION_WIN32_CONTEXT_VERSION);
    CHECK(context->Bytes == sizeof(*context));
    CHECK(context->Generation != 0u);
    CHECK(context->Reserved == 0u);
    State.ContextGeneration = context->Generation;
  }
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

static HRESULT APIENTRY TestRender(HANDLE Device, D3DDDICB_RENDER *Render) {
  (void)Device;
  ++State.RenderCalls;
  CHECK(Render != NULL);
  if (Render == NULL)
    return E_INVALIDARG;
  CHECK(Render != NULL && Render->hContext == (HANDLE)(UINT_PTR)0x200u);
  CHECK(Render != NULL && Render->CommandOffset == 0u);
  CHECK(Render != NULL && Render->CommandLength == sizeof(State.RenderCommand));
  CHECK(Render != NULL && Render->NumAllocations == 1u);
  CHECK(Render != NULL && Render->NumPatchLocations == 0u);
  CHECK(AllocationList[0].hAllocation == 0x801u);
  CHECK(AllocationList[0].WriteOperation == 1u);
  memcpy(State.RenderCommand, CommandBuffer, sizeof(State.RenderCommand));
  if (State.MutatedRequest != NULL)
    State.MutatedRequest->Color = 0u;
  Render->pNewCommandBuffer = NextCommandBuffer;
  Render->NewCommandBufferSize = sizeof(NextCommandBuffer);
  Render->pNewAllocationList = NextAllocationList;
  Render->NewAllocationListSize = ARRAYSIZE(NextAllocationList);
  Render->pNewPatchLocationList = NextPatchList;
  Render->NewPatchLocationListSize = ARRAYSIZE(NextPatchList);
  return S_OK;
}

static HRESULT APIENTRY TestAllocate(HANDLE Device,
                                     D3DDDICB_ALLOCATE *Allocate) {
  (void)Device;
  if (Allocate == NULL)
    return E_INVALIDARG;
  if (Allocate->hResource == NULL) {
    const ADMISSION_WIN32_ALLOCATION_CREATE *create;
    const ADMISSION_ALLOCATION_DESCRIPTION *description;
    CHECK(Allocate->NumAllocations == 1u);
    CHECK(Allocate->pAllocationInfo != NULL);
    if (Allocate->NumAllocations != 1u ||
        Allocate->pAllocationInfo == NULL)
      return E_INVALIDARG;
    create = (const ADMISSION_WIN32_ALLOCATION_CREATE *)
        Allocate->pAllocationInfo[0].pPrivateDriverData;
    CHECK(create != NULL);
    CHECK(Allocate->pAllocationInfo[0].PrivateDriverDataSize ==
          sizeof(*create));
    CHECK(Allocate->pAllocationInfo[0].pSystemMem == NULL);
    CHECK(Allocate->pAllocationInfo[0].Flags.Value == 0u);
    if (create == NULL ||
        Allocate->pAllocationInfo[0].PrivateDriverDataSize !=
            sizeof(*create))
      return E_INVALIDARG;
    CHECK(create->Magic == ADMISSION_WIN32_ALLOCATION_MAGIC);
    CHECK(create->Version == ADMISSION_WIN32_ALLOCATION_VERSION);
    CHECK(create->Bytes == sizeof(*create));
    CHECK(create->ClassId == AgxWin32BufferClassShader ||
          create->ClassId == AgxWin32BufferClassEncoder);
    CHECK(create->Flags == (AppleAgxWin32BufferCpuWrite |
                            AppleAgxWin32BufferGpuRead));
    description = &create->Allocation;
    CHECK(AdmissionAllocationDescriptionValid(description));
    CHECK(description->Type ==
          (unsigned int)D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE);
    CHECK(description->Format == (unsigned int)D3DDDIFMT_A8);
    CHECK(description->Width == sizeof(InternalAllocationData));
    CHECK(description->Height == 1u);
    CHECK(description->Pitch == sizeof(InternalAllocationData));
    CHECK(description->Size == sizeof(InternalAllocationData));
    CHECK(description->CpuVisible == 1u);
    State.InternalDescription = *description;
    State.InternalAllocation = 0xb001u + State.InternalAllocateCalls;
    ++State.InternalAllocateCalls;
    Allocate->hKMResource = 0u;
    Allocate->pAllocationInfo[0].hAllocation = State.InternalAllocation;
    return S_OK;
  }
  ++State.AllocateCalls;
  State.LastAllocateResource = Allocate->hResource;
  CHECK(Allocate->NumAllocations == 1u);
  CHECK(Allocate->pAllocationInfo != NULL);
  if (State.FailAllocations != 0u) {
    --State.FailAllocations;
    return E_OUTOFMEMORY;
  }
  Allocate->hKMResource = 0x700u + State.AllocateCalls;
  if (Allocate->pAllocationInfo != NULL) {
    Allocate->pAllocationInfo[0].hAllocation = 0x800u + State.AllocateCalls;
    ++State.CreatedKernelResources;
    ++State.OutstandingKernelResources;
  }
  return S_OK;
}

static HRESULT APIENTRY TestDeallocate(
    HANDLE Device, const D3DDDICB_DEALLOCATE *Deallocate) {
  HRESULT result = S_OK;
  (void)Device;
  if (Deallocate == NULL)
    return E_INVALIDARG;
  if (Deallocate->hResource == NULL) {
    CHECK(Deallocate->NumAllocations == 1u);
    CHECK(Deallocate->HandleList != NULL);
    if (Deallocate->NumAllocations != 1u ||
        Deallocate->HandleList == NULL)
      return E_INVALIDARG;
    CHECK(Deallocate->HandleList[0] == State.InternalAllocation);
    ++State.InternalDeallocateCalls;
    return S_OK;
  }
  {
  unsigned int index = State.DeallocateCalls++;
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
  if (SUCCEEDED(result)) {
    CHECK(State.OutstandingKernelResources != 0u);
    if (State.OutstandingKernelResources != 0u)
      --State.OutstandingKernelResources;
    ++State.ReleasedKernelResources;
  }
  return result;
  }
}

static HRESULT APIENTRY TestLock(HANDLE Device, D3DDDICB_LOCK *Lock) {
  (void)Device;
  CHECK(Lock != NULL);
  if (Lock == NULL)
    return E_INVALIDARG;
  CHECK(Lock->hAllocation == State.InternalAllocation);
  CHECK(Lock->PrivateDriverData == 0u);
  CHECK(Lock->NumPages == 0u);
  CHECK(Lock->pPages == NULL);
  CHECK(Lock->Flags.LockEntire == 1u);
  CHECK(Lock->Flags.WriteOnly == 1u);
  CHECK(Lock->Flags.ReadOnly == 0u);
  CHECK(Lock->GpuVirtualAddress == 0u);
  State.LastLockedAllocation = Lock->hAllocation;
  ++State.LockCalls;
  Lock->pData = InternalAllocationData;
  return S_OK;
}

static HRESULT APIENTRY TestUnlock(
    HANDLE Device, const D3DDDICB_UNLOCK *Unlock) {
  (void)Device;
  CHECK(Unlock != NULL);
  if (Unlock == NULL)
    return E_INVALIDARG;
  CHECK(Unlock->NumAllocations == 1u);
  CHECK(Unlock->phAllocations != NULL);
  if (Unlock->NumAllocations != 1u || Unlock->phAllocations == NULL)
    return E_INVALIDARG;
  CHECK(Unlock->phAllocations[0] == State.InternalAllocation);
  State.LastUnlockedAllocation = Unlock->phAllocations[0];
  ++State.UnlockCalls;
  return S_OK;
}

static HRESULT APIENTRY TestSignalSynchronizationObject2(
    HANDLE Device, const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 *Signal) {
  (void)Device;
  CHECK(Signal != NULL);
  if (Signal == NULL)
    return E_INVALIDARG;
  CHECK(Signal->hContext == (HANDLE)(UINT_PTR)0x200u);
  CHECK(Signal->ObjectCount == 0u);
  CHECK(Signal->Flags.Value == 0x2u);
  CHECK(Signal->BroadcastContextCount == 0u);
  CHECK(Signal->CpuEventHandle != NULL);
  if (Signal->CpuEventHandle == NULL)
    return E_INVALIDARG;
  State.LastCompletionEvent = Signal->CpuEventHandle;
  ++State.SignalCompletionCalls;
  if (State.FailCompletionSignals != 0u) {
    --State.FailCompletionSignals;
    return E_FAIL;
  }
  if (State.AutoCompleteFence)
    CHECK(SetEvent(Signal->CpuEventHandle));
  return S_OK;
}

static VOID APIENTRY TestSetError(D3D10DDI_HRTCORELAYER CoreLayer,
                                  HRESULT Error) {
  CHECK(CoreLayer.handle == (VOID *)(UINT_PTR)0x102u);
  CHECK(FAILED(Error));
  CHECK(!State.RuntimeTerminal);
  if (State.SetErrorCalls < ARRAYSIZE(State.SetErrors)) {
    State.SetErrors[State.SetErrorCalls] = Error;
    State.SetErrorDdis[State.SetErrorCalls] = State.ActiveDdi;
  }
  State.LastSetError = Error;
  State.RuntimeTerminal = TRUE;
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

static D3D10DDI_HRESOURCE create_resource(
    D3DWDDM1_3DDI_DEVICEFUNCS *Functions, D3D10DDI_HDEVICE Device,
    D3D10DDI_HRTRESOURCE RuntimeResource, BOOL Primary, BOOL Shared) {
  D3D11DDIARG_CREATERESOURCE create;
  D3D10DDI_MIPINFO mip;
  DXGI_DDI_PRIMARY_DESC primary;
  D3D10DDI_HRESOURCE resource;
  SIZE_T bytes;
  initialize_create_resource(&create, &mip, &primary);
  if (!Primary)
    create.pPrimaryDesc = NULL;
  if (Shared)
    create.MiscFlags |= D3D10_DDI_RESOURCE_MISC_SHARED;
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
  if (resource.pDrvPrivate != NULL &&
      ((ADMISSION_UMD_RESOURCE *)resource.pDrvPrivate)->Magic ==
          ADMISSION_UMD_RESOURCE_MAGIC) {
    ++State.CreatedKernelResources;
    ++State.OutstandingKernelResources;
  }
  return resource;
}

static unsigned BridgeCreates, BridgeDestroys;
static unsigned BridgeQueries;
static BOOL BridgeBadInfo;
static BOOL BridgeMalformed, BridgeFailCreate;
static UINT_PTR BridgeErrorOwner;
static unsigned char BridgeCommands[2][4096];
static D3DDDI_ALLOCATIONLIST BridgeAllocations[2][16];
static D3DDDI_PATCHLOCATIONLIST BridgePatches[2][16];

static HRESULT APIENTRY BridgeQueryAdapter(HANDLE Adapter,
    const D3DDDICB_QUERYADAPTERINFO *Query) {
  HRESULT result = TestQueryAdapterInfo((HANDLE)(UINT_PTR)0x100u, Query);
  AGX_WIN32_DEVICE_INFO *info = Query->pPrivateDriverData;
  ++BridgeQueries;
  CHECK(Adapter == (HANDLE)(UINT_PTR)0xc00u ||
        Adapter == (HANDLE)(UINT_PTR)0xc01u);
  info->BootGeneration = BridgeBadInfo ? 0u : (ULONG)(UINT_PTR)Adapter;
  return result;
}

static void test_runtime_adapter_bridge(void) {
  ADMISSION_UMD_ADAPTER adapters[2] = {0};
  D3D10DDIARG_OPENADAPTER args = {0};
  D3DDDI_ADAPTERCALLBACKS callbacks = {0};
  callbacks.pfnQueryAdapterInfoCb = BridgeQueryAdapter;
  args.pAdapterCallbacks = &callbacks;
  /* The initializer owns no published adapter table or pipe_screen. */
  args.pAdapterFuncs_2 = NULL;
  for (UINT i = 0; i < 2; ++i) {
    args.hRTAdapter.handle = (VOID *)(UINT_PTR)(0xc00u + i);
    CHECK(AdmissionUmdRuntimeAdapterInitialize(&adapters[i], &args) == S_OK);
    CHECK(adapters[i].RuntimeAdapter.handle == args.hRTAdapter.handle);
    CHECK(adapters[i].DeviceInfo.BootGeneration == 0xc00u + i);
  }
  CHECK(BridgeQueries == 2u);
  BridgeBadInfo = TRUE;
  {
    ADMISSION_UMD_ADAPTER rejected = {0};
    CHECK(AdmissionUmdRuntimeAdapterInitialize(&rejected, &args) == E_FAIL);
    CHECK(rejected.Magic == 0u);
  }
  BridgeBadInfo = FALSE;
  callbacks.pfnQueryAdapterInfoCb = NULL;
  {
    ADMISSION_UMD_ADAPTER rejected = {0};
    CHECK(AdmissionUmdRuntimeAdapterInitialize(&rejected, &args) == E_INVALIDARG);
    CHECK(rejected.Magic == 0u && BridgeQueries == 3u);
  }
  CHECK(adapters[0].DeviceInfo.BootGeneration == 0xc00u);
}

static HRESULT APIENTRY BridgeCreateContext(HANDLE Device,
                                             D3DDDICB_CREATECONTEXT *Create) {
  UINT index = (UINT)((UINT_PTR)Device - 0x900u);
  CHECK(index < 2u);
  if (index >= 2u) return E_INVALIDARG;
  ++BridgeCreates;
  if (BridgeFailCreate) return E_OUTOFMEMORY;
  Create->hContext = (HANDLE)(UINT_PTR)(0xa00u + index);
  Create->pCommandBuffer = BridgeMalformed ? NULL : BridgeCommands[index];
  Create->CommandBufferSize = sizeof(BridgeCommands[index]);
  Create->pAllocationList = BridgeAllocations[index];
  Create->AllocationListSize = ARRAYSIZE(BridgeAllocations[index]);
  Create->pPatchLocationList = BridgePatches[index];
  Create->PatchLocationListSize = ARRAYSIZE(BridgePatches[index]);
  return S_OK;
}

static HRESULT APIENTRY BridgeDestroyContext(HANDLE Device,
    const D3DDDICB_DESTROYCONTEXT *Destroy) {
  CHECK(Destroy->hContext == (HANDLE)((UINT_PTR)Device + 0x100u));
  ++BridgeDestroys;
  return S_OK;
}

static VOID APIENTRY BridgeSetError(D3D10DDI_HRTCORELAYER Core, HRESULT Error) {
  CHECK(Error == E_FAIL);
  BridgeErrorOwner = (UINT_PTR)Core.handle;
}

static void test_runtime_device_bridge(ADMISSION_UMD_ADAPTER *Adapter,
                                       D3D10DDIARG_CREATEDEVICE Template) {
  ADMISSION_UMD_DEVICE devices[2];
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
  AGX_WIN32_PIPE_DEVICE pipes[2] = {0};
#endif
  D3DDDI_DEVICECALLBACKS callbacks = *Template.pKTCallbacks;
  D3D10DDI_CORELAYER_DEVICECALLBACKS core10 = {0};
  D3D11DDI_CORELAYER_DEVICECALLBACKS core11 = {0};
  unsigned before;
  memset(devices, 0, sizeof(devices));
  core10.pfnSetErrorCb = BridgeSetError;
  core11.pfnSetErrorCb = BridgeSetError;
  callbacks.pfnCreateContextCb = BridgeCreateContext;
  callbacks.pfnDestroyContextCb = BridgeDestroyContext;
  Template.pKTCallbacks = &callbacks;
  for (UINT index = 0u; index < 2u; ++index) {
    Template.Interface = index == 0u ? D3D10_0_DDI_INTERFACE_VERSION :
                                      D3DWDDM1_3_DDI_INTERFACE_VERSION;
    if (index == 0u) Template.pUMCallbacks = &core10;
    else Template.p11UMCallbacks = &core11;
    Template.hDrvDevice.pDrvPrivate = &devices[index];
    Template.hRTDevice.handle = (VOID *)(UINT_PTR)(0x900u + index);
    Template.hRTCoreLayer.handle = (VOID *)(UINT_PTR)(0xb00u + index);
    CHECK(AdmissionUmdRuntimeDeviceInitialize(&devices[index], Adapter,
                                              &Template) == S_OK);
    CHECK(devices[index].Screen.Context == &devices[index]);
    CHECK(devices[index].CommandBuffer == BridgeCommands[index]);
    CHECK(devices[index].KernelContext == (HANDLE)(UINT_PTR)(0xa00u + index));
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
    CHECK(AgxWin32PipeDeviceInitialize(&pipes[index], &devices[index].Screen));
    CHECK(pipes[index].Context != NULL);
    if (pipes[index].Context != NULL)
      CHECK(pipes[index].Context->priv == &devices[index]);
#endif
    AdmissionUmdSetError(&devices[index], E_FAIL);
    CHECK(BridgeErrorOwner == 0xb00u + index);
  }
  CHECK(devices[0].Win32Generation != devices[1].Win32Generation);
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
  CHECK(pipes[0].Screen != pipes[1].Screen);
  CHECK(pipes[0].Context != pipes[1].Context);
  if (pipes[0].Screen != NULL) {
    struct pipe_context *extra = pipes[0].Screen->context_create(
        pipes[0].Screen, &devices[0], 0u);
    CHECK(extra != NULL);
    CHECK(!AgxWin32PipeDeviceClose(&pipes[0]));
    CHECK(devices[0].Screen.Active && BridgeDestroys == 0u);
    if (extra != NULL) extra->destroy(extra);
  }
  CHECK(AgxWin32PipeDeviceClose(&pipes[0]));
  CHECK(devices[0].Screen.Active && BridgeDestroys == 0u);
#endif
  AdmissionUmdRuntimeDeviceFinalize(&devices[0]);
  CHECK(devices[0].Magic == 0u && devices[1].Screen.Active);
  AdmissionUmdSetError(&devices[1], E_FAIL);
  CHECK(BridgeErrorOwner == 0xb01u);
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
  if (pipes[1].Context != NULL)
    CHECK(pipes[1].Context->priv == &devices[1]);
  CHECK(AgxWin32PipeDeviceClose(&pipes[1]));
#endif
  AdmissionUmdRuntimeDeviceFinalize(&devices[1]);
  CHECK(BridgeCreates == 2u && BridgeDestroys == 2u);
  Template.hRTDevice.handle = (VOID *)(UINT_PTR)0x900u;
  BridgeMalformed = TRUE;
  CHECK(AdmissionUmdRuntimeDeviceInitialize(&devices[0], Adapter,
                                            &Template) == E_FAIL);
  CHECK(devices[0].Magic == 0u && BridgeDestroys == 3u);
  BridgeMalformed = FALSE;
  BridgeFailCreate = TRUE;
  CHECK(AdmissionUmdRuntimeDeviceInitialize(&devices[0], Adapter,
                                            &Template) == E_OUTOFMEMORY);
  CHECK(devices[0].Magic == 0u && BridgeDestroys == 3u);
  BridgeFailCreate = FALSE;
  {
    ADMISSION_UMD_ADAPTER invalidAdapter = *Adapter;
    invalidAdapter.DeviceInfo.BootGeneration = 0u;
    CHECK(AdmissionUmdRuntimeDeviceInitialize(&devices[0], &invalidAdapter,
                                              &Template) == E_FAIL);
    CHECK(devices[0].Magic == 0u && BridgeDestroys == 4u);
  }
  before = BridgeCreates;
  Template.Interface = 0u;
  CHECK(AdmissionUmdRuntimeDeviceInitialize(&devices[0], Adapter,
                                            &Template) == E_INVALIDARG);
  CHECK(BridgeCreates == before);
  Template.Interface = D3D10_0_DDI_INTERFACE_VERSION;
  Template.pUMCallbacks = NULL;
  CHECK(AdmissionUmdRuntimeDeviceInitialize(&devices[0], Adapter,
                                            &Template) == E_INVALIDARG);
  CHECK(BridgeCreates == before);
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
  D3D10DDI_HRTRESOURCE sharedRuntime;
  D3D10DDI_HRTRESOURCE nonPrimaryRuntime;
  D3D10DDI_HRTRESOURCE allocationFailureRuntime;
  D3D10DDI_HRTRESOURCE retryRuntime;
  D3D10DDI_HRTRESOURCE permanentRuntime1;
  D3D10DDI_HRTRESOURCE permanentRuntime2;
  D3D10DDI_HRESOURCE primary;
  D3D10DDI_HRESOURCE opened;
  D3D10DDI_HRESOURCE shared;
  D3D10DDI_HRESOURCE nonPrimary;
  D3D10DDI_HRESOURCE allocationFailure;
  D3D10DDI_HRESOURCE retry;
  D3D10DDI_HRESOURCE permanent1;
  D3D10DDI_HRESOURCE permanent2;
  SIZE_T deviceBytes;
  UINT formatSupport;
  UINT32 versionCount;
  UINT64 version;
  unsigned int errorsBefore;
  unsigned int deallocationsBefore;
  ADMISSION_UMD_DEVICE *deviceState;
  AGX_WIN32_CLEAR_REQUEST clearRequest;
  APPLE_AGX_WIN32_COMMAND_VIEW clearView;
  APPLE_AGX_U32 completionFence = 0u;
  AGX_WIN32_SCREEN_BUFFER shaderBuffer;
  AGX_WIN32_SCREEN_BUFFER encoderBuffer;
  void *shaderMap = NULL;
  void *encoderMap = NULL;
  HANDLE failedCompletionEvent = NULL;
  HANDLE teardownCompletionEvent = NULL;

  memset(&State, 0, sizeof(State));
  State.AutoCompleteFence = TRUE;
  memset(&adapterCallbacks, 0, sizeof(adapterCallbacks));
  adapterCallbacks.pfnQueryAdapterInfoCb = TestQueryAdapterInfo;
  memset(&adapterFunctions, 0, sizeof(adapterFunctions));
  memset(&openAdapter, 0, sizeof(openAdapter));
  openAdapter.hRTAdapter.handle = (VOID *)(UINT_PTR)0x100u;
  openAdapter.Interface = D3DWDDM1_3_DDI_INTERFACE_VERSION;
  openAdapter.Version = 0u;
  openAdapter.pAdapterCallbacks = &adapterCallbacks;
  openAdapter.pAdapterFuncs_2 = &adapterFunctions;
  CHECK(OpenAdapter10_2(&openAdapter) == S_OK);
  CHECK(State.QueryAdapterCalls == 1u);

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
  kernelCallbacks.pfnRenderCb = TestRender;
  kernelCallbacks.pfnLockCb = TestLock;
  kernelCallbacks.pfnUnlockCb = TestUnlock;
  kernelCallbacks.pfnSignalSynchronizationObject2Cb =
      TestSignalSynchronizationObject2;
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
  kernelCallbacks.pfnLockCb = NULL;
  CHECK(adapterFunctions.pfnCreateDevice(openAdapter.hAdapter,
                                         &createDevice) == E_INVALIDARG);
  CHECK(State.CreateContextCalls == 0u);
  kernelCallbacks.pfnLockCb = TestLock;
  kernelCallbacks.pfnSignalSynchronizationObject2Cb = NULL;
  CHECK(adapterFunctions.pfnCreateDevice(openAdapter.hAdapter,
                                         &createDevice) == E_INVALIDARG);
  CHECK(State.CreateContextCalls == 0u);
  kernelCallbacks.pfnSignalSynchronizationObject2Cb =
      TestSignalSynchronizationObject2;
  CHECK(adapterFunctions.pfnCreateDevice(openAdapter.hAdapter,
                                         &createDevice) == S_OK);
  CHECK(State.CreateContextCalls == 1u);
  CHECK(State.ContextGeneration != 0u);
  deviceState = (ADMISSION_UMD_DEVICE *)device.pDrvPrivate;
  if (deviceState == NULL)
    return 1;

  memset(&shaderBuffer, 0, sizeof(shaderBuffer));
  CHECK(AgxWin32ScreenCreateBuffer(
            &deviceState->Screen, AgxWin32BufferClassShader,
            sizeof(InternalAllocationData), 0x4000u,
            AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead,
            &shaderBuffer) == AgxWin32ScreenSuccess);
  CHECK(State.InternalAllocateCalls == 1u);
  CHECK(shaderBuffer.Transport.Token != 0u);
  CHECK(AgxWin32ScreenMapBuffer(
            &deviceState->Screen, &shaderBuffer, 0x4000u, 0x4000u,
            AppleAgxWin32BufferCpuWrite,
            &shaderMap) == AgxWin32ScreenSuccess);
  CHECK(shaderMap == InternalAllocationData + 0x4000u);
  CHECK(State.LockCalls == 1u);
  CHECK(AgxWin32ScreenDestroyBuffer(&deviceState->Screen, &shaderBuffer) ==
        AgxWin32ScreenState);
  CHECK(State.InternalDeallocateCalls == 0u);
  CHECK(AgxWin32ScreenUnmapBuffer(&deviceState->Screen, &shaderBuffer) ==
        AgxWin32ScreenSuccess);
  CHECK(State.UnlockCalls == 1u);
  CHECK(AgxWin32ScreenDestroyBuffer(&deviceState->Screen, &shaderBuffer) ==
        AgxWin32ScreenSuccess);
  CHECK(State.InternalDeallocateCalls == 1u);

  errorsBefore = State.SetErrorCalls;
  formatSupport = 0xffffffffu;
  deviceFunctions.pfnCheckFormatSupport(
      device, DXGI_FORMAT_R8G8B8A8_UNORM, &formatSupport);
  CHECK(formatSupport == 0u);
  CHECK(State.SetErrorCalls == errorsBefore);

  primaryRuntime.handle = (VOID *)(UINT_PTR)0x300u;
  primary = create_resource(&deviceFunctions, device, primaryRuntime, TRUE,
                            FALSE);
  CHECK(State.AllocateCalls == 1u);
  CHECK(State.LastAllocateResource == primaryRuntime.handle);
  memset(&clearRequest, 0, sizeof(clearRequest));
  clearRequest.Generation = State.ContextGeneration;
  clearRequest.AllocationIndex = 0u;
  clearRequest.AllocationBytes = 0xfa0000ULL;
  clearRequest.Format = AppleAgxWin32FormatBgra8Unorm;
  clearRequest.Color = 0xff224466u;
  clearRequest.SurfaceWidth = 2560u;
  clearRequest.SurfaceHeight = 1600u;
  clearRequest.SurfacePitch = 10240u;
  clearRequest.Right = 2560u;
  clearRequest.Bottom = 1600u;
  State.MutatedRequest = &clearRequest;
  CHECK(AdmissionUmdSubmitClear(
            deviceState, (ADMISSION_UMD_RESOURCE *)primary.pDrvPrivate,
            &clearRequest) == S_OK);
  CHECK(State.RenderCalls == 1u);
  CHECK(clearRequest.Color == 0u);
  CHECK(AppleAgxWin32CommandValidate(
            State.RenderCommand, sizeof(State.RenderCommand),
            State.ContextGeneration, 1u, &clearView) ==
        AppleAgxWin32AbiSuccess);
  CHECK(clearView.Clear->Color == 0xff224466u);
  CHECK(deviceState->CommandBuffer == NextCommandBuffer);
  CHECK(deviceState->AllocationList == NextAllocationList);
  CHECK(deviceState->PatchList == NextPatchList);
  State.MutatedRequest = NULL;
  CHECK(AdmissionUmdScreenSignalFence(deviceState, &completionFence) == S_OK);
  CHECK(completionFence != 0u);
  CHECK(State.SignalCompletionCalls == 1u);
  CHECK(AgxWin32ScreenWaitFence(
            &deviceState->Screen, completionFence, 100u) ==
        AgxWin32ScreenSuccess);
  CHECK(AgxWin32ScreenRetireFence(
            &deviceState->Screen, completionFence) ==
        AgxWin32ScreenSuccess);
  State.AutoCompleteFence = FALSE;
  completionFence = 0u;
  CHECK(AdmissionUmdScreenSignalFence(deviceState, &completionFence) == S_OK);
  CHECK(AgxWin32ScreenWaitFence(
            &deviceState->Screen, completionFence, 0u) ==
        AgxWin32ScreenCallback);
  CHECK(SetEvent(State.LastCompletionEvent));
  CHECK(AgxWin32ScreenWaitFence(
            &deviceState->Screen, completionFence, 100u) ==
        AgxWin32ScreenSuccess);
  CHECK(AgxWin32ScreenRetireFence(
            &deviceState->Screen, completionFence) ==
        AgxWin32ScreenSuccess);
  State.FailCompletionSignals = 1u;
  completionFence = 0u;
  CHECK(AdmissionUmdScreenSignalFence(deviceState, &completionFence) == E_FAIL);
  CHECK(completionFence == 0u);
  failedCompletionEvent = State.LastCompletionEvent;
  CHECK(WaitForSingleObject(failedCompletionEvent, 0u) == WAIT_FAILED);
  CHECK(GetLastError() == ERROR_INVALID_HANDLE);
  State.AutoCompleteFence = TRUE;
  if (primary.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, primary);
  CHECK(State.DeallocateCalls == 1u);
  CHECK(State.DeallocateResources[0] == primaryRuntime.handle);
  free(primary.pDrvPrivate);

  sharedRuntime.handle = (VOID *)(UINT_PTR)0x350u;
  shared = create_resource(&deviceFunctions, device, sharedRuntime, TRUE, TRUE);
  nonPrimaryRuntime.handle = (VOID *)(UINT_PTR)0x360u;
  nonPrimary = create_resource(&deviceFunctions, device, nonPrimaryRuntime,
                               FALSE, FALSE);
  deallocationsBefore = State.DeallocateCalls;
  if (shared.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, shared);
  if (nonPrimary.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, nonPrimary);
  CHECK(State.DeallocateCalls == deallocationsBefore);
  CHECK(deviceFunctions.pfnFlush(device, 0u));
  CHECK(State.DeallocateCalls == deallocationsBefore + 2u);
  CHECK(State.DeallocateResources[deallocationsBefore] == sharedRuntime.handle);
  CHECK(State.DeallocateResources[deallocationsBefore + 1u] ==
        nonPrimaryRuntime.handle);
  free(shared.pDrvPrivate);
  free(nonPrimary.pDrvPrivate);

  openRuntime.handle = (VOID *)(UINT_PTR)0x400u;
  opened = open_resource(&deviceFunctions, device, openRuntime, 0x900u, 0xa00u);
  deallocationsBefore = State.DeallocateCalls;
  if (opened.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, opened);
  CHECK(State.DeallocateCalls == deallocationsBefore);
  CHECK(deviceFunctions.pfnFlush != NULL);
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
  State.ActiveDdi = TEST_DDI_FLUSH;
  if (deviceFunctions.pfnFlush != NULL) {
    CHECK(!deviceFunctions.pfnFlush(device, 0u));
  }
  State.ActiveDdi = TEST_DDI_NONE;
  CHECK(State.DeallocateCalls == deallocationsBefore + 1u);
  CHECK(State.SetErrorCalls == errorsBefore + 1u);
  CHECK(State.RuntimeTerminal);
  CHECK(State.LastSetError == E_FAIL);
  CHECK(State.SetErrorDdis[errorsBefore] == TEST_DDI_FLUSH);
  free(retry.pDrvPrivate);

  memset(&encoderBuffer, 0, sizeof(encoderBuffer));
  CHECK(AgxWin32ScreenCreateBuffer(
            &deviceState->Screen, AgxWin32BufferClassEncoder,
            sizeof(InternalAllocationData), 0x4000u,
            AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead,
            &encoderBuffer) == AgxWin32ScreenSuccess);
  CHECK(AgxWin32ScreenMapBuffer(
            &deviceState->Screen, &encoderBuffer, 0u,
            sizeof(InternalAllocationData), AppleAgxWin32BufferCpuWrite,
            &encoderMap) == AgxWin32ScreenSuccess);
  CHECK(encoderMap == InternalAllocationData);
  CHECK(State.InternalAllocateCalls == 2u);
  CHECK(State.LockCalls == 2u);
  CHECK(AgxWin32ScreenWaitFence(&deviceState->Screen, 1u, 1u) ==
        AgxWin32ScreenCallback);
  State.AutoCompleteFence = FALSE;
  completionFence = 0u;
  CHECK(AdmissionUmdScreenSignalFence(deviceState, &completionFence) == S_OK);
  teardownCompletionEvent = State.LastCompletionEvent;

  State.ActiveDdi = TEST_DDI_DESTROY_DEVICE;
  deviceFunctions.pfnDestroyDevice(device);
  State.ActiveDdi = TEST_DDI_NONE;
  CHECK(State.DeallocateCalls == deallocationsBefore + 2u);
  CHECK(State.DeallocateResources[deallocationsBefore + 1u] ==
        retryRuntime.handle);
  CHECK(State.DestroyContextCalls == 1u);
  CHECK(State.UnlockCalls == 2u);
  CHECK(State.InternalDeallocateCalls == 2u);
  CHECK(WaitForSingleObject(teardownCompletionEvent, 0u) == WAIT_FAILED);
  CHECK(GetLastError() == ERROR_INVALID_HANDLE);
  CHECK(State.CreatedKernelResources == State.ReleasedKernelResources +
                                            State.OutstandingKernelResources);
  CHECK(State.OutstandingKernelResources == 0u);
  free(device.pDrvPrivate);

  /* Allocation failure is terminal for this resource handle; do not continue. */
  State.RuntimeTerminal = FALSE;
  State.LastSetError = S_OK;
  device.pDrvPrivate = calloc(1u, deviceBytes);
  CHECK(device.pDrvPrivate != NULL);
  createDevice.hDrvDevice = device;
  memset(&deviceFunctions, 0, sizeof(deviceFunctions));
  memset(&dxgiFunctions, 0, sizeof(dxgiFunctions));
  CHECK(adapterFunctions.pfnCreateDevice(openAdapter.hAdapter,
                                         &createDevice) == S_OK);
  State.FailAllocations = 1u;
  errorsBefore = State.SetErrorCalls;
  allocationFailureRuntime.handle = (VOID *)(UINT_PTR)0x550u;
  State.ActiveDdi = TEST_DDI_CREATE_RESOURCE;
  allocationFailure = create_resource(&deviceFunctions, device,
                                      allocationFailureRuntime, TRUE, FALSE);
  State.ActiveDdi = TEST_DDI_NONE;
  CHECK(State.SetErrorCalls == errorsBefore + 1u);
  CHECK(State.SetErrors[errorsBefore] == E_OUTOFMEMORY);
  CHECK(State.SetErrorDdis[errorsBefore] == TEST_DDI_CREATE_RESOURCE);
  CHECK(State.RuntimeTerminal);
  CHECK(((ADMISSION_UMD_RESOURCE *)allocationFailure.pDrvPrivate)->Magic == 0u);
  free(allocationFailure.pDrvPrivate);
  State.ActiveDdi = TEST_DDI_DESTROY_DEVICE;
  deviceFunctions.pfnDestroyDevice(device);
  State.ActiveDdi = TEST_DDI_NONE;
  CHECK(State.OutstandingKernelResources == 0u);
  free(device.pDrvPrivate);

  /* A third runtime device exercises terminal cleanup independently. */
  State.RuntimeTerminal = FALSE;
  State.LastSetError = S_OK;
  device.pDrvPrivate = calloc(1u, deviceBytes);
  CHECK(device.pDrvPrivate != NULL);
  createDevice.hDrvDevice = device;
  memset(&deviceFunctions, 0, sizeof(deviceFunctions));
  memset(&dxgiFunctions, 0, sizeof(dxgiFunctions));
  CHECK(adapterFunctions.pfnCreateDevice(openAdapter.hAdapter,
                                         &createDevice) == S_OK);

  permanentRuntime1.handle = (VOID *)(UINT_PTR)0x600u;
  permanent1 = open_resource(&deviceFunctions, device, permanentRuntime1,
                             0x902u, 0xa02u);
  permanentRuntime2.handle = (VOID *)(UINT_PTR)0x601u;
  permanent2 = open_resource(&deviceFunctions, device, permanentRuntime2,
                             0x903u, 0xa03u);
  if (permanent1.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, permanent1);
  if (permanent2.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, permanent2);
  free(permanent1.pDrvPrivate);
  free(permanent2.pDrvPrivate);

  State.FailDeallocations = 2u;
  deallocationsBefore = State.DeallocateCalls;
  errorsBefore = State.SetErrorCalls;
  State.ActiveDdi = TEST_DDI_DESTROY_DEVICE;
  deviceFunctions.pfnDestroyDevice(device);
  State.ActiveDdi = TEST_DDI_NONE;
  CHECK(State.DeallocateCalls == deallocationsBefore + 2u);
  CHECK(State.DeallocateResources[deallocationsBefore] ==
        permanentRuntime1.handle);
  CHECK(State.DeallocateResources[deallocationsBefore + 1u] ==
        permanentRuntime2.handle);
  CHECK(State.DeallocateResults[deallocationsBefore] == E_FAIL);
  CHECK(State.DeallocateResults[deallocationsBefore + 1u] == E_FAIL);
  CHECK(State.SetErrorCalls == errorsBefore + 1u);
  CHECK(State.SetErrors[errorsBefore] == E_FAIL);
  CHECK(State.SetErrorDdis[errorsBefore] == TEST_DDI_DESTROY_DEVICE);
  CHECK(State.RuntimeTerminal);
  CHECK(State.DestroyContextCalls == 3u);
  CHECK(State.CreatedKernelResources == State.ReleasedKernelResources +
                                            State.OutstandingKernelResources);
  CHECK(State.OutstandingKernelResources == 2u);
  free(device.pDrvPrivate);
  test_runtime_device_bridge(AdmissionUmdAdapterFromHandle(openAdapter.hAdapter),
                             createDevice);
  CHECK(adapterFunctions.pfnCloseAdapter(openAdapter.hAdapter) == S_OK);
  test_runtime_adapter_bridge();
  return State.Failures == 0u ? 0 : (int)State.Failures;
}
