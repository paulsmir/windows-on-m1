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

#if defined(APPLE_AGX_UMD_NATIVE_FRONTEND)
HRESULT APIENTRY MesaD3d10OpenAdapter10_2(
    D3D10DDIARG_OPENADAPTER *OpenAdapter);
#endif

#if defined(APPLE_AGX_UMD_ADMISSION_TRACE)
#define ADMISSION_UMD_TRACE(Text)                                             \
  OutputDebugStringW(L"AppleAgxUMD: " Text L"\n")
#else
#define ADMISSION_UMD_TRACE(Text) ((void)0)
#endif


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
VOID APIENTRY AdmissionUmdCreateResource(
    D3D10DDI_HDEVICE Device,
    const D3D11DDIARG_CREATERESOURCE *CreateResource,
    D3D10DDI_HRESOURCE Resource, D3D10DDI_HRTRESOURCE RuntimeResource);
VOID APIENTRY AdmissionUmdOpenResource(
    D3D10DDI_HDEVICE Device,
    const D3D10DDIARG_OPENRESOURCE *OpenResource,
    D3D10DDI_HRESOURCE Resource, D3D10DDI_HRTRESOURCE RuntimeResource);
VOID APIENTRY AdmissionUmdDestroyResource(
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
HRESULT APIENTRY AdmissionUmdSetDisplayMode(
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




/* The storage AdmissionUmdDescribePrimary gives each shared DXGI format. */
static BOOLEAN AdmissionUmdSharedFormatStorage(UINT DxgiFormat,
                                               UINT StorageFormat) {
  switch (DxgiFormat) {
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    return StorageFormat == (UINT)D3DDDIFMT_A8R8G8B8;
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_A8_UNORM:
    return StorageFormat == (UINT)D3DDDIFMT_A8B8G8R8;
  default:
    return FALSE;
  }
}

static BOOLEAN AdmissionUmdDescribePrimary(
    const D3D11DDIARG_CREATERESOURCE *CreateResource,
    ADMISSION_UMD_DIRECT_FLIP_RESOURCE *Description) {
  /* Discard-on-present relaxes content preservation. The displayable marker
   * uses the existing linear BGRA allocation contract; it must not turn the
   * non-displayable RGBA path into a scanout resource. EXP1038: without the
   * WDDM 3.0 DisplayableSupport cap DXGI marks RGBA8 flip-model buffers
   * displayable too (WinUI); they keep the non-scanout RGBA allocation
   * (Displayable 0, never direct-flip compatible) instead of failing. */
  const UINT allowedMiscFlags = D3D10_DDI_RESOURCE_MISC_SHARED |
      D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT |
      D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE;
  if (CreateResource == NULL || Description == NULL ||
      CreateResource->pMipInfoList == NULL ||
      CreateResource->ResourceDimension != D3D10DDIRESOURCE_TEXTURE2D ||
      (CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
       CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
       CreateResource->Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
       CreateResource->Format != DXGI_FORMAT_A8_UNORM) ||
      (CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
       CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
       CreateResource->pPrimaryDesc != NULL) ||
      !CreateResource->pMipInfoList[0].TexelWidth ||
      !CreateResource->pMipInfoList[0].TexelHeight ||
      CreateResource->pMipInfoList[0].TexelWidth > 8192u ||
      CreateResource->pMipInfoList[0].TexelHeight > 8192u ||
      CreateResource->pMipInfoList[0].TexelDepth != 1u ||
      (CreateResource->pPrimaryDesc != NULL &&
       (CreateResource->pMipInfoList[0].TexelWidth != 2560u ||
        CreateResource->pMipInfoList[0].TexelHeight != 1600u)) ||
      CreateResource->Usage != D3D10_DDI_USAGE_DEFAULT || CreateResource->MapFlags ||
      (CreateResource->MiscFlags & ~allowedMiscFlags) ||
      ((CreateResource->MiscFlags &
        D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE) != 0u &&
       CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
       CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
       (CreateResource->Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        (CreateResource->BindFlags & D3D10_DDI_BIND_PRESENT) == 0u)) ||
      ((CreateResource->MiscFlags &
        D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT) != 0u &&
       (CreateResource->BindFlags & D3D10_DDI_BIND_PRESENT) == 0u) ||
      (CreateResource->BindFlags & ~(D3D10_DDI_BIND_RENDER_TARGET |
          D3D10_DDI_BIND_SHADER_RESOURCE | D3D10_DDI_BIND_PRESENT)) ||
      CreateResource->MipLevels != 1u || CreateResource->ArraySize != 1u ||
      CreateResource->SampleDesc.Count != 1u ||
      CreateResource->SampleDesc.Quality != 0u ||
      /* A shared texture with any RT/SRV binds is the same linear
       * allocation; EXP1226: opengl32's window redirection surface is
       * SRV-only (the GL ICD writes it, DWM samples it). EXP1327: D3D11.1
       * shares R8G8B8A8 Texture2Ds (guaranteed) and A8_UNORM ones
       * (ExtendedResourceSharing, derived from the DDI version; the FL 11_0
       * desktop's composition masks); the frontend stores A8_UNORM as
       * RGBA8 (AgxD3d10LoweredTextureFormat), so both share the RGBA8
       * allocation. */
      ((CreateResource->BindFlags & D3D10_DDI_BIND_PRESENT) == 0u &&
       (CreateResource->MiscFlags != D3D10_DDI_RESOURCE_MISC_SHARED ||
        (CreateResource->Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         CreateResource->Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
         CreateResource->Format != DXGI_FORMAT_A8_UNORM) ||
        (CreateResource->BindFlags & (D3D10_DDI_BIND_RENDER_TARGET |
                                      D3D10_DDI_BIND_SHADER_RESOURCE)) == 0u)) ||
      CreateResource->pInitialDataUP != NULL)
    return FALSE;
  ZeroMemory(Description, sizeof(*Description));
  Description->Magic = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC;
  Description->Version = ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION;
  if (!AdmissionAllocationDescribe(
          CreateResource->pMipInfoList[0].TexelWidth,
          CreateResource->pMipInfoList[0].TexelHeight,
          4u, (UINT)D3DKMDT_GDISURFACE_TEXTURE,
          CreateResource->Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
          CreateResource->Format == DXGI_FORMAT_A8_UNORM
              ? (UINT)D3DDDIFMT_A8B8G8R8 : (UINT)D3DDDIFMT_A8R8G8B8,
          0u, &Description->Allocation))
    return FALSE;
  /* AIL linear layers are cache-line padded; no layout metadata is implicit. */
  Description->Allocation.Size = (Description->Allocation.Size + 127ULL) & ~127ULL;
  Description->SegmentId = 2u;
  Description->Linear = 1u;
  Description->Displayable =
      (CreateResource->Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
       CreateResource->Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) ? 1u : 0u;
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

/* Backbuffers may require DXGI conversion; scanout eligibility stays separate. */
static BOOLEAN AdmissionUmdResourceIsPresentable(
    const ADMISSION_UMD_RESOURCE *Resource) {
  const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *r;
  const ADMISSION_ALLOCATION_DESCRIPTION *a;
  if (Resource == NULL || Resource->Magic != ADMISSION_UMD_RESOURCE_MAGIC ||
      Resource->KernelAllocation == 0u)
    return FALSE;
  r = &Resource->DirectFlip;
  a = &r->Allocation;
  return r->Magic == ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC &&
      r->Version == ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION &&
      r->SegmentId == 2u && r->Linear == 1u && r->Reserved == 0u &&
      AdmissionAllocationDescriptionValid(a) &&
      a->BytesPerPixel == 4u && a->CpuVisible == 0u &&
      ((a->Format == (UINT)D3DDDIFMT_A8R8G8B8 && r->Displayable == 1u) ||
       (a->Format == (UINT)D3DDDIFMT_A8B8G8R8 && r->Displayable == 0u));
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
  if (!AdmissionUmdNextRenderSequence(Device, &render.RenderCBSequence))
    return E_FAIL;
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

/* EXP1329 receipt-only: with FL 11_0 reported, dwm.exe, LogonUI and
 * M365Copilot fault at address 0 and the buffered trace loses their last
 * lines. A call to address 0 leaves its return address in LR (ARM64) or at
 * [ESP]/[RSP] (x86/x64): record that and the first stack words that point
 * into a module, as module-name prefix and offset, flush, and continue the
 * search (the process fails exactly as before). */
static PVOID AdmissionUmdNullCallHandler;

static void AdmissionUmdModuleOffset(ULONG_PTR Address, UINT *Name,
                                     UINT *Offset) {
  HMODULE module = NULL;
  WCHAR path[MAX_PATH];
  PCWSTR leaf = NULL;
  Name[0] = Name[1] = 0u;
  *Offset = (UINT)Address;
  if (Address == 0u ||
      !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)Address, &module) ||
      module == NULL)
    return;
  *Offset = (UINT)(Address - (ULONG_PTR)module);
  if (GetModuleFileNameW(module, path, MAX_PATH) == 0u) return;
  leaf = path;
  for (PCWSTR c = path; *c != 0; ++c)
    if (*c == L'\\') leaf = c + 1;
  for (UINT i = 0; i < 8u && leaf[i] != 0; ++i)
    Name[i / 4u] |= (UINT)(leaf[i] & 0x7f) << (8u * (i % 4u));
}

static LONG CALLBACK AdmissionUmdNullCall(EXCEPTION_POINTERS *Info) {
  const ULONG_PTR *stack;
  ULONG_PTR caller;
  UINT values[16] = {0};
  UINT found = 0u;
  if (Info == NULL || Info->ExceptionRecord == NULL ||
      Info->ContextRecord == NULL ||
      Info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
      Info->ExceptionRecord->ExceptionAddress != NULL)
    return EXCEPTION_CONTINUE_SEARCH;
#if defined(_M_ARM64)
  caller = (ULONG_PTR)Info->ContextRecord->Lr;
  stack = (const ULONG_PTR *)Info->ContextRecord->Sp;
#elif defined(_M_X64)
  stack = (const ULONG_PTR *)Info->ContextRecord->Rsp;
  caller = stack != NULL ? stack[0] : 0u;
#else
  stack = (const ULONG_PTR *)Info->ContextRecord->Esp;
  caller = stack != NULL ? stack[0] : 0u;
#endif
  values[0] = GetCurrentThreadId();
  AdmissionUmdModuleOffset(caller, &values[1], &values[3]);
  for (UINT i = 1u; stack != NULL && i < 48u && found < 4u; ++i) {
    UINT name[2], offset;
    AdmissionUmdModuleOffset(stack[i], name, &offset);
    if (name[0] == 0u) continue;
    values[4u + 3u * found] = name[0];
    values[5u + 3u * found] = name[1];
    values[6u + 3u * found] = offset;
    ++found;
  }
  AdmissionUmdDiagnostic("reject-null-call", E_POINTER, values, 16u);
  AdmissionUmdDiagnosticFlush();
  return EXCEPTION_CONTINUE_SEARCH;
}

/* EXP1332 receipt-only: on the FL 11_0 desktop WindowsTerminal.exe fails
 * fast in ucrtbase (abort from std::terminate), closing the consoles it
 * hosts, with no driver refusal recorded. The unhandled C++ exception passes
 * the vectored handlers first: record the first 32 MSVC C++ throws of a
 * process -- the thrown type's name prefix, the object's first words (a
 * wil::ResultException holds its HRESULT and source line there) and the
 * first four stack words that point into a module other than the throw
 * machinery -- and continue the search unchanged. */
static volatile LONG AdmissionUmdCxxThrows;

static BOOLEAN AdmissionUmdThrowMachinery(const UINT *Name) {
  UINT lower = Name[0] | 0x20202020u;
  return lower == 0x6e72656bu /* kern */ || lower == 0x75726376u /* vcru */ ||
         lower == 0x74726375u /* ucrt */ || lower == 0x6c64746eu /* ntdl */;
}

static void AdmissionUmdCxxThrowReceipt(EXCEPTION_POINTERS *Info) {
  const EXCEPTION_RECORD *record = Info->ExceptionRecord;
  const ULONG_PTR *stack;
  UINT thrown[11] = {0};
  UINT frames[13] = {0};
  UINT found = 0u;
  if (InterlockedIncrement(&AdmissionUmdCxxThrows) > 32) return;
  thrown[0] = frames[0] = GetCurrentThreadId();
  __try {
    const BYTE *object = (const BYTE *)record->ExceptionInformation[1];
    const BYTE *info = (const BYTE *)record->ExceptionInformation[2];
#if defined(_WIN64)
    const BYTE *base = record->NumberParameters >= 4
        ? (const BYTE *)record->ExceptionInformation[3] : NULL;
    const BYTE *types = base + *(const int *)(info + 12);
    const BYTE *type = base + *(const int *)(types + 4);
    const char *name = (const char *)(base + *(const int *)(type + 4) + 16);
#else
    const BYTE *types = *(const BYTE *const *)(info + 12);
    const BYTE *type = *(const BYTE *const *)(types + 4);
    const char *name = *(const char *const *)(type + 4) + 8;
#endif
    if (name[0] == '.' && name[1] == '?' && name[2] == 'A') name += 4;
    for (UINT i = 0u; i < 8u && name[i] != 0; ++i)
      thrown[1u + i / 4u] |= (UINT)(BYTE)name[i] << (8u * (i % 4u));
    for (UINT i = 0u; object != NULL && i < 8u; ++i)
      thrown[3u + i] = ((const UINT *)object)[i];
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
#if defined(_M_ARM64)
  stack = (const ULONG_PTR *)Info->ContextRecord->Sp;
#elif defined(_M_X64)
  stack = (const ULONG_PTR *)Info->ContextRecord->Rsp;
#else
  stack = (const ULONG_PTR *)Info->ContextRecord->Esp;
#endif
  for (UINT i = 0u; stack != NULL && i < 192u && found < 4u; ++i) {
    UINT name[2], offset;
    AdmissionUmdModuleOffset(stack[i], name, &offset);
    if (name[0] == 0u || AdmissionUmdThrowMachinery(name)) continue;
    frames[1u + 3u * found] = name[0];
    frames[2u + 3u * found] = name[1];
    frames[3u + 3u * found] = offset;
    ++found;
  }
  AdmissionUmdDiagnostic("reject-cxx-throw", E_UNEXPECTED, thrown, 11u);
  AdmissionUmdDiagnostic("reject-cxx-stack", E_UNEXPECTED, frames, 13u);
  AdmissionUmdDiagnosticFlush();
}

static LONG CALLBACK AdmissionUmdFaultReceipt(EXCEPTION_POINTERS *Info) {
  if (Info != NULL && Info->ExceptionRecord != NULL && Info->ContextRecord != NULL &&
      Info->ExceptionRecord->ExceptionCode == 0xE06D7363u &&
      Info->ExceptionRecord->NumberParameters >= 3u &&
      Info->ExceptionRecord->ExceptionInformation[0] == 0x19930520u)
    AdmissionUmdCxxThrowReceipt(Info);
  return AdmissionUmdNullCall(Info);
}

BOOL WINAPI DllMain(HINSTANCE Instance, DWORD Reason, LPVOID Reserved) {
  UNREFERENCED_PARAMETER(Instance);
  UNREFERENCED_PARAMETER(Reserved);
  if (Reason == DLL_PROCESS_ATTACH && AdmissionUmdNullCallHandler == NULL)
    AdmissionUmdNullCallHandler =
        AddVectoredExceptionHandler(0u, AdmissionUmdFaultReceipt);
  if (Reason == DLL_PROCESS_DETACH) {
    if (AdmissionUmdNullCallHandler != NULL && Reserved == NULL)
      RemoveVectoredExceptionHandler(AdmissionUmdNullCallHandler);
    AdmissionUmdDiagnosticFlush();
  }
  return TRUE;
}

HRESULT APIENTRY OpenAdapter10_2(
    D3D10DDIARG_OPENADAPTER *OpenAdapter) {
#if defined(APPLE_AGX_UMD_NATIVE_FRONTEND)
  return MesaD3d10OpenAdapter10_2(OpenAdapter);
#else
  ADMISSION_UMD_ADAPTER *adapter;
  D3D10_2DDI_ADAPTERFUNCS functions;
  HRESULT queryResult;
  ADMISSION_UMD_TRACE(L"OpenAdapter10_2 ENTER");
  if (OpenAdapter == NULL || OpenAdapter->pAdapterFuncs_2 == NULL ||
      OpenAdapter->pAdapterCallbacks == NULL)
    return E_INVALIDARG;
  adapter = (ADMISSION_UMD_ADAPTER *)HeapAlloc(
      GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*adapter));
  if (adapter == NULL)
    return E_OUTOFMEMORY;
  queryResult = AdmissionUmdRuntimeAdapterInitialize(adapter, OpenAdapter);
  if (FAILED(queryResult)) {
    HeapFree(GetProcessHeap(), 0u, adapter);
    return queryResult;
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
#endif
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
  D3DWDDM1_3DDI_DEVICEFUNCS *deviceFunctions;
  DXGI1_3_DDI_BASE_FUNCTIONS *dxgiFunctions;
  HRESULT result;
  ADMISSION_UMD_TRACE(L"CreateDevice ENTER");
  if (adapter == NULL || Args == NULL || Args->hDrvDevice.pDrvPrivate == NULL ||
      Args->Interface != D3DWDDM1_3_DDI_INTERFACE_VERSION ||
      Args->pWDDM1_3DeviceFuncs == NULL ||
      Args->DXGIBaseDDI.pDXGIDDIBaseFunctions4 == NULL)
    return E_INVALIDARG;
  device = (ADMISSION_UMD_DEVICE *)Args->hDrvDevice.pDrvPrivate;
  result = AdmissionUmdRuntimeDeviceInitialize(device, adapter, Args);
  {
    /* EXP1131: device lifetime per process (DWM recreation evidence). */
    UINT values[2] = {(UINT)(ULONG_PTR)device, (UINT)Args->Flags};
    AdmissionUmdDiagnostic("device-create", result, values,
                           (UINT)(sizeof(values) / sizeof(values[0])));
  }
  if (FAILED(result))
    return result;

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
#if defined(APPLE_AGX_UMD_NATIVE_FRONTEND)
    ADMISSION_UMD_TRACE(L"GetCaps PIPELINE: D3D10_0");
    ((D3D11DDI_3DPIPELINESUPPORT_CAPS *)Caps->pData)->Caps =
        D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(
            D3D11DDI_3DPIPELINELEVEL_10_0);
#else
    ADMISSION_UMD_TRACE(L"GetCaps PIPELINE: no implemented level");
    ((D3D11DDI_3DPIPELINESUPPORT_CAPS *)Caps->pData)->Caps = 0u;
#endif
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

VOID APIENTRY AdmissionUmdCreateResource(
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
  ADMISSION_PRESENT_RESOURCE_DATA resourceData;
  ADMISSION_UMD_RETIREMENT *retirement;
  HRESULT result;
  if (device == NULL || resource == NULL ||
      RuntimeResource.handle == NULL ||
      !AdmissionUmdDescribePrimary(CreateResource, &description)) {
    if (CreateResource != NULL) {
      UINT values[8] = {
          (UINT)CreateResource->Format,
          CreateResource->pPrimaryDesc != NULL ? 1u : 0u,
          CreateResource->BindFlags,
          CreateResource->MiscFlags,
          CreateResource->pMipInfoList != NULL
              ? CreateResource->pMipInfoList[0].TexelWidth : 0u,
          CreateResource->pMipInfoList != NULL
              ? CreateResource->pMipInfoList[0].TexelHeight : 0u,
          CreateResource->SampleDesc.Count,
          CreateResource->ResourceDimension};
      AdmissionUmdDiagnostic("reject-primary", E_INVALIDARG, values,
                             ARRAYSIZE(values));
    }
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
  ZeroMemory(&resourceData, sizeof(resourceData));
  resourceData.Magic = ADMISSION_PRESENT_RESOURCE_MAGIC;
  resourceData.Version = ADMISSION_PRESENT_RESOURCE_VERSION;
  resourceData.Bytes = sizeof(resourceData);
  resourceData.Format = (UINT)CreateResource->Format;
  /* EXP1038: only scanout-capable (BGRA) resources are written primaries;
   * the KMD admits that flag for A8R8G8B8 GDI-surface allocations only. */
  resourceData.Flags = description.Displayable &&
      (CreateResource->pPrimaryDesc != NULL ||
       (CreateResource->MiscFlags &
        D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE) != 0u)
      ? ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY : 0u;
  if (CreateResource->pPrimaryDesc != NULL &&
      CreateResource->pPrimaryDesc->ModeDesc.RefreshRate.Denominator != 0u) {
    resourceData.RefreshNumerator =
        CreateResource->pPrimaryDesc->ModeDesc.RefreshRate.Numerator;
    resourceData.RefreshDenominator =
        CreateResource->pPrimaryDesc->ModeDesc.RefreshRate.Denominator;
  }
  allocate.pPrivateDriverData = &resourceData;
  allocate.PrivateDriverDataSize = sizeof(resourceData);
  allocationInfo.pPrivateDriverData = &description.Allocation;
  allocationInfo.PrivateDriverDataSize = sizeof(description.Allocation);
  allocationInfo.VidPnSourceId = 0u;
  allocationInfo.Flags.Primary = CreateResource->pPrimaryDesc != NULL;
  allocate.hResource = RuntimeResource.handle;
  allocate.NumAllocations = 1u;
  allocate.pAllocationInfo = &allocationInfo;
  result = device->KernelCallbacks->pfnAllocateCb(
      device->RuntimeDevice.handle, &allocate);
  AdmissionUmdDiagnostic("presentation-allocate",result,NULL,0u);
  {
    /* EXP1124 measurement (unbudgeted): which D3D resources the app creates
     * as presentation allocations, and how often (EXP1123: ~37/s, 41 ms). */
    UINT values[8] = {(UINT)CreateResource->Format, CreateResource->BindFlags,
        CreateResource->MiscFlags, CreateResource->pMipInfoList[0].TexelWidth,
        CreateResource->pMipInfoList[0].TexelHeight,
        CreateResource->pPrimaryDesc != NULL ? 1u : 0u,
        (UINT)allocationInfo.hAllocation, (UINT)allocate.hKMResource};
    AdmissionUmdDiagnostic("measure-present-resource", result, values,
                           (UINT)(sizeof(values) / sizeof(values[0])));
  }
  if (FAILED(result) || allocationInfo.hAllocation == 0u) {
    AdmissionUmdRetirementFree(retirement);
    AdmissionUmdSetError(device, FAILED(result) ? result : E_FAIL);
    return;
  }
  resource->Magic = ADMISSION_UMD_RESOURCE_MAGIC;
  resource->RuntimeResource = RuntimeResource;
  resource->KernelAllocation = allocationInfo.hAllocation;
  resource->DirectFlip = description;
  resource->WrittenPrimary =
      (resourceData.Flags & ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY) != 0u;
  resource->DxgiFormat = resourceData.Format;
  retirement->RuntimeResource = RuntimeResource.handle;
  retirement->KernelResource = allocate.hKMResource;
  retirement->KernelAllocation = allocationInfo.hAllocation;
  retirement->Origin = 1u;
  retirement->Primary = CreateResource->pPrimaryDesc != NULL;
  retirement->Shared =
      (CreateResource->MiscFlags & D3D10_DDI_RESOURCE_MISC_SHARED) != 0u;
  resource->Retirement = retirement;
}

VOID APIENTRY AdmissionUmdOpenResource(
    D3D10DDI_HDEVICE DeviceHandle,
    const D3D10DDIARG_OPENRESOURCE *OpenResource,
    D3D10DDI_HRESOURCE ResourceHandle,
    D3D10DDI_HRTRESOURCE RuntimeResource) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RESOURCE *resource =
      (ADMISSION_UMD_RESOURCE *)ResourceHandle.pDrvPrivate;
  const D3DDDI_OPENALLOCATIONINFO *info;
  const ADMISSION_ALLOCATION_DESCRIPTION *description;
  const ADMISSION_PRESENT_RESOURCE_DATA *resourceData = NULL;
  ADMISSION_UMD_RETIREMENT *retirement;
  if (OpenResource != NULL && OpenResource->NumAllocations != 0u &&
      OpenResource->pOpenAllocationInfo != NULL) {
    const D3DDDI_OPENALLOCATIONINFO *input = OpenResource->pOpenAllocationInfo;
    UINT values[8] = { OpenResource->NumAllocations,
                      input->PrivateDriverDataSize, 0u, 0u, 0u, 0u, 0u, 0u };
    if (input->pPrivateDriverData != NULL &&
        input->PrivateDriverDataSize == sizeof(ADMISSION_ALLOCATION_DESCRIPTION)) {
      const ADMISSION_ALLOCATION_DESCRIPTION *d =
          (const ADMISSION_ALLOCATION_DESCRIPTION *)input->pPrivateDriverData;
      values[2] = d->Format;values[3] = d->Width;values[4] = d->Height;
      values[5] = d->Pitch;values[6] = d->BytesPerPixel;values[7] = d->Type;
    }
    AdmissionUmdDiagnostic("presentation-open", S_OK, values, ARRAYSIZE(values));
  }
  if (device == NULL || resource == NULL || OpenResource == NULL ||
      RuntimeResource.handle == NULL ||
      OpenResource->NumAllocations != 1u ||
      OpenResource->pOpenAllocationInfo == NULL) {
    AdmissionUmdSetError(device, E_INVALIDARG);
    return;
  }
  if (OpenResource->PrivateDriverDataSize != 0u ||
      OpenResource->pPrivateDriverData != NULL) {
    if (OpenResource->PrivateDriverDataSize != sizeof(*resourceData) ||
        OpenResource->pPrivateDriverData == NULL ||
        !AdmissionPresentResourceDataValid(
            (const ADMISSION_PRESENT_RESOURCE_DATA *)
                OpenResource->pPrivateDriverData)) {
      AdmissionUmdSetError(device, E_INVALIDARG);
      return;
    }
    resourceData = (const ADMISSION_PRESENT_RESOURCE_DATA *)
        OpenResource->pPrivateDriverData;
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
      (description->Format != (UINT)D3DDDIFMT_A8R8G8B8 &&
       description->Format != (UINT)D3DDDIFMT_A8B8G8R8) ||
      description->Width > 8192u || description->Height > 8192u ||
      description->Type != (UINT)D3DKMDT_GDISURFACE_TEXTURE ||
      description->BytesPerPixel != 4u ||
      description->Size > 0x1000000ULL || info->hAllocation == 0u ||
      (resourceData != NULL && resourceData->Format != 0u &&
       !AdmissionUmdSharedFormatStorage(resourceData->Format,
                                        description->Format))) {
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
  resource->DirectFlip.Displayable =
      description->Format == (UINT)D3DDDIFMT_A8R8G8B8 ? 1u : 0u;
  resource->WrittenPrimary = resourceData != NULL &&
      (resourceData->Flags & ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY) != 0u;
  /* Resources created without our resource data (GDI, earlier packages)
   * keep the format their storage implies. */
  resource->DxgiFormat = resourceData != NULL && resourceData->Format != 0u
      ? resourceData->Format
      : description->Format == (UINT)D3DDDIFMT_A8R8G8B8
          ? (UINT)DXGI_FORMAT_B8G8R8A8_UNORM : (UINT)DXGI_FORMAT_R8G8B8A8_UNORM;
  retirement->RuntimeResource = RuntimeResource.handle;
  retirement->KernelResource = OpenResource->hKMResource.handle;
  retirement->KernelAllocation = info->hAllocation;
  retirement->Origin = 2u;
  retirement->Primary = FALSE;
  retirement->Shared = TRUE;
  resource->Retirement = retirement;
}

/* EXP975: this is a D3D10-DDI driver without free threading, so the runtime
 * frees hRTResource when DestroyResource returns. A deallocation that names
 * the runtime resource (opened, shared, primary, or KM-resource allocations)
 * must therefore run before DestroyResource returns; only HandleList
 * deallocations may be deferred. */
static BOOL AdmissionUmdRetirementNeedsRuntimeResource(
    const ADMISSION_UMD_RETIREMENT *Retirement) {
  return Retirement->Origin == 2u || Retirement->Primary ||
      Retirement->Shared || Retirement->KernelResource != 0u;
}

HRESULT AdmissionUmdReleaseRuntimeResource(
    D3D10DDI_HDEVICE DeviceHandle, D3D10DDI_HRESOURCE ResourceHandle) {
  ADMISSION_UMD_DEVICE *device = AdmissionUmdDeviceFromHandle(DeviceHandle);
  ADMISSION_UMD_RESOURCE *resource =
      AdmissionUmdResourceFromHandle(ResourceHandle);
  ADMISSION_UMD_RETIREMENT *retirement;
  HRESULT result;
  if (device == NULL || resource == NULL)
    return E_INVALIDARG;
  retirement = resource->Retirement;
  if (retirement == NULL ||
      !AdmissionUmdRetirementNeedsRuntimeResource(retirement))
    return S_OK;
  /* The runtime handle dies with this call either way: never retain it. */
  resource->Retirement = NULL;
  result = AdmissionUmdRetirementDeallocate(&device->Retirement, retirement);
  AdmissionUmdRetirementFree(retirement);
  return result;
}

VOID APIENTRY AdmissionUmdDestroyResource(
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
  {
    UINT values[4] = {(UINT)retirement->KernelAllocation, retirement->Origin,
        retirement->Shared ? 1u : 0u, retirement->Primary ? 1u : 0u};
    AdmissionUmdDiagnostic("measure-present-destroy", S_OK, values,
                           (UINT)(sizeof(values) / sizeof(values[0])));
  }
  if (AdmissionUmdRetirementNeedsRuntimeResource(retirement)) {
    result = AdmissionUmdRetirementDeallocate(&device->Retirement, retirement);
    AdmissionUmdRetirementFree(retirement);
    if (FAILED(result))
      AdmissionUmdSetError(device, result);
    return;
  }
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
  BOOL consumed = FALSE;
  HRESULT result = AdmissionUmdRuntimeDeviceFinalize(device, &consumed);
  {
    UINT values[1] = {(UINT)(ULONG_PTR)device};
    AdmissionUmdDiagnostic("device-destroy", result, values, 1u);
  }
  if (FAILED(result) && !consumed)
    AdmissionUmdSetError(device, result);
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

HRESULT AdmissionUmdSubmitPresent(ADMISSION_UMD_DEVICE *Device,
                                         ADMISSION_UMD_RESOURCE *Source,
                                         PVOID DxgiContext) {
  DXGIDDICB_PRESENT present;
  if (Device == NULL || !AdmissionUmdResourceIsPresentable(Source) ||
      Device->DxgiCallbacks == NULL ||
      Device->DxgiCallbacks->pfnPresentCb == NULL ||
      Device->KernelContext == NULL) {
    const ADMISSION_UMD_RESOURCE *resource=Device?Source:NULL;
    const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *flip=
        resource?&resource->DirectFlip:NULL;
    const ADMISSION_ALLOCATION_DESCRIPTION *allocation=
        flip?&flip->Allocation:NULL;
    UINT mask=(Device!=NULL) |
        ((UINT)(resource!=NULL)<<1) |
        ((UINT)(resource && resource->Magic==ADMISSION_UMD_RESOURCE_MAGIC)<<2) |
        ((UINT)(resource && resource->KernelAllocation!=0u)<<3) |
        ((UINT)(flip && flip->Magic==ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC)<<4) |
        ((UINT)(flip && flip->Version==ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION)<<5) |
        ((UINT)(flip && flip->SegmentId==2u)<<6) |
        ((UINT)(flip && flip->Linear==1u)<<7) |
        ((UINT)(flip && flip->Reserved==0u)<<8) |
        ((UINT)(allocation && AdmissionAllocationDescriptionValid(allocation))<<9) |
        ((UINT)(allocation && allocation->Width==2560u && allocation->Height==1600u)<<10) |
        ((UINT)(allocation && allocation->Pitch==10240u &&
                allocation->BytesPerPixel==4u && allocation->Size==0xfa0000ULL)<<11) |
        ((UINT)(allocation && allocation->CpuVisible==0u)<<12) |
        ((UINT)(allocation &&
            ((allocation->Format==(UINT)D3DDDIFMT_A8R8G8B8 && flip->Displayable==1u) ||
             (allocation->Format==(UINT)D3DDDIFMT_A8B8G8R8 && flip->Displayable==0u)))<<13) |
        ((UINT)(Device && Device->DxgiCallbacks &&
                Device->DxgiCallbacks->pfnPresentCb)<<14) |
        ((UINT)(Device && Device->KernelContext)<<15);
    UINT guard[14]={mask,resource?(UINT)resource->KernelAllocation:0u,
        flip?flip->Magic:0u,flip?flip->Version:0u,
        flip?flip->SegmentId:0u,flip?flip->Linear:0u,
        flip?flip->Displayable:0u,allocation?allocation->Format:0u,
        allocation?allocation->Width:0u,allocation?allocation->Height:0u,
        allocation?allocation->Pitch:0u,allocation?(UINT)allocation->Size:0u,
        (UINT)(ULONG_PTR)DxgiContext,
        (UINT)((ULONGLONG)(ULONG_PTR)DxgiContext>>32)};
    AdmissionUmdPresentMeasure(AdmissionUmdMeasurePresentGuard,E_INVALIDARG,
                               guard,ARRAYSIZE(guard));
    AdmissionUmdPresentMeasure(AdmissionUmdMeasurePresentCallbackReturn,E_INVALIDARG,NULL,0u);
    return E_INVALIDARG;
  }
  ZeroMemory(&present, sizeof(present));
  present.hSrcAllocation = Source->KernelAllocation;
  present.pDXGIContext = DxgiContext;
  present.hContext = Device->KernelContext;
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  ULONGLONG canonicalVa = AdmissionUmdGpuvaFrameArm(Device,
      Source->KernelAllocation, 0ULL);
  static volatile LONG presentReceipts;
  LONG ordinal = InterlockedIncrement(&presentReceipts);
  UINT ticket[16] = {(UINT)ordinal, (UINT)present.hSrcAllocation,
      (UINT)(ULONG_PTR)present.hContext,
      (UINT)((ULONGLONG)(ULONG_PTR)present.hContext >> 32),
      (UINT)canonicalVa, (UINT)(canonicalVa >> 32),
      (UINT)Device->FrameSubmittedFence,
      (UINT)(Device->FrameSubmittedFence >> 32),
      (UINT)Device->FrameCompletedFence,
      (UINT)(Device->FrameCompletedFence >> 32),
      (UINT)Device->FrameSubmitStatus,
      (UINT)Device->NextRenderFence,
      (UINT)(ULONG_PTR)present.pDXGIContext,
      (UINT)((ULONGLONG)(ULONG_PTR)present.pDXGIContext >> 32),
      (UINT)Device->DrawTerminal, (UINT)Device->RenderSyncObject};
  if (ordinal <= 16)
    AdmissionUmdDiagnostic("measure-present-before", S_OK, ticket,
                           ARRAYSIZE(ticket));
  if (ordinal <= 16) {
    UINT primary[6] = {(UINT)ordinal, (UINT)present.hSrcAllocation,
        Source->WrittenPrimary ? 1u : 0u, (UINT)canonicalVa,
        (UINT)(canonicalVa >> 32), (UINT)Device->FrameSubmittedFence};
    AdmissionUmdDiagnostic("measure-present-primary", S_OK, primary,
                           ARRAYSIZE(primary));
  }
#endif
  AdmissionUmdPresentMeasure(AdmissionUmdMeasurePresentCallbackEnter,S_OK,NULL,0u);
  HRESULT result=Device->DxgiCallbacks->pfnPresentCb(
      Device->RuntimeDevice.handle, &present);
#if defined(APPLE_AGX_EXP907_FRAME_RECEIPT)
  if (ordinal <= 16)
    AdmissionUmdDiagnostic("measure-present-after", result, ticket,
                           ARRAYSIZE(ticket));
#endif
  UINT values[4]={(UINT)present.hSrcAllocation,
      (UINT)(ULONG_PTR)present.hContext,
      (UINT)(ULONG_PTR)present.pDXGIContext,
      (UINT)((ULONGLONG)(ULONG_PTR)present.pDXGIContext>>32)};
  AdmissionUmdDiagnostic("present-callback",result,values,ARRAYSIZE(values));
  AdmissionUmdPresentMeasure(AdmissionUmdMeasurePresentCallbackReturn,result,
                             values,ARRAYSIZE(values));
  return result;
}

static HRESULT APIENTRY AdmissionUmdPresent(DXGI_DDI_ARG_PRESENT *Args) {
  ADMISSION_UMD_DEVICE *device;
  ADMISSION_UMD_RESOURCE *source;
  AdmissionUmdPresentMeasure(AdmissionUmdMeasureLegacyPresentEntry,S_OK,NULL,0u);
  if (Args == NULL) return E_INVALIDARG;
  UINT values[4]={(UINT)Args->Flags.Value,(UINT)Args->FlipInterval,
      (UINT)Args->SrcSubResourceIndex,(UINT)(ULONG_PTR)Args->hDstResource};
  AdmissionUmdDiagnostic("present-entry",S_OK,values,ARRAYSIZE(values));
  if (Args->hDstResource != 0u ||
      Args->SrcSubResourceIndex != 0u ||
      (Args->Flags.Value != 0x1u && Args->Flags.Value != 0x2u) ||
      (Args->FlipInterval != DXGI_DDI_FLIP_INTERVAL_IMMEDIATE &&
       Args->FlipInterval != DXGI_DDI_FLIP_INTERVAL_ONE))
    return E_INVALIDARG;
  device = AdmissionUmdDeviceFromDxgi(Args->hDevice);
  source = AdmissionUmdResourceFromDxgi(Args->hSurfaceToPresent);
  return AdmissionUmdSubmitPresent(device, source, Args->pDXGIContext);
}

static HRESULT APIENTRY AdmissionUmdPresent1(DXGI_DDI_ARG_PRESENT1 *Args) {
  ADMISSION_UMD_DEVICE *device;
  ADMISSION_UMD_RESOURCE *source;
  AdmissionUmdPresentMeasure(AdmissionUmdMeasureLegacyPresent1Entry,S_OK,NULL,0u);
  if (Args == NULL || Args->hDstResource != 0u ||
      Args->SurfacesToPresent != 1u || Args->phSurfacesToPresent == NULL ||
      Args->phSurfacesToPresent[0].SubResourceIndex != 0u ||
      (Args->Flags.Value != 0x1u && Args->Flags.Value != 0x2u) ||
      (Args->FlipInterval != DXGI_DDI_FLIP_INTERVAL_IMMEDIATE &&
       Args->FlipInterval != DXGI_DDI_FLIP_INTERVAL_ONE) ||
      Args->Reserved != 0u || Args->DirtyRects != 0u)
    return E_INVALIDARG;
  device = AdmissionUmdDeviceFromDxgi(Args->hDevice);
  source = AdmissionUmdResourceFromDxgi(
      Args->phSurfacesToPresent[0].hSurface);
  return AdmissionUmdSubmitPresent(device, source, Args->pDXGIContext);
}

HRESULT APIENTRY AdmissionUmdSetDisplayMode(
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
