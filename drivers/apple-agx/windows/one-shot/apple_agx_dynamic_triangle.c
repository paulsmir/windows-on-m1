#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <wchar.h>

#include "render_allocation.h"
#include "render_win32_transport.h"
#include "render_qualification.h"
#include "agx_win32_transport.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define ALLOCATION_COUNT 10u
#define REFERENCE_COUNT 11u
#define RELOCATION_COUNT 7u
#define INTERNAL_BYTES 0x4000u
#define FRAMEBUFFER_BYTES (2560u * 1600u * 4u)
#define BACKGROUND_COLOR 0xff101820u

typedef NTSTATUS(WINAPI *PFN_LOCAL_ENUMADAPTERS3)(D3DKMT_ENUMADAPTERS3 *);

typedef struct _ASSET {
  BYTE *Bytes;
  UINT Size;
} ASSET;

static UINT Align4(UINT value) { return (value + 3u) & ~3u; }

static BOOL LoadAsset(const wchar_t *Directory, const wchar_t *Name,
                      ASSET *Asset) {
  wchar_t path[MAX_PATH];
  HANDLE file;
  LARGE_INTEGER size;
  DWORD read = 0u;
  if (Directory == NULL || Name == NULL || Asset == NULL ||
      swprintf_s(path, ARRAYSIZE(path), L"%ls\\%ls", Directory, Name) < 0)
    return FALSE;
  ZeroMemory(Asset, sizeof(*Asset));
  file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                     FILE_ATTRIBUTE_NORMAL, NULL);
  if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) ||
      size.QuadPart <= 0 || size.QuadPart > INTERNAL_BYTES) {
    if (file != INVALID_HANDLE_VALUE)
      CloseHandle(file);
    return FALSE;
  }
  Asset->Size = Align4((UINT)size.QuadPart);
  Asset->Bytes = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                   Asset->Size);
  if (Asset->Bytes == NULL ||
      !ReadFile(file, Asset->Bytes, (DWORD)size.QuadPart, &read, NULL) ||
      read != (DWORD)size.QuadPart) {
    if (Asset->Bytes != NULL)
      HeapFree(GetProcessHeap(), 0u, Asset->Bytes);
    ZeroMemory(Asset, sizeof(*Asset));
    CloseHandle(file);
    return FALSE;
  }
  CloseHandle(file);
  return TRUE;
}

static NTSTATUS CreateAllocation(
    D3DKMT_HANDLE Device, const void *PrivateData, UINT PrivateBytes,
    D3DKMT_HANDLE *Handle) {
  D3DDDI_ALLOCATIONINFO info = {0};
  D3DKMT_CREATEALLOCATION create = {0};
  NTSTATUS status;
  info.pPrivateDriverData = (void *)PrivateData;
  info.PrivateDriverDataSize = PrivateBytes;
  create.hDevice = Device;
  create.NumAllocations = 1u;
  create.pAllocationInfo = &info;
  status = D3DKMTCreateAllocation(&create);
  if (NT_SUCCESS(status) && info.hAllocation != 0u)
    *Handle = info.hAllocation;
  return status;
}

static BOOL MakeInternalDescription(UINT ClassId,
                                    ADMISSION_WIN32_ALLOCATION_CREATE *Create) {
  ZeroMemory(Create, sizeof(*Create));
  if (!AdmissionAllocationDescribe(
          INTERNAL_BYTES, 1u, 1u,
          ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
          ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 1u, &Create->Allocation))
    return FALSE;
  Create->Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  Create->Version = ADMISSION_WIN32_ALLOCATION_VERSION;
  Create->Bytes = sizeof(*Create);
  Create->ClassId = ClassId;
  Create->Flags = AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  return TRUE;
}

static NTSTATUS Upload(D3DKMT_HANDLE Device, D3DKMT_HANDLE Allocation,
                       const ASSET *Asset) {
  D3DKMT_LOCK2 lock = {0};
  D3DKMT_UNLOCK2 unlock = {0};
  NTSTATUS status;
  lock.hDevice = Device;
  lock.hAllocation = Allocation;
  status = D3DKMTLock2(&lock);
  if (!NT_SUCCESS(status) || lock.pData == NULL)
    return NT_SUCCESS(status) ? (NTSTATUS)0xc0000001L : status;
  ZeroMemory(lock.pData, INTERNAL_BYTES);
  if (Asset != NULL && Asset->Bytes != NULL && Asset->Size != 0u)
    CopyMemory(lock.pData, Asset->Bytes, Asset->Size);
  unlock.hDevice = Device;
  unlock.hAllocation = Allocation;
  status = D3DKMTUnlock2(&unlock);
  return status;
}

static void SetReference(APPLE_AGX_WIN32_ALLOCATION_REFERENCE *Reference,
                         UINT AllocationIndex, UINT Role, UINT Access,
                         ULONGLONG Offset, ULONGLONG Bytes) {
  ZeroMemory(Reference, sizeof(*Reference));
  Reference->AllocationIndex = AllocationIndex;
  Reference->Role = Role;
  Reference->Access = Access;
  Reference->Offset = Offset;
  Reference->Bytes = Bytes;
}

static NTSTATUS QueryPresentation(D3DKMT_HANDLE Adapter,
                                  D3DKMT_HANDLE Device,
                                  D3DKMT_HANDLE Context,
                                  ADMISSION_PRESENT_QUERY *Result) {
  ULONGLONG deadline = GetTickCount64() + 20000u;
  NTSTATUS status = (NTSTATUS)0x00000103L;
  do {
    D3DKMT_ESCAPE escape = {0};
    ZeroMemory(Result, sizeof(*Result));
    Result->Magic = ADMISSION_PRESENT_QUERY_MAGIC;
    Result->Version = ADMISSION_PRESENT_QUERY_VERSION;
    Result->Index = 0u;
    escape.hAdapter = Adapter;
    escape.hDevice = Device;
    escape.hContext = Context;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = Result;
    escape.PrivateDriverDataSize = sizeof(*Result);
    status = D3DKMTEscape(&escape);
    if (!NT_SUCCESS(status))
      break;
    if (Result->Valid == 1u && Result->Status == 0u &&
        Result->Purpose == AdmissionPresentPurposeRenderFrame &&
        Result->PixelsExpected == 4096000u &&
        Result->PixelsVerified == 4096000u &&
        Result->ExpectedColor != BACKGROUND_COLOR &&
        Result->ContentHash != 0ULL)
      return (NTSTATUS)0;
    Sleep(1u);
  } while (GetTickCount64() < deadline);
  return NT_SUCCESS(status) ? (NTSTATUS)0x00000102L : status;
}

int __cdecl wmain(int argc, wchar_t **argv) {
  static const wchar_t *assetNames[7] = {
      L"vertex.bin", L"fragment-linked.bin", L"fragment.bin",
      L"pipeline.bin", L"encoder.bin", L"scissor.bin", L"depth_bias.bin"};
  ASSET assets[7] = {0};
  D3DKMT_ENUMADAPTERS3 enumeration = {0};
  D3DKMT_ADAPTERINFO adapters[16] = {0};
  D3DKMT_ADAPTERTYPE adapterType = {0};
  D3DKMT_QUERYADAPTERINFO query = {0};
  D3DKMT_CREATEDEVICE device = {0};
  D3DKMT_CREATEPAGINGQUEUE paging = {0};
  D3DKMT_CREATECONTEXT context = {0};
  ADMISSION_WIN32_CONTEXT_CREATE contextPrivate = {0};
  ADMISSION_WIN32_ALLOCATION_CREATE internal = {0};
  ADMISSION_ALLOCATION_DESCRIPTION surface = {0};
  D3DKMT_HANDLE allocations[ALLOCATION_COUNT] = {0};
  UINT priorities[ALLOCATION_COUNT];
  D3DDDI_MAKERESIDENT resident = {0};
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[REFERENCE_COUNT];
  APPLE_AGX_WIN32_RELOCATION relocations[RELOCATION_COUNT];
  AGX_WIN32_DRAW_REQUEST request;
  D3DKMT_RENDER render = {0};
  ADMISSION_PRESENT_QUERY presentation;
  ADMISSION_PRESENT_QUERY held;
  ADMISSION_RETIREMENT_QUERY retirement = {0};
  HMODULE gdi;
  PFN_LOCAL_ENUMADAPTERS3 enumerate;
  ULONG selected = 16u;
  ULONG matches = 0u;
  NTSTATUS status = (NTSTATUS)0xc0000001L;
  UINT commandBytes = 0u;
  int result = 1;
  if (argc != 2) {
    fwprintf(stderr, L"usage: AppleAgxDynamicTriangle.exe <asset-directory>\n");
    return 2;
  }
  (void)setvbuf(stdout, NULL, _IONBF, 0);
  for (UINT index = 0u; index < ARRAYSIZE(assets); ++index)
    if (!LoadAsset(argv[1], assetNames[index], &assets[index])) {
      fwprintf(stderr, L"ASSET_FAIL name=%ls error=%lu\n",
               assetNames[index], GetLastError());
      goto Cleanup;
    }
  gdi = GetModuleHandleW(L"gdi32.dll");
  enumerate = gdi == NULL ? NULL : (PFN_LOCAL_ENUMADAPTERS3)GetProcAddress(
      gdi, "D3DKMTEnumAdapters3");
  if (enumerate == NULL)
    goto Cleanup;
  enumeration.NumAdapters = ARRAYSIZE(adapters);
  enumeration.pAdapters = adapters;
  status = enumerate(&enumeration);
  if (!NT_SUCCESS(status))
    goto Cleanup;
  for (ULONG index = 0u; index < enumeration.NumAdapters; ++index) {
    ZeroMemory(&adapterType, sizeof(adapterType));
    ZeroMemory(&query, sizeof(query));
    query.hAdapter = adapters[index].hAdapter;
    query.Type = KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData = &adapterType;
    query.PrivateDriverDataSize = sizeof(adapterType);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)) &&
        adapterType.RenderSupported && adapterType.DisplaySupported &&
        adapterType.PostDevice && !adapterType.SoftwareDevice &&
        !adapterType.ComputeOnly) {
      selected = index;
      ++matches;
    }
  }
  if (matches != 1u || selected >= enumeration.NumAdapters)
    goto Cleanup;
  device.hAdapter = adapters[selected].hAdapter;
  status = D3DKMTCreateDevice(&device);
  if (!NT_SUCCESS(status))
    goto Cleanup;
  paging.hDevice = device.hDevice;
  paging.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
  status = D3DKMTCreatePagingQueue(&paging);
  if (!NT_SUCCESS(status))
    goto Cleanup;
  contextPrivate.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
  contextPrivate.Version = ADMISSION_WIN32_CONTEXT_VERSION;
  contextPrivate.Bytes = sizeof(contextPrivate);
  contextPrivate.Generation = 0x06640001u;
  context.hDevice = device.hDevice;
  context.NodeOrdinal = 0u;
  context.EngineAffinity = 1u;
  context.ClientHint = D3DKMT_CLIENTHINT_UNKNOWN;
  context.pPrivateDriverData = &contextPrivate;
  context.PrivateDriverDataSize = sizeof(contextPrivate);
  status = D3DKMTCreateContext(&context);
  if (!NT_SUCCESS(status) || context.pCommandBuffer == NULL ||
      context.CommandBufferSize < 1024u || context.pAllocationList == NULL ||
      context.AllocationListSize < ALLOCATION_COUNT ||
      context.pPatchLocationList == NULL)
    goto Cleanup;
  if (!AdmissionAllocationDescribe(
          2560u, 1600u, 4u, D3DKMDT_GDISURFACE_TEXTURE,
          D3DDDIFMT_A8R8G8B8, 0u, &surface))
    goto Cleanup;
  status = CreateAllocation(device.hDevice, &surface, sizeof(surface),
                            &allocations[0]);
  if (!NT_SUCCESS(status))
    goto Cleanup;
  for (UINT index = 1u; index < ALLOCATION_COUNT; ++index) {
    UINT classId = index == 1u ? AgxWin32BufferClassGeneral :
                   ((index == 2u || index == 3u || index == 9u)
                        ? AgxWin32BufferClassShader
                        : AgxWin32BufferClassEncoder);
    if (!MakeInternalDescription(classId, &internal))
      goto Cleanup;
    status = CreateAllocation(device.hDevice, &internal, sizeof(internal),
                              &allocations[index]);
    if (!NT_SUCCESS(status))
      goto Cleanup;
  }
  for (UINT index = 0u; index < ALLOCATION_COUNT; ++index)
    priorities[index] = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  resident.hPagingQueue = paging.hPagingQueue;
  resident.NumAllocations = ALLOCATION_COUNT;
  resident.AllocationList = allocations;
  resident.PriorityList = priorities;
  status = D3DKMTMakeResident(&resident);
  if (!NT_SUCCESS(status) || resident.NumAllocations != ALLOCATION_COUNT)
    goto Cleanup;
  {
    volatile UINT64 *pagingFence =
        (volatile UINT64 *)paging.FenceValueCPUVirtualAddress;
    const ULONGLONG pagingDeadline = GetTickCount64() + 15000u;
    while (*pagingFence < resident.PagingFenceValue &&
           GetTickCount64() < pagingDeadline)
      Sleep(1u);
    if (*pagingFence < resident.PagingFenceValue) {
      status = (NTSTATUS)0x00000102L;
      printf("MakeResident paging fence timeout: expected=%llu actual=%llu\n",
             (unsigned long long)resident.PagingFenceValue,
             (unsigned long long)*pagingFence);
      goto Cleanup;
    }
  }
  status = Upload(device.hDevice, allocations[1], NULL);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[2], &assets[0]);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[3], &assets[1]);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[4], &assets[3]);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[5], NULL);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[6], &assets[5]);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[7], &assets[6]);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[8], &assets[4]);
  if (!NT_SUCCESS(status)) goto Cleanup;
  status = Upload(device.hDevice, allocations[9], &assets[2]);
  if (!NT_SUCCESS(status)) goto Cleanup;

  ZeroMemory(references, sizeof(references));
  SetReference(&references[0], 0u, AppleAgxWin32RoleRenderTarget,
               AppleAgxWin32AccessWrite, 0u, FRAMEBUFFER_BYTES);
  SetReference(&references[1], 1u, AppleAgxWin32RoleVertex,
               AppleAgxWin32AccessRead, 0u, 4u);
  SetReference(&references[2], 2u, AppleAgxWin32RoleShader,
               AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
               0u, assets[0].Size);
  SetReference(&references[3], 3u, AppleAgxWin32RoleShader,
               AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
               0u, assets[1].Size);
  SetReference(&references[4], 4u, AppleAgxWin32RoleUscPipeline,
               AppleAgxWin32AccessRead, 0u, assets[3].Size);
  SetReference(&references[5], 5u, AppleAgxWin32RoleDescriptor,
               AppleAgxWin32AccessRead, 0u, 4u);
  SetReference(&references[6], 6u, AppleAgxWin32RoleScissor,
               AppleAgxWin32AccessRead, 0u, assets[5].Size);
  SetReference(&references[7], 7u, AppleAgxWin32RoleDepthBias,
               AppleAgxWin32AccessRead, 0u, assets[6].Size);
  SetReference(&references[8], 8u, AppleAgxWin32RoleEncoder,
               AppleAgxWin32AccessRead, 0u, assets[4].Size);
  SetReference(&references[9], 2u, AppleAgxWin32RoleShaderRodata,
               AppleAgxWin32AccessRead, 0u, 8u);
  SetReference(&references[10], 9u, AppleAgxWin32RoleShaderRodata,
               AppleAgxWin32AccessRead, 0u, 12u);
  ZeroMemory(relocations, sizeof(relocations));
#define RELOC(i, kind, width, dst, target, dstoff, targetoff)                \
  relocations[i] = (APPLE_AGX_WIN32_RELOCATION){                             \
      kind, width, 0u, dst, target, dstoff, targetoff, 0ULL}
  RELOC(0, AppleAgxWin32RelocationUscBufferAddress40, 8u, 4u, 9u, 4u, 0u);
  RELOC(1, AppleAgxWin32RelocationUscShaderOffset32, 6u, 4u, 2u, 12u, 128u);
  RELOC(2, AppleAgxWin32RelocationUscBufferAddress40, 8u, 4u, 10u, 64u, 0u);
  RELOC(3, AppleAgxWin32RelocationUscShaderOffset32, 6u, 4u, 3u, 76u, 0u);
  RELOC(4, AppleAgxWin32RelocationVdmPipelineOffset32, 4u, 8u, 4u, 8u, 0u);
  RELOC(5, AppleAgxWin32RelocationPppStateAddress40, 8u, 8u, 8u, 24u, 128u);
  RELOC(6, AppleAgxWin32RelocationVdmPipelineOffset32, 4u, 8u, 4u, 212u, 64u);
#undef RELOC
  ZeroMemory(&request, sizeof(request));
  request.Generation = contextPrivate.Generation;
  request.AllocationCount = ALLOCATION_COUNT;
  request.ReferenceCount = REFERENCE_COUNT;
  request.RelocationCount = RELOCATION_COUNT;
  request.References = references;
  request.Relocations = relocations;
  request.Draw.Format = AppleAgxWin32FormatBgra8Unorm;
  request.Draw.SurfaceWidth = 2560u;
  request.Draw.SurfaceHeight = 1600u;
  request.Draw.SurfacePitch = 10240u;
  request.Draw.Topology = AppleAgxWin32TopologyTriangleList;
  request.Draw.VertexCount = 3u;
  request.Draw.InstanceCount = 1u;
  request.Draw.DestinationReference = 0u;
  request.Draw.VertexReference = 1u;
  request.Draw.IndexReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  request.Draw.ConstantReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  request.Draw.TextureReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  request.Draw.VertexShaderReference = 2u;
  request.Draw.FragmentShaderReference = 3u;
  request.Draw.VertexRodataReference = 9u;
  request.Draw.FragmentRodataReference = 10u;
  request.Draw.UscPipelineReference = 4u;
  request.Draw.DescriptorReference = 5u;
  request.Draw.ScissorReference = 6u;
  request.Draw.DepthBiasReference = 7u;
  request.Draw.EncoderReference = 8u;
  if (AgxWin32TransportBuildDraw(&request, context.pCommandBuffer,
                                 context.CommandBufferSize,
                                 &commandBytes) != AppleAgxWin32AbiSuccess)
    goto Cleanup;
  ZeroMemory(context.pAllocationList,
             ALLOCATION_COUNT * sizeof(context.pAllocationList[0]));
  for (UINT index = 0u; index < ALLOCATION_COUNT; ++index) {
    context.pAllocationList[index].hAllocation = allocations[index];
    context.pAllocationList[index].WriteOperation = index == 0u ? 1u : 0u;
  }
  ZeroMemory(&render, sizeof(render));
  render.hContext = context.hContext;
  render.CommandLength = commandBytes;
  render.AllocationCount = ALLOCATION_COUNT;
  wprintf(L"PHASE DYNAMIC_RENDER_BEGIN bytes=%u refs=%u allocations=%u\n",
          commandBytes, REFERENCE_COUNT, ALLOCATION_COUNT);
  status = D3DKMTRender(&render);
  wprintf(L"PHASE DYNAMIC_RENDER_END status=0x%08lx queued=%u\n",
          (ULONG)status, render.QueuedBufferCount);
  if (!NT_SUCCESS(status))
    goto Preserve;
  status = QueryPresentation(adapters[selected].hAdapter, device.hDevice,
                             context.hContext, &presentation);
  wprintf(L"DYNAMIC_PRESENT status=0x%08lx valid=%u result=0x%08x "
          L"fence=%u color=0x%08x pixels=%u/%u hash=0x%llx "
          L"sequence=%llu physical=0x%llx\n",
          (ULONG)status, presentation.Valid, presentation.Status,
          presentation.Fence, presentation.ExpectedColor,
          presentation.PixelsVerified, presentation.PixelsExpected,
          presentation.ContentHash, presentation.Sequence,
          presentation.PhysicalAddress);
  if (!NT_SUCCESS(status))
    goto Preserve;
  Sleep(15000u);
  status = QueryPresentation(adapters[selected].hAdapter, device.hDevice,
                             context.hContext, &held);
  if (!NT_SUCCESS(status) || memcmp(&presentation, &held, sizeof(held)) != 0)
    goto Preserve;
  wprintf(L"PHASE DYNAMIC_HOLD_PASS duration_ms=15000\n");
  retirement.Magic = ADMISSION_RETIREMENT_QUERY_MAGIC;
  retirement.Version = ADMISSION_RETIREMENT_QUERY_VERSION;
  retirement.Command = AdmissionRetirementCommandExecute;
  {
    D3DKMT_ESCAPE escape = {0};
    escape.hAdapter = adapters[selected].hAdapter;
    escape.hDevice = device.hDevice;
    escape.hContext = context.hContext;
    escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    escape.pPrivateDriverData = &retirement;
    escape.PrivateDriverDataSize = sizeof(retirement);
    status = D3DKMTEscape(&escape);
  }
  wprintf(L"DYNAMIC_RETIRE status=0x%08lx result=0x%08x valid=%u "
          L"sequence=%llu allocation=0x%llx\n",
          (ULONG)status, retirement.Status, retirement.Valid,
          retirement.Sequence, retirement.AllocationToken);
  if (!NT_SUCCESS(status) || retirement.Status != 0u ||
      retirement.Valid != 1u ||
      retirement.Sequence <= presentation.Sequence)
    goto Preserve;
  result = 0;
  goto Cleanup;

Preserve:
  wprintf(L"PHASE DYNAMIC_PRESERVE status=0x%08lx\n", (ULONG)status);
  for (;;) Sleep(1000u);

Cleanup:
  if (allocations[0] != 0u) {
    D3DKMT_DESTROYALLOCATION2 destroy = {0};
    UINT count = 0u;
    while (count < ALLOCATION_COUNT && allocations[count] != 0u)
      ++count;
    destroy.hDevice = device.hDevice;
    destroy.phAllocationList = allocations;
    destroy.AllocationCount = count;
    destroy.Flags.SynchronousDestroy = 1u;
    if (count != 0u && !NT_SUCCESS(D3DKMTDestroyAllocation2(&destroy)))
      result = 1;
  }
  if (context.hContext != 0u) {
    D3DKMT_DESTROYCONTEXT destroy = {0};
    destroy.hContext = context.hContext;
    if (!NT_SUCCESS(D3DKMTDestroyContext(&destroy))) result = 1;
  }
  if (paging.hPagingQueue != 0u) {
    D3DDDI_DESTROYPAGINGQUEUE destroy = {0};
    destroy.hPagingQueue = paging.hPagingQueue;
    if (!NT_SUCCESS(D3DKMTDestroyPagingQueue(&destroy))) result = 1;
  }
  if (device.hDevice != 0u) {
    D3DKMT_DESTROYDEVICE destroy = {0};
    destroy.hDevice = device.hDevice;
    if (!NT_SUCCESS(D3DKMTDestroyDevice(&destroy))) result = 1;
  }
  for (ULONG index = 0u; index < enumeration.NumAdapters; ++index) {
    if (adapters[index].hAdapter != 0u) {
      D3DKMT_CLOSEADAPTER close = {0};
      close.hAdapter = adapters[index].hAdapter;
      if (!NT_SUCCESS(D3DKMTCloseAdapter(&close))) result = 1;
    }
  }
  for (UINT index = 0u; index < ARRAYSIZE(assets); ++index)
    if (assets[index].Bytes != NULL)
      HeapFree(GetProcessHeap(), 0u, assets[index].Bytes);
  wprintf(L"RESULT=%d\n", result);
  return result;
}
