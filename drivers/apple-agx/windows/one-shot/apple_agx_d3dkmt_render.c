#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <wchar.h>

#include "render_allocation.h"
#include "render_umd_command.h"
#include "apple_agx_exp208_gdi.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define APPLE_AGX_OUTPUT_INITIAL_PATTERN 0xa5u

int __cdecl wmain(int argc, wchar_t **argv) {
  D3DKMT_ENUMADAPTERS3 enumeration = {0};
  D3DKMT_ADAPTERINFO adapters[MAX_ENUM_ADAPTERS] = {0};
  D3DKMT_ADAPTERTYPE adapterType = {0};
  D3DKMT_QUERYADAPTERINFO query = {0};
  D3DKMT_CREATEDEVICE createDevice = {0};
  D3DKMT_CREATEPAGINGQUEUE createPagingQueue = {0};
  D3DKMT_CREATECONTEXT createContext = {0};
  D3DKMT_CREATEALLOCATION createAllocation = {0};
  D3DDDI_ALLOCATIONINFO allocationInfo = {0};
  ADMISSION_ALLOCATION_DESCRIPTION allocation = {0};
  ADMISSION_UMD_COLOR_FILL_COMMAND command = {0};
  D3DKMT_RENDER render = {0};
  D3DKMT_LOCK2 lock = {0};
  D3DKMT_UNLOCK2 unlock = {0};
  D3DDDI_MAKERESIDENT makeResident = {0};
  D3DKMT_DESTROYALLOCATION2 destroy = {0};
  D3DKMT_DESTROYCONTEXT destroyContext = {0};
  D3DKMT_DESTROYDEVICE destroyDevice = {0};
  D3DDDI_DESTROYPAGINGQUEUE destroyPagingQueue = {0};
  D3DKMT_CLOSEADAPTER closeAdapter = {0};
  D3DKMT_HANDLE allocationHandle = 0;
  PFND3DKMT_ENUMADAPTERS3 enumAdapters3 = NULL;
  HMODULE gdiModule = NULL;
  ULONG selectedAdapter = MAX_ENUM_ADAPTERS;
  ULONG matchingAdapters = 0u;
  ULONG index;
  LUID selectedLuid = {0};
  ULONG selectedSources = 0u;
  UINT residencyPriority = D3DDDI_ALLOCATIONPRIORITY_NORMAL;
  NTSTATUS openStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS deviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS pagingQueueStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS contextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS allocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS residentStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS renderStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS initialLockStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS initialUnlockStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS resultLockStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS resultUnlockStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyAllocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyContextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyDeviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyPagingQueueStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS closeAdapterStatus = (NTSTATUS)0xc0000001L;
  ULONG output_pixels_verified = 0u;
  ULONG output_guard_corrupt = 0u;
  int result = 1;

  UNREFERENCED_PARAMETER(argv);
  if (argc != 1) {
    fwprintf(stderr, L"usage: AppleAgxD3dKmRender.exe\n");
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
      createContext.AllocationListSize < 1u ||
      createContext.pPatchLocationList == NULL)
    goto cleanup;

  if (!AdmissionAllocationDescribe(
          APPLE_AGX_EXP208_GDI_WIDTH,
          APPLE_AGX_EXP208_GDI_HEIGHT, 4u,
          (unsigned int)D3DKMDT_GDISURFACE_TEXTURE,
          (unsigned int)D3DDDIFMT_A8R8G8B8, 1u, &allocation))
    goto cleanup;
  allocation.Reserved = ADMISSION_UMD_CORRELATION_COOKIE;
  allocationInfo.pPrivateDriverData = &allocation;
  allocationInfo.PrivateDriverDataSize = sizeof(allocation);
  createAllocation.hDevice = createDevice.hDevice;
  createAllocation.NumAllocations = 1u;
  createAllocation.pAllocationInfo = &allocationInfo;
  allocationStatus = D3DKMTCreateAllocation(&createAllocation);
  if (!NT_SUCCESS(allocationStatus) || allocationInfo.hAllocation == 0u)
    goto cleanup;
  allocationHandle = allocationInfo.hAllocation;

  lock.hDevice = createDevice.hDevice;
  lock.hAllocation = allocationHandle;
  lock.Flags.Value = 0u;
  initialLockStatus = D3DKMTLock2(&lock);
  if (!NT_SUCCESS(initialLockStatus) || lock.pData == NULL)
    goto cleanup;
  FillMemory(lock.pData, ADMISSION_ALLOCATION_ALIGNMENT,
             APPLE_AGX_OUTPUT_INITIAL_PATTERN);
  unlock.hDevice = createDevice.hDevice;
  unlock.hAllocation = allocationHandle;
  initialUnlockStatus = D3DKMTUnlock2(&unlock);
  if (!NT_SUCCESS(initialUnlockStatus))
    goto cleanup;

  makeResident.hPagingQueue = createPagingQueue.hPagingQueue;
  makeResident.NumAllocations = 1u;
  makeResident.AllocationList = &allocationHandle;
  makeResident.PriorityList = &residencyPriority;
  residentStatus = D3DKMTMakeResident(&makeResident);
  if (!NT_SUCCESS(residentStatus) || makeResident.NumAllocations != 1u)
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
  command.Destination.Right = APPLE_AGX_EXP208_GDI_WIDTH;
  command.Destination.Bottom = APPLE_AGX_EXP208_GDI_HEIGHT;
  command.DestinationAllocationIndex = 0u;
  command.Color = APPLE_AGX_EXP208_GDI_COLOR;
  command.Rop = AdmissionUmdRopPatCopy;
  CopyMemory(createContext.pCommandBuffer, &command, sizeof(command));
  ZeroMemory(createContext.pAllocationList,
             sizeof(createContext.pAllocationList[0]));
  createContext.pAllocationList[0].hAllocation = allocationHandle;
  createContext.pAllocationList[0].WriteOperation = 1u;
  ZeroMemory(createContext.pPatchLocationList,
             sizeof(createContext.pPatchLocationList[0]));

  render.hContext = createContext.hContext;
  render.CommandOffset = 0u;
  render.CommandLength = sizeof(command);
  render.AllocationCount = 1u;
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
  ZeroMemory(&lock, sizeof(lock));
  lock.hDevice = createDevice.hDevice;
  lock.hAllocation = allocationHandle;
  lock.Flags.Value = 0u;
  resultLockStatus = D3DKMTLock2(&lock);
  if (!NT_SUCCESS(resultLockStatus) || lock.pData == NULL)
    goto cleanup;
  for (index = 0u;
       index < APPLE_AGX_EXP208_GDI_WIDTH * APPLE_AGX_EXP208_GDI_HEIGHT;
       ++index) {
    if (((const ULONG *)lock.pData)[index] != APPLE_AGX_EXP208_GDI_COLOR)
      break;
    ++output_pixels_verified;
  }
  for (index = APPLE_AGX_EXP208_GDI_WIDTH * APPLE_AGX_EXP208_GDI_HEIGHT * 4u;
       index < ADMISSION_ALLOCATION_ALIGNMENT; ++index)
    if (((const UCHAR *)lock.pData)[index] !=
        APPLE_AGX_OUTPUT_INITIAL_PATTERN)
      ++output_guard_corrupt;
  ZeroMemory(&unlock, sizeof(unlock));
  unlock.hDevice = createDevice.hDevice;
  unlock.hAllocation = allocationHandle;
  resultUnlockStatus = D3DKMTUnlock2(&unlock);
  if (!NT_SUCCESS(resultUnlockStatus) ||
      output_pixels_verified !=
          APPLE_AGX_EXP208_GDI_WIDTH * APPLE_AGX_EXP208_GDI_HEIGHT ||
      output_guard_corrupt != 0u)
    goto cleanup;
  result = 0;

cleanup:
  if (allocationHandle != 0u) {
    destroy.hDevice = createDevice.hDevice;
    destroy.phAllocationList = &allocationHandle;
    destroy.AllocationCount = 1u;
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
          L"\"resident\":\"0x%08lx\",\"paging_fence\":%llu,"
          L"\"initial_lock\":\"0x%08lx\",\"initial_unlock\":\"0x%08lx\","
          L"\"render\":\"0x%08lx\",\"queued\":%u,"
          L"\"result_lock\":\"0x%08lx\",\"result_unlock\":\"0x%08lx\","
          L"\"output_pixels_verified\":%lu,\"output_guard_corrupt\":%lu,"
          L"\"destroy_allocation\":\"0x%08lx\","
          L"\"destroy_context\":\"0x%08lx\","
          L"\"destroy_paging_queue\":\"0x%08lx\","
          L"\"destroy_device\":\"0x%08lx\","
          L"\"close_adapter\":\"0x%08lx\",\"result\":%d}\n",
          enumeration.NumAdapters, matchingAdapters,
          selectedLuid.HighPart, selectedLuid.LowPart, selectedSources,
          (ULONG)openStatus, (ULONG)deviceStatus, (ULONG)pagingQueueStatus,
          (ULONG)contextStatus, (ULONG)allocationStatus,
          (ULONG)residentStatus, makeResident.PagingFenceValue,
          (ULONG)initialLockStatus, (ULONG)initialUnlockStatus,
          (ULONG)renderStatus, render.QueuedBufferCount,
          (ULONG)resultLockStatus, (ULONG)resultUnlockStatus,
          output_pixels_verified, output_guard_corrupt,
          (ULONG)destroyAllocationStatus,
          (ULONG)destroyContextStatus, (ULONG)destroyPagingQueueStatus,
          (ULONG)destroyDeviceStatus,
          (ULONG)closeAdapterStatus, result);
  return result;
}
