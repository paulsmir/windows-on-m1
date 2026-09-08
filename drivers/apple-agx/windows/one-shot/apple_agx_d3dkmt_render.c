#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <wchar.h>

#include "render_allocation.h"
#include "render_umd_command.h"
#include "apple_agx_exp208_gdi.h"
#include "apple_agx_scanout.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

int __cdecl wmain(int argc, wchar_t **argv) {
  D3DKMT_ENUMADAPTERS3 enumeration = {0};
  D3DKMT_ADAPTERINFO adapters[MAX_ENUM_ADAPTERS] = {0};
  D3DKMT_ADAPTERTYPE adapterType = {0};
  D3DKMT_QUERYADAPTERINFO query = {0};
  D3DKMT_CREATEDEVICE createDevice = {0};
  D3DKMT_CREATEPAGINGQUEUE createPagingQueue = {0};
  D3DKMT_CREATECONTEXT createContext = {0};
  D3DKMT_CREATEALLOCATION createAllocation = {0};
  D3DDDI_ALLOCATIONINFO allocationInfo[2] = {0};
  ADMISSION_ALLOCATION_DESCRIPTION allocation[2] = {0};
  ADMISSION_UMD_COLOR_FILL_COMMAND command = {0};
  D3DKMT_RENDER render = {0};
  D3DKMT_ESCAPE escape = {0};
  D3DKMT_TDRDBGCTRL_ESCAPE tdr = {0};
  D3DDDI_MAKERESIDENT makeResident = {0};
  D3DKMT_DESTROYALLOCATION2 destroy = {0};
  D3DKMT_DESTROYCONTEXT destroyContext = {0};
  D3DKMT_DESTROYDEVICE destroyDevice = {0};
  D3DDDI_DESTROYPAGINGQUEUE destroyPagingQueue = {0};
  D3DKMT_CLOSEADAPTER closeAdapter = {0};
  D3DKMT_HANDLE allocationHandles[2] = {0};
  PFND3DKMT_ENUMADAPTERS3 enumAdapters3 = NULL;
  HMODULE gdiModule = NULL;
  ULONG selectedAdapter = MAX_ENUM_ADAPTERS;
  ULONG matchingAdapters = 0u;
  ULONG index;
  LUID selectedLuid = {0};
  ULONG selectedSources = 0u;
  UINT residencyPriority[2] = {
      D3DDDI_ALLOCATIONPRIORITY_NORMAL,
      D3DDDI_ALLOCATIONPRIORITY_NORMAL};
  NTSTATUS openStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS deviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS pagingQueueStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS contextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS allocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destinationAllocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS residentStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS renderStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS resetStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyAllocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyContextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyDeviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyPagingQueueStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS closeAdapterStatus = (NTSTATUS)0xc0000001L;
  int result = 1;
  BOOL requestEngineTdr = FALSE;

  if (argc == 2 && wcscmp(argv[1], L"--engine-tdr") == 0)
    requestEngineTdr = TRUE;
  else if (argc != 1) {
    fwprintf(stderr, L"usage: AppleAgxD3dKmRender.exe [--engine-tdr]\n");
    return 2;
  }
  gdiModule = GetModuleHandleW(L"gdi32.dll");
  if (gdiModule == NULL)
    goto cleanup;
  enumAdapters3 = (PFND3DKMT_ENUMADAPTERS3)GetProcAddress(
      gdiModule, "D3DKMTEnumAdapters3");
  if (enumAdapters3 == NULL)
    goto cleanup;
  enumeration.NumAdapters = ARRAYSIZE(adapters);
  enumeration.pAdapters = adapters;
  openStatus = enumAdapters3(&enumeration);
  if (!NT_SUCCESS(openStatus))
    goto cleanup;

  for (index = 0u; index < enumeration.NumAdapters; ++index) {
    NTSTATUS typeStatus;
    ZeroMemory(&adapterType, sizeof(adapterType));
    ZeroMemory(&query, sizeof(query));
    query.hAdapter = adapters[index].hAdapter;
    query.Type = KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData = &adapterType;
    query.PrivateDriverDataSize = sizeof(adapterType);
    typeStatus = D3DKMTQueryAdapterInfo(&query);
    wprintf(L"ADAPTER index=%lu luid_high=%ld luid_low=%lu sources=%lu "
            L"query=0x%08lx type=0x%08lx\n",
            index, adapters[index].AdapterLuid.HighPart,
            adapters[index].AdapterLuid.LowPart,
            adapters[index].NumOfSources, (ULONG)typeStatus,
            adapterType.Value);
    if (NT_SUCCESS(typeStatus) &&
        adapterType.RenderSupported && adapterType.DisplaySupported &&
        adapterType.PostDevice && !adapterType.SoftwareDevice &&
        !adapterType.ComputeOnly) {
      selectedAdapter = index;
      ++matchingAdapters;
    }
  }
  if (matchingAdapters != 1u || selectedAdapter >= enumeration.NumAdapters)
    goto cleanup;
  selectedLuid = adapters[selectedAdapter].AdapterLuid;
  selectedSources = adapters[selectedAdapter].NumOfSources;

  createDevice.hAdapter = adapters[selectedAdapter].hAdapter;
  deviceStatus = D3DKMTCreateDevice(&createDevice);
  if (!NT_SUCCESS(deviceStatus))
    goto cleanup;

  createPagingQueue.hDevice = createDevice.hDevice;
  createPagingQueue.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
  createPagingQueue.PhysicalAdapterIndex = 0u;
  pagingQueueStatus = D3DKMTCreatePagingQueue(&createPagingQueue);
  if (!NT_SUCCESS(pagingQueueStatus) ||
      createPagingQueue.hPagingQueue == 0u ||
      createPagingQueue.FenceValueCPUVirtualAddress == NULL)
    goto cleanup;

  createContext.hDevice = createDevice.hDevice;
  createContext.NodeOrdinal = 0u;
  createContext.EngineAffinity = 1u;
  createContext.Flags.Value = 0u;
  createContext.ClientHint = D3DKMT_CLIENTHINT_UNKNOWN;
  contextStatus = D3DKMTCreateContext(&createContext);
  if (!NT_SUCCESS(contextStatus) || createContext.hContext == 0u ||
      createContext.pCommandBuffer == NULL ||
      createContext.CommandBufferSize < sizeof(command) ||
      createContext.pAllocationList == NULL ||
      createContext.AllocationListSize < 2u ||
      createContext.pPatchLocationList == NULL)
    goto cleanup;

  if (!AdmissionAllocationDescribe(
          APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH,
          APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT, 4u,
          (unsigned int)D3DKMDT_GDISURFACE_TEXTURE,
          (unsigned int)D3DDDIFMT_A8R8G8B8, 0u, &allocation[0]) ||
      !AdmissionAllocationDescribe(
          APPLE_AGX_SCANOUT_J313_WIDTH,
          APPLE_AGX_SCANOUT_J313_HEIGHT, 4u,
          (unsigned int)D3DKMDT_GDISURFACE_TEXTURE,
          (unsigned int)D3DDDIFMT_A8R8G8B8, 0u, &allocation[1]))
    goto cleanup;
  allocation[0].Reserved = ADMISSION_UMD_CORRELATION_COOKIE;
  allocationInfo[0].pPrivateDriverData = &allocation[0];
  allocationInfo[0].PrivateDriverDataSize = sizeof(allocation[0]);
  allocationInfo[1].pPrivateDriverData = &allocation[1];
  allocationInfo[1].PrivateDriverDataSize = sizeof(allocation[1]);
  createAllocation.hDevice = createDevice.hDevice;
  createAllocation.NumAllocations = 1u;
  createAllocation.pAllocationInfo = &allocationInfo[0];
  allocationStatus = D3DKMTCreateAllocation(&createAllocation);
  if (!NT_SUCCESS(allocationStatus) || allocationInfo[0].hAllocation == 0u)
    goto cleanup;
  allocationHandles[0] = allocationInfo[0].hAllocation;

  ZeroMemory(&createAllocation, sizeof(createAllocation));
  createAllocation.hDevice = createDevice.hDevice;
  createAllocation.NumAllocations = 1u;
  createAllocation.pAllocationInfo = &allocationInfo[1];
  destinationAllocationStatus = D3DKMTCreateAllocation(&createAllocation);
  if (!NT_SUCCESS(destinationAllocationStatus) ||
      allocationInfo[1].hAllocation == 0u)
    goto cleanup;
  allocationHandles[1] = allocationInfo[1].hAllocation;

  makeResident.hPagingQueue = createPagingQueue.hPagingQueue;
  makeResident.NumAllocations = ARRAYSIZE(allocationHandles);
  makeResident.AllocationList = allocationHandles;
  makeResident.PriorityList = residencyPriority;
  residentStatus = D3DKMTMakeResident(&makeResident);
  if (!NT_SUCCESS(residentStatus) ||
      makeResident.NumAllocations != ARRAYSIZE(allocationHandles))
    goto cleanup;
  {
    ULONGLONG deadline = GetTickCount64() + 15000u;
    volatile const UINT64 *fence =
        (volatile const UINT64 *)createPagingQueue.FenceValueCPUVirtualAddress;
    while (*fence < makeResident.PagingFenceValue &&
           GetTickCount64() < deadline)
      Sleep(1u);
    if (*fence < makeResident.PagingFenceValue) {
      residentStatus = (NTSTATUS)0x00000102L;
      goto cleanup;
    }
  }

  command.Magic = ADMISSION_UMD_COMMAND_MAGIC;
  command.Version = ADMISSION_UMD_COMMAND_VERSION;
  command.Bytes = sizeof(command);
  command.Opcode = AdmissionUmdOpcodeColorFill;
  command.Destination.Right = APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH;
  command.Destination.Bottom = APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT;
  command.DestinationAllocationIndex = 0u;
  command.Color = APPLE_AGX_EXP208_GDI_COLOR;
  command.Rop = AdmissionUmdRopPatCopy;
  CopyMemory(createContext.pCommandBuffer, &command, sizeof(command));
  ZeroMemory(createContext.pAllocationList,
             2u * sizeof(createContext.pAllocationList[0]));
  createContext.pAllocationList[0].hAllocation = allocationHandles[0];
  createContext.pAllocationList[0].WriteOperation = 1u;
  createContext.pAllocationList[1].hAllocation = allocationHandles[1];
  createContext.pAllocationList[1].WriteOperation = 1u;
  ZeroMemory(createContext.pPatchLocationList,
             sizeof(createContext.pPatchLocationList[0]));

  render.hContext = createContext.hContext;
  render.CommandOffset = 0u;
  render.CommandLength = sizeof(command);
  render.AllocationCount = ARRAYSIZE(allocationHandles);
  render.PatchLocationCount = 0u;
  wprintf(L"BUFFERS device_command=%p device_command_bytes=%u "
          L"device_allocations=%p device_allocation_count=%u "
          L"device_patches=%p device_patch_count=%u "
          L"context_command=%p context_command_bytes=%u "
          L"context_allocations=%p context_allocation_count=%u "
          L"context_patches=%p context_patch_count=%u context_gpuva=0x%llx\n",
          createDevice.pCommandBuffer, createDevice.CommandBufferSize,
          createDevice.pAllocationList, createDevice.AllocationListSize,
          createDevice.pPatchLocationList, createDevice.PatchLocationListSize,
          createContext.pCommandBuffer, createContext.CommandBufferSize,
          createContext.pAllocationList, createContext.AllocationListSize,
          createContext.pPatchLocationList, createContext.PatchLocationListSize,
          createContext.CommandBuffer);
  renderStatus = D3DKMTRender(&render);
  wprintf(L"RENDER_OUT command=%p command_bytes=%u allocations=%p "
          L"allocation_count=%u patches=%p patch_count=%u gpuva=0x%llx "
          L"queued=%u\n", render.pNewCommandBuffer,
          render.NewCommandBufferSize, render.pNewAllocationList,
          render.NewAllocationListSize, render.pNewPatchLocationList,
          render.NewPatchLocationListSize, render.NewCommandBuffer,
          render.QueuedBufferCount);
  if (!NT_SUCCESS(renderStatus))
    goto cleanup;
  Sleep(10000u);
  if (requestEngineTdr) {
    tdr.TdrControl = D3DKMT_TDRDBGCTRLTYPE_ENGINETDR;
    tdr.NodeOrdinal = 0u;
    escape.hAdapter = adapters[selectedAdapter].hAdapter;
    escape.hDevice = createDevice.hDevice;
    escape.hContext = createContext.hContext;
    escape.Type = D3DKMT_ESCAPE_TDRDBGCTRL;
    escape.pPrivateDriverData = &tdr;
    escape.PrivateDriverDataSize = sizeof(tdr);
    resetStatus = D3DKMTEscape(&escape);
    wprintf(L"ENGINE_TDR status=0x%08lx node=%lu\n",
            (ULONG)resetStatus, tdr.NodeOrdinal);
    if (!NT_SUCCESS(resetStatus))
      goto cleanup;
  }
  result = 0;

cleanup:
  if (allocationHandles[0] != 0u) {
    destroy.hDevice = createDevice.hDevice;
    destroy.phAllocationList = allocationHandles;
    destroy.AllocationCount =
        allocationHandles[1] != 0u ? 2u : 1u;
    destroy.Flags.AssumeNotInUse = 0;
    destroy.Flags.SynchronousDestroy = 1;
    destroyAllocationStatus = D3DKMTDestroyAllocation2(&destroy);
    if (!NT_SUCCESS(destroyAllocationStatus))
      result = 1;
  }
  if (createContext.hContext != 0u) {
    destroyContext.hContext = createContext.hContext;
    destroyContextStatus = D3DKMTDestroyContext(&destroyContext);
    if (!NT_SUCCESS(destroyContextStatus))
      result = 1;
  }
  if (createPagingQueue.hPagingQueue != 0u) {
    destroyPagingQueue.hPagingQueue = createPagingQueue.hPagingQueue;
    destroyPagingQueueStatus = D3DKMTDestroyPagingQueue(&destroyPagingQueue);
    if (!NT_SUCCESS(destroyPagingQueueStatus))
      result = 1;
  }
  if (createDevice.hDevice != 0u) {
    destroyDevice.hDevice = createDevice.hDevice;
    destroyDeviceStatus = D3DKMTDestroyDevice(&destroyDevice);
    if (!NT_SUCCESS(destroyDeviceStatus))
      result = 1;
  }
  for (index = 0u; index < enumeration.NumAdapters; ++index) {
    if (adapters[index].hAdapter != 0u) {
      closeAdapter.hAdapter = adapters[index].hAdapter;
      closeAdapterStatus = D3DKMTCloseAdapter(&closeAdapter);
      if (!NT_SUCCESS(closeAdapterStatus))
        result = 1;
      adapters[index].hAdapter = 0u;
    }
  }
  wprintf(L"{\"enumerated\":%lu,\"matching\":%lu,"
          L"\"luid_high\":%ld,\"luid_low\":%lu,\"sources\":%lu,"
          L"\"open\":\"0x%08lx\",\"device\":\"0x%08lx\","
          L"\"paging_queue\":\"0x%08lx\","
          L"\"context\":\"0x%08lx\",\"allocation\":\"0x%08lx\","
          L"\"destination_allocation\":\"0x%08lx\","
          L"\"resident\":\"0x%08lx\",\"paging_fence\":%llu,"
          L"\"render\":\"0x%08lx\",\"queued\":%u,"
          L"\"engine_tdr\":\"0x%08lx\","
          L"\"destroy_allocation\":\"0x%08lx\","
          L"\"destroy_context\":\"0x%08lx\","
          L"\"destroy_paging_queue\":\"0x%08lx\","
          L"\"destroy_device\":\"0x%08lx\","
          L"\"close_adapter\":\"0x%08lx\",\"result\":%d}\n",
          enumeration.NumAdapters, matchingAdapters,
          selectedLuid.HighPart, selectedLuid.LowPart, selectedSources,
          (ULONG)openStatus, (ULONG)deviceStatus, (ULONG)pagingQueueStatus,
          (ULONG)contextStatus, (ULONG)allocationStatus,
          (ULONG)destinationAllocationStatus,
          (ULONG)residentStatus, makeResident.PagingFenceValue,
          (ULONG)renderStatus, render.QueuedBufferCount,
          (ULONG)resetStatus,
          (ULONG)destroyAllocationStatus,
          (ULONG)destroyContextStatus, (ULONG)destroyPagingQueueStatus,
          (ULONG)destroyDeviceStatus,
          (ULONG)closeAdapterStatus, result);
  return result;
}
