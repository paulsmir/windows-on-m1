#include "apple_agx_driver.h"

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
