#include "apple_agx_driver.h"

#ifdef APPLE_AGX_G2_UAT_SNAPSHOT_QUALIFICATION
typedef struct _APPLE_AGX_WINDOWS_UAT_INSPECTION {
  PDXGKRNL_INTERFACE Interface;
  PHYSICAL_ADDRESS GpuRegionAddress;
  volatile unsigned char *MappedBase;
} APPLE_AGX_WINDOWS_UAT_INSPECTION;

static unsigned char AppleAgxWindowsUatInspectMap(
    void *Context, unsigned long long PhysicalAddress, unsigned int Length,
    volatile unsigned char **VirtualAddress) {
  APPLE_AGX_WINDOWS_UAT_INSPECTION *inspection = Context;
  PHYSICAL_ADDRESS address;
  PVOID mapped = NULL;

  if (inspection == NULL || VirtualAddress == NULL ||
      inspection->Interface == NULL ||
      inspection->Interface->DeviceHandle == NULL ||
      inspection->Interface->DxgkCbMapMemory == NULL ||
      inspection->MappedBase != NULL ||
      PhysicalAddress != (unsigned long long)inspection->GpuRegionAddress.QuadPart ||
      Length != (unsigned int)J313_AGX_G2_GPU_SIZE)
    return 0u;
  address.QuadPart = (LONGLONG)PhysicalAddress;
  if (!NT_SUCCESS(inspection->Interface->DxgkCbMapMemory(
          inspection->Interface->DeviceHandle, address, Length, FALSE, FALSE,
          MmNonCached, &mapped)) || mapped == NULL)
    return 0u;
  inspection->MappedBase = mapped;
  *VirtualAddress = inspection->MappedBase;
  return 1u;
}

static void AppleAgxWindowsUatInspectBarrier(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  KeMemoryBarrier();
}

static unsigned char AppleAgxWindowsUatInspectUnmap(
    void *Context, volatile unsigned char *VirtualAddress) {
  APPLE_AGX_WINDOWS_UAT_INSPECTION *inspection = Context;

  if (inspection == NULL || inspection->MappedBase == NULL ||
      inspection->Interface == NULL ||
      inspection->Interface->DeviceHandle == NULL ||
      inspection->Interface->DxgkCbUnmapMemory == NULL ||
      VirtualAddress != inspection->MappedBase)
    return 0u;
  if (!NT_SUCCESS(inspection->Interface->DxgkCbUnmapMemory(
          inspection->Interface->DeviceHandle,
          (PVOID)inspection->MappedBase)))
    return 0u;
  inspection->MappedBase = NULL;
  return 1u;
}

_Use_decl_annotations_ NTSTATUS AppleAgxWindowsInspectUatRoots(
    PDXGKRNL_INTERFACE DxgkInterface,
    PHYSICAL_ADDRESS GpuRegionAddress,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot,
    APPLE_AGX_UAT_ROOT_SNAPSHOT *Roots) {
  APPLE_AGX_WINDOWS_UAT_INSPECTION inspection = {
      .Interface = DxgkInterface,
      .GpuRegionAddress = GpuRegionAddress,
  };
  APPLE_AGX_UAT_PUBLICATION_IO io;
  APPLE_AGX_UAT_PUBLICATION_RESULT result;

  if (DxgkInterface == NULL || Snapshot == NULL || Roots == NULL ||
      (unsigned long long)GpuRegionAddress.QuadPart != J313_AGX_G2_GPU_BASE)
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(&io, sizeof(io));
  io.Context = &inspection;
  io.Map = AppleAgxWindowsUatInspectMap;
  io.Barrier = AppleAgxWindowsUatInspectBarrier;
  io.Unmap = AppleAgxWindowsUatInspectUnmap;
  result = AppleAgxUatInspectJ313(Snapshot, &io, Roots);
  if (result == AppleAgxUatPublicationResultOk)
    return STATUS_SUCCESS;
  if (result == AppleAgxUatPublicationResultInvalidArgument)
    return STATUS_INVALID_PARAMETER;
  if (result == AppleAgxUatPublicationResultMapFailed)
    return STATUS_NONE_MAPPED;
  return STATUS_UNSUCCESSFUL;
}
#endif

static unsigned char AppleAgxWindowsUatMap(
    void *Context, unsigned long long PhysicalAddress, unsigned int Length,
    volatile unsigned char **VirtualAddress) {
  APPLE_AGX_WINDOWS_UAT_PUBLICATION *publication = Context;
  PHYSICAL_ADDRESS address;
  PVOID mapped = NULL;

  if (publication == NULL || publication->Interface == NULL ||
      publication->Interface->DeviceHandle == NULL ||
      publication->Interface->DxgkCbMapMemory == NULL ||
      VirtualAddress == NULL || Length == 0u)
    return 0u;
  address.QuadPart = (LONGLONG)PhysicalAddress;
  publication->LastStatus = publication->Interface->DxgkCbMapMemory(
      publication->Interface->DeviceHandle, address, Length, FALSE, FALSE,
      MmNonCached, &mapped);
  if (!NT_SUCCESS(publication->LastStatus) || mapped == NULL)
    return 0u;
  *VirtualAddress = (volatile unsigned char *)mapped;
  return 1u;
}

static void AppleAgxWindowsUatBarrier(void *Context) {
  UNREFERENCED_PARAMETER(Context);
  KeMemoryBarrier();
}

static unsigned char AppleAgxWindowsUatUnmap(
    void *Context, volatile unsigned char *VirtualAddress) {
  APPLE_AGX_WINDOWS_UAT_PUBLICATION *publication = Context;

  if (publication == NULL || publication->Interface == NULL ||
      publication->Interface->DeviceHandle == NULL ||
      publication->Interface->DxgkCbUnmapMemory == NULL ||
      VirtualAddress == NULL)
    return 0u;
  publication->LastStatus = publication->Interface->DxgkCbUnmapMemory(
      publication->Interface->DeviceHandle, (PVOID)VirtualAddress);
  return NT_SUCCESS(publication->LastStatus) ? 1u : 0u;
}

_Use_decl_annotations_ NTSTATUS AppleAgxWindowsUatPublicationInitialize(
    PDXGKRNL_INTERFACE DxgkInterface,
    APPLE_AGX_WINDOWS_UAT_PUBLICATION *Publication,
    APPLE_AGX_UAT_PUBLICATION_IO *Io) {
  if (DxgkInterface == NULL || Publication == NULL || Io == NULL ||
      DxgkInterface->DeviceHandle == NULL ||
      DxgkInterface->DxgkCbMapMemory == NULL ||
      DxgkInterface->DxgkCbUnmapMemory == NULL)
    return STATUS_INVALID_PARAMETER;

  RtlZeroMemory(Publication, sizeof(*Publication));
  RtlZeroMemory(Io, sizeof(*Io));
  Publication->Interface = DxgkInterface;
  Publication->LastStatus = STATUS_SUCCESS;
  Io->Context = Publication;
  Io->Map = AppleAgxWindowsUatMap;
  Io->Barrier = AppleAgxWindowsUatBarrier;
  Io->Unmap = AppleAgxWindowsUatUnmap;
  return STATUS_SUCCESS;
}
