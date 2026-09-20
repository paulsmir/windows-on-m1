#include <windows.h>
#include <wingdi.h>

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)

#include "direct_flip_contract.h"
#include "apple_agx_g13_compute_work.h"

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
EXTERN_C BOOL APIENTRY MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT);
EXTERN_C BOOL AgxD3d10FormatViewCompatible(
    DXGI_FORMAT,DXGI_FORMAT,BOOL);
void AdmissionUmdRuntimeExpectTextureSubresource(int);
EXTERN_C BOOL APIENTRY MesaD3d10FrontendSetSoOffsetForTest(
    D3D10DDI_HDEVICE,D3D10DDI_HRESOURCE,UINT);
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
unsigned AppleAgxG13QueueRuntimeContractTests(void);
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

static ULONGLONG ComputeWorkRead64(const unsigned char *p) {
  ULONGLONG value=0;
  for(unsigned i=0;i<8u;++i)value|=(ULONGLONG)p[i]<<(i*8u);
  return value;
}
static void test_g13_compute_work_contract(void) {
  APPLE_AGX_G13_COMPUTE_WORK_INPUT input={0};
  unsigned char work[APPLE_AGX_G13_COMPUTE_WORK_BYTES];
  input.Counter=7;input.VmSlot=2;input.NotifierGpuAddress=0x1500010000ULL;
  input.PreemptionGpuAddress=0x1500020000ULL;
  input.CdmStreamBase=0x1500030000ULL;input.CdmStreamEnd=0x1500030200ULL;
  input.UscExecutionBase=0x1100000000ULL;
  input.MicrosequenceGpuAddress=0x1500040000ULL;input.MicrosequenceBytes=0x100;
  input.StampGpuAddress=0x1500050000ULL;
  input.FirmwareStampGpuAddress=0x1500051000ULL;
  input.StampValue=9;input.StampSlot=3;input.EventControlIndex=4;
  input.EventSequence=11;input.ClientSequence=5;
  CHECK(AppleAgxG13ComputeWorkBuild(&input,work));
  CHECK(ComputeWorkRead64(work+0x70)==input.PreemptionGpuAddress &&
        ComputeWorkRead64(work+0x78)==input.CdmStreamBase &&
        ComputeWorkRead64(work+0x224)==input.PreemptionGpuAddress &&
        ComputeWorkRead64(work+0x22c)==input.CdmStreamEnd &&
        ComputeWorkRead64(work+0x288)==input.StampGpuAddress &&
        ComputeWorkRead64(work+0x290)==input.FirmwareStampGpuAddress &&
        work[0x2d8]==5u && work[sizeof(work)-1]==0u);
  memset(work,0x5a,sizeof(work));input.CdmStreamEnd=input.CdmStreamBase;
  CHECK(!AppleAgxG13ComputeWorkBuild(&input,work) && work[0]==0x5a);
  {
    APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT micro={0};
    unsigned char sequence[APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES];
    micro.WorkGpuAddress=0x1500100000ULL;
    micro.StatisticsGpuAddress=0x1500200000ULL;
    micro.QueueInfoGpuAddress=0x1500300000ULL;
    micro.NotifierBufferGpuAddress=0x1500400000ULL;
    micro.FirmwareStampGpuAddress=0x1500500000ULL;
    micro.Counter=7;micro.EventSequence=11;micro.EventGeneration=13;
    micro.VmSlot=2;micro.StampValue=9;
    CHECK(AppleAgxG13ComputeMicrosequenceBuild(&micro,sequence));
    CHECK(ComputeWorkRead64(sequence+0x0c)==micro.WorkGpuAddress+0x70 &&
          ComputeWorkRead64(sequence+0x198)==micro.FirmwareStampGpuAddress &&
          ComputeWorkRead64(sequence+0x1dd)==micro.WorkGpuAddress+0x305 &&
          sequence[0x16c]==1u && sequence[0x170]==0x2au &&
          sequence[0x1ec]==0x18u);
  }
}

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
  CHECK(functions.pfnGetCaps(open.hAdapter,&caps)==S_OK && pipeline.Caps==
      D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0));
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

static HRESULT APIENTRY TestSetPriority(
    HANDLE Device,D3DDDICB_SETPRIORITY *Priority) {
  (void)Device;
  return Priority && !Priority->hResource && Priority->NumAllocations==1u &&
      Priority->HandleList && Priority->pPriorities ? S_OK : E_INVALIDARG;
}

static HRESULT APIENTRY TestQueryResidency(
    HANDLE Device,const D3DDDICB_QUERYRESIDENCY *Query) {
  (void)Device;
  if(!Query || Query->hResource || Query->NumAllocations!=1u ||
     !Query->HandleList || !Query->pResidencyStatus) return E_INVALIDARG;
  Query->pResidencyStatus[0]=D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY;
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
    Template.Interface = index == 0u ? D3D10_0_7_DDI_INTERFACE_VERSION :
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
  callbacks.pfnSetPriorityCb = TestSetPriority;
  callbacks.pfnQueryResidencyCb = TestQueryResidency;
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
static unsigned FrontendPriorityCalls,FrontendResidencyCalls;
static D3DKMT_HANDLE FrontendPriorityAllocation;
static UINT FrontendPriorityValue;
static HRESULT FrontendPriorityResult;
static unsigned FrontendResidencyFailAt;
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
static HRESULT APIENTRY FrontendSetPriority(
    HANDLE device,D3DDDICB_SETPRIORITY *priority) {
  CHECK(device==(HANDLE)(UINT_PTR)0x904u && priority &&
        !priority->hResource && priority->NumAllocations==1u &&
        priority->HandleList && priority->pPriorities);
  if(priority && priority->HandleList && priority->pPriorities) {
    FrontendPriorityAllocation=priority->HandleList[0];
    FrontendPriorityValue=priority->pPriorities[0];
  }
  ++FrontendPriorityCalls;return FrontendPriorityResult;
}
static HRESULT APIENTRY FrontendQueryResidency(
    HANDLE device,const D3DDDICB_QUERYRESIDENCY *query) {
  CHECK(device==(HANDLE)(UINT_PTR)0x904u && query && !query->hResource &&
        query->NumAllocations==1u && query->HandleList &&
        query->pResidencyStatus);
  if(!query || !query->HandleList || !query->pResidencyStatus)
    return E_INVALIDARG;
  if(FrontendResidencyFailAt==FrontendResidencyCalls+1u) {
    ++FrontendResidencyCalls;return E_FAIL;
  }
  query->pResidencyStatus[0]=query->HandleList[0]==0x771u ?
      D3DDDI_RESIDENCYSTATUS_RESIDENTINSHAREDMEMORY :
      D3DDDI_RESIDENCYSTATUS_RESIDENTINGPUMEMORY;
  ++FrontendResidencyCalls;return S_OK;
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
  D3D10DDI_HSHADERRESOURCEVIEW appSrv={0};
  D3D10DDI_HSAMPLER appSampler={0};
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
  callbacks.pfnSetPriorityCb=FrontendSetPriority;
  callbacks.pfnQueryResidencyCb=FrontendQueryResidency;
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
    CHECK(MesaD3d10FrontendFormatMappedForTest(
              DXGI_FORMAT_R32G32B32A32_FLOAT) &&
          MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT_R16_UINT) &&
          MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT_B8G8R8A8_UNORM) &&
          MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT_R8G8B8A8_UNORM) &&
          MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT_D32_FLOAT) &&
          MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT_D16_UNORM) &&
          MesaD3d10FrontendFormatMappedForTest(
              DXGI_FORMAT_D24_UNORM_S8_UINT) &&
          MesaD3d10FrontendFormatMappedForTest(
              DXGI_FORMAT_D32_FLOAT_S8X24_UINT) &&
          !MesaD3d10FrontendFormatMappedForTest(DXGI_FORMAT_UNKNOWN));
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_B8G8R8A8_UNORM,&formatCaps);
    CHECK(formatCaps==(D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET|
                       D3D10_DDI_FORMAT_SUPPORT_BLENDABLE));
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_D32_FLOAT,&formatCaps);
    CHECK(formatCaps==0u); /* Depth support is base-assumed by this DDI. */
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_R32G32B32A32_FLOAT,&formatCaps);
    CHECK(formatCaps==(D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET|
                       D3D10_DDI_FORMAT_SUPPORT_BLENDABLE));
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_R8G8B8A8_UNORM,&formatCaps);
    CHECK(formatCaps==(D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET|
                       D3D10_DDI_FORMAT_SUPPORT_BLENDABLE));
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_R16G16B16A16_FLOAT,&formatCaps);
    CHECK(formatCaps==(D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET|
                       D3D10_DDI_FORMAT_SUPPORT_BLENDABLE));
    deviceFunctions.pfnCheckFormatSupport(device,DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,&formatCaps);
    CHECK(formatCaps==D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_B8G8R8A8_UNORM,1,&quality);
    CHECK(quality==1u);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_B8G8R8A8_UNORM,2,&quality);
    CHECK(quality==0u);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_B8G8R8A8_UNORM,4,&quality);
    CHECK(quality==0u);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_R32G32B32A32_FLOAT,1,&quality);
    CHECK(quality==1u);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_R16_UINT,1,&quality);
    CHECK(quality==1u);
    quality=0;
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_R16G16B16A16_FLOAT,1,&quality);
    CHECK(quality==1u);
    const DXGI_FORMAT requiredColorWidths[]={DXGI_FORMAT_R8_UNORM,
        DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,
        DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R11G11B10_FLOAT,
        DXGI_FORMAT_B5G6R5_UNORM,DXGI_FORMAT_A8_UNORM};
    for(unsigned colorIndex=0;colorIndex<ARRAYSIZE(requiredColorWidths);++colorIndex) {
      formatCaps=0;quality=0;
      deviceFunctions.pfnCheckFormatSupport(
          device,requiredColorWidths[colorIndex],&formatCaps);
      CHECK(formatCaps==(D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET|
                         D3D10_DDI_FORMAT_SUPPORT_BLENDABLE));
      deviceFunctions.pfnCheckMultisampleQualityLevels(
          device,requiredColorWidths[colorIndex],1,&quality);
      CHECK(quality==1u);
    }
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_D32_FLOAT,1,&quality);
    CHECK(quality==1u);
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_D32_FLOAT,2,&quality);
    CHECK(quality==0u);
    quality=0;
    deviceFunctions.pfnCheckMultisampleQualityLevels(
        device,DXGI_FORMAT_D32_FLOAT_S8X24_UINT,1,&quality);
    CHECK(quality==1u);
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
      D3D10DDI_HQUERY nullPredicate={0};
      unsigned nullPredicateErrors=FrontendErrors;
      deviceFunctions.pfnSetPredication(device,nullPredicate,FALSE);
      CHECK(FrontendErrors==nullPredicateErrors);
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
    UINT gs[]={
      0x00020040u,0x00000025u,0x05000061u,0x002010f2u,0x00000003u,0x00000000u,
      0x00000001u,0x0100185du,0x0100285cu,0x04000067u,0x001020f2u,0x00000000u,
      0x00000001u,0x0200005eu,0x00000003u,0x06000036u,0x001020f2u,0x00000000u,
      0x00201e46u,0x00000000u,0x00000000u,0x01000013u,0x06000036u,0x001020f2u,
      0x00000000u,0x00201e46u,0x00000001u,0x00000000u,0x01000013u,0x06000036u,
      0x001020f2u,0x00000000u,0x00201e46u,0x00000002u,0x00000000u,0x01000013u,
      0x0100003eu};
    float vertices[12]={-1,-1,0,1,1,-1,0,1,0,1,0,1};
    D3D10DDI_MIPINFO rtMip={0},vbMip={0},soMip={0},cbMip={0},ibMip={0},stagingMip={0};
    D3D10_DDIARG_SUBRESOURCE_UP cbInitial={0},ibInitial={0};
    D3D10DDIARG_CREATERESOURCE rtCreate={0},vbCreate={0},soCreate={0},cbCreate={0},ibCreate={0},stagingCreate={0};
    D3D10DDI_HRESOURCE rt={0},vb={0},soBuffer={0},cb={0},vsCb={0},ib={0},staging={0};
    D3D10DDI_HRTRESOURCE rtRuntime={0},vbRuntime={0},soRuntime={0},cbRuntime={0},vsCbRuntime={0},ibRuntime={0},stagingRuntime={0};
    rtMip.TexelWidth=16;rtMip.TexelHeight=16;rtMip.TexelDepth=1;
    rtCreate.pMipInfoList=&rtMip;rtCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
    rtCreate.Usage=D3D10_DDI_USAGE_DEFAULT;rtCreate.BindFlags=D3D10_DDI_BIND_RENDER_TARGET;
    rtCreate.Format=DXGI_FORMAT_B8G8R8A8_UNORM;rtCreate.SampleDesc.Count=1;
    rtCreate.MipLevels=1;rtCreate.ArraySize=1;
    {
      D3D10DDIARG_CREATERESOURCE rejectedMsaa=rtCreate;
      D3D10DDI_HRESOURCE rejectedHandle={0};
      D3D10DDI_HRTRESOURCE rejectedRuntime={0};
      rejectedMsaa.SampleDesc.Count=2;
      rejectedHandle.pDrvPrivate=calloc(1,
          deviceFunctions.pfnCalcPrivateResourceSize(device,&rejectedMsaa));
      unsigned errorsBefore=FrontendErrors,createsBefore=PoolCreates;
      deviceFunctions.pfnCreateResource(device,&rejectedMsaa,
          rejectedHandle,rejectedRuntime);
      CHECK(FrontendErrors==errorsBefore+1u && FrontendLastError==E_NOTIMPL &&
            PoolCreates==createsBefore);
      deviceFunctions.pfnDestroyResource(device,rejectedHandle);
      free(rejectedHandle.pDrvPrivate);
    }
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
    D3D10DDI_HRESOURCE depthResource={0};
    D3D10DDI_HRTRESOURCE depthRuntimeResource={0};
    D3D10DDI_HDEPTHSTENCILVIEW depthView={0};
    D3D10DDI_HRTDEPTHSTENCILVIEW depthViewRuntime={0};
    {
      D3D10DDI_MIPINFO depthMip={0};
      D3D10DDIARG_CREATERESOURCE depthCreate={0};
      D3D10DDIARG_CREATEDEPTHSTENCILVIEW depthViewCreate={0};
      depthMip.TexelWidth=16;depthMip.TexelHeight=16;depthMip.TexelDepth=1;
      depthCreate.pMipInfoList=&depthMip;
      depthCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
      depthCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
      depthCreate.BindFlags=D3D10_DDI_BIND_DEPTH_STENCIL;
      depthCreate.Format=DXGI_FORMAT_D32_FLOAT;
      depthCreate.SampleDesc.Count=1;depthCreate.MipLevels=1;
      depthCreate.ArraySize=1;
      depthResource.pDrvPrivate=calloc(1,
          deviceFunctions.pfnCalcPrivateResourceSize(device,&depthCreate));
      depthRuntimeResource.handle=(VOID *)(UINT_PTR)0xd0du;
      unsigned depthErrors=FrontendErrors;
      deviceFunctions.pfnCreateResource(device,&depthCreate,depthResource,
          depthRuntimeResource);
      CHECK(depthResource.pDrvPrivate && FrontendErrors==depthErrors);
      depthViewCreate.hDrvResource=depthResource;
      depthViewCreate.Format=DXGI_FORMAT_D32_FLOAT;
      depthViewCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
      depthViewCreate.Tex2D.MipSlice=0;
      depthViewCreate.Tex2D.FirstArraySlice=0;
      depthViewCreate.Tex2D.ArraySize=1;
      SIZE_T depthViewBytes=deviceFunctions.pfnCalcPrivateDepthStencilViewSize(
          device,&depthViewCreate);
      depthView.pDrvPrivate=calloc(1,depthViewBytes);
      depthViewRuntime.handle=(VOID *)(UINT_PTR)0xd0eu;
      deviceFunctions.pfnCreateDepthStencilView(device,&depthViewCreate,
          depthView,depthViewRuntime);
      CHECK(depthView.pDrvPrivate && depthViewBytes &&
            FrontendErrors==depthErrors);
      CHECK(FrontendErrors==depthErrors);
    }
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
    {
      D3D10DDIARG_CREATESHADERRESOURCEVIEW srv={0};
      D3D10DDI_HRTSHADERRESOURCEVIEW runtimeSrv={0};
      D3D10_DDI_SAMPLER_DESC sampler={0};
      D3D10DDI_HRTSAMPLER runtimeSampler={0};
      srv.hDrvResource=presentResource;srv.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
      srv.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
      srv.Tex2D.MostDetailedMip=0;srv.Tex2D.MipLevels=1;
      srv.Tex2D.FirstArraySlice=0;srv.Tex2D.ArraySize=1;
      runtimeSrv.handle=(VOID *)(UINT_PTR)0x77au;
      SIZE_T srvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&srv);
      appSrv.pDrvPrivate=calloc(1,srvBytes);
      sampler.Filter=D3D10_DDI_FILTER_MIN_MAG_MIP_POINT;
      sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D10_DDI_TEXTURE_ADDRESS_CLAMP;
      sampler.ComparisonFunc=D3D10_DDI_COMPARISON_NEVER;
      runtimeSampler.handle=(VOID *)(UINT_PTR)0x77bu;
      SIZE_T samplerBytes=deviceFunctions.pfnCalcPrivateSamplerSize(device,&sampler);
      appSampler.pDrvPrivate=calloc(1,samplerBytes);
      CHECK(appSrv.pDrvPrivate && appSampler.pDrvPrivate && srvBytes && samplerBytes);
      unsigned textureErrors=FrontendErrors;
      deviceFunctions.pfnCreateShaderResourceView(device,&srv,appSrv,runtimeSrv);
      deviceFunctions.pfnCreateSampler(device,&sampler,appSampler,runtimeSampler);
      CHECK(FrontendErrors==textureErrors);
      deviceFunctions.pfnPsSetShaderResources(device,0,1,&appSrv);
      deviceFunctions.pfnPsSetSamplers(device,0,1,&appSampler);
      deviceFunctions.pfnVsSetShaderResources(device,0,1,&appSrv);
      deviceFunctions.pfnVsSetSamplers(device,0,1,&appSampler);
      deviceFunctions.pfnGsSetShaderResources(device,0,1,&appSrv);
      deviceFunctions.pfnGsSetSamplers(device,0,1,&appSampler);
      CHECK(FrontendErrors==textureErrors);
      deviceFunctions.pfnGenMips(device,appSrv);
      CHECK(FrontendErrors==textureErrors);
      deviceFunctions.pfnShaderResourceViewReadAfterWriteHazard(
          device,appSrv,presentResource);
      deviceFunctions.pfnResourceReadAfterWriteHazard(device,presentResource);
      D3D10DDI_COUNTER_INFO counterInfo;
      memset(&counterInfo,0x5a,sizeof(counterInfo));
      deviceFunctions.pfnCheckCounterInfo(device,&counterInfo);
      CHECK(FrontendErrors==textureErrors &&
          counterInfo.LastDeviceDependentCounter==0 &&
          counterInfo.NumSimultaneousCounters==0 &&
          counterInfo.NumDetectableParallelUnits==0);
      unsigned counterErrors=FrontendErrors;
      deviceFunctions.pfnCheckCounter(device,(D3D10DDI_QUERY)0x1234,
          NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
      CHECK(FrontendErrors==counterErrors+1u &&
          FrontendLastError==DXGI_DDI_ERR_UNSUPPORTED);
      textureErrors=FrontendErrors;
      deviceFunctions.pfnRelocateDeviceFuncs(device,&deviceFunctions);
      deviceFunctions.pfnSetTextFilterSize(device,1,1);
      CHECK(FrontendErrors==textureErrors);
      D3D10DDI_HSHADERRESOURCEVIEW nullSrv={0};
      D3D10DDI_HSAMPLER nullSampler={0};
      deviceFunctions.pfnPsSetShaderResources(device,0,1,&nullSrv);
      deviceFunctions.pfnPsSetSamplers(device,0,1,&nullSampler);
      deviceFunctions.pfnVsSetShaderResources(device,0,1,&nullSrv);
      deviceFunctions.pfnVsSetSamplers(device,0,1,&nullSampler);
      deviceFunctions.pfnGsSetShaderResources(device,0,1,&nullSrv);
      deviceFunctions.pfnGsSetSamplers(device,0,1,&nullSampler);
      CHECK(FrontendErrors==textureErrors);
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
    unsupportedPriority.Priority=0x12345678u;
    FrontendPriorityCalls=0;FrontendPriorityAllocation=0;FrontendPriorityValue=0;
    FrontendPriorityResult=S_OK;
    CHECK(dxgiFunctions.pfnSetResourcePriority(&unsupportedPriority)==S_OK &&
          FrontendPriorityCalls==1u && FrontendPriorityAllocation!=0u &&
          FrontendPriorityValue==unsupportedPriority.Priority);
    FrontendPriorityResult=E_FAIL;
    CHECK(dxgiFunctions.pfnSetResourcePriority(&unsupportedPriority)==E_FAIL &&
          FrontendPriorityCalls==2u);
    FrontendPriorityResult=S_OK;
    DXGI_DDI_HRESOURCE residencyResources[2]={dxgiRt,
        (DXGI_DDI_HRESOURCE)(UINT_PTR)presentResource.pDrvPrivate};
    DXGI_DDI_RESIDENCY residency[2]={(DXGI_DDI_RESIDENCY)0x5a,
                                    (DXGI_DDI_RESIDENCY)0x5a};
    DXGI_DDI_ARG_QUERYRESOURCERESIDENCY unsupportedResidency={0};
    unsupportedResidency.hDevice=(UINT_PTR)device.pDrvPrivate;
    unsupportedResidency.pResources=residencyResources;
    unsupportedResidency.pStatus=residency;unsupportedResidency.Resources=2;
    FrontendResidencyCalls=0;FrontendResidencyFailAt=0;
    CHECK(dxgiFunctions.pfnQueryResourceResidency(&unsupportedResidency)==
          AGX_DXGI_STATUS_RESIDENT_IN_SHARED_MEMORY &&
          FrontendResidencyCalls==2u &&
          residency[0]==DXGI_DDI_RESIDENCY_FULLY_RESIDENT &&
          residency[1]==DXGI_DDI_RESIDENCY_RESIDENT_IN_SHARED_MEMORY);
    residency[0]=residency[1]=(DXGI_DDI_RESIDENCY)0x5a;
    FrontendResidencyCalls=0;FrontendResidencyFailAt=2;
    CHECK(dxgiFunctions.pfnQueryResourceResidency(&unsupportedResidency)==E_FAIL &&
          FrontendResidencyCalls==2u &&
          residency[0]==(DXGI_DDI_RESIDENCY)0x5a &&
          residency[1]==(DXGI_DDI_RESIDENCY)0x5a);
    FrontendResidencyFailAt=0;
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
    CHECK(dxgiFunctions.pfnBlt(&unsupportedBlt)==E_INVALIDARG);
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
    vbCreate.pMipInfoList=&vbMip;
    vbCreate.ResourceDimension=D3D10DDIRESOURCE_BUFFER;vbCreate.Usage=D3D10_DDI_USAGE_DYNAMIC;
    vbCreate.BindFlags=D3D10_DDI_BIND_VERTEX_BUFFER;vbCreate.Format=DXGI_FORMAT_UNKNOWN;
    vbCreate.SampleDesc.Count=1;vbCreate.MipLevels=1;vbCreate.ArraySize=1;
    for(unsigned limitCase=0;limitCase<6u;++limitCase) {
      D3D10DDIARG_CREATERESOURCE invalid=vbCreate;
      D3D10DDI_MIPINFO mip=vbMip;
      invalid.pMipInfoList=&mip;invalid.Usage=D3D10_DDI_USAGE_DEFAULT;
      invalid.BindFlags=0;invalid.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
      switch(limitCase) {
      case 0: invalid.ResourceDimension=D3D10DDIRESOURCE_TEXTURE1D;
              mip.TexelWidth=D3D10_REQ_TEXTURE1D_U_DIMENSION+1u;break;
      case 1: invalid.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
              mip.TexelWidth=D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION+1u;break;
      case 2: invalid.ResourceDimension=D3D10DDIRESOURCE_TEXTURE3D;
              mip.TexelWidth=D3D10_REQ_TEXTURE3D_U_V_OR_W_DIMENSION+1u;break;
      case 3: invalid.ResourceDimension=D3D10DDIRESOURCE_TEXTURECUBE;
              mip.TexelWidth=D3D10_REQ_TEXTURECUBE_DIMENSION+1u;break;
      case 4: invalid.ResourceDimension=D3D10DDIRESOURCE_TEXTURE1D;
              mip.TexelWidth=1;invalid.ArraySize=
                D3D10_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION+1u;break;
      default: invalid.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
               mip.TexelWidth=mip.TexelHeight=1;
               invalid.MipLevels=D3D10_REQ_MIP_LEVELS+1u;break;
      }
      SIZE_T limitPrivateBytes=
          deviceFunctions.pfnCalcPrivateResourceSize(device,&invalid);
      D3D10DDI_HRESOURCE handle={0};D3D10DDI_HRTRESOURCE runtime={0};
      handle.pDrvPrivate=malloc(limitPrivateBytes);
      runtime.handle=(VOID *)(UINT_PTR)(0xd20u+limitCase);
      CHECK(handle.pDrvPrivate!=NULL);
      if(handle.pDrvPrivate) {
        memset(handle.pDrvPrivate,0x5a,limitPrivateBytes);
        unsigned errors=FrontendErrors,creates=PoolCreates;
        unsigned renders=RuntimeRenders,signals=RuntimeSignals;
        deviceFunctions.pfnCreateResource(device,&invalid,handle,runtime);
        CHECK(FrontendErrors==errors+1u && FrontendLastError==E_NOTIMPL &&
              PoolCreates==creates && RuntimeRenders==renders &&
              RuntimeSignals==signals);
        if(FrontendErrors==errors)
          deviceFunctions.pfnDestroyResource(device,handle);
        else {
          unsigned char *storage=handle.pDrvPrivate;
          for(SIZE_T i=0;i<limitPrivateBytes;++i) CHECK(storage[i]==0x5a);
        }
        free(handle.pDrvPrivate);
      }
    }
    SIZE_T vbPrivateBytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&vbCreate);
    vb.pDrvPrivate=calloc(1,vbPrivateBytes);
    vbRuntime.handle=(VOID *)(UINT_PTR)0xd02u;
    CHECK(vb.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&vbCreate,vb,vbRuntime);
    soMip.TexelWidth=256;soMip.TexelHeight=soMip.TexelDepth=1;
    soCreate.pMipInfoList=&soMip;
    soCreate.ResourceDimension=D3D10DDIRESOURCE_BUFFER;
    soCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
    soCreate.BindFlags=D3D10_DDI_BIND_VERTEX_BUFFER|D3D10_DDI_BIND_STREAM_OUTPUT;
    soCreate.Format=DXGI_FORMAT_UNKNOWN;soCreate.SampleDesc.Count=1;
    soCreate.MipLevels=1;soCreate.ArraySize=1;
    soBuffer.pDrvPrivate=calloc(1,
        deviceFunctions.pfnCalcPrivateResourceSize(device,&soCreate));
    soRuntime.handle=(VOID *)(UINT_PTR)0xd12u;
    CHECK(soBuffer.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&soCreate,soBuffer,soRuntime);
    D3D10DDI_MAPPED_SUBRESOURCE mappedVb={0};
    unsigned mapErrors=FrontendErrors;
    deviceFunctions.pfnDynamicIABufferMapDiscard(device,vb,0,
        D3D10_DDI_MAP_WRITE_DISCARD,0,&mappedVb);
    CHECK(FrontendErrors==mapErrors && mappedVb.pData!=NULL);
    if(mappedVb.pData) memcpy(mappedVb.pData,vertices,sizeof(vertices));
    deviceFunctions.pfnDynamicIABufferUnmap(device,vb,0);
    CHECK(FrontendErrors==mapErrors);
    memset(&mappedVb,0,sizeof(mappedVb));
    deviceFunctions.pfnDynamicIABufferMapNoOverwrite(device,vb,0,
        D3D10_DDI_MAP_WRITE_NOOVERWRITE,0,&mappedVb);
    CHECK(FrontendErrors==mapErrors && mappedVb.pData!=NULL);
    if(mappedVb.pData) ((float *)mappedVb.pData)[0]=vertices[0];
    deviceFunctions.pfnDynamicIABufferUnmap(device,vb,0);
    CHECK(FrontendErrors==mapErrors);
    memset(&mappedVb,0,sizeof(mappedVb));
    deviceFunctions.pfnDynamicResourceMapDiscard(device,vb,0,
        D3D10_DDI_MAP_WRITE_DISCARD,0,&mappedVb);
    CHECK(FrontendErrors==mapErrors && mappedVb.pData!=NULL);
    if(mappedVb.pData) memcpy(mappedVb.pData,vertices,sizeof(vertices));
    deviceFunctions.pfnDynamicResourceUnmap(device,vb,0);
    CHECK(FrontendErrors==mapErrors);
    memset(&mappedVb,0,sizeof(mappedVb));
    deviceFunctions.pfnResourceMap(device,vb,0,
        D3D10_DDI_MAP_WRITE_NOOVERWRITE,0,&mappedVb);
    CHECK(FrontendErrors==mapErrors && mappedVb.pData!=NULL);
    deviceFunctions.pfnResourceUnmap(device,vb,0);
    CHECK(FrontendErrors==mapErrors);
    stagingMip.TexelWidth=sizeof(vertices);
    stagingMip.TexelHeight=stagingMip.TexelDepth=1;
    stagingCreate.pMipInfoList=&stagingMip;
    stagingCreate.ResourceDimension=D3D10DDIRESOURCE_BUFFER;
    stagingCreate.Usage=D3D10_DDI_USAGE_STAGING;
    stagingCreate.Format=DXGI_FORMAT_UNKNOWN;
    stagingCreate.SampleDesc.Count=1;stagingCreate.MipLevels=1;
    stagingCreate.ArraySize=1;
    staging.pDrvPrivate=calloc(1,
        deviceFunctions.pfnCalcPrivateResourceSize(device,&stagingCreate));
    stagingRuntime.handle=(VOID *)(UINT_PTR)0xd0cu;
    CHECK(staging.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&stagingCreate,staging,stagingRuntime);
    CHECK(FrontendErrors==mapErrors);
    D3D10DDI_MAPPED_SUBRESOURCE mappedStaging={0};
    deviceFunctions.pfnStagingResourceMap(device,staging,0,
        D3D10_DDI_MAP_WRITE,0,&mappedStaging);
    CHECK(FrontendErrors==mapErrors && mappedStaging.pData!=NULL);
    if(mappedStaging.pData) memcpy(mappedStaging.pData,vertices,sizeof(vertices));
    deviceFunctions.pfnStagingResourceUnmap(device,staging,0);
    CHECK(FrontendErrors==mapErrors &&
          !deviceFunctions.pfnResourceIsStagingBusy(device,staging));
    memset(&mappedStaging,0,sizeof(mappedStaging));
    deviceFunctions.pfnStagingResourceMap(device,staging,0,
        D3D10_DDI_MAP_READ,0,&mappedStaging);
    CHECK(FrontendErrors==mapErrors && mappedStaging.pData &&
          !memcmp(mappedStaging.pData,vertices,sizeof(vertices)));
    deviceFunctions.pfnStagingResourceUnmap(device,staging,0);
    CHECK(FrontendErrors==mapErrors);
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
    cbCreate.Usage=D3D10_DDI_USAGE_DYNAMIC;
    cbCreate.pInitialDataUP=NULL;
    vsCb.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateResourceSize(device,&cbCreate));
    vsCbRuntime.handle=(VOID *)(UINT_PTR)0xd0bu;
    CHECK(vsCb.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateResource(device,&cbCreate,vsCb,vsCbRuntime);
    CHECK(FrontendErrors==cbErrorsBefore);
    D3D10DDI_MAPPED_SUBRESOURCE mappedCb={0};
    deviceFunctions.pfnDynamicConstantBufferMapDiscard(device,vsCb,0,
        D3D10_DDI_MAP_WRITE_DISCARD,0,&mappedCb);
    CHECK(FrontendErrors==cbErrorsBefore && mappedCb.pData!=NULL);
    if(mappedCb.pData) memcpy(mappedCb.pData,vsCbValues,sizeof(vsCbValues));
    deviceFunctions.pfnDynamicConstantBufferUnmap(device,vsCb,0);
    CHECK(FrontendErrors==cbErrorsBefore);
    D3D10DDI_HRESOURCE nullCb={0};
    deviceFunctions.pfnGsSetConstantBuffers(device,0,1,&vsCb);
    deviceFunctions.pfnGsSetConstantBuffers(device,0,1,&nullCb);
    CHECK(FrontendErrors==cbErrorsBefore);
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
    {
      unsigned soErrors=FrontendErrors;
      deviceFunctions.pfnSoSetTargets(device,0,0,NULL,NULL);
      CHECK(FrontendErrors==soErrors);
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
    D3D10DDIARG_CREATERENDERTARGETVIEW depthColorRtvCreate={0};
    D3D10DDI_HRENDERTARGETVIEW depthColorRtv={0};
    D3D10DDI_HRTRENDERTARGETVIEW depthColorRtvRuntime={0};
    depthColorRtvCreate.hDrvResource=rt;
    depthColorRtvCreate.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
    depthColorRtvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
    depthColorRtvCreate.Tex2D.ArraySize=1;
    depthColorRtv.pDrvPrivate=calloc(1,
        deviceFunctions.pfnCalcPrivateRenderTargetViewSize(
            device,&depthColorRtvCreate));
    depthColorRtvRuntime.handle=(VOID *)(UINT_PTR)0xd0fu;
    CHECK(depthColorRtv.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateRenderTargetView(device,&depthColorRtvCreate,
        depthColorRtv,depthColorRtvRuntime);
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
    D3D10DDI_HSHADER vsh={0},psh={0},gsh={0};D3D10DDI_HRTSHADER vsRuntime={0},psRuntime={0},gsRuntime={0};
    vsh.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateShaderSize(device,vs,NULL));
    psh.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateShaderSize(device,ps,NULL));
    gsh.pDrvPrivate=calloc(1,deviceFunctions.pfnCalcPrivateShaderSize(device,gs,NULL));
    vsRuntime.handle=(VOID *)(UINT_PTR)0xd05u;psRuntime.handle=(VOID *)(UINT_PTR)0xd06u;
    gsRuntime.handle=(VOID *)(UINT_PTR)0xd10u;
    CHECK(vsh.pDrvPrivate && psh.pDrvPrivate && gsh.pDrvPrivate);
    deviceFunctions.pfnCreateVertexShader(device,vs,vsh,vsRuntime,NULL);
    deviceFunctions.pfnCreatePixelShader(device,ps,psh,psRuntime,NULL);
    deviceFunctions.pfnCreateGeometryShader(device,gs,gsh,gsRuntime,NULL);
    D3D10DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY soDecl={0};
    soDecl.OutputSlot=0;soDecl.RegisterIndex=0;soDecl.RegisterMask=0xf;
    D3D10DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT soGsCreate={0};
    soGsCreate.pShaderCode=gs;soGsCreate.pOutputStreamDecl=&soDecl;
    soGsCreate.NumEntries=1;soGsCreate.StreamOutputStrideInBytes=16;
    D3D10DDI_HSHADER soGsh={0};D3D10DDI_HRTSHADER soGsRuntime={0};
    soGsh.pDrvPrivate=calloc(1,
        deviceFunctions.pfnCalcPrivateGeometryShaderWithStreamOutput(
          device,&soGsCreate,NULL));
    soGsRuntime.handle=(VOID *)(UINT_PTR)0xd11u;
    CHECK(soGsh.pDrvPrivate!=NULL);
    deviceFunctions.pfnCreateGeometryShaderWithStreamOutput(
        device,&soGsCreate,soGsh,soGsRuntime,NULL);
    CHECK(MesaD3d10FrontendShaderValidForTest(vsh) &&
          MesaD3d10FrontendShaderValidForTest(psh) &&
          MesaD3d10FrontendShaderValidForTest(gsh) &&
          MesaD3d10FrontendShaderValidForTest(soGsh));
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
    CHECK(FrontendErrors==cbBindErrors);
    D3D10DDI_HRESOURCE nullGsConstant={0};
    deviceFunctions.pfnGsSetConstantBuffers(device,0,1,&nullGsConstant);
    CHECK(FrontendErrors==cbBindErrors);
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
    CHECK(!AgxWin32AsahiContextFaulted(MesaD3d10FrontendContextForTest(device)));
    FRONTEND_STAGE("bind-target");
    FLOAT blendFactor[4]={0};deviceFunctions.pfnSetBlendState(device,blend,blendFactor,~0u);
    deviceFunctions.pfnSetRasterizerState(device,raster);deviceFunctions.pfnSetDepthStencilState(device,depth,0);
    CHECK(!AgxWin32AsahiContextFaulted(MesaD3d10FrontendContextForTest(device)));
    FRONTEND_STAGE("bind-fixed-state");
    D3D10_DDI_VIEWPORT viewport={0,0,2560,1600,0,1};deviceFunctions.pfnSetViewports(device,1,0,&viewport);
    D3D10_DDI_RECT rect={0,0,2560,1600};deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
    CHECK(!AgxWin32AsahiContextFaulted(MesaD3d10FrontendContextForTest(device)));
    FRONTEND_STAGE("viewport-scissor");
    FLOAT clear[4]={0.05f,0.05f,0.05f,1.0f};
    {
      unsigned depthErrors=FrontendErrors;
      RuntimeActiveDevice=MesaD3d10FrontendRuntimeForTest(device);
      ADMISSION_UMD_ASAHI_OWNER *depthOwner=MesaD3d10FrontendOwnerForTest(device);
      RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
      memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
      RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;
      RuntimeExpectedTargetAllocation=0;RuntimeExpectedTargetBytes=0;
      RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH;
      RuntimeConsumerGates=RuntimeConsumerRetirements=0;
      RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
      CHECK(RuntimeActiveDevice && depthOwner);
      deviceFunctions.pfnSetRenderTargets(device,&depthColorRtv,1,0,depthView);
      D3D10_DDI_VIEWPORT depthViewport={0,0,16,16,0,1};
      D3D10_DDI_RECT depthRect={0,0,16,16};
      deviceFunctions.pfnSetViewports(device,1,0,&depthViewport);
      deviceFunctions.pfnSetScissorRects(device,1,0,&depthRect);
      deviceFunctions.pfnClearDepthStencilView(device,depthView,
          D3D10_DDI_CLEAR_DEPTH,0.5f,0);
      CHECK(FrontendErrors==depthErrors && !AgxWin32AsahiContextFaulted(
          MesaD3d10FrontendContextForTest(device)));
      deviceFunctions.pfnDraw(device,3,0);
      CHECK(FrontendErrors==depthErrors && AgxWin32AsahiContextDrawReceipt(
          MesaD3d10FrontendContextForTest(device)));
      deviceFunctions.pfnFlush(device);
      CHECK(FrontendErrors==depthErrors && RuntimeRenders==1u &&
            RuntimeSignals==1u && RuntimeMaterializations==2u &&
            RuntimeConsumerGates==2u && RuntimeMarker!=NULL);
      RuntimeCheckpoint(depthOwner,1u);
      CHECK(AgxWin32AsahiContextRetire(
          MesaD3d10FrontendContextForTest(device),0u));
      RuntimeCheckpoint(depthOwner,5u);
      RuntimeExpectedCommandVersion=0;
      deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,
          (D3D10DDI_HDEPTHSTENCILVIEW){0});
      deviceFunctions.pfnSetViewports(device,1,0,&viewport);
      deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
      {
        D3D10DDI_MIPINFO d16Mip={0};
        D3D10DDIARG_CREATERESOURCE d16Create={0};
        D3D10DDI_HRESOURCE d16={0};D3D10DDI_HRTRESOURCE d16Runtime={0};
        D3D10DDIARG_CREATEDEPTHSTENCILVIEW d16ViewCreate={0};
        D3D10DDI_HDEPTHSTENCILVIEW d16View={0};
        D3D10DDI_HRTDEPTHSTENCILVIEW d16ViewRuntime={0};
        d16Mip.TexelWidth=d16Mip.TexelHeight=16;d16Mip.TexelDepth=1;
        d16Create.pMipInfoList=&d16Mip;
        d16Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        d16Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        d16Create.BindFlags=D3D10_DDI_BIND_DEPTH_STENCIL;
        d16Create.Format=DXGI_FORMAT_D16_UNORM;
        d16Create.SampleDesc.Count=1;d16Create.MipLevels=1;
        d16Create.ArraySize=1;
        SIZE_T d16Bytes=deviceFunctions.pfnCalcPrivateResourceSize(
            device,&d16Create);
        d16.pDrvPrivate=calloc(1,d16Bytes);
        d16Runtime.handle=(VOID *)(UINT_PTR)0xd32u;
        CHECK(d16.pDrvPrivate!=NULL);
        unsigned d16Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&d16Create,d16,d16Runtime);
        CHECK(FrontendErrors==d16Errors);
        d16ViewCreate.hDrvResource=d16;
        d16ViewCreate.Format=DXGI_FORMAT_D16_UNORM;
        d16ViewCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        d16ViewCreate.Tex2D.MipSlice=0;
        d16ViewCreate.Tex2D.FirstArraySlice=0;
        d16ViewCreate.Tex2D.ArraySize=1;
        SIZE_T d16ViewBytes=deviceFunctions.pfnCalcPrivateDepthStencilViewSize(
            device,&d16ViewCreate);
        d16View.pDrvPrivate=calloc(1,d16ViewBytes);
        d16ViewRuntime.handle=(VOID *)(UINT_PTR)0xd33u;
        CHECK(d16View.pDrvPrivate!=NULL);
        deviceFunctions.pfnCreateDepthStencilView(
            device,&d16ViewCreate,d16View,d16ViewRuntime);
        CHECK(FrontendErrors==d16Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
        RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
        memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;
        RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH;
        D3D10_DDI_VIEWPORT d16Viewport={0,0,16,16,0,1};
        D3D10_DDI_RECT d16Rect={0,0,16,16};
        deviceFunctions.pfnSetRenderTargets(
            device,&depthColorRtv,1,0,d16View);
        deviceFunctions.pfnSetViewports(device,1,0,&d16Viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&d16Rect);
        deviceFunctions.pfnClearDepthStencilView(
            device,d16View,D3D10_DDI_CLEAR_DEPTH,0.25f,0);
        CHECK(FrontendErrors==d16Errors);
        deviceFunctions.pfnDraw(device,3,0);
        CHECK(FrontendErrors==d16Errors && AgxWin32AsahiContextDrawReceipt(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnFlush(device);
        CHECK(RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u &&
              RuntimeMarker!=NULL);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(
              MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyDepthStencilView(device,d16View);
        deviceFunctions.pfnDestroyResource(device,d16);
        free(d16View.pDrvPrivate);free(d16.pDrvPrivate);
        deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        deviceFunctions.pfnSetViewports(device,1,0,&viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
      }
      {
        D3D10DDI_MIPINFO dsMip={0};D3D10DDIARG_CREATERESOURCE dsCreate={0};
        D3D10DDI_HRESOURCE ds={0};D3D10DDI_HRTRESOURCE dsRuntime={0};
        D3D10DDIARG_CREATEDEPTHSTENCILVIEW dsViewCreate={0};
        D3D10DDI_HDEPTHSTENCILVIEW dsView={0};
        D3D10DDI_HRTDEPTHSTENCILVIEW dsViewRuntime={0};
        dsMip.TexelWidth=dsMip.TexelHeight=16;dsMip.TexelDepth=1;
        dsCreate.pMipInfoList=&dsMip;
        dsCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        dsCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
        dsCreate.BindFlags=D3D10_DDI_BIND_DEPTH_STENCIL;
        dsCreate.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsCreate.SampleDesc.Count=1;dsCreate.MipLevels=1;dsCreate.ArraySize=1;
        SIZE_T dsBytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&dsCreate);
        ds.pDrvPrivate=calloc(1,dsBytes);dsRuntime.handle=(VOID *)(UINT_PTR)0xd34u;
        CHECK(ds.pDrvPrivate!=NULL);
        unsigned dsErrors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&dsCreate,ds,dsRuntime);
        CHECK(FrontendErrors==dsErrors);
        dsViewCreate.hDrvResource=ds;dsViewCreate.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsViewCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        dsViewCreate.Tex2D.MipSlice=0;dsViewCreate.Tex2D.FirstArraySlice=0;
        dsViewCreate.Tex2D.ArraySize=1;
        SIZE_T dsViewBytes=deviceFunctions.pfnCalcPrivateDepthStencilViewSize(
            device,&dsViewCreate);
        dsView.pDrvPrivate=calloc(1,dsViewBytes);
        dsViewRuntime.handle=(VOID *)(UINT_PTR)0xd35u;
        CHECK(dsView.pDrvPrivate!=NULL);
        deviceFunctions.pfnCreateDepthStencilView(
            device,&dsViewCreate,dsView,dsViewRuntime);
        CHECK(FrontendErrors==dsErrors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
        RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
        memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;
        RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH;
        D3D10_DDI_VIEWPORT dsViewport={0,0,16,16,0,1};
        D3D10_DDI_RECT dsRect={0,0,16,16};
        deviceFunctions.pfnSetRenderTargets(device,&depthColorRtv,1,0,dsView);
        deviceFunctions.pfnSetViewports(device,1,0,&dsViewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&dsRect);
        deviceFunctions.pfnClearDepthStencilView(device,dsView,
            D3D10_DDI_CLEAR_DEPTH|D3D10_DDI_CLEAR_STENCIL,0.75f,0x5au);
        CHECK(FrontendErrors==dsErrors && !AgxWin32AsahiContextFaulted(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnDraw(device,3,0);
        CHECK(FrontendErrors==dsErrors && AgxWin32AsahiContextDrawReceipt(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnFlush(device);
        CHECK(RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u &&
              RuntimeMarker!=NULL);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(
              MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyDepthStencilView(device,dsView);
        deviceFunctions.pfnDestroyResource(device,ds);
        free(dsView.pDrvPrivate);free(ds.pDrvPrivate);
        deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        deviceFunctions.pfnSetViewports(device,1,0,&viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
      }
      {
        D3D10DDI_MIPINFO d32s8Mip={0};D3D10DDIARG_CREATERESOURCE d32s8Create={0};
        D3D10DDI_HRESOURCE d32s8={0};D3D10DDI_HRTRESOURCE d32s8Runtime={0};
        D3D10DDIARG_CREATEDEPTHSTENCILVIEW d32s8ViewCreate={0};
        D3D10DDI_HDEPTHSTENCILVIEW d32s8View={0};
        D3D10DDI_HRTDEPTHSTENCILVIEW d32s8ViewRuntime={0};
        d32s8Mip.TexelWidth=d32s8Mip.TexelHeight=16;d32s8Mip.TexelDepth=1;
        d32s8Create.pMipInfoList=&d32s8Mip;
        d32s8Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        d32s8Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        d32s8Create.BindFlags=D3D10_DDI_BIND_DEPTH_STENCIL;
        d32s8Create.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        d32s8Create.SampleDesc.Count=1;d32s8Create.MipLevels=1;d32s8Create.ArraySize=1;
        SIZE_T d32s8Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&d32s8Create);
        d32s8.pDrvPrivate=calloc(1,d32s8Bytes);d32s8Runtime.handle=(VOID *)(UINT_PTR)0xd34u;
        CHECK(d32s8.pDrvPrivate!=NULL);
        unsigned d32s8Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&d32s8Create,d32s8,d32s8Runtime);
        CHECK(FrontendErrors==d32s8Errors);
        d32s8ViewCreate.hDrvResource=d32s8;d32s8ViewCreate.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        d32s8ViewCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        d32s8ViewCreate.Tex2D.MipSlice=0;d32s8ViewCreate.Tex2D.FirstArraySlice=0;
        d32s8ViewCreate.Tex2D.ArraySize=1;
        SIZE_T d32s8ViewBytes=deviceFunctions.pfnCalcPrivateDepthStencilViewSize(
            device,&d32s8ViewCreate);
        d32s8View.pDrvPrivate=calloc(1,d32s8ViewBytes);
        d32s8ViewRuntime.handle=(VOID *)(UINT_PTR)0xd35u;
        CHECK(d32s8View.pDrvPrivate!=NULL);
        deviceFunctions.pfnCreateDepthStencilView(
            device,&d32s8ViewCreate,d32s8View,d32s8ViewRuntime);
        CHECK(FrontendErrors==d32s8Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
        RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
        memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;
        RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH;
        D3D10_DDI_VIEWPORT d32s8Viewport={0,0,16,16,0,1};
        D3D10_DDI_RECT d32s8Rect={0,0,16,16};
        deviceFunctions.pfnSetRenderTargets(device,&depthColorRtv,1,0,d32s8View);
        deviceFunctions.pfnSetViewports(device,1,0,&d32s8Viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&d32s8Rect);
        deviceFunctions.pfnClearDepthStencilView(device,d32s8View,
            D3D10_DDI_CLEAR_DEPTH|D3D10_DDI_CLEAR_STENCIL,0.75f,0x5au);
        CHECK(FrontendErrors==d32s8Errors && !AgxWin32AsahiContextFaulted(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnDraw(device,3,0);
        CHECK(FrontendErrors==d32s8Errors && AgxWin32AsahiContextDrawReceipt(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnFlush(device);
        CHECK(RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u &&
              RuntimeMarker!=NULL);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(
              MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyDepthStencilView(device,d32s8View);
        deviceFunctions.pfnDestroyResource(device,d32s8);
        free(d32s8View.pDrvPrivate);free(d32s8.pDrvPrivate);
        deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        deviceFunctions.pfnSetViewports(device,1,0,&viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
      }
      {
        D3D10DDI_MIPINFO rgbaMip={0};
        D3D10DDIARG_CREATERESOURCE rgbaCreate={0};
        D3D10DDI_HRESOURCE rgba={0};D3D10DDI_HRTRESOURCE rgbaRuntime={0};
        D3D10DDIARG_CREATERENDERTARGETVIEW rgbaViewCreate={0};
        D3D10DDI_HRENDERTARGETVIEW rgbaView={0};
        D3D10DDI_HRTRENDERTARGETVIEW rgbaViewRuntime={0};
        rgbaMip.TexelWidth=rgbaMip.TexelHeight=16;rgbaMip.TexelDepth=1;
        rgbaCreate.pMipInfoList=&rgbaMip;
        rgbaCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        rgbaCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
        rgbaCreate.BindFlags=D3D10_DDI_BIND_RENDER_TARGET|
            D3D10_DDI_BIND_SHADER_RESOURCE;
        rgbaCreate.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
        rgbaCreate.SampleDesc.Count=1;rgbaCreate.MipLevels=1;
        rgbaCreate.ArraySize=1;
        SIZE_T rgbaBytes=deviceFunctions.pfnCalcPrivateResourceSize(
            device,&rgbaCreate);
        rgba.pDrvPrivate=calloc(1,rgbaBytes);
        rgbaRuntime.handle=(VOID *)(UINT_PTR)0xd30u;
        CHECK(rgba.pDrvPrivate!=NULL);
        unsigned rgbaErrors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&rgbaCreate,rgba,rgbaRuntime);
        CHECK(FrontendErrors==rgbaErrors);
        rgbaViewCreate.hDrvResource=rgba;
        rgbaViewCreate.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        rgbaViewCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        rgbaViewCreate.Tex2D.MipSlice=0;
        rgbaViewCreate.Tex2D.FirstArraySlice=0;
        rgbaViewCreate.Tex2D.ArraySize=1;
        SIZE_T rgbaViewBytes=deviceFunctions.pfnCalcPrivateRenderTargetViewSize(
            device,&rgbaViewCreate);
        rgbaView.pDrvPrivate=calloc(1,rgbaViewBytes);
        rgbaViewRuntime.handle=(VOID *)(UINT_PTR)0xd31u;
        CHECK(rgbaView.pDrvPrivate!=NULL);
        deviceFunctions.pfnCreateRenderTargetView(
            device,&rgbaViewCreate,rgbaView,rgbaViewRuntime);
        CHECK(FrontendErrors==rgbaErrors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
        RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
        memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;
        RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
        D3D10_DDI_VIEWPORT rgbaViewport={0,0,16,16,0,1};
        D3D10_DDI_RECT rgbaRect={0,0,16,16};
        deviceFunctions.pfnSetRenderTargets(device,&rgbaView,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        CHECK(FrontendErrors==rgbaErrors);
        deviceFunctions.pfnSetViewports(device,1,0,&rgbaViewport);
        CHECK(FrontendErrors==rgbaErrors);
        deviceFunctions.pfnSetScissorRects(device,1,0,&rgbaRect);
        CHECK(FrontendErrors==rgbaErrors);
        deviceFunctions.pfnClearRenderTargetView(device,rgbaView,clear);
        CHECK(FrontendErrors==rgbaErrors);
        deviceFunctions.pfnDraw(device,3,0);
        CHECK(FrontendErrors==rgbaErrors && AgxWin32AsahiContextDrawReceipt(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnFlush(device);
        CHECK(RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u &&
              RuntimeMarker!=NULL);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(
              MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        CHECK(AgxD3d10FormatViewCompatible(
            DXGI_FORMAT_R8G8B8A8_TYPELESS,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,FALSE));
        CHECK(!AgxD3d10FormatViewCompatible(
            DXGI_FORMAT_R8G8B8A8_TYPELESS,
            DXGI_FORMAT_R16G16_FLOAT,FALSE));
        D3D10DDIARG_CREATESHADERRESOURCEVIEW rgbaSrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW rgbaSrv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW rgbaSrvRuntime={0};
        rgbaSrvCreate.hDrvResource=rgba;
        rgbaSrvCreate.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        rgbaSrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        rgbaSrvCreate.Tex2D.MostDetailedMip=0;rgbaSrvCreate.Tex2D.MipLevels=1;
        rgbaSrvCreate.Tex2D.FirstArraySlice=0;rgbaSrvCreate.Tex2D.ArraySize=1;
        SIZE_T rgbaSrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(
            device,&rgbaSrvCreate);
        rgbaSrv.pDrvPrivate=calloc(1,rgbaSrvBytes);
        rgbaSrvRuntime.handle=(VOID *)(UINT_PTR)0xd62u;
        CHECK(rgbaSrv.pDrvPrivate && rgbaSrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(
            device,&rgbaSrvCreate,rgbaSrv,rgbaSrvRuntime);
        CHECK(FrontendErrors==rgbaErrors);
        deviceFunctions.pfnPsSetShaderResources(device,0,1,&rgbaSrv);
        CHECK(FrontendErrors==rgbaErrors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
        RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
        memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;
        RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,rgba);
        CHECK(FrontendErrors==rgbaErrors && RuntimeRenders==1u &&
              RuntimeSignals==1u && RuntimeMaterializations==2u &&
              RuntimeConsumerGates==2u && RuntimeMarker!=NULL);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(
              MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        D3D10DDI_HSHADERRESOURCEVIEW nullRgbaSrv={0};
        deviceFunctions.pfnPsSetShaderResources(device,0,1,&nullRgbaSrv);
        CHECK(FrontendErrors==rgbaErrors);
        deviceFunctions.pfnDestroyShaderResourceView(device,rgbaSrv);
        free(rgbaSrv.pDrvPrivate);
        deviceFunctions.pfnDestroyRenderTargetView(device,rgbaView);
        deviceFunctions.pfnDestroyResource(device,rgba);
        free(rgbaView.pDrvPrivate);free(rgba.pDrvPrivate);
        deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        deviceFunctions.pfnSetViewports(device,1,0,&viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
      }
      {
        static unsigned char bc1Data[32]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO bc1Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP bc1Upload={0};
        D3D10DDIARG_CREATERESOURCE bc1Create={0};
        D3D10DDI_HRESOURCE bc1={0};D3D10DDI_HRTRESOURCE bc1Runtime={0};
        bc1Mip.TexelWidth=7;bc1Mip.TexelHeight=5;bc1Mip.TexelDepth=1;
        bc1Upload.pSysMem=bc1Data;bc1Upload.SysMemPitch=16;
        bc1Upload.SysMemSlicePitch=32;
        bc1Create.pMipInfoList=&bc1Mip;bc1Create.pInitialDataUP=&bc1Upload;
        bc1Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        bc1Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        bc1Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        bc1Create.Format=DXGI_FORMAT_BC1_TYPELESS;
        bc1Create.SampleDesc.Count=1;bc1Create.MipLevels=1;bc1Create.ArraySize=1;
        SIZE_T bc1Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&bc1Create);
        bc1.pDrvPrivate=calloc(1,bc1Bytes);bc1Runtime.handle=(VOID *)(UINT_PTR)0xd70u;
        CHECK(bc1.pDrvPrivate && bc1Bytes);
        unsigned bc1Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&bc1Create,bc1,bc1Runtime);
        CHECK(FrontendErrors==bc1Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW bc1SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW bc1Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW bc1SrvRuntime={0};
        bc1SrvCreate.hDrvResource=bc1;bc1SrvCreate.Format=DXGI_FORMAT_BC1_UNORM_SRGB;
        bc1SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        bc1SrvCreate.Tex2D.MostDetailedMip=0;bc1SrvCreate.Tex2D.MipLevels=1;
        bc1SrvCreate.Tex2D.FirstArraySlice=0;bc1SrvCreate.Tex2D.ArraySize=1;
        SIZE_T bc1SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&bc1SrvCreate);
        bc1Srv.pDrvPrivate=calloc(1,bc1SrvBytes);bc1SrvRuntime.handle=(VOID *)(UINT_PTR)0xd71u;
        CHECK(bc1Srv.pDrvPrivate && bc1SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&bc1SrvCreate,bc1Srv,bc1SrvRuntime);
        CHECK(FrontendErrors==bc1Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,bc1);
        CHECK(FrontendErrors==bc1Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,bc1Srv);
        deviceFunctions.pfnDestroyResource(device,bc1);
        free(bc1Srv.pDrvPrivate);free(bc1.pDrvPrivate);
      }
      {
        static unsigned char bc3Data[64]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO bc3Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP bc3Upload={0};
        D3D10DDIARG_CREATERESOURCE bc3Create={0};
        D3D10DDI_HRESOURCE bc3={0};D3D10DDI_HRTRESOURCE bc3Runtime={0};
        bc3Mip.TexelWidth=7;bc3Mip.TexelHeight=5;bc3Mip.TexelDepth=1;
        bc3Upload.pSysMem=bc3Data;bc3Upload.SysMemPitch=32;
        bc3Upload.SysMemSlicePitch=64;
        bc3Create.pMipInfoList=&bc3Mip;bc3Create.pInitialDataUP=&bc3Upload;
        bc3Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        bc3Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        bc3Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        bc3Create.Format=DXGI_FORMAT_BC3_TYPELESS;
        bc3Create.SampleDesc.Count=1;bc3Create.MipLevels=1;bc3Create.ArraySize=1;
        SIZE_T bc3Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&bc3Create);
        bc3.pDrvPrivate=calloc(1,bc3Bytes);bc3Runtime.handle=(VOID *)(UINT_PTR)0xd72u;
        CHECK(bc3.pDrvPrivate && bc3Bytes);
        unsigned bc3Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&bc3Create,bc3,bc3Runtime);
        CHECK(FrontendErrors==bc3Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW bc3SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW bc3Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW bc3SrvRuntime={0};
        bc3SrvCreate.hDrvResource=bc3;bc3SrvCreate.Format=DXGI_FORMAT_BC3_UNORM_SRGB;
        bc3SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        bc3SrvCreate.Tex2D.MostDetailedMip=0;bc3SrvCreate.Tex2D.MipLevels=1;
        bc3SrvCreate.Tex2D.FirstArraySlice=0;bc3SrvCreate.Tex2D.ArraySize=1;
        SIZE_T bc3SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&bc3SrvCreate);
        bc3Srv.pDrvPrivate=calloc(1,bc3SrvBytes);bc3SrvRuntime.handle=(VOID *)(UINT_PTR)0xd73u;
        CHECK(bc3Srv.pDrvPrivate && bc3SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&bc3SrvCreate,bc3Srv,bc3SrvRuntime);
        CHECK(FrontendErrors==bc3Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,bc3);
        CHECK(FrontendErrors==bc3Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,bc3Srv);
        deviceFunctions.pfnDestroyResource(device,bc3);
        free(bc3Srv.pDrvPrivate);free(bc3.pDrvPrivate);
      }
      {
        static unsigned char bc5Data[64]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO bc5Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP bc5Upload={0};
        D3D10DDIARG_CREATERESOURCE bc5Create={0};
        D3D10DDI_HRESOURCE bc5={0};D3D10DDI_HRTRESOURCE bc5Runtime={0};
        bc5Mip.TexelWidth=7;bc5Mip.TexelHeight=5;bc5Mip.TexelDepth=1;
        bc5Upload.pSysMem=bc5Data;bc5Upload.SysMemPitch=32;
        bc5Upload.SysMemSlicePitch=64;
        bc5Create.pMipInfoList=&bc5Mip;bc5Create.pInitialDataUP=&bc5Upload;
        bc5Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        bc5Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        bc5Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        bc5Create.Format=DXGI_FORMAT_BC5_TYPELESS;
        bc5Create.SampleDesc.Count=1;bc5Create.MipLevels=1;bc5Create.ArraySize=1;
        SIZE_T bc5Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&bc5Create);
        bc5.pDrvPrivate=calloc(1,bc5Bytes);bc5Runtime.handle=(VOID *)(UINT_PTR)0xd74u;
        CHECK(bc5.pDrvPrivate && bc5Bytes);
        unsigned bc5Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&bc5Create,bc5,bc5Runtime);
        CHECK(FrontendErrors==bc5Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW bc5SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW bc5Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW bc5SrvRuntime={0};
        bc5SrvCreate.hDrvResource=bc5;bc5SrvCreate.Format=DXGI_FORMAT_BC5_SNORM;
        bc5SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        bc5SrvCreate.Tex2D.MostDetailedMip=0;bc5SrvCreate.Tex2D.MipLevels=1;
        bc5SrvCreate.Tex2D.FirstArraySlice=0;bc5SrvCreate.Tex2D.ArraySize=1;
        SIZE_T bc5SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&bc5SrvCreate);
        bc5Srv.pDrvPrivate=calloc(1,bc5SrvBytes);bc5SrvRuntime.handle=(VOID *)(UINT_PTR)0xd75u;
        CHECK(bc5Srv.pDrvPrivate && bc5SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&bc5SrvCreate,bc5Srv,bc5SrvRuntime);
        CHECK(FrontendErrors==bc5Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,bc5);
        CHECK(FrontendErrors==bc5Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,bc5Srv);
        deviceFunctions.pfnDestroyResource(device,bc5);
        free(bc5Srv.pDrvPrivate);free(bc5.pDrvPrivate);
      }
      {
        static unsigned char r9Data[140]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO r9Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP r9Upload={0};
        D3D10DDIARG_CREATERESOURCE r9Create={0};
        D3D10DDI_HRESOURCE r9={0};D3D10DDI_HRTRESOURCE r9Runtime={0};
        r9Mip.TexelWidth=7;r9Mip.TexelHeight=5;r9Mip.TexelDepth=1;
        r9Upload.pSysMem=r9Data;r9Upload.SysMemPitch=28;
        r9Upload.SysMemSlicePitch=140;
        r9Create.pMipInfoList=&r9Mip;r9Create.pInitialDataUP=&r9Upload;
        r9Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        r9Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        r9Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        r9Create.Format=DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        r9Create.SampleDesc.Count=1;r9Create.MipLevels=1;r9Create.ArraySize=1;
        SIZE_T r9Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&r9Create);
        r9.pDrvPrivate=calloc(1,r9Bytes);r9Runtime.handle=(VOID *)(UINT_PTR)0xd76u;
        CHECK(r9.pDrvPrivate && r9Bytes);
        unsigned r9Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&r9Create,r9,r9Runtime);
        CHECK(FrontendErrors==r9Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW r9SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW r9Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW r9SrvRuntime={0};
        r9SrvCreate.hDrvResource=r9;r9SrvCreate.Format=DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        r9SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        r9SrvCreate.Tex2D.MostDetailedMip=0;r9SrvCreate.Tex2D.MipLevels=1;
        r9SrvCreate.Tex2D.FirstArraySlice=0;r9SrvCreate.Tex2D.ArraySize=1;
        SIZE_T r9SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&r9SrvCreate);
        r9Srv.pDrvPrivate=calloc(1,r9SrvBytes);r9SrvRuntime.handle=(VOID *)(UINT_PTR)0xd77u;
        CHECK(r9Srv.pDrvPrivate && r9SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&r9SrvCreate,r9Srv,r9SrvRuntime);
        CHECK(FrontendErrors==r9Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,r9);
        CHECK(FrontendErrors==r9Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,r9Srv);
        deviceFunctions.pfnDestroyResource(device,r9);
        free(r9Srv.pDrvPrivate);free(r9.pDrvPrivate);
      }
      {
        static unsigned char pair0Data[80]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO pair0Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP pair0Upload={0};
        D3D10DDIARG_CREATERESOURCE pair0Create={0};
        D3D10DDI_HRESOURCE pair0={0};D3D10DDI_HRTRESOURCE pair0Runtime={0};
        pair0Mip.TexelWidth=8;pair0Mip.TexelHeight=5;pair0Mip.TexelDepth=1;
        pair0Upload.pSysMem=pair0Data;pair0Upload.SysMemPitch=16;
        pair0Upload.SysMemSlicePitch=80;
        pair0Create.pMipInfoList=&pair0Mip;pair0Create.pInitialDataUP=&pair0Upload;
        pair0Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        pair0Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        pair0Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        pair0Create.Format=DXGI_FORMAT_R8G8_B8G8_UNORM;
        pair0Create.SampleDesc.Count=1;pair0Create.MipLevels=1;pair0Create.ArraySize=1;
        SIZE_T pair0Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&pair0Create);
        pair0.pDrvPrivate=calloc(1,pair0Bytes);pair0Runtime.handle=(VOID *)(UINT_PTR)0xd78u;
        CHECK(pair0.pDrvPrivate && pair0Bytes);
        unsigned pair0Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&pair0Create,pair0,pair0Runtime);
        CHECK(FrontendErrors==pair0Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW pair0SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW pair0Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW pair0SrvRuntime={0};
        pair0SrvCreate.hDrvResource=pair0;pair0SrvCreate.Format=DXGI_FORMAT_R8G8_B8G8_UNORM;
        pair0SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        pair0SrvCreate.Tex2D.MostDetailedMip=0;pair0SrvCreate.Tex2D.MipLevels=1;
        pair0SrvCreate.Tex2D.FirstArraySlice=0;pair0SrvCreate.Tex2D.ArraySize=1;
        SIZE_T pair0SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&pair0SrvCreate);
        pair0Srv.pDrvPrivate=calloc(1,pair0SrvBytes);pair0SrvRuntime.handle=(VOID *)(UINT_PTR)0xd79u;
        CHECK(pair0Srv.pDrvPrivate && pair0SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&pair0SrvCreate,pair0Srv,pair0SrvRuntime);
        CHECK(FrontendErrors==pair0Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,pair0);
        CHECK(FrontendErrors==pair0Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,pair0Srv);
        deviceFunctions.pfnDestroyResource(device,pair0);
        free(pair0Srv.pDrvPrivate);free(pair0.pDrvPrivate);
      }
      {
        static unsigned char pair1Data[80]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO pair1Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP pair1Upload={0};
        D3D10DDIARG_CREATERESOURCE pair1Create={0};
        D3D10DDI_HRESOURCE pair1={0};D3D10DDI_HRTRESOURCE pair1Runtime={0};
        pair1Mip.TexelWidth=8;pair1Mip.TexelHeight=5;pair1Mip.TexelDepth=1;
        pair1Upload.pSysMem=pair1Data;pair1Upload.SysMemPitch=16;
        pair1Upload.SysMemSlicePitch=80;
        pair1Create.pMipInfoList=&pair1Mip;pair1Create.pInitialDataUP=&pair1Upload;
        pair1Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        pair1Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        pair1Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        pair1Create.Format=DXGI_FORMAT_G8R8_G8B8_UNORM;
        pair1Create.SampleDesc.Count=1;pair1Create.MipLevels=1;pair1Create.ArraySize=1;
        SIZE_T pair1Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&pair1Create);
        pair1.pDrvPrivate=calloc(1,pair1Bytes);pair1Runtime.handle=(VOID *)(UINT_PTR)0xd7au;
        CHECK(pair1.pDrvPrivate && pair1Bytes);
        unsigned pair1Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&pair1Create,pair1,pair1Runtime);
        CHECK(FrontendErrors==pair1Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW pair1SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW pair1Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW pair1SrvRuntime={0};
        pair1SrvCreate.hDrvResource=pair1;pair1SrvCreate.Format=DXGI_FORMAT_G8R8_G8B8_UNORM;
        pair1SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        pair1SrvCreate.Tex2D.MostDetailedMip=0;pair1SrvCreate.Tex2D.MipLevels=1;
        pair1SrvCreate.Tex2D.FirstArraySlice=0;pair1SrvCreate.Tex2D.ArraySize=1;
        SIZE_T pair1SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&pair1SrvCreate);
        pair1Srv.pDrvPrivate=calloc(1,pair1SrvBytes);pair1SrvRuntime.handle=(VOID *)(UINT_PTR)0xd7bu;
        CHECK(pair1Srv.pDrvPrivate && pair1SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&pair1SrvCreate,pair1Srv,pair1SrvRuntime);
        CHECK(FrontendErrors==pair1Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,pair1);
        CHECK(FrontendErrors==pair1Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,pair1Srv);
        deviceFunctions.pfnDestroyResource(device,pair1);
        free(pair1Srv.pDrvPrivate);free(pair1.pDrvPrivate);
      }
      {
        static unsigned char b5551Data[70]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO b5551Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP b5551Upload={0};
        D3D10DDIARG_CREATERESOURCE b5551Create={0};
        D3D10DDI_HRESOURCE b5551={0};D3D10DDI_HRTRESOURCE b5551Runtime={0};
        b5551Mip.TexelWidth=7;b5551Mip.TexelHeight=5;b5551Mip.TexelDepth=1;
        b5551Upload.pSysMem=b5551Data;b5551Upload.SysMemPitch=14;
        b5551Upload.SysMemSlicePitch=70;
        b5551Create.pMipInfoList=&b5551Mip;b5551Create.pInitialDataUP=&b5551Upload;
        b5551Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        b5551Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        b5551Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        b5551Create.Format=DXGI_FORMAT_B5G5R5A1_UNORM;
        b5551Create.SampleDesc.Count=1;b5551Create.MipLevels=1;b5551Create.ArraySize=1;
        SIZE_T b5551Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&b5551Create);
        b5551.pDrvPrivate=calloc(1,b5551Bytes);b5551Runtime.handle=(VOID *)(UINT_PTR)0xd7cu;
        CHECK(b5551.pDrvPrivate && b5551Bytes);
        unsigned b5551Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&b5551Create,b5551,b5551Runtime);
        CHECK(FrontendErrors==b5551Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW b5551SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW b5551Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW b5551SrvRuntime={0};
        b5551SrvCreate.hDrvResource=b5551;b5551SrvCreate.Format=DXGI_FORMAT_B5G5R5A1_UNORM;
        b5551SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        b5551SrvCreate.Tex2D.MostDetailedMip=0;b5551SrvCreate.Tex2D.MipLevels=1;
        b5551SrvCreate.Tex2D.FirstArraySlice=0;b5551SrvCreate.Tex2D.ArraySize=1;
        SIZE_T b5551SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&b5551SrvCreate);
        b5551Srv.pDrvPrivate=calloc(1,b5551SrvBytes);b5551SrvRuntime.handle=(VOID *)(UINT_PTR)0xd7du;
        CHECK(b5551Srv.pDrvPrivate && b5551SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&b5551SrvCreate,b5551Srv,b5551SrvRuntime);
        CHECK(FrontendErrors==b5551Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,b5551);
        CHECK(FrontendErrors==b5551Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,b5551Srv);
        deviceFunctions.pfnDestroyResource(device,b5551);
        free(b5551Srv.pDrvPrivate);free(b5551.pDrvPrivate);
      }
      {
        static unsigned char b4444Data[70]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO b4444Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP b4444Upload={0};
        D3D10DDIARG_CREATERESOURCE b4444Create={0};
        D3D10DDI_HRESOURCE b4444={0};D3D10DDI_HRTRESOURCE b4444Runtime={0};
        b4444Mip.TexelWidth=7;b4444Mip.TexelHeight=5;b4444Mip.TexelDepth=1;
        b4444Upload.pSysMem=b4444Data;b4444Upload.SysMemPitch=14;
        b4444Upload.SysMemSlicePitch=70;
        b4444Create.pMipInfoList=&b4444Mip;b4444Create.pInitialDataUP=&b4444Upload;
        b4444Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        b4444Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        b4444Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        b4444Create.Format=DXGI_FORMAT_B4G4R4A4_UNORM;
        b4444Create.SampleDesc.Count=1;b4444Create.MipLevels=1;b4444Create.ArraySize=1;
        SIZE_T b4444Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&b4444Create);
        b4444.pDrvPrivate=calloc(1,b4444Bytes);b4444Runtime.handle=(VOID *)(UINT_PTR)0xd7eu;
        CHECK(b4444.pDrvPrivate && b4444Bytes);
        unsigned b4444Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&b4444Create,b4444,b4444Runtime);
        CHECK(FrontendErrors==b4444Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW b4444SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW b4444Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW b4444SrvRuntime={0};
        b4444SrvCreate.hDrvResource=b4444;b4444SrvCreate.Format=DXGI_FORMAT_B4G4R4A4_UNORM;
        b4444SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        b4444SrvCreate.Tex2D.MostDetailedMip=0;b4444SrvCreate.Tex2D.MipLevels=1;
        b4444SrvCreate.Tex2D.FirstArraySlice=0;b4444SrvCreate.Tex2D.ArraySize=1;
        SIZE_T b4444SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&b4444SrvCreate);
        b4444Srv.pDrvPrivate=calloc(1,b4444SrvBytes);b4444SrvRuntime.handle=(VOID *)(UINT_PTR)0xd7fu;
        CHECK(b4444Srv.pDrvPrivate && b4444SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&b4444SrvCreate,b4444Srv,b4444SrvRuntime);
        CHECK(FrontendErrors==b4444Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,b4444);
        CHECK(FrontendErrors==b4444Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,b4444Srv);
        deviceFunctions.pfnDestroyResource(device,b4444);
        free(b4444Srv.pDrvPrivate);free(b4444.pDrvPrivate);
      }
      {
        static unsigned char mipPixels[4][256];
        D3D10DDI_MIPINFO mipInfo[2]={{8,8,1,0},{4,4,1,0}};
        D3D10_DDIARG_SUBRESOURCE_UP mipUpload[4]={0};
        for(unsigned sub=0;sub<4;++sub) {
          unsigned level=sub&1u;
          mipUpload[sub].pSysMem=mipPixels[sub];
          mipUpload[sub].SysMemPitch=level?16u:32u;
          mipUpload[sub].SysMemSlicePitch=level?64u:256u;
        }
        D3D10DDIARG_CREATERESOURCE mipCreate={0};
        D3D10DDI_HRESOURCE mipResource={0};D3D10DDI_HRTRESOURCE mipRuntime={0};
        mipCreate.pMipInfoList=mipInfo;mipCreate.pInitialDataUP=mipUpload;
        mipCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        mipCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
        mipCreate.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        mipCreate.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;
        mipCreate.SampleDesc.Count=1;mipCreate.MipLevels=2;mipCreate.ArraySize=2;
        SIZE_T mipBytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&mipCreate);
        mipResource.pDrvPrivate=calloc(1,mipBytes);mipRuntime.handle=(VOID *)(UINT_PTR)0xd80u;
        CHECK(mipResource.pDrvPrivate && mipBytes);
        unsigned mipErrors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&mipCreate,mipResource,mipRuntime);
        CHECK(FrontendErrors==mipErrors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW mipSrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW mipSrv={0};D3D10DDI_HRTSHADERRESOURCEVIEW mipSrvRuntime={0};
        mipSrvCreate.hDrvResource=mipResource;mipSrvCreate.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        mipSrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        mipSrvCreate.Tex2D.MostDetailedMip=1;mipSrvCreate.Tex2D.MipLevels=1;
        mipSrvCreate.Tex2D.FirstArraySlice=1;mipSrvCreate.Tex2D.ArraySize=1;
        SIZE_T mipSrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&mipSrvCreate);
        mipSrv.pDrvPrivate=calloc(1,mipSrvBytes);mipSrvRuntime.handle=(VOID *)(UINT_PTR)0xd81u;
        CHECK(mipSrv.pDrvPrivate && mipSrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&mipSrvCreate,mipSrv,mipSrvRuntime);
        CHECK(FrontendErrors==mipErrors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        AdmissionUmdRuntimeExpectTextureSubresource(1);
        deviceFunctions.pfnResourceCopy(device,rt,mipResource);
        AdmissionUmdRuntimeExpectTextureSubresource(0);
        CHECK(FrontendErrors==mipErrors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,mipSrv);
        deviceFunctions.pfnDestroyResource(device,mipResource);
        free(mipSrv.pDrvPrivate);free(mipResource.pDrvPrivate);
      }
      {
        static unsigned char a8Data[35]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO a8Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP a8Upload={0};
        D3D10DDIARG_CREATERESOURCE a8Create={0};
        D3D10DDI_HRESOURCE a8={0};D3D10DDI_HRTRESOURCE a8Runtime={0};
        a8Mip.TexelWidth=7;a8Mip.TexelHeight=5;a8Mip.TexelDepth=1;
        a8Upload.pSysMem=a8Data;a8Upload.SysMemPitch=7;
        a8Upload.SysMemSlicePitch=35;
        a8Create.pMipInfoList=&a8Mip;a8Create.pInitialDataUP=&a8Upload;
        a8Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        a8Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        a8Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        a8Create.Format=DXGI_FORMAT_A8_UNORM;
        a8Create.SampleDesc.Count=1;a8Create.MipLevels=1;a8Create.ArraySize=1;
        SIZE_T a8Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&a8Create);
        a8.pDrvPrivate=calloc(1,a8Bytes);a8Runtime.handle=(VOID *)(UINT_PTR)0xd82u;
        CHECK(a8.pDrvPrivate && a8Bytes);
        unsigned a8Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&a8Create,a8,a8Runtime);
        CHECK(FrontendErrors==a8Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW a8SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW a8Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW a8SrvRuntime={0};
        a8SrvCreate.hDrvResource=a8;a8SrvCreate.Format=DXGI_FORMAT_A8_UNORM;
        a8SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        a8SrvCreate.Tex2D.MostDetailedMip=0;a8SrvCreate.Tex2D.MipLevels=1;
        a8SrvCreate.Tex2D.FirstArraySlice=0;a8SrvCreate.Tex2D.ArraySize=1;
        SIZE_T a8SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&a8SrvCreate);
        a8Srv.pDrvPrivate=calloc(1,a8SrvBytes);a8SrvRuntime.handle=(VOID *)(UINT_PTR)0xd83u;
        CHECK(a8Srv.pDrvPrivate && a8SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&a8SrvCreate,a8Srv,a8SrvRuntime);
        CHECK(FrontendErrors==a8Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,a8);
        CHECK(FrontendErrors==a8Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,a8Srv);
        deviceFunctions.pfnDestroyResource(device,a8);
        free(a8Srv.pDrvPrivate);free(a8.pDrvPrivate);
      }
      {
        static unsigned char rgb32Data[420]={
          0xff,0xff,0,0,0,0,0,0, 0,0,0xff,0xff,0,0,0,0,
          0,0,0xff,0xff,0,0,0,0, 0xff,0xff,0,0,0,0,0,0};
        D3D10DDI_MIPINFO rgb32Mip={0};
        D3D10_DDIARG_SUBRESOURCE_UP rgb32Upload={0};
        D3D10DDIARG_CREATERESOURCE rgb32Create={0};
        D3D10DDI_HRESOURCE rgb32={0};D3D10DDI_HRTRESOURCE rgb32Runtime={0};
        rgb32Mip.TexelWidth=7;rgb32Mip.TexelHeight=5;rgb32Mip.TexelDepth=1;
        rgb32Upload.pSysMem=rgb32Data;rgb32Upload.SysMemPitch=84;
        rgb32Upload.SysMemSlicePitch=420;
        rgb32Create.pMipInfoList=&rgb32Mip;rgb32Create.pInitialDataUP=&rgb32Upload;
        rgb32Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        rgb32Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        rgb32Create.BindFlags=D3D10_DDI_BIND_SHADER_RESOURCE;
        rgb32Create.Format=DXGI_FORMAT_R32G32B32_FLOAT;
        rgb32Create.SampleDesc.Count=1;rgb32Create.MipLevels=1;rgb32Create.ArraySize=1;
        SIZE_T rgb32Bytes=deviceFunctions.pfnCalcPrivateResourceSize(device,&rgb32Create);
        rgb32.pDrvPrivate=calloc(1,rgb32Bytes);rgb32Runtime.handle=(VOID *)(UINT_PTR)0xd84u;
        CHECK(rgb32.pDrvPrivate && rgb32Bytes);
        unsigned rgb32Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&rgb32Create,rgb32,rgb32Runtime);
        CHECK(FrontendErrors==rgb32Errors);
        D3D10DDIARG_CREATESHADERRESOURCEVIEW rgb32SrvCreate={0};
        D3D10DDI_HSHADERRESOURCEVIEW rgb32Srv={0};
        D3D10DDI_HRTSHADERRESOURCEVIEW rgb32SrvRuntime={0};
        rgb32SrvCreate.hDrvResource=rgb32;rgb32SrvCreate.Format=DXGI_FORMAT_R32G32B32_FLOAT;
        rgb32SrvCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        rgb32SrvCreate.Tex2D.MostDetailedMip=0;rgb32SrvCreate.Tex2D.MipLevels=1;
        rgb32SrvCreate.Tex2D.FirstArraySlice=0;rgb32SrvCreate.Tex2D.ArraySize=1;
        SIZE_T rgb32SrvBytes=deviceFunctions.pfnCalcPrivateShaderResourceViewSize(device,&rgb32SrvCreate);
        rgb32Srv.pDrvPrivate=calloc(1,rgb32SrvBytes);rgb32SrvRuntime.handle=(VOID *)(UINT_PTR)0xd85u;
        CHECK(rgb32Srv.pDrvPrivate && rgb32SrvBytes);
        deviceFunctions.pfnCreateShaderResourceView(device,&rgb32SrvCreate,rgb32Srv,rgb32SrvRuntime);
        CHECK(FrontendErrors==rgb32Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;
        RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
        memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH;
        deviceFunctions.pfnResourceCopy(device,rt,rgb32);
        CHECK(FrontendErrors==rgb32Errors && RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u && RuntimeMarker);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyShaderResourceView(device,rgb32Srv);
        deviceFunctions.pfnDestroyResource(device,rgb32);
        free(rgb32Srv.pDrvPrivate);free(rgb32.pDrvPrivate);
      }
      {
        D3D10DDI_MIPINFO fp16Mip={0};
        D3D10DDIARG_CREATERESOURCE fp16Create={0};
        D3D10DDI_HRESOURCE fp16={0};D3D10DDI_HRTRESOURCE fp16Runtime={0};
        D3D10DDIARG_CREATERENDERTARGETVIEW fp16ViewCreate={0};
        D3D10DDI_HRENDERTARGETVIEW fp16View={0};
        D3D10DDI_HRTRENDERTARGETVIEW fp16ViewRuntime={0};
        fp16Mip.TexelWidth=fp16Mip.TexelHeight=16;fp16Mip.TexelDepth=1;
        fp16Create.pMipInfoList=&fp16Mip;
        fp16Create.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        fp16Create.Usage=D3D10_DDI_USAGE_DEFAULT;
        fp16Create.BindFlags=D3D10_DDI_BIND_RENDER_TARGET;
        fp16Create.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        fp16Create.SampleDesc.Count=1;fp16Create.MipLevels=1;
        fp16Create.ArraySize=1;
        SIZE_T fp16Bytes=deviceFunctions.pfnCalcPrivateResourceSize(
            device,&fp16Create);
        fp16.pDrvPrivate=calloc(1,fp16Bytes);
        fp16Runtime.handle=(VOID *)(UINT_PTR)0xd32u;
        CHECK(fp16.pDrvPrivate!=NULL);
        unsigned fp16Errors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&fp16Create,fp16,fp16Runtime);
        CHECK(FrontendErrors==fp16Errors);
        fp16ViewCreate.hDrvResource=fp16;
        fp16ViewCreate.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        fp16ViewCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        fp16ViewCreate.Tex2D.MipSlice=0;
        fp16ViewCreate.Tex2D.FirstArraySlice=0;
        fp16ViewCreate.Tex2D.ArraySize=1;
        SIZE_T fp16ViewBytes=deviceFunctions.pfnCalcPrivateRenderTargetViewSize(
            device,&fp16ViewCreate);
        fp16View.pDrvPrivate=calloc(1,fp16ViewBytes);
        fp16ViewRuntime.handle=(VOID *)(UINT_PTR)0xd33u;
        CHECK(fp16View.pDrvPrivate!=NULL);
        deviceFunctions.pfnCreateRenderTargetView(
            device,&fp16ViewCreate,fp16View,fp16ViewRuntime);
        CHECK(FrontendErrors==fp16Errors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
        RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
        memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;
        RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
        D3D10_DDI_VIEWPORT fp16Viewport={0,0,16,16,0,1};
        D3D10_DDI_RECT fp16Rect={0,0,16,16};
        deviceFunctions.pfnSetRenderTargets(device,&fp16View,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        CHECK(FrontendErrors==fp16Errors);
        deviceFunctions.pfnSetViewports(device,1,0,&fp16Viewport);
        CHECK(FrontendErrors==fp16Errors);
        deviceFunctions.pfnSetScissorRects(device,1,0,&fp16Rect);
        CHECK(FrontendErrors==fp16Errors);
        deviceFunctions.pfnClearRenderTargetView(device,fp16View,clear);
        CHECK(FrontendErrors==fp16Errors);
        deviceFunctions.pfnDraw(device,3,0);
        CHECK(FrontendErrors==fp16Errors && AgxWin32AsahiContextDrawReceipt(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnFlush(device);
        CHECK(RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u &&
              RuntimeMarker!=NULL);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(
              MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyRenderTargetView(device,fp16View);
        deviceFunctions.pfnDestroyResource(device,fp16);
        free(fp16View.pDrvPrivate);free(fp16.pDrvPrivate);
        deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        deviceFunctions.pfnSetViewports(device,1,0,&viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
      }
      {
        const DXGI_FORMAT typedFormats[]={DXGI_FORMAT_R8_UNORM,
            DXGI_FORMAT_R16_FLOAT,DXGI_FORMAT_R32G32B32A32_FLOAT,
            DXGI_FORMAT_R10G10B10A2_UNORM,DXGI_FORMAT_R11G11B10_FLOAT,
            DXGI_FORMAT_B5G6R5_UNORM,DXGI_FORMAT_A8_UNORM};
        for(unsigned typedIndex=0;typedIndex<ARRAYSIZE(typedFormats);++typedIndex) {
        D3D10DDI_MIPINFO typedMip={0};
        D3D10DDIARG_CREATERESOURCE typedCreate={0};
        D3D10DDI_HRESOURCE typed={0};D3D10DDI_HRTRESOURCE typedRuntime={0};
        D3D10DDIARG_CREATERENDERTARGETVIEW typedViewCreate={0};
        D3D10DDI_HRENDERTARGETVIEW typedView={0};
        D3D10DDI_HRTRENDERTARGETVIEW typedViewRuntime={0};
        typedMip.TexelWidth=typedMip.TexelHeight=16;typedMip.TexelDepth=1;
        typedCreate.pMipInfoList=&typedMip;
        typedCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        typedCreate.Usage=D3D10_DDI_USAGE_DEFAULT;
        typedCreate.BindFlags=D3D10_DDI_BIND_RENDER_TARGET;
        typedCreate.Format=typedFormats[typedIndex];
        typedCreate.SampleDesc.Count=1;typedCreate.MipLevels=1;
        typedCreate.ArraySize=1;
        SIZE_T typedBytes=deviceFunctions.pfnCalcPrivateResourceSize(
            device,&typedCreate);
        typed.pDrvPrivate=calloc(1,typedBytes);
        typedRuntime.handle=(VOID *)(UINT_PTR)(0xd40u+typedIndex*2u);
        CHECK(typed.pDrvPrivate!=NULL);
        unsigned typedErrors=FrontendErrors;
        deviceFunctions.pfnCreateResource(device,&typedCreate,typed,typedRuntime);
        CHECK(FrontendErrors==typedErrors);
        typedViewCreate.hDrvResource=typed;
        typedViewCreate.Format=typedFormats[typedIndex];
        typedViewCreate.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;
        typedViewCreate.Tex2D.MipSlice=0;
        typedViewCreate.Tex2D.FirstArraySlice=0;
        typedViewCreate.Tex2D.ArraySize=1;
        SIZE_T typedViewBytes=deviceFunctions.pfnCalcPrivateRenderTargetViewSize(
            device,&typedViewCreate);
        typedView.pDrvPrivate=calloc(1,typedViewBytes);
        typedViewRuntime.handle=(VOID *)(UINT_PTR)(0xd41u+typedIndex*2u);
        CHECK(typedView.pDrvPrivate!=NULL);
        deviceFunctions.pfnCreateRenderTargetView(
            device,&typedViewCreate,typedView,typedViewRuntime);
        CHECK(FrontendErrors==typedErrors);
        RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
        RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
        memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
        RuntimeConsumerGates=RuntimeConsumerRetirements=0;
        RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
        RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
        D3D10_DDI_VIEWPORT typedViewport={0,0,16,16,0,1};
        D3D10_DDI_RECT typedRect={0,0,16,16};
        deviceFunctions.pfnSetRenderTargets(device,&typedView,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        CHECK(FrontendErrors==typedErrors);
        deviceFunctions.pfnSetViewports(device,1,0,&typedViewport);
        CHECK(FrontendErrors==typedErrors);
        deviceFunctions.pfnSetScissorRects(device,1,0,&typedRect);
        CHECK(FrontendErrors==typedErrors);
        deviceFunctions.pfnClearRenderTargetView(device,typedView,clear);
        CHECK(FrontendErrors==typedErrors);
        deviceFunctions.pfnDraw(device,3,0);
        CHECK(FrontendErrors==typedErrors && AgxWin32AsahiContextDrawReceipt(
            MesaD3d10FrontendContextForTest(device)));
        deviceFunctions.pfnFlush(device);
        CHECK(RuntimeRenders==1u && RuntimeSignals==1u &&
              RuntimeMaterializations==2u && RuntimeConsumerGates==2u &&
              RuntimeMarker!=NULL);
        if(RuntimeMarker) {
          RuntimeCheckpoint(depthOwner,1u);
          CHECK(AgxWin32AsahiContextRetire(
              MesaD3d10FrontendContextForTest(device),0u));
          RuntimeCheckpoint(depthOwner,5u);
        }
        RuntimeExpectedCommandVersion=0;
        deviceFunctions.pfnDestroyRenderTargetView(device,typedView);
        deviceFunctions.pfnDestroyResource(device,typed);
        free(typedView.pDrvPrivate);free(typed.pDrvPrivate);
        deviceFunctions.pfnSetRenderTargets(device,&rtv,1,0,
            (D3D10DDI_HDEPTHSTENCILVIEW){0});
        deviceFunctions.pfnSetViewports(device,1,0,&viewport);
        deviceFunctions.pfnSetScissorRects(device,1,0,&rect);
        }
      }
      deviceFunctions.pfnGsSetShader(device,soGsh);
      UINT activeSoOffset=0;
      unsigned activeSoErrors=FrontendErrors;
      deviceFunctions.pfnSoSetTargets(device,1,0,&soBuffer,&activeSoOffset);
      CHECK(FrontendErrors==activeSoErrors);
      RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH;
      deviceFunctions.pfnClearRenderTargetView(device,rtv,clear);
      CHECK(!AgxWin32AsahiContextFaulted(
          MesaD3d10FrontendContextForTest(device)));
      FRONTEND_STAGE("depth-clear-draw");
      FRONTEND_STAGE("bound-clear");
    }
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
    deviceFunctions.pfnDrawInstanced(device,3,1,0,0);
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
      float genericCbValues[4]={0.7f,0.1f,0.6f,1.0f};
      unsigned genericUpdateErrors=FrontendErrors;
      deviceFunctions.pfnResourceUpdateSubresourceUP(
          device,cb,0,NULL,genericCbValues,1,1);
      CHECK(FrontendErrors==genericUpdateErrors);
      deviceFunctions.pfnVsSetConstantBuffers(device,0,1,&vsCb);
      /* The mixed v8 GS experiment is complete. Keep the following existing
       * indexed admission checks on their independent v5 producer path. */
      deviceFunctions.pfnGsSetShader(device,(D3D10DDI_HSHADER){0});
      deviceFunctions.pfnSoSetTargets(device,0,1,NULL,NULL);
      RuntimeExpectedCommandVersion=0;
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
      deviceFunctions.pfnDrawIndexedInstanced(device,3,1,0,0,0);
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
    if(RuntimeMarker) {
      RuntimeCheckpoint(frontendOwner,1u);
      CHECK(MesaD3d10FrontendSetSoOffsetForTest(device,soBuffer,48u));
      CHECK(AgxWin32AsahiContextRetire(
          MesaD3d10FrontendContextForTest(device),0u));
      RuntimeCheckpoint(frontendOwner,5u);
    }
    {
      deviceFunctions.pfnGsSetShader(device,(D3D10DDI_HSHADER){0});
      deviceFunctions.pfnSoSetTargets(device,0,1,NULL,NULL);
      UINT drawAutoStride=16u,drawAutoOffset=0u;
      deviceFunctions.pfnIaSetVertexBuffers(device,0,1,&soBuffer,
          &drawAutoStride,&drawAutoOffset);
      RuntimeExpectedCommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
      RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
      memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
      RuntimeConsumerGates=RuntimeConsumerRetirements=0;
      RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
      unsigned drawAutoErrors=FrontendErrors;
      deviceFunctions.pfnDrawAuto(device);
      CHECK(FrontendErrors==drawAutoErrors);
      CHECK(AgxWin32AsahiContextDrawReceipt(
          MesaD3d10FrontendContextForTest(device)));
      deviceFunctions.pfnFlush(device);
      CHECK(RuntimeRenders==1u&&RuntimeSignals==1u&&
            RuntimeMaterializations==2u&&RuntimeConsumerGates==2u&&RuntimeMarker);
      if(RuntimeMarker) {
        RuntimeCheckpoint(frontendOwner,1u);
        CHECK(AgxWin32AsahiContextRetire(
            MesaD3d10FrontendContextForTest(device),0u));
        RuntimeCheckpoint(frontendOwner,5u);
      }
      RuntimeExpectedCommandVersion=0;
    }
    {
      RuntimeActiveDevice=MesaD3d10FrontendRuntimeForTest(device);
      ADMISSION_UMD_ASAHI_OWNER *bltOwner=
          MesaD3d10FrontendOwnerForTest(device);
      RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
      memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
      RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;
      RuntimeExpectedTargetAllocation=0x775u;
      RuntimeExpectedTargetBytes=0xfa0000ULL;
      RuntimeConsumerGates=RuntimeConsumerRetirements=0;
      RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
      CHECK(RuntimeActiveDevice && bltOwner);
      DXGI_DDI_ARG_BLT blt={0};
      blt.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      blt.hDstResource=(DXGI_DDI_HRESOURCE)(UINT_PTR)
          createdPresentResource.pDrvPrivate;
      blt.hSrcResource=(DXGI_DDI_HRESOURCE)(UINT_PTR)presentResource.pDrvPrivate;
      blt.DstRight=2560;blt.DstBottom=1600;blt.Flags.Value=0x8u;
      blt.Rotate=DXGI_DDI_MODE_ROTATION_IDENTITY;
      CHECK(dxgiFunctions.pfnBlt(&blt)==S_OK);
      CHECK(!AgxWin32AsahiContextFaulted(
          MesaD3d10FrontendContextForTest(device)));
      CHECK(RuntimeRenders==0u && RuntimeSignals==0u &&
          RuntimeMaterializations==0u);
      DXGI_DDI_ARG_PRESENT bltPresent={0};
      bltPresent.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      bltPresent.hSurfaceToPresent=blt.hDstResource;
      bltPresent.Flags.Value=0x2u;
      bltPresent.FlipInterval=DXGI_DDI_FLIP_INTERVAL_ONE;
      bltPresent.pDXGIContext=(PVOID)(UINT_PTR)0x779u;
      FrontendPresentAllocation=0x775u;
      FrontendPresentContext=bltPresent.pDXGIContext;
      CHECK(dxgiFunctions.pfnPresent(&bltPresent)==S_OK &&
            FrontendPresentCalls==3u && RuntimeRenders==1u &&
            RuntimeSignals==1u && RuntimeMaterializations==2u &&
            RuntimeConsumerGates==2u && RuntimeMarker!=NULL);
      CHECK(!AgxWin32AsahiContextFaulted(
          MesaD3d10FrontendContextForTest(device)));
      RuntimeCheckpoint(bltOwner,1u);
      CHECK(AgxWin32AsahiContextRetire(
          MesaD3d10FrontendContextForTest(device),0u));
      CHECK(!AgxWin32AsahiContextFaulted(
          MesaD3d10FrontendContextForTest(device)));
      RuntimeCheckpoint(bltOwner,5u);
      CHECK(!AgxWin32AsahiContextFaulted(
          MesaD3d10FrontendContextForTest(device)));
      RuntimeExpectedTargetAllocation=0;
      RuntimeExpectedTargetBytes=0;
    }
    {
      RuntimeActiveDevice=MesaD3d10FrontendRuntimeForTest(device);
      ADMISSION_UMD_ASAHI_OWNER *copyOwner=
          MesaD3d10FrontendOwnerForTest(device);
      RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
      memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
      RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;
      RuntimeExpectedTargetAllocation=0x775u;
      RuntimeExpectedTargetBytes=0xfa0000ULL;
      RuntimeConsumerGates=RuntimeConsumerRetirements=0;
      RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
      unsigned copyErrors=FrontendErrors;
      deviceFunctions.pfnResourceCopy(device,createdPresentResource,presentResource);
      CHECK(FrontendErrors==copyErrors && RuntimeRenders==0u);
      DXGI_DDI_ARG_PRESENT copyPresent={0};
      copyPresent.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      copyPresent.hSurfaceToPresent=(DXGI_DDI_HRESOURCE)(UINT_PTR)
          createdPresentResource.pDrvPrivate;
      copyPresent.Flags.Value=0x2u;
      copyPresent.FlipInterval=DXGI_DDI_FLIP_INTERVAL_ONE;
      copyPresent.pDXGIContext=(PVOID)(UINT_PTR)0x77cu;
      FrontendPresentAllocation=0x775u;
      FrontendPresentContext=copyPresent.pDXGIContext;
      CHECK(dxgiFunctions.pfnPresent(&copyPresent)==S_OK &&
            FrontendPresentCalls==4u && RuntimeRenders==1u &&
            RuntimeSignals==1u && RuntimeMaterializations==2u &&
            RuntimeConsumerGates==2u && RuntimeMarker!=NULL);
      RuntimeCheckpoint(copyOwner,1u);
      CHECK(AgxWin32AsahiContextRetire(
          MesaD3d10FrontendContextForTest(device),0u));
      RuntimeCheckpoint(copyOwner,5u);
      RuntimeExpectedTargetAllocation=0;
      RuntimeExpectedTargetBytes=0;
    }
    {
      RuntimeActiveDevice=MesaD3d10FrontendRuntimeForTest(device);
      ADMISSION_UMD_ASAHI_OWNER *copyRegionOwner=
          MesaD3d10FrontendOwnerForTest(device);
      RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
      RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
      memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));
      RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;
      RuntimeExpectedTargetAllocation=0x775u;
      RuntimeExpectedTargetBytes=0xfa0000ULL;
      RuntimeConsumerGates=RuntimeConsumerRetirements=0;
      RuntimeConsumerFence=0;memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
      D3D10_DDI_BOX fullBox={0,0,0,2560,1600,1};
      unsigned copyRegionErrors=FrontendErrors;
      deviceFunctions.pfnResourceCopyRegion(device,createdPresentResource,
          0,0,0,0,presentResource,0,&fullBox);
      CHECK(FrontendErrors==copyRegionErrors && RuntimeRenders==0u);
      DXGI_DDI_ARG_PRESENT copyRegionPresent={0};
      copyRegionPresent.hDevice=(DXGI_DDI_HDEVICE)(UINT_PTR)device.pDrvPrivate;
      copyRegionPresent.hSurfaceToPresent=(DXGI_DDI_HRESOURCE)(UINT_PTR)
          createdPresentResource.pDrvPrivate;
      copyRegionPresent.Flags.Value=0x2u;
      copyRegionPresent.FlipInterval=DXGI_DDI_FLIP_INTERVAL_ONE;
      copyRegionPresent.pDXGIContext=(PVOID)(UINT_PTR)0x77du;
      FrontendPresentAllocation=0x775u;
      FrontendPresentContext=copyRegionPresent.pDXGIContext;
      CHECK(dxgiFunctions.pfnPresent(&copyRegionPresent)==S_OK &&
            FrontendPresentCalls==5u && RuntimeRenders==1u &&
            RuntimeSignals==1u && RuntimeMaterializations==2u &&
            RuntimeConsumerGates==2u && RuntimeMarker!=NULL);
      RuntimeCheckpoint(copyRegionOwner,1u);
      CHECK(AgxWin32AsahiContextRetire(
          MesaD3d10FrontendContextForTest(device),0u));
      RuntimeCheckpoint(copyRegionOwner,5u);
      RuntimeExpectedTargetAllocation=0;
      RuntimeExpectedTargetBytes=0;
    }
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
    deviceFunctions.pfnGsSetShader(device,(D3D10DDI_HSHADER){0});
    D3D10DDI_HRESOURCE nullConstant={0};
    deviceFunctions.pfnIaSetIndexBuffer(device,(D3D10DDI_HRESOURCE){0},
                                        DXGI_FORMAT_UNKNOWN,0);
    deviceFunctions.pfnVsSetConstantBuffers(device,0,1,&nullConstant);
    deviceFunctions.pfnPsSetConstantBuffers(device,0,1,&nullConstant);
    deviceFunctions.pfnDestroyDepthStencilState(device,depth);deviceFunctions.pfnDestroyRasterizerState(device,raster);
    deviceFunctions.pfnDestroyBlendState(device,blend);deviceFunctions.pfnDestroyShader(device,psh);
    deviceFunctions.pfnDestroyShader(device,gsh);
    deviceFunctions.pfnDestroyShader(device,soGsh);
    deviceFunctions.pfnDestroyShader(device,vsh);deviceFunctions.pfnDestroyElementLayout(device,layout);
    deviceFunctions.pfnDestroyDepthStencilView(device,depthView);
    deviceFunctions.pfnDestroyRenderTargetView(device,depthColorRtv);
    deviceFunctions.pfnDestroyRenderTargetView(device,rtv);deviceFunctions.pfnDestroyResource(device,vb);
    deviceFunctions.pfnDestroyShaderResourceView(device,appSrv);
    deviceFunctions.pfnDestroySampler(device,appSampler);
    free(appSrv.pDrvPrivate);free(appSampler.pDrvPrivate);
    deviceFunctions.pfnDestroyResource(device,presentResource);
    deviceFunctions.pfnDestroyResource(device,createdPresentResource);
    deviceFunctions.pfnDestroyResource(device,vsCb);
    deviceFunctions.pfnDestroyResource(device,cb);
    deviceFunctions.pfnDestroyResource(device,ib);
    deviceFunctions.pfnDestroyResource(device,depthResource);
    deviceFunctions.pfnDestroyResource(device,rt);
    deviceFunctions.pfnDestroyResource(device,staging);
    deviceFunctions.pfnDestroyResource(device,soBuffer);
    RuntimeExpectedTargetAllocation=0;
    RuntimeExpectedTargetBytes=0;
    free(depth.pDrvPrivate);free(raster.pDrvPrivate);free(blend.pDrvPrivate);free(psh.pDrvPrivate);free(gsh.pDrvPrivate);
    free(vsh.pDrvPrivate);free(layout.pDrvPrivate);free(rtv.pDrvPrivate);
    free(depthColorRtv.pDrvPrivate);free(depthView.pDrvPrivate);
    free(depthResource.pDrvPrivate);free(vb.pDrvPrivate);
    free(soBuffer.pDrvPrivate);free(soGsh.pDrvPrivate);
    free(vsCb.pDrvPrivate);free(cb.pDrvPrivate);free(ib.pDrvPrivate);
    free(presentResource.pDrvPrivate);free(createdPresentResource.pDrvPrivate);
    free(rt.pDrvPrivate);
    free(staging.pDrvPrivate);
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
        RuntimeRenders==0u && RuntimeSignals==0u &&
        RuntimeConsumerRetirements==0u && PoolPresentationDeletes==2u);
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
  test_g13_compute_work_contract();
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
  kernelCallbacks.pfnSetPriorityCb = TestSetPriority;
  kernelCallbacks.pfnQueryResidencyCb = TestQueryResidency;
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
  {
    D3D10DDI_HDEVICE peer={0};
    D3DWDDM1_3DDI_DEVICEFUNCS peerFunctions={0};
    DXGI1_3_DDI_BASE_FUNCTIONS peerDxgi={0};
    D3D10DDIARG_CREATEDEVICE peerCreate=createDevice;
    D3D10DDI_HRTRESOURCE peerRuntime={(VOID *)(UINT_PTR)0x351u};
    D3D10DDI_HRESOURCE peerOpened={0};
    ADMISSION_UMD_RESOURCE *sharedState=(ADMISSION_UMD_RESOURCE *)shared.pDrvPrivate;
    peer.pDrvPrivate=calloc(1u,deviceBytes);
    peerCreate.hRTDevice.handle=(VOID *)(UINT_PTR)0x103u;
    peerCreate.hRTCoreLayer.handle=(VOID *)(UINT_PTR)0x104u;
    peerCreate.hDrvDevice=peer;
    peerCreate.pWDDM1_3DeviceFuncs=&peerFunctions;
    peerCreate.DXGIBaseDDI.pDXGIDDIBaseFunctions4=&peerDxgi;
    CHECK(peer.pDrvPrivate && sharedState &&
          sharedState->Magic==ADMISSION_UMD_RESOURCE_MAGIC &&
          sharedState->Retirement && sharedState->Retirement->Shared &&
          adapterFunctions.pfnCreateDevice(openAdapter.hAdapter,&peerCreate)==S_OK);
    if(peer.pDrvPrivate && sharedState &&
       sharedState->Magic==ADMISSION_UMD_RESOURCE_MAGIC) {
      D3DKMT_HANDLE sharedAllocation=sharedState->KernelAllocation;
      peerOpened=open_resource(&peerFunctions,peer,peerRuntime,
          sharedAllocation,0xa50u);
      ADMISSION_UMD_RESOURCE *peerState=
          (ADMISSION_UMD_RESOURCE *)peerOpened.pDrvPrivate;
      CHECK(peerState && peerState->Magic==ADMISSION_UMD_RESOURCE_MAGIC &&
            peerState->KernelAllocation==sharedAllocation &&
            peerState->Retirement && peerState->Retirement->Shared &&
            peerState->DirectFlip.Allocation.Size==
                sharedState->DirectFlip.Allocation.Size);
      deallocationsBefore=State.DeallocateCalls;
      deviceFunctions.pfnDestroyResource(device,shared);
      CHECK(deviceFunctions.pfnFlush(device,0u) &&
            State.DeallocateCalls==deallocationsBefore+1u &&
            peerState->Magic==ADMISSION_UMD_RESOURCE_MAGIC &&
            peerState->KernelAllocation==sharedAllocation);
      deallocationsBefore=State.DeallocateCalls;
      peerFunctions.pfnDestroyResource(peer,peerOpened);
      CHECK(peerFunctions.pfnFlush(peer,0u) &&
            State.DeallocateCalls==deallocationsBefore+1u);
      free(peerOpened.pDrvPrivate);
      peerFunctions.pfnDestroyDevice(peer);
      free(peer.pDrvPrivate);
    }
  }
  deallocationsBefore = State.DeallocateCalls;
  if (nonPrimary.pDrvPrivate != NULL)
    deviceFunctions.pfnDestroyResource(device, nonPrimary);
  CHECK(State.DeallocateCalls == deallocationsBefore);
  CHECK(deviceFunctions.pfnFlush(device, 0u));
  CHECK(State.DeallocateCalls == deallocationsBefore + 1u);
  CHECK(State.DeallocateResources[deallocationsBefore] ==
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
  CHECK(State.DestroyContextCalls == 2u);
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
  CHECK(State.DestroyContextCalls == 4u);
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
  State.Failures += AppleAgxG13QueueRuntimeContractTests();
#if defined(ADMISSION_UMD_NATIVE_POOL_TEST)
  State.Failures += TestAsahiNativePoolOwner();
#endif
  return State.Failures == 0u ? 0 : (int)State.Failures;
}
