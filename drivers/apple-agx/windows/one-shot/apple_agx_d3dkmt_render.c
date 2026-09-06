#include <windows.h>
#include <d3dkmthk.h>
#include <stdio.h>
#include <wchar.h>

#include "render_allocation.h"
#include "render_umd_command.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

int __cdecl wmain(int argc, wchar_t **argv) {
  D3DKMT_ENUMADAPTERS3 enumeration = {0};
  D3DKMT_ADAPTERINFO adapters[MAX_ENUM_ADAPTERS] = {0};
  D3DKMT_ADAPTERTYPE adapterType = {0};
  D3DKMT_QUERYADAPTERINFO query = {0};
  D3DKMT_CREATEDEVICE createDevice = {0};
  D3DKMT_CREATECONTEXT createContext = {0};
  D3DKMT_CREATEALLOCATION createAllocation = {0};
  D3DDDI_ALLOCATIONINFO allocationInfo = {0};
  ADMISSION_ALLOCATION_DESCRIPTION allocation = {0};
  ADMISSION_UMD_COLOR_FILL_COMMAND command = {0};
  D3DKMT_RENDER render = {0};
  D3DKMT_DESTROYALLOCATION2 destroy = {0};
  D3DKMT_DESTROYCONTEXT destroyContext = {0};
  D3DKMT_DESTROYDEVICE destroyDevice = {0};
  D3DKMT_CLOSEADAPTER closeAdapter = {0};
  D3DKMT_HANDLE allocationHandle = 0;
  PFND3DKMT_ENUMADAPTERS3 enumAdapters3 = NULL;
  HMODULE gdiModule = NULL;
  ULONG selectedAdapter = MAX_ENUM_ADAPTERS;
  ULONG matchingAdapters = 0u;
  ULONG index;
  LUID selectedLuid = {0};
  ULONG selectedSources = 0u;
  NTSTATUS openStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS deviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS contextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS allocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS renderStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyAllocationStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyContextStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS destroyDeviceStatus = (NTSTATUS)0xc0000001L;
  NTSTATUS closeAdapterStatus = (NTSTATUS)0xc0000001L;
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

  createContext.hDevice = createDevice.hDevice;
  createContext.NodeOrdinal = 0u;
  createContext.EngineAffinity = 1u;
  createContext.Flags.Value = 0u;
  createContext.ClientHint = D3DKMT_CLIENTHINT_OPENGL;
  contextStatus = D3DKMTCreateContext(&createContext);
  if (!NT_SUCCESS(contextStatus) || createContext.hContext == 0u ||
      createContext.pCommandBuffer == NULL ||
      createContext.CommandBufferSize < sizeof(command) ||
      createContext.pAllocationList == NULL ||
      createContext.AllocationListSize < 1u ||
      createContext.pPatchLocationList == NULL)
    goto cleanup;

  if (!AdmissionAllocationDescribe(
          2560u, 1600u, 4u,
          (unsigned int)D3DKMDT_GDISURFACE_TEXTURE,
          (unsigned int)D3DDDIFMT_A8R8G8B8, 0u, &allocation))
    goto cleanup;
  allocationInfo.pPrivateDriverData = &allocation;
  allocationInfo.PrivateDriverDataSize = sizeof(allocation);
  createAllocation.hDevice = createDevice.hDevice;
  createAllocation.NumAllocations = 1u;
  createAllocation.pAllocationInfo = &allocationInfo;
  allocationStatus = D3DKMTCreateAllocation(&createAllocation);
  if (!NT_SUCCESS(allocationStatus) || allocationInfo.hAllocation == 0u)
    goto cleanup;
  allocationHandle = allocationInfo.hAllocation;

  command.Magic = ADMISSION_UMD_COMMAND_MAGIC;
  command.Version = ADMISSION_UMD_COMMAND_VERSION;
  command.Bytes = sizeof(command);
  command.Opcode = AdmissionUmdOpcodeColorFill;
  command.Destination.Right = 2560u;
  command.Destination.Bottom = 1600u;
  command.DestinationAllocationIndex = 0u;
  command.Color = 0xff336699u;
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
  renderStatus = D3DKMTRender(&render);
  if (!NT_SUCCESS(renderStatus))
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
          L"\"context\":\"0x%08lx\",\"allocation\":\"0x%08lx\","
          L"\"render\":\"0x%08lx\",\"queued\":%u,"
          L"\"destroy_allocation\":\"0x%08lx\","
          L"\"destroy_context\":\"0x%08lx\","
          L"\"destroy_device\":\"0x%08lx\","
          L"\"close_adapter\":\"0x%08lx\",\"result\":%d}\n",
          enumeration.NumAdapters, matchingAdapters,
          selectedLuid.HighPart, selectedLuid.LowPart, selectedSources,
          (ULONG)openStatus, (ULONG)deviceStatus, (ULONG)contextStatus,
          (ULONG)allocationStatus, (ULONG)renderStatus,
          render.QueuedBufferCount, (ULONG)destroyAllocationStatus,
          (ULONG)destroyContextStatus, (ULONG)destroyDeviceStatus,
          (ULONG)closeAdapterStatus, result);
  return result;
}
