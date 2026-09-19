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
#include "umd_draw_composer_windows.c"
#if defined(ADMISSION_UMD_NATIVE_POOL_TEST)
#include "umd_asahi_pool_windows.c"
#endif
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "agx_d3d10_windows.h"
#include "agx_win32_asahi_scene.h"
#endif

#if defined(ADMISSION_UMD_D3D10_FRONTEND_TEST)
EXTERN_C HRESULT APIENTRY MesaD3d10OpenAdapter10(D3D10DDIARG_OPENADAPTER *);
EXTERN_C HRESULT APIENTRY MesaD3d10OpenAdapter10_2(D3D10DDIARG_OPENADAPTER *);
EXTERN_C AGX_D3D10_WINDOWS_ADAPTER *APIENTRY
MesaD3d10FrontendAdapterForTest(D3D10DDI_HADAPTER);
EXTERN_C HRESULT APIENTRY MesaD3d10FrontendCleanupResult(D3D10DDI_HDEVICE);
EXTERN_C ADMISSION_UMD_DEVICE *APIENTRY MesaD3d10FrontendRuntimeForTest(D3D10DDI_HDEVICE);
EXTERN_C void *APIENTRY MesaD3d10FrontendOwnerForTest(D3D10DDI_HDEVICE);
EXTERN_C struct pipe_context *APIENTRY MesaD3d10FrontendContextForTest(D3D10DDI_HDEVICE);
EXTERN_C BOOL APIENTRY MesaD3d10FrontendShaderValidForTest(D3D10DDI_HSHADER);
EXTERN_C ULONG APIENTRY MesaD3d10FrontendEventQuerySetGenerationForTest(
    D3D10DDI_HQUERY,ULONG);
EXTERN_C struct pipe_screen *d3d10_create_screen(void) { return NULL; }

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
  ADMISSION_UMD_DEVICE *ReentryDevice;
  APPLE_AGX_U64 ReentryToken;
  HRESULT ReentryResult;
} TEST_STATE;

enum {
  TEST_DDI_NONE = 0u,
  TEST_DDI_FLUSH = 1u,
  TEST_DDI_DESTROY_DEVICE = 2u,
  TEST_DDI_CREATE_RESOURCE = 3u,
};

static TEST_STATE State;
unsigned AdmissionUmdDrawComposerTests(void);
unsigned AdmissionUmdAsahiBatchAdapterTests(void);
int AdmissionWin32ReferenceContractTests(void);
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

#if defined(ADMISSION_UMD_D3D10_FRONTEND_TEST)
static void test_mesa_d3d10_adapter2_contract(void) {
  D3D10DDIARG_OPENADAPTER open={0};
  D3DDDI_ADAPTERCALLBACKS callbacks={0};
  D3D10_2DDI_ADAPTERFUNCS functions={0};
  UINT32 entries=0;
  UINT64 version=0;
  D3D10_2DDIARG_GETCAPS caps={0};
  D3D11DDI_THREADING_CAPS threading={~0u};
  D3D11DDI_3DPIPELINESUPPORT_CAPS pipeline={~0u};
  callbacks.pfnQueryAdapterInfoCb=TestQueryAdapterInfo;
  open.hRTAdapter.handle=(VOID *)(UINT_PTR)0x100u;
  open.Interface=D3D10_0_DDI_INTERFACE_VERSION;
  open.pAdapterCallbacks=&callbacks;
  open.pAdapterFuncs_2=&functions;
  CHECK(MesaD3d10OpenAdapter10_2(&open)==S_OK && open.hAdapter.pDrvPrivate);
  CHECK(functions.pfnGetSupportedVersions(open.hAdapter,&entries,NULL)==S_OK &&
        entries==1u);
  CHECK(functions.pfnGetSupportedVersions(open.hAdapter,&entries,&version)==S_OK &&
        entries==1u && version==D3D10_0_DDI_SUPPORTED);
  caps.Type=D3D11DDICAPS_THREADING;caps.pData=&threading;caps.DataSize=sizeof(threading);
  CHECK(functions.pfnGetCaps(open.hAdapter,&caps)==S_OK && threading.Caps==0u);
  caps.DataSize=sizeof(threading)-1u;
  CHECK(functions.pfnGetCaps(open.hAdapter,&caps)==E_INVALIDARG);
  caps.Type=D3D11DDICAPS_3DPIPELINESUPPORT;caps.pData=&pipeline;caps.DataSize=sizeof(pipeline);
  CHECK(functions.pfnGetCaps(open.hAdapter,&caps)==S_OK && pipeline.Caps==0u);
  caps.Type=(D3D10_2DDICAPS_TYPE)0xffffffffu;
  CHECK(functions.pfnGetCaps(open.hAdapter,&caps)==E_NOTIMPL);
  CHECK(functions.pfnCloseAdapter(open.hAdapter)==S_OK);
}
#endif

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
          create->ClassId == AgxWin32BufferClassEncoder ||
          create->ClassId == AgxWin32BufferClassGeneral);
    CHECK(create->Flags != 0u);
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
  CHECK(Lock->Flags.WriteOnly != Lock->Flags.ReadOnly);
  CHECK(Lock->GpuVirtualAddress == 0u);
  State.LastLockedAllocation = Lock->hAllocation;
  ++State.LockCalls;
  if (State.ReentryDevice != NULL) {
    ADMISSION_UMD_SCREEN_SOURCE source;
    State.ReentryResult = AdmissionUmdScreenQuerySource(
        State.ReentryDevice, State.ReentryToken, &source);
  }
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

#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
static HRESULT APIENTRY FactoryCreateContext(HANDLE Device,D3DDDICB_CREATECONTEXT *Create) {
  ++BridgeCreates;
  Create->hContext=(HANDLE)((UINT_PTR)Device+0x100u);
  Create->pCommandBuffer=RuntimeCommand;
  Create->CommandBufferSize=sizeof(RuntimeCommand);
  Create->pAllocationList=RuntimeAllocations;
  Create->AllocationListSize=ARRAYSIZE(RuntimeAllocations);
  Create->pPatchLocationList=RuntimePatches;
  Create->PatchLocationListSize=ARRAYSIZE(RuntimePatches);
  return S_OK;
}
#endif

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
    AdmissionUmdSetError(&devices[index], E_FAIL);
    CHECK(BridgeErrorOwner == 0xb00u + index);
  }
  CHECK(devices[0].Win32Generation != devices[1].Win32Generation);
  {
    BOOL consumed = TRUE;
    devices[0].NativeBackendCount = 1u;
    CHECK(AdmissionUmdRuntimeDeviceFinalize(&devices[0], &consumed) ==
          HRESULT_FROM_WIN32(ERROR_BUSY));
    CHECK(!consumed && devices[0].Magic == ADMISSION_UMD_DEVICE_MAGIC);
    devices[0].NativeBackendCount = 0u;
  }
  {
    BOOL consumed = FALSE;
    CHECK(AdmissionUmdRuntimeDeviceFinalize(&devices[0], &consumed) == S_OK);
    CHECK(consumed);
  }
  CHECK(devices[0].Magic == 0u && devices[1].Screen.Active);
  AdmissionUmdSetError(&devices[1], E_FAIL);
  CHECK(BridgeErrorOwner == 0xb01u);
  {
    BOOL consumed = FALSE;
    CHECK(AdmissionUmdRuntimeDeviceFinalize(&devices[1], &consumed) == S_OK);
    CHECK(consumed);
  }
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

#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
static void test_mesa_windows_owners(D3D10DDIARG_CREATEDEVICE args) {
  AGX_D3D10_WINDOWS_ADAPTER *adapter = NULL;
  AGX_D3D10_WINDOWS_DEVICE *first = NULL, *second = NULL;
  D3D10DDIARG_OPENADAPTER open = {0};
  D3DDDI_ADAPTERCALLBACKS adapterCallbacks = {0};
  D3DDDI_DEVICECALLBACKS callbacks = *args.pKTCallbacks;
  D3D10DDI_CORELAYER_DEVICECALLBACKS core = {0};
  AGX_WIN32_ASAHI_SCENE scene = {0};
  unsigned closedBefore = BridgeDestroys;
  unsigned poolCreatesBefore, poolDeletesBefore;
  PoolErrors=PoolCreates=PoolMaps=PoolUnlocks=PoolDeletes=0;
  PoolPresentationDeletes=0;
  PoolNextHandle=0;PoolFailAllocation=0;PoolFailMap=0;PoolFailDeallocation=0;
  memset(PoolMemory,0,sizeof(PoolMemory));memset(PoolHandles,0,sizeof(PoolHandles));
  poolCreatesBefore=PoolCreates;poolDeletesBefore=PoolDeletes;
  adapterCallbacks.pfnQueryAdapterInfoCb = TestQueryAdapterInfo;
  open.hRTAdapter.handle = (VOID *)(UINT_PTR)0x100u;
  open.pAdapterCallbacks = &adapterCallbacks;
  CHECK(AgxD3d10WindowsOpenAdapter(&open, &adapter) == S_OK);
#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
  callbacks.pfnCreateContextCb = FactoryCreateContext;
  callbacks.pfnRenderCb = RuntimeRender;
  callbacks.pfnSignalSynchronizationObject2Cb = RuntimeSignal;
#else
  callbacks.pfnCreateContextCb = BridgeCreateContext;
#endif
  callbacks.pfnDestroyContextCb = BridgeDestroyContext;
  callbacks.pfnAllocateCb = PoolAllocate;
  callbacks.pfnDeallocateCb = PoolDeallocate;
  callbacks.pfnLockCb = PoolLock;
  callbacks.pfnUnlockCb = PoolUnlock;
  core.pfnSetErrorCb = BridgeSetError;
  args.pKTCallbacks = &callbacks;
  args.pUMCallbacks = &core;
  args.Interface = D3D10_0_DDI_INTERFACE_VERSION;
  args.hRTDevice.handle = (VOID *)(UINT_PTR)0x900u;
  CHECK(AgxD3d10WindowsCreateDevice(adapter, &args, &first) == S_OK);
  args.hRTDevice.handle = (VOID *)(UINT_PTR)0x901u;
  CHECK(AgxD3d10WindowsCreateDevice(adapter, &args, &second) == S_OK);
  CHECK(FAILED(AgxD3d10WindowsCloseAdapter(&adapter)));
  if (first != NULL && second != NULL) {
    struct pipe_context *a = AgxD3d10WindowsContext(first);
    struct pipe_context *b = AgxD3d10WindowsContext(second);
    CHECK(a != NULL && b != NULL && a != b);
    if (a != NULL && b != NULL) {
      /* The production private factory must expose the real native producer,
       * not the map-only compatibility pipe used by the old fixture. */
      CHECK(a->create_vs_state != NULL && a->create_fs_state != NULL &&
            a->create_blend_state != NULL && a->set_framebuffer_state != NULL &&
            a->clear != NULL && a->draw_vbo != NULL && a->flush != NULL);
      CHECK(b->create_vs_state != NULL && b->create_fs_state != NULL &&
            b->clear != NULL && b->draw_vbo != NULL && b->flush != NULL);
      CHECK(a->screen != b->screen && a->priv != b->priv);
#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
      RuntimeActiveDevice=AgxD3d10WindowsRuntimeForTest(first);
      ADMISSION_UMD_ASAHI_OWNER *nativeOwner=AgxD3d10WindowsOwnerForTest(first);
      RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
      RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;
      memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
      CHECK(RuntimeActiveDevice != NULL && nativeOwner != NULL);
      CHECK(AgxWin32AsahiSceneInit(&scene,a->screen,a));
      CHECK(AgxWin32AsahiSceneDraw(&scene));
      CHECK(AgxWin32AsahiSceneSubmit(&scene));
      CHECK(scene.Receipt.References==30u && scene.Receipt.Relocations==126u &&
            scene.Receipt.EncoderBytes==137u);
      CHECK(!AgxWin32AsahiSceneRetire(&scene,0u));
      CHECK(FAILED(AgxD3d10WindowsCloseDevice(&first)));
      CHECK(first != NULL && AgxD3d10WindowsContext(first)==NULL &&
            AgxD3d10WindowsContext(second)==b && BridgeDestroys==closedBefore);
      if(nativeOwner) RuntimeCheckpoint(nativeOwner,1u);
      CHECK(AgxWin32AsahiSceneRetire(&scene,0u));
      CHECK(AgxWin32AsahiSceneCleanup(&scene,1000u));
      if(nativeOwner) RuntimeCheckpoint(nativeOwner,5u);
#endif
    }
  }
  CHECK(AgxD3d10WindowsCloseDevice(&first) == S_OK && first == NULL);
  CHECK(BridgeDestroys == closedBefore + 1u);
  CHECK(second != NULL && AgxD3d10WindowsContext(second) != NULL);
  if(second != NULL) {
    struct pipe_context *primary=AgxD3d10WindowsContext(second);
    struct pipe_context *extra=primary?primary->screen->context_create(primary->screen,primary->priv,0u):NULL;
    CHECK(extra != NULL);
    CHECK(FAILED(AgxD3d10WindowsCloseDevice(&second)) && second != NULL);
    if(extra) extra->destroy(extra);
    PoolFailDeallocation=32u;
    fprintf(stderr,"FACTORY_DEALLOC_INJECT: before creates=%u deletes=%u unlocks=%u remaining=%u\n",
        PoolCreates,PoolDeletes,PoolUnlocks,PoolFailDeallocation);
    HRESULT injectedClose=AgxD3d10WindowsCloseDevice(&second);
    fprintf(stderr,"FACTORY_DEALLOC_INJECT: after result=0x%08lx owner=%u creates=%u deletes=%u unlocks=%u remaining=%u\n",
        (ULONG)injectedClose,second!=NULL,PoolCreates,PoolDeletes,PoolUnlocks,PoolFailDeallocation);
    CHECK(FAILED(injectedClose) && second != NULL);
    CHECK(AgxD3d10WindowsContext(second)==NULL &&
          BridgeDestroys==closedBefore+1u);
    PoolFailDeallocation=0u;
  }
  CHECK(AgxD3d10WindowsCloseDevice(&second) == S_OK && second == NULL);
  CHECK(BridgeDestroys == closedBefore + 2u);
  CHECK(PoolErrors==0u && PoolCreates>poolCreatesBefore &&
        PoolCreates-poolCreatesBefore==PoolDeletes-poolDeletesBefore &&
        PoolMaps==PoolUnlocks);
  {
    AGX_D3D10_WINDOWS_DEVICE *failed=NULL;
    args.hRTDevice.handle=(VOID *)(UINT_PTR)0x902u;
    PoolFailAllocation=1;
    CHECK(FAILED(AgxD3d10WindowsCreateDevice(adapter,&args,&failed)) && failed==NULL);
    PoolFailAllocation=0;
    PoolFailMap=1;
    CHECK(FAILED(AgxD3d10WindowsCreateDevice(adapter,&args,&failed)) && failed==NULL);
    PoolFailDeallocation=32u;
    CHECK(FAILED(AgxD3d10WindowsCreateDevice(adapter,&args,&failed)) && failed!=NULL);
    CHECK(failed!=NULL && AgxD3d10WindowsContext(failed)==NULL);
    PoolFailMap=0;PoolFailDeallocation=0u;
    CHECK(AgxD3d10WindowsCloseDevice(&failed)==S_OK && failed==NULL);
  }
  CHECK(AgxD3d10WindowsCloseAdapter(&adapter) == S_OK && adapter == NULL);
}
#endif

#if defined(ADMISSION_UMD_D3D10_FRONTEND_TEST)
static HRESULT FrontendLastError;
static unsigned FrontendErrors;
static ADMISSION_UMD_ASAHI_OWNER *FrontendDestroyOwner;
static unsigned FrontendDestroyCalls,FrontendPostReturnCallbacks;
static unsigned FrontendPresentCalls;
static D3DKMT_HANDLE FrontendPresentAllocation;
static PVOID FrontendPresentContext;
static unsigned FrontendSetModeCalls;
static BOOL FrontendCallbacksInvalid;
static UINT_PTR FrontendFailDestroyDevice;
static VOID APIENTRY FrontendSetError(D3D10DDI_HRTCORELAYER core,HRESULT error) {
  (void)core;if(FrontendCallbacksInvalid) ++FrontendPostReturnCallbacks;
  FrontendLastError=error;++FrontendErrors;
}
static HRESULT APIENTRY FrontendPresent(HANDLE device,DXGIDDICB_PRESENT *present) {
  CHECK(device==(HANDLE)(UINT_PTR)0x904u && present &&
        present->hContext==(HANDLE)(UINT_PTR)0xa04u &&
        present->hSrcAllocation==FrontendPresentAllocation &&
        present->hDstAllocation==0u &&
        present->pDXGIContext==FrontendPresentContext);
  ++FrontendPresentCalls;
  return S_OK;
}
static HRESULT APIENTRY FrontendSetDisplayMode(
    HANDLE device,const D3DDDICB_SETDISPLAYMODE *mode) {
  CHECK(device==(HANDLE)(UINT_PTR)0x904u && mode &&
        mode->hPrimaryAllocation==0x775u);
  ++FrontendSetModeCalls;return S_OK;
}
static HRESULT APIENTRY FrontendDestroyContext(HANDLE device,
    const D3DDDICB_DESTROYCONTEXT *destroy) {
  if(FrontendCallbacksInvalid) ++FrontendPostReturnCallbacks;
  ++FrontendDestroyCalls;
  CHECK(destroy->hContext==(HANDLE)((UINT_PTR)device+0x100u));
  if((UINT_PTR)device==FrontendFailDestroyDevice) return E_FAIL;
  if((UINT_PTR)device==0x904u && RuntimeMarker && FrontendDestroyOwner)
    RuntimeCheckpoint(FrontendDestroyOwner,1u);
  ++BridgeDestroys;
  return S_OK;
}
static void test_mesa_d3d10_frontend_open(void) {
  test_mesa_d3d10_adapter2_contract();
  D3D10DDIARG_OPENADAPTER open={0};
  D3D10DDI_ADAPTERFUNCS functions={0};
  D3DDDI_ADAPTERCALLBACKS adapterCallbacks={0};
  D3DDDI_DEVICECALLBACKS callbacks={0};
  D3D10DDI_CORELAYER_DEVICECALLBACKS core={0};
  D3D10DDI_DEVICEFUNCS deviceFunctions={0};
  DXGI_DDI_BASE_CALLBACKS dxgiCallbacks={0};
  DXGI_DDI_BASE_FUNCTIONS dxgiFunctions={0};
  D3D10DDIARG_CALCPRIVATEDEVICESIZE sizeArgs={0};
  D3D10DDIARG_CREATEDEVICE create={0};
  D3D10DDI_HDEVICE device={0};
  D3D10DDI_HRESOURCE presentResource={0};
  D3D10DDI_HRESOURCE createdPresentResource={0};
  void *pendingDeviceQueryStorage=NULL;
  SIZE_T pendingDeviceQueryBytes=0;
  void *crossDeviceQueryStorage=NULL;
  SIZE_T crossDeviceQueryBytes=0;
  adapterCallbacks.pfnQueryAdapterInfoCb=TestQueryAdapterInfo;
  open.hRTAdapter.handle=(VOID *)(UINT_PTR)0x100u;
  open.Interface=D3D10_0_DDI_INTERFACE_VERSION;
  open.pAdapterCallbacks=&adapterCallbacks;
  open.pAdapterFuncs=&functions;
  CHECK(MesaD3d10OpenAdapter10(&open)==S_OK);
  if(!open.hAdapter.pDrvPrivate) return;
  PoolErrors=PoolCreates=PoolMaps=PoolUnlocks=PoolDeletes=0;
  PoolNextHandle=0;PoolFailAllocation=0;PoolFailMap=0;PoolFailDeallocation=0;
  memset(PoolMemory,0,sizeof(PoolMemory));memset(PoolHandles,0,sizeof(PoolHandles));
  callbacks.pfnCreateContextCb=FactoryCreateContext;
  callbacks.pfnDestroyContextCb=FrontendDestroyContext;
  callbacks.pfnAllocateCb=PoolAllocate;callbacks.pfnDeallocateCb=PoolDeallocate;
  callbacks.pfnLockCb=PoolLock;callbacks.pfnUnlockCb=PoolUnlock;
  callbacks.pfnRenderCb=RuntimeRender;
  callbacks.pfnSignalSynchronizationObject2Cb=RuntimeSignal;
  callbacks.pfnSetDisplayModeCb=FrontendSetDisplayMode;
  dxgiCallbacks.pfnPresentCb=FrontendPresent;
  core.pfnSetErrorCb=FrontendSetError;
  sizeArgs.Interface=D3D10_0_DDI_INTERFACE_VERSION;
  SIZE_T bytes=functions.pfnCalcPrivateDeviceSize(open.hAdapter,&sizeArgs);
  device.pDrvPrivate=calloc(1,bytes);
  CHECK(device.pDrvPrivate!=NULL);
  create.hRTDevice.handle=(VOID *)(UINT_PTR)0x904u;
  create.hRTCoreLayer.handle=(VOID *)(UINT_PTR)0xb04u;
  create.hDrvDevice=device;
  create.Interface=D3D10_0_DDI_INTERFACE_VERSION;
  create.pKTCallbacks=&callbacks;create.pUMCallbacks=&core;
  create.pDeviceFuncs=&deviceFunctions;
  create.DXGIBaseDDI.pDXGIBaseCallbacks=&dxgiCallbacks;
  create.DXGIBaseDDI.pDXGIDDIBaseFunctions=&dxgiFunctions;
  CHECK(SUCCEEDED(functions.pfnCreateDevice(open.hAdapter,&create)));
  unsigned ordinarySlots=(unsigned)(sizeof(deviceFunctions)/sizeof(void *));
  unsigned dxgiSlots=(unsigned)(sizeof(dxgiFunctions)/sizeof(void *));
  unsigned ordinaryPresent=0,dxgiPresent=0;
  for(unsigned i=0;i<ordinarySlots;++i)
    if(((void **)&deviceFunctions)[i]) ++ordinaryPresent;
  for(unsigned i=0;i<dxgiSlots;++i)
    if(((void **)&dxgiFunctions)[i]) ++dxgiPresent;
  CHECK(ordinarySlots==101u && ordinaryPresent==101u &&
        dxgiSlots==7u && dxgiPresent==7u);
  CHECK(deviceFunctions.pfnDraw && deviceFunctions.pfnFlush &&
        deviceFunctions.pfnCreateVertexShader && deviceFunctions.pfnCreatePixelShader &&
        deviceFunctions.pfnCreateResource && deviceFunctions.pfnDestroyDevice);
  CHECK(deviceFunctions.pfnCreateQuery && deviceFunctions.pfnDestroyQuery &&
        deviceFunctions.pfnQueryBegin && deviceFunctions.pfnQueryEnd &&
        deviceFunctions.pfnQueryGetData && deviceFunctions.pfnSetPredication &&
        deviceFunctions.pfnSoSetTargets && deviceFunctions.pfnDrawAuto &&
        deviceFunctions.pfnCreateGeometryShaderWithStreamOutput &&
        deviceFunctions.pfnClearDepthStencilView && deviceFunctions.pfnGenMips &&
        deviceFunctions.pfnResourceCopy && deviceFunctions.pfnResourceCopyRegion &&
        deviceFunctions.pfnResourceResolveSubresource &&
        deviceFunctions.pfnCheckFormatSupport &&
        deviceFunctions.pfnCheckMultisampleQualityLevels && dxgiFunctions.pfnPresent);
  if(deviceFunctions.pfnDraw && deviceFunctions.pfnFlush) {
    UINT formatCaps=~0u,quality=~0u;
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_B8G8R8A8_UNORM,&formatCaps);
    CHECK(formatCaps==(D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET|
                       D3D10_DDI_FORMAT_SUPPORT_BLENDABLE));
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_R32G32B32A32_FLOAT,&formatCaps);
    CHECK(formatCaps==0u);
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_R8G8B8A8_UNORM,&formatCaps);
    CHECK(formatCaps==0u);
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,&formatCaps);
    CHECK(formatCaps==D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_B8G8R8A8_UNORM,1,&quality);
    CHECK(quality==1u);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_B8G8R8A8_UNORM,2,&quality);
    CHECK(quality==0u);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_R32G32B32A32_FLOAT,1,&quality);
    CHECK(quality==0u);
    D3D10DDIARG_CREATEQUERY eventQuery={0};
    D3D10DDI_HQUERY eventQueryHandle={0};
    D3D10DDI_HRTQUERY eventQueryRuntime={0};
    eventQuery.Query=D3D10DDI_QUERY_EVENT;
    SIZE_T eventQueryBytes=deviceFunctions.pfnCalcPrivateQuerySize(
        device,&eventQuery);
    eventQueryHandle.pDrvPrivate=calloc(1,eventQueryBytes);
    CHECK(eventQueryHandle.pDrvPrivate!=NULL);
    if(eventQueryHandle.pDrvPrivate) {
      unsigned errorsBefore=FrontendErrors,createsBefore=PoolCreates;
      deviceFunctions.pfnCreateQuery(device,&eventQuery,
          eventQueryHandle,eventQueryRuntime);
      CHECK(FrontendErrors==errorsBefore && PoolCreates==createsBefore);
      if(FrontendErrors==errorsBefore) {
        deviceFunctions.pfnDestroyQuery(device,eventQueryHandle);
        CHECK(FrontendErrors==errorsBefore);
      }
      free(eventQueryHandle.pDrvPrivate);
    }
    D3D10DDIARG_CREATEQUERY unsupportedQuery={0};
    D3D10DDI_HQUERY unsupportedQueryHandle={0};
    D3D10DDI_HRTQUERY unsupportedQueryRuntime={0};
    unsupportedQuery.Query=D3D10DDI_QUERY_OCCLUSION;
    SIZE_T unsupportedQueryBytes=deviceFunctions.pfnCalcPrivateQuerySize(
        device,&unsupportedQuery);
    unsupportedQueryHandle.pDrvPrivate=malloc(unsupportedQueryBytes);
    CHECK(unsupportedQueryHandle.pDrvPrivate!=NULL);
    if(unsupportedQueryHandle.pDrvPrivate) {
      memset(unsupportedQueryHandle.pDrvPrivate,0x5a,unsupportedQueryBytes);
      unsigned errorsBefore=FrontendErrors,createsBefore=PoolCreates;
      unsigned rendersBefore=RuntimeRenders,signalsBefore=RuntimeSignals;
      deviceFunctions.pfnCreateQuery(device,&unsupportedQuery,
          unsupportedQueryHandle,unsupportedQueryRuntime);
      CHECK(FrontendErrors==errorsBefore+1u && FrontendLastError==E_NOTIMPL &&
            PoolCreates==createsBefore && RuntimeRenders==rendersBefore &&
            RuntimeSignals==signalsBefore);
      unsigned char *queryBytes=(unsigned char *)unsupportedQueryHandle.pDrvPrivate;
      for(SIZE_T i=0;i<unsupportedQueryBytes;++i) CHECK(queryBytes[i]==0x5a);
#define FRONTEND_QUERY_REJECT(call,expected) do { \
        unsigned beforeErrors=FrontendErrors,beforeCreates=PoolCreates; \
        unsigned beforeRenders=RuntimeRenders,beforeSignals=RuntimeSignals; \
        call; \
        CHECK(FrontendErrors==beforeErrors+1u && FrontendLastError==(expected) && \
              PoolCreates==beforeCreates && RuntimeRenders==beforeRenders && \
              RuntimeSignals==beforeSignals); \
        for(SIZE_T qi=0;qi<unsupportedQueryBytes;++qi) CHECK(queryBytes[qi]==0x5a); \
      } while(0)
      FRONTEND_QUERY_REJECT(deviceFunctions.pfnQueryBegin(
          device,unsupportedQueryHandle),E_INVALIDARG);
      FRONTEND_QUERY_REJECT(deviceFunctions.pfnQueryEnd(
          device,unsupportedQueryHandle),E_INVALIDARG);
      UINT64 queryResult=0x8877665544332211ULL;
      FRONTEND_QUERY_REJECT(deviceFunctions.pfnQueryGetData(
          device,unsupportedQueryHandle,&queryResult,sizeof(queryResult),0),E_INVALIDARG);
      CHECK(queryResult==0x8877665544332211ULL);
      FRONTEND_QUERY_REJECT(deviceFunctions.pfnSetPredication(
          device,unsupportedQueryHandle,FALSE),E_NOTIMPL);
      {
        unsigned beforeErrors=FrontendErrors,beforeCreates=PoolCreates;
        unsigned beforeRenders=RuntimeRenders,beforeSignals=RuntimeSignals;
        deviceFunctions.pfnDestroyQuery(device,unsupportedQueryHandle);
        CHECK(FrontendErrors==beforeErrors+1u && FrontendLastError==E_INVALIDARG &&
              PoolCreates==beforeCreates && RuntimeRenders==beforeRenders &&
              RuntimeSignals==beforeSignals);
        for(SIZE_T qi=0;qi<unsupportedQueryBytes;++qi) CHECK(queryBytes[qi]==0x5a);
      }
#undef FRONTEND_QUERY_REJECT
      free(unsupportedQueryHandle.pDrvPrivate);
    }
    D3D10DDIARG_CREATEDEPTHSTENCILVIEW unsupportedDepthDesc={0};
    D3D10DDI_HDEPTHSTENCILVIEW unsupportedDepth={0};
    unsupportedDepth.pDrvPrivate=calloc(1,
        deviceFunctions.pfnCalcPrivateDepthStencilViewSize(
            device,&unsupportedDepthDesc));
    CHECK(unsupportedDepth.pDrvPrivate!=NULL);
    if(unsupportedDepth.pDrvPrivate) {
      unsigned errorsBefore=FrontendErrors,createsBefore=PoolCreates;
      unsigned rendersBefore=RuntimeRenders,signalsBefore=RuntimeSignals;
      deviceFunctions.pfnClearDepthStencilView(device,unsupportedDepth,
          D3D10_DDI_CLEAR_DEPTH,1.0f,0);
      CHECK(FrontendErrors==errorsBefore+1u && FrontendLastError==E_NOTIMPL &&
            PoolCreates==createsBefore && RuntimeRenders==rendersBefore &&
            RuntimeSignals==signalsBefore);
      free(unsupportedDepth.pDrvPrivate);
    }
#define FRONTEND_STAGE(name) do { fprintf(stderr,"D3D10_FRONTEND_STAGE: %s\n",name);fflush(stderr); } while(0)
#define FRONTEND_OP(op,len) (ENCODE_D3D10_SB_OPCODE_TYPE(op)|ENCODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(len))
#define FRONTEND_REG(type,selection,components) (ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT)|ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(selection)|components|ENCODE_D3D10_SB_OPERAND_TYPE(type)|ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_1D)|ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0,D3D10_SB_OPERAND_INDEX_IMMEDIATE32))
#define FRONTEND_CB (ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT)|ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE)|D3D10_SB_OPERAND_4_COMPONENT_NOSWIZZLE|ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER)|ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_2D)|ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(0,D3D10_SB_OPERAND_INDEX_IMMEDIATE32)|ENCODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(1,D3D10_SB_OPERAND_INDEX_IMMEDIATE32))
#define FRONTEND_IMM4 (ENCODE_D3D10_SB_OPERAND_NUM_COMPONENTS(D3D10_SB_OPERAND_4_COMPONENT)|ENCODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE)|D3D10_SB_OPERAND_4_COMPONENT_NOSWIZZLE|ENCODE_D3D10_SB_OPERAND_TYPE(D3D10_SB_OPERAND_TYPE_IMMEDIATE32)|ENCODE_D3D10_SB_OPERAND_INDEX_DIMENSION(D3D10_SB_OPERAND_INDEX_0D))
    UINT vs[]={
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_VERTEX_SHADER,4,0),22,
      FRONTEND_OP(D3D10_SB_OPCODE_DCL_INPUT,3),
      FRONTEND_REG(D3D10_SB_OPERAND_TYPE_INPUT,D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE,D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),0,
      FRONTEND_OP(D3D10_SB_OPCODE_DCL_OUTPUT_SIV,4),
      FRONTEND_REG(D3D10_SB_OPERAND_TYPE_OUTPUT,D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE,D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),0,
      ENCODE_D3D10_SB_NAME(D3D10_SB_NAME_POSITION),
      FRONTEND_OP(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER,4),FRONTEND_CB,0,1,
      FRONTEND_OP(D3D10_SB_OPCODE_ADD,8),
      FRONTEND_REG(D3D10_SB_OPERAND_TYPE_OUTPUT,D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE,D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),0,
      FRONTEND_REG(D3D10_SB_OPERAND_TYPE_INPUT,D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE,D3D10_SB_OPERAND_4_COMPONENT_NOSWIZZLE),0,
      FRONTEND_CB,0,0,
      FRONTEND_OP(D3D10_SB_OPCODE_RET,1)};
    UINT ps[]={
      ENCODE_D3D10_SB_TOKENIZED_PROGRAM_VERSION_TOKEN(D3D10_SB_PIXEL_SHADER,4,0),16,
      FRONTEND_OP(D3D10_SB_OPCODE_DCL_OUTPUT,3),
      FRONTEND_REG(D3D10_SB_OPERAND_TYPE_OUTPUT,D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE,D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),0,
      FRONTEND_OP(D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER,4),FRONTEND_CB,0,1,
      FRONTEND_OP(D3D10_SB_OPCODE_MOV,6),
      FRONTEND_REG(D3D10_SB_OPERAND_TYPE_OUTPUT,D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE,D3D10_SB_OPERAND_4_COMPONENT_MASK_ALL),0,
      FRONTEND_CB,0,0,
      FRONTEND_OP(D3D10_SB_OPCODE_RET,1)};
    float vertices[12]={-1,-1,0,1,1,-1,0,1,0,1,0,1};
    D3D10DDI_MIPINFO rtMip={0},vbMip={0},cbMip={0},ibMip={0};
    D3D10_DDIARG_SUBRESOURCE_UP vbInitial={0},cbInitial={0},ibInitial={0};
    D3D10DDIARG_CREATERESOURCE rtCreate={0},vbCreate={0},cbCreate={0},ibCreate={0};
    D3D10DDI_HRESOURCE rt={0},vb={0},cb={0},vsCb={0},ib={0};
    D3D10DDI_HRTRESOURCE rtRuntime={0},vbRuntime={0},cbRuntime={0},vsCbRuntime={0},ibRuntime={0};
    rtMip.TexelWidth=16;rtMip.TexelHeight=16;rtMip.TexelDepth=1;
    rtCreate.pMipInfoList=&rtMip;rtCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
    rtCreate.Usage=D3D10_DDI_USAGE_DEFAULT;rtCreate.BindFlags=D3D10_DDI_BIND_RENDER_TARGET;
    rtCreate.Format=DXGI_FORMAT_B8G8R8A8_UNORM;rtCreate.SampleDesc.Count=1;
    rtCreate.MipLevels=1;rtCreate.ArraySize=1;
    {
      D3D10DDIARG_CREATERESOURCE rejected=rtCreate;
      D3D10DDI_HRESOURCE rejectedHandle={0};D3D10DDI_HRTRESOURCE rejectedRuntime={0};
      rejected.BindFlags|=D3D10_DDI_BIND_SHADER_RESOURCE;
      rejectedHandle.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateResourceSize(device,&rejected));
      unsigned createsBefore=PoolCreates,rendersBefore=RuntimeRenders,errorsBefore=FrontendErrors;
      deviceFunctions.pfnCreateResource(device,&rejected,rejectedHandle,rejectedRuntime);
      CHECK(FrontendErrors==errorsBefore+1 && FrontendLastError==E_NOTIMPL &&
            PoolCreates==createsBefore && RuntimeRenders==rendersBefore);
      deviceFunctions.pfnDestroyResource(device,rejectedHandle);free(rejectedHandle.pDrvPrivate);
    }
    rt.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateResourceSize(device,&rtCreate));
    rtRuntime.handle=(VOID *)(UINT_PTR)0xd01u;
    CHECK(rt.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&rtCreate,rt,rtRuntime);
    FRONTEND_STAGE("rt-resource");
    {
      D3D10DDIARG_OPENRESOURCE presentOpen={0};
      D3DDDI_OPENALLOCATIONINFO presentInfo={0};
      ADMISSION_ALLOCATION_DESCRIPTION presentDescription={0};
      D3D10DDI_HRTRESOURCE presentRuntime={0};
      initialize_open_resource(&presentOpen,&presentInfo,&presentDescription,
          0x771u,0x772u);
      presentRuntime.handle=(VOID *)(UINT_PTR)0x773u;
      SIZE_T presentBytes=deviceFunctions.pfnCalcPrivateOpenedResourceSize(
          device,&presentOpen);
      presentResource.pDrvPrivate=calloc(1,presentBytes);
      CHECK(presentResource.pDrvPrivate && presentBytes);
      unsigned presentOpenErrors=FrontendErrors;
      deviceFunctions.pfnOpenResource(device,&presentOpen,presentResource,presentRuntime);
      CHECK(FrontendErrors==presentOpenErrors);
      DXGI_DDI_ARG_PRESENT presentArgs={0};
      presentArgs.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      presentArgs.hSurfaceToPresent=
          (DXGI_DDI_HRESOURCE)(UINT_PTR)presentResource.pDrvPrivate;
      presentArgs.Flags.Value=0x2u;
      presentArgs.FlipInterval=DXGI_DDI_FLIP_INTERVAL_ONE;
      presentArgs.pDXGIContext=(PVOID)(UINT_PTR)0x774u;
      FrontendPresentCalls=0;FrontendPresentAllocation=0x771u;
      FrontendPresentContext=presentArgs.pDXGIContext;
      CHECK(dxgiFunctions.pfnPresent(&presentArgs)==S_OK &&
            FrontendPresentCalls==1u);
      presentArgs.SrcSubResourceIndex=1u;
      CHECK(dxgiFunctions.pfnPresent(&presentArgs)==E_INVALIDARG &&
            FrontendPresentCalls==1u);
      presentArgs.SrcSubResourceIndex=0u;presentArgs.Flags.Value=0u;
      CHECK(dxgiFunctions.pfnPresent(&presentArgs)==E_INVALIDARG &&
            FrontendPresentCalls==1u);
    }
    {
      D3D10DDIARG_CREATERESOURCE createPresent={0};
      D3D10DDI_MIPINFO presentMip={0};
      DXGI_DDI_PRIMARY_DESC primary={0};
      D3D10DDI_HRTRESOURCE presentRuntime={0};
      presentMip.TexelWidth=2560u;presentMip.TexelHeight=1600u;
      presentMip.TexelDepth=1u;
      createPresent.pMipInfoList=&presentMip;
      createPresent.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
      createPresent.Usage=D3D10_DDI_USAGE_DEFAULT;
      createPresent.BindFlags=D3D10_DDI_BIND_PRESENT;
      createPresent.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
      createPresent.SampleDesc.Count=1u;createPresent.MipLevels=1u;
      createPresent.ArraySize=1u;createPresent.pPrimaryDesc=&primary;
      presentRuntime.handle=(VOID *)(UINT_PTR)0x777u;
      SIZE_T presentBytes=deviceFunctions.pfnCalcPrivateResourceSize(
          device,&createPresent);
      createdPresentResource.pDrvPrivate=calloc(1,presentBytes);
      CHECK(createdPresentResource.pDrvPrivate && presentBytes);
      unsigned createPresentErrors=FrontendErrors;
      deviceFunctions.pfnCreateResource(device,&createPresent,
          createdPresentResource,presentRuntime);
      CHECK(FrontendErrors==createPresentErrors);
      DXGI_DDI_ARG_SETDISPLAYMODE mode={0};
      mode.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      mode.hResource=(DXGI_DDI_HRESOURCE)(UINT_PTR)
          createdPresentResource.pDrvPrivate;
      FrontendSetModeCalls=0;
      CHECK(dxgiFunctions.pfnSetDisplayMode(&mode)==S_OK &&
            FrontendSetModeCalls==1u);
      mode.SubResourceIndex=1u;
      CHECK(dxgiFunctions.pfnSetDisplayMode(&mode)==E_INVALIDARG &&
            FrontendSetModeCalls==1u);
    }
    {
      DXGI_DDI_HRESOURCE rotating[2]={
          (DXGI_DDI_HRESOURCE)(UINT_PTR)presentResource.pDrvPrivate,
          (DXGI_DDI_HRESOURCE)(UINT_PTR)createdPresentResource.pDrvPrivate};
      DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES rotate={0};
      rotate.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      rotate.pResources=rotating;rotate.Resources=2;
      CHECK(dxgiFunctions.pfnRotateResourceIdentities(&rotate)==S_OK);
      CHECK(dxgiFunctions.pfnRotateResourceIdentities(&rotate)==S_OK);
    }
    DXGI_DDI_ARG_PRESENT unsupportedPresent={0};
    unsupportedPresent.hDevice=(UINT_PTR)device.pDrvPrivate;
    unsupportedPresent.hSurfaceToPresent=(UINT_PTR)rt.pDrvPrivate;
    unsigned presentErrorsBefore=FrontendErrors,presentCreatesBefore=PoolCreates;
    unsigned presentRendersBefore=RuntimeRenders,presentSignalsBefore=RuntimeSignals;
    CHECK(dxgiFunctions.pfnPresent(&unsupportedPresent)==E_INVALIDARG &&
          FrontendErrors==presentErrorsBefore &&
          PoolCreates==presentCreatesBefore && RuntimeRenders==presentRendersBefore &&
          RuntimeSignals==presentSignalsBefore);
#define FRONTEND_DXGI_REJECT(call) do { \
      unsigned beforeErrors=FrontendErrors,beforeCreates=PoolCreates; \
      unsigned beforeRenders=RuntimeRenders,beforeSignals=RuntimeSignals; \
      CHECK((call)==E_NOTIMPL); \
      CHECK(FrontendErrors==beforeErrors+1u && FrontendLastError==E_NOTIMPL && \
            PoolCreates==beforeCreates && RuntimeRenders==beforeRenders && \
            RuntimeSignals==beforeSignals); \
    } while(0)
    DXGI_DDI_HRESOURCE dxgiRt=(UINT_PTR)rt.pDrvPrivate;
    DXGI_DDI_ARG_SETDISPLAYMODE unsupportedMode={0};
    unsupportedMode.hDevice=(UINT_PTR)device.pDrvPrivate;unsupportedMode.hResource=dxgiRt;
    CHECK(dxgiFunctions.pfnSetDisplayMode(&unsupportedMode)==E_INVALIDARG);
    DXGI_DDI_ARG_SETRESOURCEPRIORITY unsupportedPriority={0};
    unsupportedPriority.hDevice=(UINT_PTR)device.pDrvPrivate;unsupportedPriority.hResource=dxgiRt;
    FRONTEND_DXGI_REJECT(dxgiFunctions.pfnSetResourcePriority(&unsupportedPriority));
    DXGI_DDI_RESIDENCY residency=(DXGI_DDI_RESIDENCY)0x5a;
    DXGI_DDI_ARG_QUERYRESOURCERESIDENCY unsupportedResidency={0};
    unsupportedResidency.hDevice=(UINT_PTR)device.pDrvPrivate;
    unsupportedResidency.pResources=&dxgiRt;unsupportedResidency.pStatus=&residency;
    unsupportedResidency.Resources=1;
    FRONTEND_DXGI_REJECT(dxgiFunctions.pfnQueryResourceResidency(&unsupportedResidency));
    CHECK(residency==(DXGI_DDI_RESIDENCY)0x5a);
    DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES unsupportedRotate={0};
    unsupportedRotate.hDevice=(UINT_PTR)device.pDrvPrivate;
    unsupportedRotate.pResources=&dxgiRt;unsupportedRotate.Resources=1;
    CHECK(dxgiFunctions.pfnRotateResourceIdentities(&unsupportedRotate)==E_INVALIDARG);
    DXGI_GAMMA_CONTROL_CAPABILITIES gamma;
    memset(&gamma,0x5a,sizeof(gamma));
    DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS unsupportedGamma={0};
    unsupportedGamma.hDevice=(UINT_PTR)device.pDrvPrivate;
    unsupportedGamma.pGammaCapabilities=&gamma;
    CHECK(dxgiFunctions.pfnGetGammaCaps(&unsupportedGamma)==S_OK);
    DXGI_GAMMA_CONTROL_CAPABILITIES zeroGamma={0};
    CHECK(memcmp(&gamma,&zeroGamma,sizeof(gamma))==0);
    DXGI_DDI_ARG_BLT unsupportedBlt={0};
    unsupportedBlt.hDevice=(UINT_PTR)device.pDrvPrivate;
    unsupportedBlt.hDstResource=dxgiRt;unsupportedBlt.hSrcResource=dxgiRt;
    FRONTEND_DXGI_REJECT(dxgiFunctions.pfnBlt(&unsupportedBlt));
#undef FRONTEND_DXGI_REJECT
    D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT unsupportedGsSo={0};
    D3D10DDI_HSHADER unsupportedGsSoHandle={0};
    D3D10DDI_HRTSHADER unsupportedGsSoRuntime={0};
    SIZE_T unsupportedGsSoBytes=
        deviceFunctions.pfnCalcPrivateGeometryShaderWithStreamOutput(
            device,&unsupportedGsSo,NULL);
    unsupportedGsSoHandle.pDrvPrivate=malloc(unsupportedGsSoBytes);
    CHECK(unsupportedGsSoHandle.pDrvPrivate!=NULL);
    if(unsupportedGsSoHandle.pDrvPrivate) {
      memset(unsupportedGsSoHandle.pDrvPrivate,0x5a,unsupportedGsSoBytes);
      unsigned errorsBefore=FrontendErrors,createsBefore=PoolCreates;
      unsigned rendersBefore=RuntimeRenders,signalsBefore=RuntimeSignals;
      deviceFunctions.pfnCreateGeometryShaderWithStreamOutput(device,
          &unsupportedGsSo,unsupportedGsSoHandle,unsupportedGsSoRuntime,NULL);
      CHECK(FrontendErrors==errorsBefore+1u && FrontendLastError==E_NOTIMPL &&
            PoolCreates==createsBefore && RuntimeRenders==rendersBefore &&
            RuntimeSignals==signalsBefore);
      unsigned char *gsBytes=(unsigned char *)unsupportedGsSoHandle.pDrvPrivate;
      for(SIZE_T i=0;i<unsupportedGsSoBytes;++i) CHECK(gsBytes[i]==0x5a);
      free(unsupportedGsSoHandle.pDrvPrivate);
    }
    vbMip.TexelWidth=sizeof(vertices);vbMip.TexelHeight=vbMip.TexelDepth=1;
    vbInitial.pSysMem=vertices;vbInitial.SysMemPitch=sizeof(vertices);vbInitial.SysMemSlicePitch=sizeof(vertices);
    vbCreate.pMipInfoList=&vbMip;vbCreate.pInitialDataUP=&vbInitial;
    vbCreate.ResourceDimension=D3D10DDIRESOURCE_BUFFER;vbCreate.Usage=D3D10_DDI_USAGE_IMMUTABLE;
    vbCreate.BindFlags=D3D10_DDI_BIND_VERTEX_BUFFER;vbCreate.Format=DXGI_FORMAT_UNKNOWN;
    vbCreate.SampleDesc.Count=1;vbCreate.MipLevels=1;vbCreate.ArraySize=1;
    SIZE_T vbPrivateBytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&vbCreate);
    vb.pDrvPrivate=calloc(1,vbPrivateBytes);
    vbRuntime.handle=(VOID *)(UINT_PTR)0xd02u;
    CHECK(vb.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&vbCreate,vb,vbRuntime);
    FRONTEND_STAGE("vb-resource");
    float cbValues[4]={0.9f,0.2f,0.1f,1.0f};
    cbMip.TexelWidth=sizeof(cbValues);cbMip.TexelHeight=cbMip.TexelDepth=1;
    cbInitial.pSysMem=cbValues;cbInitial.SysMemPitch=1;cbInitial.SysMemSlicePitch=1;
    cbCreate.pMipInfoList=&cbMip;cbCreate.pInitialDataUP=&cbInitial;
    cbCreate.ResourceDimension=D3D10DDIRESOURCE_BUFFER;
    cbCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
    cbCreate.BindFlags=D3D10_DDI_BIND_CONSTANT_BUFFER;
    cbCreate.Format=DXGI_FORMAT_UNKNOWN;cbCreate.SampleDesc.Count=1;
    cbCreate.MipLevels=1;cbCreate.ArraySize=1;
    cb.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateResourceSize(device,&cbCreate));
    cbRuntime.handle=(VOID *)(UINT_PTR)0xd0au;
    CHECK(cb.pDrvPrivate!=NULL);
    unsigned cbErrorsBefore=FrontendErrors;
    deviceFunctions.pfnCreateResource(device,&cbCreate,cb,cbRuntime);
    CHECK(FrontendErrors==cbErrorsBefore);
    float vsCbValues[4]={0.0f,0.0f,0.0f,0.0f};
    cbInitial.pSysMem=vsCbValues;
    vsCb.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateResourceSize(device,&cbCreate));
    vsCbRuntime.handle=(VOID *)(UINT_PTR)0xd0bu;
    CHECK(vsCb.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&cbCreate,vsCb,vsCbRuntime);
    CHECK(FrontendErrors==cbErrorsBefore);
    cbInitial.pSysMem=cbValues;
    UINT16 ibValues[4]={0u,1u,2u,0u};
    ibMip.TexelWidth=sizeof(ibValues);ibMip.TexelHeight=ibMip.TexelDepth=1;
    ibInitial.pSysMem=ibValues;ibInitial.SysMemPitch=sizeof(ibValues);
    ibInitial.SysMemSlicePitch=sizeof(ibValues);
    ibCreate.pMipInfoList=&ibMip;ibCreate.pInitialDataUP=&ibInitial;
    ibCreate.ResourceDimension=D3D10DDIRESOURCE_BUFFER;
    ibCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
    ibCreate.BindFlags=D3D10_DDI_BIND_INDEX_BUFFER;
    ibCreate.Format=DXGI_FORMAT_UNKNOWN;ibCreate.SampleDesc.Count=1;
    ibCreate.MipLevels=1;ibCreate.ArraySize=1;
    ib.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateResourceSize(device,&ibCreate));
    ibRuntime.handle=(VOID *)(UINT_PTR)0xd0cu;
    CHECK(ib.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&ibCreate,ib,ibRuntime);
    CHECK(FrontendErrors==cbErrorsBefore);
    for(unsigned invalidIndex=0;invalidIndex<8u;++invalidIndex) {
      D3D10DDIARG_CREATERESOURCE invalid=ibCreate;
      D3D10DDI_MIPINFO mip=ibMip;
      D3D10_DDIARG_SUBRESOURCE_UP initial=ibInitial;
      UINT16 badPadding[4]={0u,1u,2u,1u};
      invalid.pMipInfoList=&mip;invalid.pInitialDataUP=&initial;
      switch(invalidIndex) {
      case 0: mip.TexelWidth=6; break;
      case 1: mip.TexelWidth=10; break;
      case 2: invalid.BindFlags|=D3D10_DDI_BIND_VERTEX_BUFFER; break;
      case 3: invalid.Usage=D3D10_DDI_USAGE_IMMUTABLE; break;
      case 4: invalid.Format=DXGI_FORMAT_R16_UINT; break;
      case 5: invalid.pInitialDataUP=NULL; break;
      case 6: initial.pSysMem=NULL; break;
      default: initial.pSysMem=badPadding; break;
      }
      SIZE_T privateBytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&invalid);
      D3D10DDI_HRESOURCE handle={0};D3D10DDI_HRTRESOURCE runtime={0};
      handle.pDrvPrivate=malloc(privateBytes);runtime.handle=(VOID *)(UINT_PTR)(0xf00u+invalidIndex);
      CHECK(handle.pDrvPrivate!=NULL);
      if(handle.pDrvPrivate) {
        memset(handle.pDrvPrivate,0x5a,privateBytes);
        unsigned errors=FrontendErrors,creates=PoolCreates,renders=RuntimeRenders;
        deviceFunctions.pfnCreateResource(device,&invalid,handle,runtime);
        CHECK(FrontendErrors==errors+1u && FrontendLastError==E_NOTIMPL &&
              PoolCreates==creates && RuntimeRenders==renders);
        unsigned char *storage=handle.pDrvPrivate;
        for(SIZE_T i=0;i<privateBytes;++i) CHECK(storage[i]==0x5a);
        free(handle.pDrvPrivate);
      }
    }
    for(unsigned invalidCase=0;invalidCase<14u;++invalidCase) {
      D3D10DDIARG_CREATERESOURCE invalid=cbCreate;
      D3D10DDI_MIPINFO invalidMip=cbMip;
      D3D10_DDIARG_SUBRESOURCE_UP invalidInitial=cbInitial;
      invalid.pMipInfoList=&invalidMip;
      invalid.pInitialDataUP=&invalidInitial;
      switch(invalidCase) {
      case 0: invalidMip.TexelWidth=15; break;
      case 1: invalidMip.TexelWidth=65552; break;
      case 2: invalid.BindFlags|=D3D10_DDI_BIND_VERTEX_BUFFER; break;
      case 3: invalid.Usage=D3D10_DDI_USAGE_IMMUTABLE; break;
      case 4: invalid.MapFlags=1; break;
      case 5: invalid.MiscFlags=1; break;
      case 6: invalid.MipLevels=2; break;
      case 7: invalid.ArraySize=2; break;
      case 8: invalidMip.TexelHeight=2; break;
      case 9: invalidMip.TexelDepth=2; break;
      case 10: invalid.SampleDesc.Count=2; break;
      case 11: invalid.SampleDesc.Quality=1; break;
      case 12: invalid.Format=DXGI_FORMAT_R32_FLOAT; break;
      default: invalidInitial.pSysMem=NULL; break;
      }
      SIZE_T invalidBytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&invalid);
      D3D10DDI_HRESOURCE invalidHandle={0};
      D3D10DDI_HRTRESOURCE invalidRuntime={0};
      invalidHandle.pDrvPrivate=calloc(1,invalidBytes);
      invalidRuntime.handle=(VOID *)(UINT_PTR)(0xe00u+invalidCase);
      CHECK(invalidHandle.pDrvPrivate!=NULL);
      if(invalidHandle.pDrvPrivate) {
        memset(invalidHandle.pDrvPrivate,0x5a,invalidBytes);
        unsigned errorsBefore=FrontendErrors,createsBefore=PoolCreates;
        unsigned rendersBefore=RuntimeRenders,signalsBefore=RuntimeSignals;
        deviceFunctions.pfnCreateResource(device,&invalid,invalidHandle,invalidRuntime);
        CHECK(FrontendErrors==errorsBefore+1u && FrontendLastError==E_NOTIMPL &&
              PoolCreates==createsBefore && RuntimeRenders==rendersBefore &&
              RuntimeSignals==signalsBefore);
        unsigned char *invalidStorage=invalidHandle.pDrvPrivate;
        for(SIZE_T i=0;i<invalidBytes;++i) CHECK(invalidStorage[i]==0x5a);
        free(invalidHandle.pDrvPrivate);
      }
    }
    if(vb.pDrvPrivate) {
      void *vbSnapshot=malloc(vbPrivateBytes);
      CHECK(vbSnapshot!=NULL);
      if(vbSnapshot) {
        memcpy(vbSnapshot,vb.pDrvPrivate,vbPrivateBytes);
        UINT soOffset=0;
        unsigned errorsBefore=FrontendErrors,createsBefore=PoolCreates;
        unsigned rendersBefore=RuntimeRenders,signalsBefore=RuntimeSignals;
        deviceFunctions.pfnSoSetTargets(device,1,0,&vb,&soOffset);
        CHECK(FrontendErrors==errorsBefore+1u && FrontendLastError==E_NOTIMPL &&
              PoolCreates==createsBefore && RuntimeRenders==rendersBefore &&
              RuntimeSignals==signalsBefore &&
              memcmp(vbSnapshot,vb.pDrvPrivate,vbPrivateBytes)==0);
        free(vbSnapshot);
      }
    }
#define FRONTEND_UNSUPPORTED_REJECT(call) do { \
      unsigned beforeErrors=FrontendErrors,beforeCreates=PoolCreates; \
      unsigned beforeRenders=RuntimeRenders,beforeSignals=RuntimeSignals; \
      call; \
      CHECK(FrontendErrors==beforeErrors+1u && FrontendLastError==E_NOTIMPL && \
            PoolCreates==beforeCreates && RuntimeRenders==beforeRenders && \
            RuntimeSignals==beforeSignals); \
    } while(0)
    FRONTEND_UNSUPPORTED_REJECT(deviceFunctions.pfnResourceResolveSubresource(
        device,rt,0,rt,0,DXGI_FORMAT_B8G8R8A8_UNORM));
    FRONTEND_UNSUPPORTED_REJECT(deviceFunctions.pfnDrawAuto(device));
    D3D10DDIARG_CREATESHADERRESOURCEVIEW unsupportedSrvDesc={0};
    D3D10DDI_HSHADERRESOURCEVIEW unsupportedSrv={0};
    unsupportedSrv.pDrvPrivate=calloc(1,
        deviceFunctions.pfnCalcPrivateShaderResourceViewSize(
            device,&unsupportedSrvDesc));
    CHECK(unsupportedSrv.pDrvPrivate!=NULL);
    if(unsupportedSrv.pDrvPrivate) {
      FRONTEND_UNSUPPORTED_REJECT(deviceFunctions.pfnGenMips(device,unsupportedSrv));
      free(unsupportedSrv.pDrvPrivate);
    }
    FRONTEND_UNSUPPORTED_REJECT(deviceFunctions.pfnResourceCopy(device,rt,rt));
    FRONTEND_UNSUPPORTED_REJECT(deviceFunctions.pfnResourceCopyRegion(
        device,rt,0,0,0,0,rt,0,NULL));
#undef FRONTEND_UNSUPPORTED_REJECT
    D3D10DDIARG_CREATERENDERTARGETVIEW rtvCreate={0};
    D3D10DDI_HRENDERTARGETVIEW rtv={0};D3D10DDI_HRTRENDERTARGETVIEW rtvRuntime={0};
    rtvCreate.hDrvResource=createdPresentResource;
    rtvCreate.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    rtvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
    rtvCreate.Tex2D.ArraySize=1;
    rtv.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateRenderTargetViewSize(device,&rtvCreate));
    rtvRuntime.handle=(VOID *)(UINT_PTR)0xd03u;
    CHECK(rtv.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateRenderTargetView(device,&rtvCreate,rtv,rtvRuntime);
    FRONTEND_STAGE("rt-view");
    D3D10DDIARG_INPUT_ELEMENT_DESC element={0};D3D10DDIARG_CREATEELEMENTLAYOUT layoutCreate={0};
    D3D10DDI_HELEMENTLAYOUT layout={0};D3D10DDI_HRTELEMENTLAYOUT layoutRuntime={0};
    element.InputSlot=0;element.InputRegister=0;element.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    element.InputSlotClass=D3D10_DDI_INPUT_PER_VERTEX_DATA;
    layoutCreate.NumElements=1;layoutCreate.pVertexElements=&element;
    layout.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateElementLayoutSize(device,&layoutCreate));
    layoutRuntime.handle=(VOID *)(UINT_PTR)0xd04u;
    CHECK(layout.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateElementLayout(device,&layoutCreate,layout,layoutRuntime);
    FRONTEND_STAGE("element-layout");
    D3D10DDI_HSHADER vsh={0},psh={0};D3D10DDI_HRTSHADER vsRuntime={0},psRuntime={0};
    vsh.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateShaderSize(device,vs,NULL));
    psh.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateShaderSize(device,ps,NULL));
    vsRuntime.handle=(VOID *)(UINT_PTR)0xd05u;psRuntime.handle=(VOID *)(UINT_PTR)0xd06u;
    CHECK(vsh.pDrvPrivate && psh.pDrvPrivate);
    deviceFunctions.pfnCreateVertexShader(device,vs,vsh,vsRuntime,NULL);
    deviceFunctions.pfnCreatePixelShader(device,ps,psh,psRuntime,NULL);
    CHECK(MesaD3d10FrontendShaderValidForTest(vsh) &&
          MesaD3d10FrontendShaderValidForTest(psh));
    FRONTEND_STAGE("shaders");
    D3D10_DDI_BLEND_DESC blendDesc={0};D3D10DDI_HBLENDSTATE blend={0};D3D10DDI_HRTBLENDSTATE blendRuntime={0};
    blendDesc.RenderTargetWriteMask[0]=D3D10_DDI_COLOR_WRITE_ENABLE_ALL;
    blendDesc.BlendOp=blendDesc.BlendOpAlpha=D3D10_DDI_BLEND_OP_ADD;
    blendDesc.SrcBlend=blendDesc.SrcBlendAlpha=D3D10_DDI_BLEND_ONE;
    blendDesc.DestBlend=blendDesc.DestBlendAlpha=D3D10_DDI_BLEND_ZERO;
    blend.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateBlendStateSize(device,&blendDesc));
    blendRuntime.handle=(VOID *)(UINT_PTR)0xd07u;
    CHECK(blend.pDrvPrivate!=NULL);deviceFunctions.pfnCreateBlendState(device,&blendDesc,blend,blendRuntime);
    D3D10_DDI_RASTERIZER_DESC rasterDesc={0};D3D10DDI_HRASTERIZERSTATE raster={0};D3D10DDI_HRTRASTERIZERSTATE rasterRuntime={0};
    rasterDesc.FillMode=D3D10_DDI_FILL_SOLID;rasterDesc.CullMode=D3D10_DDI_CULL_NONE;rasterDesc.DepthClipEnable=TRUE;
    raster.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateRasterizerStateSize(device,&rasterDesc));
    rasterRuntime.handle=(VOID *)(UINT_PTR)0xd08u;
    CHECK(raster.pDrvPrivate!=NULL);deviceFunctions.pfnCreateRasterizerState(device,&rasterDesc,raster,rasterRuntime);
    D3D10_DDI_DEPTH_STENCIL_DESC depthDesc={0};D3D10DDI_HDEPTHSTENCILSTATE depth={0};D3D10DDI_HRTDEPTHSTENCILSTATE depthRuntime={0};
    depthDesc.DepthFunc=D3D10_DDI_COMPARISON_ALWAYS;
    depth.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateDepthStencilStateSize(device,&depthDesc));
    depthRuntime.handle=(VOID *)(UINT_PTR)0xd09u;
    CHECK(depth.pDrvPrivate!=NULL);deviceFunctions.pfnCreateDepthStencilState(device,&depthDesc,depth,depthRuntime);
    FRONTEND_STAGE("states");
    deviceFunctions.pfnVsSetShader(device,vsh);deviceFunctions.pfnPsSetShader(device,psh);
    unsigned cbBindErrors=FrontendErrors;
    D3D10DDI_HRESOURCE invalidRange[2]={cb,cb};
    deviceFunctions.pfnVsSetConstantBuffers(device,0,2,invalidRange);
    CHECK(FrontendErrors==++cbBindErrors && FrontendLastError==E_NOTIMPL);
    deviceFunctions.pfnGsSetConstantBuffers(device,0,1,&cb);
    CHECK(FrontendErrors==++cbBindErrors && FrontendLastError==E_NOTIMPL);
    deviceFunctions.pfnPsSetConstantBuffers(device,1,1,&cb);
    CHECK(FrontendErrors==++cbBindErrors && FrontendLastError==E_NOTIMPL);
    D3D10DDI_HRESOURCE nullConstantSlot={0};
    deviceFunctions.pfnVsSetConstantBuffers(device,1,1,&nullConstantSlot);
    deviceFunctions.pfnPsSetConstantBuffers(device,0,0,NULL);
    CHECK(FrontendErrors==cbBindErrors);
    deviceFunctions.pfnVsSetConstantBuffers(device,0,1,&cb);
    deviceFunctions.pfnPsSetConstantBuffers(device,0,1,&cb);
    CHECK(FrontendErrors==cbBindErrors);
    D3D10_DDI_BOX invalidConstantBox={0,0,0,16,1,1};
    unsigned invalidUpdateErrors=FrontendErrors;
    deviceFunctions.pfnDefaultConstantBufferUpdateSubresourceUP(
        device,cb,1,NULL,cbValues,0,0);
    CHECK(FrontendErrors==++invalidUpdateErrors && FrontendLastError==E_INVALIDARG);
    deviceFunctions.pfnDefaultConstantBufferUpdateSubresourceUP(
        device,cb,0,&invalidConstantBox,cbValues,0,0);
    CHECK(FrontendErrors==++invalidUpdateErrors && FrontendLastError==E_INVALIDARG);
    deviceFunctions.pfnDefaultConstantBufferUpdateSubresourceUP(
        device,cb,0,NULL,NULL,0,0);
    CHECK(FrontendErrors==++invalidUpdateErrors && FrontendLastError==E_INVALIDARG);
    FRONTEND_STAGE("bind-shaders");
    deviceFunctions.pfnIaSetInputLayout(device,layout);
    UINT stride=16,offset=0;D3D10DDI_HRESOURCE nullBuffer={0};
    deviceFunctions.pfnIaSetVertexBuffers(device,3,1,&vb,&stride,&offset);
    deviceFunctions.pfnIaSetVertexBuffers(device,3,1,&nullBuffer,&stride,&offset);
    deviceFunctions.pfnIaSetVertexBuffers(device,0,1,&vb,&stride,&offset);
    deviceFunctions.pfnIaSetVertexBuffers(device,0,0,NULL,NULL,NULL);
    deviceFunctions.pfnIaSetVertexBuffers(device,0,1,&nullBuffer,&stride,&offset);
    deviceFunctions.pfnIaSetVertexBuffers(device,0,1,&vb,&stride,&offset);
    deviceFunctions.pfnIaSetTopology(device,D3D10_DDI_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    FRONTEND_STAGE("bind-input");
    deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,(D3D10DDI_HDEPTHSTENCILVIEW){0});
    FRONTEND_STAGE("bind-target");
    FLOAT blendFactor[4]={0};deviceFunctions.pfnSetBlendState(device,blend,blendFactor,~0u);
    deviceFunctions.pfnSetRasterizerState(device,raster);deviceFunctions.pfnSetDepthStencilState(device,depth,0);
    FRONTEND_STAGE("bind-fixed-state");
    D3D10_DDI_VIEWPORT viewport={0,0,2560,1600,0,1};deviceFunctions.pfnSetViewports(device,1,0,&viewport);
    D3D10_DDI_RECT rect={0,0,2560,1600};deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
    FRONTEND_STAGE("viewport-scissor");
    FLOAT clear[4]={0.05f,0.05f,0.05f,1.0f};deviceFunctions.pfnClearRenderTargetView(device,rtv,clear);
    FRONTEND_STAGE("bound-clear");
    RuntimeActiveDevice=MesaD3d10FrontendRuntimeForTest(device);
    ADMISSION_UMD_ASAHI_OWNER *frontendOwner=MesaD3d10FrontendOwnerForTest(device);
    FrontendDestroyOwner=frontendOwner;
    RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;
    RuntimeExpectedTargetAllocation=0x775u;
    RuntimeExpectedTargetBytes=0xfa0000ULL;
    RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
    memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
    CHECK(RuntimeActiveDevice && frontendOwner);
    D3D10DDIARG_CREATEQUERY orderedEventDesc={0};
    D3D10DDI_HQUERY orderedEvent={0};D3D10DDI_HRTQUERY orderedEventRuntime={0};
    orderedEventDesc.Query=D3D10DDI_QUERY_EVENT;
    SIZE_T orderedEventBytes=deviceFunctions.pfnCalcPrivateQuerySize(
        device,&orderedEventDesc);
    orderedEvent.pDrvPrivate=calloc(1,orderedEventBytes);
    CHECK(orderedEvent.pDrvPrivate!=NULL);
    unsigned eventErrorsBefore=FrontendErrors;
    deviceFunctions.pfnCreateQuery(device,&orderedEventDesc,
        orderedEvent,orderedEventRuntime);
    CHECK(FrontendErrors==eventErrorsBefore);
    deviceFunctions.pfnQueryBegin(device,orderedEvent);
    CHECK(FrontendErrors==eventErrorsBefore);
    deviceFunctions.pfnDraw(device,3,0);
    CHECK(AgxWin32AsahiContextDrawReceipt(MesaD3d10FrontendContextForTest(device)));
    deviceFunctions.pfnQueryEnd(device,orderedEvent);
    {
      DXGI_DDI_ARG_PRESENT primaryPresent={0};
      primaryPresent.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      primaryPresent.hSurfaceToPresent=(DXGI_DDI_HRESOURCE)(UINT_PTR)
          createdPresentResource.pDrvPrivate;
      primaryPresent.Flags.Value=0x2u;
      primaryPresent.FlipInterval=DXGI_DDI_FLIP_INTERVAL_ONE;
      primaryPresent.pDXGIContext=(PVOID)(UINT_PTR)0x778u;
      FrontendPresentAllocation=0x775u;
      FrontendPresentContext=primaryPresent.pDXGIContext;
      CHECK(dxgiFunctions.pfnPresent(&primaryPresent)==S_OK &&
            FrontendPresentCalls==2u);
    }
    BOOL eventEndSubmitted=FrontendErrors==eventErrorsBefore &&
        RuntimeRenders==1u && RuntimeSignals==2u && RuntimeQueryMarkerCount==1u;
    CHECK(eventEndSubmitted && RuntimeMaterializations==2 &&
          RuntimeConsumerGates==2 && RuntimeMarker && RuntimeQueryMarkers[0]);
    if(eventEndSubmitted) {
      BOOL eventResult=(BOOL)0x5a5a5a5a;
      unsigned pendingErrors=FrontendErrors;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,
          sizeof(eventResult),D3D10_DDI_GET_DATA_DO_NOT_FLUSH);
      CHECK(FrontendErrors==pendingErrors+1u &&
            FrontendLastError==DXGI_DDI_ERR_WASSTILLDRAWING &&
            eventResult==(BOOL)0x5a5a5a5a && RuntimeRenders==1u && RuntimeSignals==2u);
      pendingErrors=FrontendErrors;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,
          sizeof(eventResult),0);
      CHECK(FrontendErrors==pendingErrors+1u &&
            FrontendLastError==DXGI_DDI_ERR_WASSTILLDRAWING &&
            eventResult==(BOOL)0x5a5a5a5a && RuntimeRenders==1u && RuntimeSignals==2u);
      CHECK(SetEvent(RuntimeQueryMarkers[0]));
      unsigned completedErrors=FrontendErrors;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,
          sizeof(eventResult),0);
      CHECK(FrontendErrors==completedErrors && eventResult==TRUE &&
            RuntimeRenders==1u && RuntimeSignals==2u);
      eventResult=FALSE;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,
          sizeof(eventResult),D3D10_DDI_GET_DATA_DO_NOT_FLUSH);
      CHECK(FrontendErrors==completedErrors && eventResult==TRUE &&
            RuntimeRenders==1u && RuntimeSignals==2u);
      RuntimeCheckpoint(frontendOwner,1u);
      void *firstCommand=malloc(sizeof(RuntimeCommand));
      void *firstImages=malloc(sizeof(RuntimeImages));
      void *firstDma=malloc(sizeof(RuntimeDma));
      CHECK(firstCommand && firstImages && firstDma);
      if(firstCommand && firstImages && firstDma) {
        memcpy(firstCommand,RuntimeCommand,sizeof(RuntimeCommand));
        memcpy(firstImages,RuntimeImages,sizeof(RuntimeImages));
        memcpy(firstDma,RuntimeDma,sizeof(RuntimeDma));
      }
      float cbValues2[4]={0.2f,0.8f,0.3f,1.0f};
      unsigned updateErrors=FrontendErrors;
      deviceFunctions.pfnDefaultConstantBufferUpdateSubresourceUP(
          device,cb,0,NULL,cbValues2,1,1);
      CHECK(FrontendErrors==updateErrors &&
            (!firstCommand || !memcmp(firstCommand,RuntimeCommand,sizeof(RuntimeCommand))) &&
            (!firstImages || !memcmp(firstImages,RuntimeImages,sizeof(RuntimeImages))) &&
            (!firstDma || !memcmp(firstDma,RuntimeDma,sizeof(RuntimeDma))));
      free(firstCommand);free(firstImages);free(firstDma);
      RuntimeCheckpoint(frontendOwner,5u);
      RuntimeImmediateMarker=0;
      deviceFunctions.pfnVsSetConstantBuffers(device,0,1,&vsCb);
      unsigned indexedErrors=FrontendErrors;
      deviceFunctions.pfnIaSetIndexBuffer(device,ib,DXGI_FORMAT_R32_UINT,0);
      CHECK(FrontendErrors==++indexedErrors && FrontendLastError==E_NOTIMPL);
      deviceFunctions.pfnIaSetIndexBuffer(device,ib,DXGI_FORMAT_R16_UINT,2);
      CHECK(FrontendErrors==++indexedErrors && FrontendLastError==E_NOTIMPL);
      deviceFunctions.pfnIaSetIndexBuffer(device,ib,DXGI_FORMAT_R16_UINT,0);
      deviceFunctions.pfnDrawIndexed(device,4,0,0);
      CHECK(FrontendErrors==++indexedErrors && FrontendLastError==E_NOTIMPL);
      deviceFunctions.pfnDrawIndexed(device,3,1,0);
      CHECK(FrontendErrors==++indexedErrors && FrontendLastError==E_NOTIMPL);
      deviceFunctions.pfnDrawIndexed(device,3,0,-1);
      CHECK(FrontendErrors==++indexedErrors && FrontendLastError==E_NOTIMPL);
      deviceFunctions.pfnDrawIndexed(device,3,0,0);
      CHECK(AgxWin32AsahiContextDrawReceipt(
          MesaD3d10FrontendContextForTest(device)));
      deviceFunctions.pfnQueryEnd(device,orderedEvent);
      CHECK(FrontendErrors==indexedErrors && RuntimeRenders==1u &&
            RuntimeSignals==2u && RuntimeQueryMarkerCount==1u &&
            RuntimeMarker && RuntimeQueryMarkers[0]);
      CHECK(SetEvent(RuntimeQueryMarkers[0]));
      eventResult=FALSE;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,
          sizeof(eventResult),0);
      CHECK(FrontendErrors==indexedErrors && eventResult==TRUE &&
            RuntimeRenders==1u && RuntimeSignals==2u);
      completedErrors=FrontendErrors;
      unsigned invalidDataErrors=FrontendErrors;
      eventResult=FALSE;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,NULL,sizeof(BOOL),0);
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,1,0);
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,
          sizeof(eventResult),2u);
      CHECK(FrontendErrors==invalidDataErrors+3u &&
            FrontendLastError==E_INVALIDARG && !eventResult);
      unsigned reissueErrors=FrontendErrors;
      deviceFunctions.pfnQueryEnd(device,orderedEvent);
      CHECK(FrontendErrors==reissueErrors && RuntimeRenders==1u &&
            RuntimeSignals==3u && RuntimeQueryMarkerCount==2u);
      deviceFunctions.pfnQueryBegin(device,orderedEvent);
      CHECK(FrontendErrors==reissueErrors);
      deviceFunctions.pfnQueryGetData(device,orderedEvent,NULL,0,
          D3D10_DDI_GET_DATA_DO_NOT_FLUSH);
      CHECK(FrontendErrors==reissueErrors+1u &&
            FrontendLastError==DXGI_DDI_ERR_WASSTILLDRAWING &&
            RuntimeRenders==1u && RuntimeSignals==3u);
      unsigned repeatedEndErrors=FrontendErrors;
      deviceFunctions.pfnQueryEnd(device,orderedEvent);
      CHECK(FrontendErrors==repeatedEndErrors && RuntimeRenders==1u &&
            RuntimeSignals==4u && RuntimeQueryMarkerCount==3u);
      CHECK(SetEvent(RuntimeQueryMarkers[1]));
      deviceFunctions.pfnFlush(device);
      CHECK(RuntimeRenders==1u && RuntimeSignals==4u);
      CHECK(SetEvent(RuntimeQueryMarkers[2]));
      unsigned reissueCompleteErrors=FrontendErrors;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,NULL,0,0);
      CHECK(FrontendErrors==reissueCompleteErrors);
      eventResult=FALSE;
      deviceFunctions.pfnQueryGetData(device,orderedEvent,&eventResult,
          sizeof(eventResult),D3D10_DDI_GET_DATA_DO_NOT_FLUSH);
      CHECK(FrontendErrors==reissueCompleteErrors && eventResult==TRUE &&
            RuntimeRenders==1u && RuntimeSignals==4u);
      D3D10DDI_HQUERY detachedQuery={0};
      detachedQuery.pDrvPrivate=calloc(1,orderedEventBytes);
      CHECK(detachedQuery.pDrvPrivate!=NULL);
      if(detachedQuery.pDrvPrivate) {
        deviceFunctions.pfnCreateQuery(device,&orderedEventDesc,
            detachedQuery,orderedEventRuntime);
        unsigned detachedErrors=FrontendErrors;
        deviceFunctions.pfnQueryEnd(device,detachedQuery);
        CHECK(FrontendErrors==detachedErrors && RuntimeSignals==5u &&
              RuntimeQueryMarkerCount==4u);
        deviceFunctions.pfnDestroyQuery(device,detachedQuery);
        CHECK(FrontendErrors==detachedErrors);
        memset(detachedQuery.pDrvPrivate,0xdd,orderedEventBytes);
        free(detachedQuery.pDrvPrivate);
        CHECK(SetEvent(RuntimeQueryMarkers[3]));
        deviceFunctions.pfnFlush(device);
        CHECK(FrontendErrors==detachedErrors && RuntimeSignals==5u);
      }
      D3D10DDI_HQUERY cyclingQuery={0};
      cyclingQuery.pDrvPrivate=calloc(1,orderedEventBytes);
      CHECK(cyclingQuery.pDrvPrivate!=NULL);
      if(cyclingQuery.pDrvPrivate) {
        deviceFunctions.pfnCreateQuery(device,&orderedEventDesc,
            cyclingQuery,orderedEventRuntime);
        unsigned cycleErrors=FrontendErrors;
        unsigned cycleMarkerBase=RuntimeQueryMarkerCount;
        unsigned cycleSignalBase=RuntimeSignals;
        for(unsigned cycle=0;cycle<70u;++cycle) {
          deviceFunctions.pfnQueryEnd(device,cyclingQuery);
          CHECK(FrontendErrors==cycleErrors &&
                RuntimeQueryMarkerCount==cycleMarkerBase+cycle+1u &&
                RuntimeSignals==cycleSignalBase+cycle+1u &&
                RuntimeQueryMarkers[cycleMarkerBase+cycle]);
          CHECK(SetEvent(RuntimeQueryMarkers[cycleMarkerBase+cycle]));
        }
        deviceFunctions.pfnDestroyQuery(device,cyclingQuery);
        CHECK(FrontendErrors==cycleErrors);
        free(cyclingQuery.pDrvPrivate);
      }
      D3D10DDI_HQUERY failedMarkerQuery={0};
      failedMarkerQuery.pDrvPrivate=calloc(1,orderedEventBytes);
      CHECK(failedMarkerQuery.pDrvPrivate!=NULL);
      if(failedMarkerQuery.pDrvPrivate) {
        deviceFunctions.pfnCreateQuery(device,&orderedEventDesc,
            failedMarkerQuery,orderedEventRuntime);
        unsigned failedMarkerErrors=FrontendErrors;
        unsigned markersBefore=RuntimeQueryMarkerCount,signalsBefore=RuntimeSignals;
        RuntimeFailSignals=1;
        deviceFunctions.pfnQueryEnd(device,failedMarkerQuery);
        CHECK(FrontendErrors==failedMarkerErrors+1u && FrontendLastError==E_FAIL &&
              RuntimeSignals==signalsBefore+1u &&
              RuntimeFailedSignalCalls==1u &&
              RuntimeQueryMarkerCount==markersBefore);
        failedMarkerErrors=FrontendErrors;
        BOOL falseCompletion=FALSE;
        deviceFunctions.pfnQueryGetData(device,failedMarkerQuery,
            &falseCompletion,sizeof(falseCompletion),0);
        CHECK(FrontendErrors==failedMarkerErrors+1u && FrontendLastError==E_FAIL &&
              !falseCompletion && RuntimeQueryMarkerCount==markersBefore);
        failedMarkerErrors=FrontendErrors;
        deviceFunctions.pfnDestroyQuery(device,failedMarkerQuery);
        CHECK(FrontendErrors==failedMarkerErrors);
        free(failedMarkerQuery.pDrvPrivate);
      }
      D3D10DDI_HQUERY devicePendingQuery={0};
      devicePendingQuery.pDrvPrivate=calloc(1,orderedEventBytes);
      CHECK(devicePendingQuery.pDrvPrivate!=NULL);
      if(devicePendingQuery.pDrvPrivate) {
        deviceFunctions.pfnCreateQuery(device,&orderedEventDesc,
            devicePendingQuery,orderedEventRuntime);
        unsigned devicePendingErrors=FrontendErrors;
        unsigned pendingSignalsBefore=RuntimeSignals;
        unsigned pendingMarkersBefore=RuntimeQueryMarkerCount;
        deviceFunctions.pfnQueryEnd(device,devicePendingQuery);
        CHECK(FrontendErrors==devicePendingErrors &&
              RuntimeSignals==pendingSignalsBefore+1u &&
              RuntimeQueryMarkerCount==pendingMarkersBefore+1u);
        pendingDeviceQueryStorage=devicePendingQuery.pDrvPrivate;
        pendingDeviceQueryBytes=orderedEventBytes;
      }
    } else {
      deviceFunctions.pfnFlush(device);
    }
    FRONTEND_STAGE("draw-query-end");
    unsigned destroyEventErrors=FrontendErrors;
    deviceFunctions.pfnDestroyQuery(device,orderedEvent);
    CHECK(FrontendErrors==destroyEventErrors);
    free(orderedEvent.pDrvPrivate);
    D3D10DDI_HQUERY crossDeviceQuery={0};
    crossDeviceQuery.pDrvPrivate=calloc(1,orderedEventBytes);
    CHECK(crossDeviceQuery.pDrvPrivate!=NULL);
    if(crossDeviceQuery.pDrvPrivate) {
      deviceFunctions.pfnCreateQuery(device,&orderedEventDesc,
          crossDeviceQuery,orderedEventRuntime);
      unsigned staleErrors=FrontendErrors;
      ULONG validGeneration=MesaD3d10FrontendEventQuerySetGenerationForTest(
          crossDeviceQuery,0);
      deviceFunctions.pfnQueryBegin(device,crossDeviceQuery);
      CHECK(FrontendErrors==staleErrors+1u && FrontendLastError==E_INVALIDARG);
      CHECK(MesaD3d10FrontendEventQuerySetGenerationForTest(
          crossDeviceQuery,validGeneration)==0u);
      staleErrors=FrontendErrors;
      deviceFunctions.pfnQueryBegin(device,crossDeviceQuery);
      CHECK(FrontendErrors==staleErrors);
      crossDeviceQueryStorage=crossDeviceQuery.pDrvPrivate;
      crossDeviceQueryBytes=orderedEventBytes;
    }
    deviceFunctions.pfnSetRenderTargets(device,NULL,0,1,(D3D10DDI_HDEPTHSTENCILVIEW){0});
    deviceFunctions.pfnIaSetVertexBuffers(device,0,0,NULL,NULL,NULL);
    deviceFunctions.pfnVsSetShader(device,(D3D10DDI_HSHADER){0});deviceFunctions.pfnPsSetShader(device,(D3D10DDI_HSHADER){0});
    D3D10DDI_HRESOURCE nullConstant={0};
    deviceFunctions.pfnIaSetIndexBuffer(device,(D3D10DDI_HRESOURCE){0},
                                        DXGI_FORMAT_UNKNOWN,0);
    deviceFunctions.pfnVsSetConstantBuffers(device,0,1,&nullConstant);
    deviceFunctions.pfnPsSetConstantBuffers(device,0,1,&nullConstant);
    deviceFunctions.pfnDestroyDepthStencilState(device,depth);deviceFunctions.pfnDestroyRasterizerState(device,raster);
    deviceFunctions.pfnDestroyBlendState(device,blend);deviceFunctions.pfnDestroyShader(device,psh);
    deviceFunctions.pfnDestroyShader(device,vsh);deviceFunctions.pfnDestroyElementLayout(device,layout);
    deviceFunctions.pfnDestroyRenderTargetView(device,rtv);deviceFunctions.pfnDestroyResource(device,vb);
    deviceFunctions.pfnDestroyResource(device,presentResource);
    deviceFunctions.pfnDestroyResource(device,createdPresentResource);
    deviceFunctions.pfnDestroyResource(device,vsCb);
    deviceFunctions.pfnDestroyResource(device,cb);
    deviceFunctions.pfnDestroyResource(device,ib);
    deviceFunctions.pfnDestroyResource(device,rt);
    RuntimeExpectedTargetAllocation=0;
    RuntimeExpectedTargetBytes=0;
    free(depth.pDrvPrivate);free(raster.pDrvPrivate);free(blend.pDrvPrivate);free(psh.pDrvPrivate);
    free(vsh.pDrvPrivate);free(layout.pDrvPrivate);free(rtv.pDrvPrivate);free(vb.pDrvPrivate);
    free(vsCb.pDrvPrivate);free(cb.pDrvPrivate);free(ib.pDrvPrivate);
    free(presentResource.pDrvPrivate);free(createdPresentResource.pDrvPrivate);
    free(rt.pDrvPrivate);
#undef FRONTEND_IMM4
#undef FRONTEND_REG
#undef FRONTEND_CB
#undef FRONTEND_OP
#undef FRONTEND_STAGE
  }
  {
    D3D10DDI_HDEVICE secondDevice={0};D3D10DDI_DEVICEFUNCS secondFunctions={0};
    DXGI_DDI_BASE_FUNCTIONS secondDxgi={0};D3D10DDIARG_CREATEDEVICE secondCreate=create;
    secondDevice.pDrvPrivate=calloc(1,bytes);secondCreate.hDrvDevice=secondDevice;
    secondCreate.hRTDevice.handle=(VOID *)(UINT_PTR)0x905u;
    secondCreate.hRTCoreLayer.handle=(VOID *)(UINT_PTR)0xb05u;
    secondCreate.pDeviceFuncs=&secondFunctions;
    secondCreate.DXGIBaseDDI.pDXGIDDIBaseFunctions=&secondDxgi;
    unsigned destroysBefore=BridgeDestroys;
    CHECK(SUCCEEDED(functions.pfnCreateDevice(open.hAdapter,&secondCreate)));
    CHECK(MesaD3d10FrontendContextForTest(secondDevice)!=NULL &&
          MesaD3d10FrontendContextForTest(secondDevice)!=MesaD3d10FrontendContextForTest(device));
    if(crossDeviceQueryStorage) {
      D3D10DDI_HQUERY crossDeviceQuery={crossDeviceQueryStorage};
      BOOL crossResult=(BOOL)0x5a5a5a5a;
      unsigned crossErrors=FrontendErrors;
      secondFunctions.pfnQueryBegin(secondDevice,crossDeviceQuery);
      secondFunctions.pfnQueryEnd(secondDevice,crossDeviceQuery);
      secondFunctions.pfnQueryGetData(secondDevice,crossDeviceQuery,&crossResult,
          sizeof(crossResult),D3D10_DDI_GET_DATA_DO_NOT_FLUSH);
      secondFunctions.pfnDestroyQuery(secondDevice,crossDeviceQuery);
      CHECK(FrontendErrors==crossErrors+4u && FrontendLastError==E_INVALIDARG &&
            crossResult==(BOOL)0x5a5a5a5a);
      crossErrors=FrontendErrors;
      deviceFunctions.pfnDestroyQuery(device,crossDeviceQuery);
      CHECK(FrontendErrors==crossErrors);
      memset(crossDeviceQueryStorage,0xdd,crossDeviceQueryBytes);
      free(crossDeviceQueryStorage);crossDeviceQueryStorage=NULL;
    }
    secondFunctions.pfnDestroyDevice(secondDevice);
    CHECK(MesaD3d10FrontendCleanupResult(secondDevice)==S_OK &&
          BridgeDestroys==destroysBefore+1u);
    free(secondDevice.pDrvPrivate);
    D3D10DDI_HDEVICE failedDevice={0};D3D10DDI_DEVICEFUNCS failedFunctions={0};
    D3D10DDIARG_CREATEDEVICE failedCreate=create;
    failedDevice.pDrvPrivate=calloc(1,bytes);failedCreate.hDrvDevice=failedDevice;
    failedCreate.hRTDevice.handle=(VOID *)(UINT_PTR)0x906u;
    failedCreate.hRTCoreLayer.handle=(VOID *)(UINT_PTR)0xb06u;
    failedCreate.pDeviceFuncs=&failedFunctions;
    PoolFailAllocation=1;
    CHECK(FAILED(functions.pfnCreateDevice(open.hAdapter,&failedCreate)));
    PoolFailAllocation=0;
    CHECK(MesaD3d10FrontendCleanupResult(failedDevice)==E_OUTOFMEMORY);
    free(failedDevice.pDrvPrivate);
    failedDevice.pDrvPrivate=calloc(1,bytes);failedCreate.hDrvDevice=failedDevice;
    failedCreate.hRTDevice.handle=(VOID *)(UINT_PTR)0x90au;
    failedCreate.hRTCoreLayer.handle=(VOID *)(UINT_PTR)0xb0au;
    unsigned errorsBefore=FrontendErrors;
    PoolFailMap=1;PoolFailDeallocation=32;
    CHECK(FAILED(functions.pfnCreateDevice(open.hAdapter,&failedCreate)));
    HRESULT failedCleanup=MesaD3d10FrontendCleanupResult(failedDevice);
    CHECK(failedCleanup==E_FAIL && FrontendErrors==errorsBefore+1u &&
          FrontendLastError==failedCleanup);
    CHECK(MesaD3d10FrontendRuntimeForTest(failedDevice)==NULL &&
          MesaD3d10FrontendOwnerForTest(failedDevice)==NULL &&
          MesaD3d10FrontendContextForTest(failedDevice)==NULL);
    PoolFailMap=0;PoolFailDeallocation=0;
    memset(failedDevice.pDrvPrivate,0xdd,bytes);free(failedDevice.pDrvPrivate);
  }
  unsigned mainDestroyBefore=FrontendDestroyCalls;
  if(deviceFunctions.pfnDestroyDevice) deviceFunctions.pfnDestroyDevice(device);
  HRESULT mainCleanup=MesaD3d10FrontendCleanupResult(device);
  CHECK(mainCleanup==S_OK && FrontendDestroyCalls==mainDestroyBefore+1u &&
        RuntimeRenders==1u && RuntimeSignals==1u+RuntimeQueryMarkerCount+
        RuntimeFailedSignalCalls &&
        RuntimeConsumerRetirements==2u && PoolPresentationDeletes==2u);
  CHECK(MesaD3d10FrontendRuntimeForTest(device)==NULL &&
        MesaD3d10FrontendOwnerForTest(device)==NULL &&
        MesaD3d10FrontendContextForTest(device)==NULL);
  if(pendingDeviceQueryStorage) {
    memset(pendingDeviceQueryStorage,0xdd,pendingDeviceQueryBytes);
    free(pendingDeviceQueryStorage);pendingDeviceQueryStorage=NULL;
  }
  for(unsigned failure=0;failure<4;++failure) {
    D3D10DDI_HDEVICE doomed={0};D3D10DDI_DEVICEFUNCS doomedFunctions={0};
    DXGI_DDI_BASE_FUNCTIONS doomedDxgi={0};D3D10DDIARG_CREATEDEVICE doomedCreate=create;
    UINT_PTR handle=0x907u+failure;
    doomed.pDrvPrivate=calloc(1,bytes);doomedCreate.hDrvDevice=doomed;
    doomedCreate.hRTDevice.handle=(VOID *)handle;
    doomedCreate.hRTCoreLayer.handle=(VOID *)(handle+0x200u);
    doomedCreate.pDeviceFuncs=&doomedFunctions;
    doomedCreate.DXGIBaseDDI.pDXGIDDIBaseFunctions=&doomedDxgi;
    CHECK(SUCCEEDED(functions.pfnCreateDevice(open.hAdapter,&doomedCreate)));
    unsigned errorsBefore=FrontendErrors;
    unsigned destroysBefore=FrontendDestroyCalls;
    if(failure==0) {
      FrontendFailDestroyDevice=handle;
      RuntimeActiveDevice=MesaD3d10FrontendRuntimeForTest(doomed);
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
      memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
      RuntimeFailedSignalCalls=0;RuntimeFailSignals=0;
      D3D10DDIARG_CREATEQUERY pendingDesc={D3D10DDI_QUERY_EVENT,0};
      D3D10DDI_HQUERY pendingQuery={0};D3D10DDI_HRTQUERY pendingRuntime={0};
      SIZE_T pendingBytes=doomedFunctions.pfnCalcPrivateQuerySize(doomed,&pendingDesc);
      pendingQuery.pDrvPrivate=calloc(1,pendingBytes);
      CHECK(pendingQuery.pDrvPrivate!=NULL);
      if(pendingQuery.pDrvPrivate) {
        doomedFunctions.pfnCreateQuery(doomed,&pendingDesc,pendingQuery,pendingRuntime);
        doomedFunctions.pfnQueryEnd(doomed,pendingQuery);
        CHECK(RuntimeQueryMarkerCount==1u && RuntimeQueryMarkers[0]);
        doomedFunctions.pfnDestroyQuery(doomed,pendingQuery);
        memset(pendingQuery.pDrvPrivate,0xdd,pendingBytes);
        free(pendingQuery.pDrvPrivate);
      }
    }
    if(failure==1) PoolFailUnlock=32;
    if(failure==2) PoolFailDeallocation=32;
    if(failure==3) {
      AGX_WIN32_ASAHI_SCENE doomedScene={0};
      struct pipe_context *doomedContext=MesaD3d10FrontendContextForTest(doomed);
      RuntimeActiveDevice=MesaD3d10FrontendRuntimeForTest(doomed);
      ADMISSION_UMD_ASAHI_OWNER *doomedOwner=MesaD3d10FrontendOwnerForTest(doomed);
      RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
      RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;RuntimeFailSignals=1;
      memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
      CHECK(doomedContext && doomedOwner &&
            AgxWin32AsahiSceneInit(&doomedScene,doomedContext->screen,doomedContext) &&
            AgxWin32AsahiSceneDraw(&doomedScene));
      D3D10DDIARG_CREATEQUERY flushDesc={D3D10DDI_QUERY_EVENT,0};
      D3D10DDI_HQUERY flushQuery={0};D3D10DDI_HRTQUERY flushRuntime={0};
      SIZE_T flushBytes=doomedFunctions.pfnCalcPrivateQuerySize(doomed,&flushDesc);
      flushQuery.pDrvPrivate=calloc(1,flushBytes);
      CHECK(flushQuery.pDrvPrivate!=NULL);
      if(flushQuery.pDrvPrivate) {
        doomedFunctions.pfnCreateQuery(doomed,&flushDesc,flushQuery,flushRuntime);
        doomedFunctions.pfnQueryEnd(doomed,flushQuery);
        CHECK(FrontendErrors==errorsBefore+1u && FrontendLastError==E_FAIL &&
              RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeFailedSignalCalls==1u && !RuntimeMarker &&
              RuntimeQueryMarkerCount==0u);
        unsigned flushErrors=FrontendErrors;
        doomedFunctions.pfnDestroyQuery(doomed,flushQuery);
        CHECK(FrontendErrors==flushErrors);
        free(flushQuery.pDrvPrivate);
      }
    }
    doomedFunctions.pfnDestroyDevice(doomed);
    HRESULT cleanup=MesaD3d10FrontendCleanupResult(doomed);
    CHECK(cleanup==E_FAIL && FrontendErrors==errorsBefore+(failure==3?2u:1u) &&
          FrontendLastError==cleanup &&
          FrontendDestroyCalls==destroysBefore+1u);
    CHECK(MesaD3d10FrontendRuntimeForTest(doomed)==NULL &&
          MesaD3d10FrontendOwnerForTest(doomed)==NULL &&
          MesaD3d10FrontendContextForTest(doomed)==NULL);
    FrontendFailDestroyDevice=0;PoolFailUnlock=0;PoolFailDeallocation=0;
    RuntimeFailSignals=0;
    memset(doomed.pDrvPrivate,0xdd,bytes);free(doomed.pDrvPrivate);
  }
  if(mainCleanup==S_OK) {
    FrontendCallbacksInvalid=TRUE;
    memset(device.pDrvPrivate,0xdd,bytes);free(device.pDrvPrivate);device.pDrvPrivate=NULL;
  }
  AGX_D3D10_WINDOWS_TERMINAL_RECEIPT terminal={0};
  CHECK(AgxD3d10WindowsTerminalReceiptForTest(
            MesaD3d10FrontendAdapterForTest(open.hAdapter),&terminal) &&
        terminal.Count==5u && terminal.ActiveBuffers>0u &&
        terminal.NativeContexts>0u && terminal.LiveBos>0u &&
        terminal.QueryMarkers>0u &&
        terminal.Quiesced==4u && terminal.CallbacksCleared);
  CHECK(functions.pfnCloseAdapter(open.hAdapter)==S_OK);
  CHECK(FrontendPostReturnCallbacks==0u);
}
#endif

#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
unsigned AgxKmtNativeBridgeResidencyContractTest(void);
unsigned AgxKmtNativeBridgeCommandDumpContractTest(void);
unsigned AgxKmtNativeQualificationFreshnessContractTest(void);
#endif
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
  AGX_WIN32_SCREEN_BUFFER sourceBuffer;
  AGX_WIN32_SCREEN_BUFFER encoderBuffer;
  void *shaderMap = NULL;
  void *sourceMap = NULL;
  ADMISSION_UMD_SCREEN_SOURCE sourceIdentity;
  ADMISSION_UMD_SCREEN_SOURCE sourceHold;
  ADMISSION_UMD_SCREEN_SOURCE sourceHoldSecond;
  void *encoderMap = NULL;
  HANDLE failedCompletionEvent = NULL;
  HANDLE teardownCompletionEvent = NULL;
  int nativeBoSentinel = 0;

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

  memset(&sourceBuffer, 0, sizeof(sourceBuffer));
  CHECK(AgxWin32ScreenCreateBuffer(
            &deviceState->Screen, AgxWin32BufferClassGeneral,
            sizeof(InternalAllocationData), 0x4000u,
            AppleAgxWin32BufferCpuRead | AppleAgxWin32BufferCpuWrite |
                AppleAgxWin32BufferGpuRead,
            &sourceBuffer) == AgxWin32ScreenSuccess);
  State.ReentryDevice = deviceState;
  State.ReentryToken = sourceBuffer.Transport.Token;
  State.ReentryResult = S_OK;
  CHECK(AgxWin32ScreenMapBuffer(
            &deviceState->Screen, &sourceBuffer, 0x200u, 0x400u,
            AppleAgxWin32BufferCpuRead, &sourceMap) == AgxWin32ScreenSuccess);
  CHECK(FAILED(State.ReentryResult));
  State.ReentryDevice = NULL;
  State.ReentryToken = 0ULL;
  CHECK(sourceMap == InternalAllocationData + 0x200u);
  CHECK(AdmissionUmdScreenAssociateNativeBo(
            deviceState, sourceBuffer.Transport.Token, &nativeBoSentinel,
            77u) == S_OK);
  CHECK(AdmissionUmdScreenAssociateNativeBo(
            deviceState, sourceBuffer.Transport.Token, &nativeBoSentinel,
            77u) != S_OK);
  CHECK(AdmissionUmdScreenQueryNativeBo(deviceState, &nativeBoSentinel, 77u,
                                        &sourceIdentity) == S_OK);
  CHECK(AdmissionUmdScreenQuerySource(deviceState, sourceBuffer.Transport.Token,
                                      &sourceIdentity) == S_OK);
  sourceIdentity.Offset = 0x200u;
  sourceIdentity.Bytes = 0x400u;
  CHECK(AdmissionUmdScreenAcquireSource(deviceState, &sourceIdentity,
                                        &sourceHold) == S_OK);
  CHECK(AdmissionUmdScreenAcquireSource(deviceState, &sourceIdentity,
                                        &sourceHoldSecond) == S_OK);
  CHECK(sourceHold.HoldId != 0ULL &&
        sourceHoldSecond.HoldId != sourceHold.HoldId);
  CHECK(AdmissionUmdScreenDetachNativeBo(
            deviceState, sourceBuffer.Transport.Token, &nativeBoSentinel,
            77u) != S_OK);
  CHECK(sourceHold.Address == InternalAllocationData + 0x200u);
  {
    ULONG undeallocated = 0xffffffffu;
    CHECK(AdmissionUmdScreenFinalize(deviceState, &undeallocated) ==
          HRESULT_FROM_WIN32(ERROR_BUSY));
    CHECK(undeallocated == 0u && deviceState->Magic == ADMISSION_UMD_DEVICE_MAGIC);
  }
  CHECK(AgxWin32ScreenUnmapBuffer(&deviceState->Screen, &sourceBuffer) ==
        AgxWin32ScreenCallback);
  CHECK(AgxWin32ScreenDestroyBuffer(&deviceState->Screen, &sourceBuffer) ==
        AgxWin32ScreenState);
  CHECK(AdmissionUmdScreenReleaseSource(deviceState, &sourceHold) == S_OK);
  CHECK(AdmissionUmdScreenReleaseSource(deviceState, &sourceHold) != S_OK);
  CHECK(AgxWin32ScreenUnmapBuffer(&deviceState->Screen, &sourceBuffer) ==
        AgxWin32ScreenCallback);
  CHECK(AdmissionUmdScreenReleaseSource(deviceState, &sourceHoldSecond) ==
        S_OK);
  CHECK(AdmissionUmdScreenDetachNativeBo(
            deviceState, sourceBuffer.Transport.Token, &nativeBoSentinel,
            77u) == S_OK);
  CHECK(AdmissionUmdScreenQueryNativeBo(deviceState, &nativeBoSentinel, 77u,
                                        &sourceIdentity) != S_OK);
  CHECK(AgxWin32ScreenUnmapBuffer(&deviceState->Screen, &sourceBuffer) ==
        AgxWin32ScreenSuccess);
  CHECK(AgxWin32ScreenDestroyBuffer(&deviceState->Screen, &sourceBuffer) ==
        AgxWin32ScreenSuccess);

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
  CHECK(State.InternalAllocateCalls == 3u);
  CHECK(State.LockCalls == 3u);
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
  CHECK(State.UnlockCalls == 3u);
  CHECK(State.InternalDeallocateCalls == 3u);
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
#if defined(ADMISSION_UMD_PIPE_FACTORY_TEST)
  test_mesa_windows_owners(createDevice);
#endif
#if defined(ADMISSION_UMD_D3D10_FRONTEND_TEST)
  test_mesa_d3d10_frontend_open();
#endif
#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
  State.Failures += AgxKmtNativeBridgeResidencyContractTest();
  State.Failures += AgxKmtNativeBridgeCommandDumpContractTest();
  State.Failures += AgxKmtNativeQualificationFreshnessContractTest();
#endif
  State.Failures += AdmissionUmdDrawComposerTests();
  State.Failures += AdmissionUmdAsahiBatchAdapterTests();
  State.Failures += AdmissionWin32ReferenceContractTests();
#if defined(ADMISSION_UMD_NATIVE_POOL_TEST)
  State.Failures += TestAsahiNativePoolOwner();
#endif
  return State.Failures == 0u ? 0 : (int)State.Failures;
}
