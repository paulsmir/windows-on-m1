#include "apple_agx_driver.h"

#if defined(APPLE_AGX_G2_MMIO_QUALIFICATION) ||                                \
    defined(APPLE_AGX_G2_FIRMWARE_QUALIFICATION)

typedef struct _APPLE_AGX_DXGK_MAPPING_CONTEXT {
  PDXGKRNL_INTERFACE Interface;
  NTSTATUS LastStatus;
} APPLE_AGX_DXGK_MAPPING_CONTEXT;

static unsigned char AppleAgxDxgkMap(void *Context,
                                     unsigned long long PhysicalAddress,
                                     unsigned int Length,
                                     unsigned char **VirtualAddress) {
  APPLE_AGX_DXGK_MAPPING_CONTEXT *mapping = Context;
  PHYSICAL_ADDRESS address;
  PVOID base = NULL;

  if (mapping == NULL || mapping->Interface == NULL || VirtualAddress == NULL) {
    return 0u;
  }

  address.QuadPart = (LONGLONG)PhysicalAddress;
  mapping->LastStatus = mapping->Interface->DxgkCbMapMemory(
      mapping->Interface->DeviceHandle, address, (ULONG)Length, FALSE, FALSE,
      MmNonCached, &base);
  if (!NT_SUCCESS(mapping->LastStatus) || base == NULL)
    return 0u;

  *VirtualAddress = (unsigned char *)base;
  return 1u;
}

static unsigned char AppleAgxDxgkUnmap(void *Context,
                                       unsigned char *VirtualAddress) {
  APPLE_AGX_DXGK_MAPPING_CONTEXT *mapping = Context;

  if (mapping == NULL || mapping->Interface == NULL || VirtualAddress == NULL) {
    return 0u;
  }

  mapping->LastStatus = mapping->Interface->DxgkCbUnmapMemory(
      mapping->Interface->DeviceHandle, VirtualAddress);
  return NT_SUCCESS(mapping->LastStatus) ? 1u : 0u;
}

static NTSTATUS AppleAgxMappingResultToStatus(APPLE_AGX_UAT_RESULT Result,
                                              NTSTATUS CallbackStatus) {
  if (Result == AppleAgxUatResultOk)
    return STATUS_SUCCESS;
  if (Result == AppleAgxUatResultAllocationFailed &&
      !NT_SUCCESS(CallbackStatus))
    return CallbackStatus;
  if (Result == AppleAgxUatResultAlreadyMapped)
    return STATUS_DEVICE_BUSY;
  if (Result == AppleAgxUatResultInvalidArgument)
    return STATUS_INVALID_PARAMETER;
  return STATUS_INVALID_DEVICE_STATE;
}

_Use_decl_annotations_ NTSTATUS AppleAgxQualifyMmioMapping(
    PDXGKRNL_INTERFACE DxgkInterface, APPLE_AGX_MAPPING_STATE *MappingState) {
  APPLE_AGX_DXGK_MAPPING_CONTEXT context;
  APPLE_AGX_MAPPING_IO io;
  APPLE_AGX_UAT_RESULT result;

  if (DxgkInterface == NULL || MappingState == NULL ||
      DxgkInterface->DxgkCbMapMemory == NULL ||
      DxgkInterface->DxgkCbUnmapMemory == NULL)
    return STATUS_INVALID_PARAMETER;

  context.Interface = DxgkInterface;
  context.LastStatus = STATUS_SUCCESS;
  io.Context = &context;
  io.Map = AppleAgxDxgkMap;
  io.Unmap = AppleAgxDxgkUnmap;
  result = AppleAgxMappingStart(&io, MappingState);
  return AppleAgxMappingResultToStatus(result, context.LastStatus);
}

_Use_decl_annotations_ NTSTATUS AppleAgxReleaseMmioMapping(
    PDXGKRNL_INTERFACE DxgkInterface, APPLE_AGX_MAPPING_STATE *MappingState) {
  APPLE_AGX_DXGK_MAPPING_CONTEXT context;
  APPLE_AGX_MAPPING_IO io;
  APPLE_AGX_UAT_RESULT result;

  if (DxgkInterface == NULL || MappingState == NULL ||
      DxgkInterface->DxgkCbUnmapMemory == NULL)
    return STATUS_INVALID_PARAMETER;

  context.Interface = DxgkInterface;
  context.LastStatus = STATUS_SUCCESS;
  io.Context = &context;
  io.Map = AppleAgxDxgkMap;
  io.Unmap = AppleAgxDxgkUnmap;
  result = AppleAgxMappingStop(&io, MappingState);
  return AppleAgxMappingResultToStatus(result, context.LastStatus);
}

#endif /* MMIO or firmware qualification */
