#include "apple_agx_driver.h"

#include "j313_agx_g2.generated.h"

typedef struct _APPLE_AGX_POWER_MAPPING {
  PUCHAR Base;
} APPLE_AGX_POWER_MAPPING;

static APPLE_AGX_POWER_U32 AppleAgxPowerRead32(
    void *Context, APPLE_AGX_POWER_U32 Offset) {
  APPLE_AGX_POWER_MAPPING *mapping = Context;
  return READ_REGISTER_ULONG((volatile ULONG *)(mapping->Base + Offset));
}

static APPLE_AGX_POWER_U64 AppleAgxPowerRead64(
    void *Context, APPLE_AGX_POWER_U32 Offset) {
  APPLE_AGX_POWER_MAPPING *mapping = Context;
  return READ_REGISTER_ULONG64((volatile ULONG64 *)(mapping->Base + Offset));
}

static void AppleAgxPowerWrite32(void *Context, APPLE_AGX_POWER_U32 Offset,
                                APPLE_AGX_POWER_U32 Value) {
  APPLE_AGX_POWER_MAPPING *mapping = Context;
  WRITE_REGISTER_ULONG((volatile ULONG *)(mapping->Base + Offset), Value);
}

static void AppleAgxPowerWrite64(void *Context, APPLE_AGX_POWER_U32 Offset,
                                APPLE_AGX_POWER_U64 Value) {
  APPLE_AGX_POWER_MAPPING *mapping = Context;
  WRITE_REGISTER_ULONG64((volatile ULONG64 *)(mapping->Base + Offset), Value);
}

_Use_decl_annotations_ NTSTATUS AppleAgxQualifyPowerBroker(
    PDXGKRNL_INTERFACE DxgkInterface, PHYSICAL_ADDRESS PowerBrokerAddress) {
  APPLE_AGX_POWER_MAPPING mapping = {0};
  APPLE_AGX_POWER_IO io;
  NTSTATUS status;
  BOOLEAN qualified;

  if (DxgkInterface == NULL || DxgkInterface->DxgkCbMapMemory == NULL ||
      DxgkInterface->DxgkCbUnmapMemory == NULL)
    return STATUS_INVALID_PARAMETER;

  status = DxgkInterface->DxgkCbMapMemory(
      DxgkInterface->DeviceHandle, PowerBrokerAddress,
      (ULONG)J313_AGX_G2_POWER_BROKER_SIZE, FALSE, FALSE, MmNonCached,
      (PVOID *)&mapping.Base);
  if (!NT_SUCCESS(status))
    return status;

  io.Context = &mapping;
  io.Read32 = AppleAgxPowerRead32;
  io.Read64 = AppleAgxPowerRead64;
  io.Write32 = AppleAgxPowerWrite32;
  io.Write64 = AppleAgxPowerWrite64;
  qualified = AppleAgxPowerQualify(&io) ? TRUE : FALSE;

  status = DxgkInterface->DxgkCbUnmapMemory(DxgkInterface->DeviceHandle,
                                             mapping.Base);
  if (!NT_SUCCESS(status))
    return status;
  if (!qualified)
    return STATUS_DEVICE_HARDWARE_ERROR;

  DbgPrintEx(DPFLTR_IHVVIDEO_ID, DPFLTR_INFO_LEVEL,
             "AppleAgx: G2 power broker qualification passed\n");
  return STATUS_SUCCESS;
}
