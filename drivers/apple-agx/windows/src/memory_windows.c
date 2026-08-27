#include "apple_agx_driver.h"

typedef struct _APPLE_AGX_WINDOWS_MEMORY_ALLOCATION {
  PDXGKRNL_INTERFACE Interface;
  HANDLE PhysicalMemoryObject;
  HANDLE AdapterMemoryObject;
  PDXGK_ADL Adl;
  PVOID MappedBase;
  SIZE_T MappedSize;
} APPLE_AGX_WINDOWS_MEMORY_ALLOCATION;

static BOOLEAN
AppleAgxWindowsMemoryCallbacksValid(_In_ PDXGKRNL_INTERFACE Interface) {
  return Interface != NULL && Interface->DeviceHandle != NULL &&
         Interface->DxgkCbCreatePhysicalMemoryObject != NULL &&
         Interface->DxgkCbDestroyPhysicalMemoryObject != NULL &&
         Interface->DxgkCbMapPhysicalMemory != NULL &&
         Interface->DxgkCbUnmapPhysicalMemory != NULL &&
         Interface->DxgkCbAllocateAdl != NULL &&
         Interface->DxgkCbFreeAdl != NULL;
}

static VOID AppleAgxWindowsMemoryCleanup(
    _Inout_ APPLE_AGX_WINDOWS_MEMORY_ALLOCATION *Allocation) {
  if (Allocation->MappedBase != NULL) {
    DXGKARGCB_UNMAP_PHYSICAL_MEMORY unmapArgs;
    RtlZeroMemory(&unmapArgs, sizeof(unmapArgs));
    unmapArgs.hPhysicalMemoryObject = Allocation->PhysicalMemoryObject;
    unmapArgs.pBaseAddress = Allocation->MappedBase;
    unmapArgs.Size = Allocation->MappedSize;
    Allocation->Interface->DxgkCbUnmapPhysicalMemory(&unmapArgs);
    Allocation->MappedBase = NULL;
    Allocation->MappedSize = 0;
  }
  if (Allocation->Adl != NULL) {
    DXGKARGCB_FREE_ADL freeAdlArgs;
    RtlZeroMemory(&freeAdlArgs, sizeof(freeAdlArgs));
    freeAdlArgs.hAdapterMemoryObject = Allocation->AdapterMemoryObject;
    freeAdlArgs.pAdl = Allocation->Adl;
    Allocation->Interface->DxgkCbFreeAdl(&freeAdlArgs);
    Allocation->Adl = NULL;
  }
  if (Allocation->PhysicalMemoryObject != NULL) {
    DXGKARGCB_DESTROY_PHYSICAL_MEMORY_OBJECT destroyArgs;
    RtlZeroMemory(&destroyArgs, sizeof(destroyArgs));
    destroyArgs.hPhysicalMemoryObject = Allocation->PhysicalMemoryObject;
    destroyArgs.hAdapterMemoryObject = Allocation->AdapterMemoryObject;
    Allocation->Interface->DxgkCbDestroyPhysicalMemoryObject(&destroyArgs);
    Allocation->PhysicalMemoryObject = NULL;
    Allocation->AdapterMemoryObject = NULL;
  }
}

_IRQL_requires_max_(APC_LEVEL) static unsigned char AppleAgxWindowsAllocateContiguous(
    void *Context, unsigned long long Bytes, void **CpuBase,
    unsigned long long *DeviceBase, void **AllocationHandle) {
  APPLE_AGX_WINDOWS_MEMORY_ALLOCATOR *allocator = Context;
  APPLE_AGX_WINDOWS_MEMORY_ALLOCATION *allocation;
  DXGKARGCB_CREATE_PHYSICAL_MEMORY_OBJECT createArgs;
  DXGKARGCB_ALLOCATE_ADL adlArgs;
  DXGKARGCB_MAP_PHYSICAL_MEMORY mapArgs;
  unsigned long long requiredPages;
  unsigned long long basePage;
  NTSTATUS status;

  if (allocator == NULL || CpuBase == NULL || DeviceBase == NULL ||
      AllocationHandle == NULL || Bytes == 0ULL ||
      (unsigned long long)(SIZE_T)Bytes != Bytes ||
      !AppleAgxWindowsMemoryCallbacksValid(allocator->Interface))
    return 0u;

  *CpuBase = NULL;
  *DeviceBase = 0ULL;
  *AllocationHandle = NULL;
  allocation = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*allocation),
                               APPLE_AGX_POOL_TAG);
  if (allocation == NULL)
    return 0u;
  RtlZeroMemory(allocation, sizeof(*allocation));
  allocation->Interface = allocator->Interface;

  RtlZeroMemory(&createArgs, sizeof(createArgs));
  createArgs.hAdapter = allocator->Interface->DeviceHandle;
  createArgs.Size = (SIZE_T)Bytes;
  createArgs.Context = (ULONG_PTR)allocation;
  createArgs.Type = DXGK_PHYSICAL_MEMORY_TYPE_CONTIGUOUS_MEMORY;
  createArgs.CacheType = DXGK_MEMORY_CACHING_TYPE_NON_CACHED;
  createArgs.ContiguousMemory.LowestAcceptableAddress.QuadPart = 0;
  createArgs.ContiguousMemory.HighestAcceptableAddress.QuadPart =
      APPLE_AGX_MEMORY_DEVICE_ADDRESS_LIMIT - 1ULL;
  createArgs.ContiguousMemory.BoundaryAddressMultiple.QuadPart = 0;
  status = allocator->Interface->DxgkCbCreatePhysicalMemoryObject(&createArgs);
  if (!NT_SUCCESS(status) || createArgs.hPhysicalMemoryObject == NULL ||
      createArgs.hAdapterMemoryObject == NULL)
    goto Fail;
  allocation->PhysicalMemoryObject = createArgs.hPhysicalMemoryObject;
  allocation->AdapterMemoryObject = createArgs.hAdapterMemoryObject;

  RtlZeroMemory(&adlArgs, sizeof(adlArgs));
  adlArgs.hAdapterMemoryObject = allocation->AdapterMemoryObject;
  adlArgs.Offset = 0;
  adlArgs.Size = (SIZE_T)Bytes;
  adlArgs.Flags.RequireContiguous = 1;
  status = allocator->Interface->DxgkCbAllocateAdl(&adlArgs);
  if (!NT_SUCCESS(status) || adlArgs.pAdl == NULL ||
      !adlArgs.pAdl->Flags.Contiguous)
    goto Fail;
  allocation->Adl = adlArgs.pAdl;

  requiredPages = (Bytes + (unsigned long long)PAGE_SIZE - 1ULL) / PAGE_SIZE;
  if ((unsigned long long)allocation->Adl->PageCount < requiredPages)
    goto Fail;
  basePage = (unsigned long long)allocation->Adl->BasePageNumber;
  if (basePage >= APPLE_AGX_MEMORY_DEVICE_ADDRESS_LIMIT / PAGE_SIZE)
    goto Fail;

  RtlZeroMemory(&mapArgs, sizeof(mapArgs));
  mapArgs.hPhysicalMemoryObject = allocation->PhysicalMemoryObject;
  mapArgs.AccessMode = DXGK_ACCESS_MODE_KERNEL_MODE;
  mapArgs.Offset = 0;
  mapArgs.Size = (SIZE_T)Bytes;
  status = allocator->Interface->DxgkCbMapPhysicalMemory(&mapArgs);
  if (!NT_SUCCESS(status) || mapArgs.pMappedAddress == NULL ||
      mapArgs.Offset > mapArgs.Size ||
      (SIZE_T)Bytes > mapArgs.Size - mapArgs.Offset)
    goto Fail;
  allocation->MappedBase = mapArgs.pMappedAddress;
  allocation->MappedSize = mapArgs.Size;

  *CpuBase = (PUCHAR)mapArgs.pMappedAddress + mapArgs.Offset;
  *DeviceBase = basePage * PAGE_SIZE;
  *AllocationHandle = allocation;
  return 1u;

Fail:
  AppleAgxWindowsMemoryCleanup(allocation);
  ExFreePoolWithTag(allocation, APPLE_AGX_POOL_TAG);
  return 0u;
}

_IRQL_requires_max_(APC_LEVEL) static unsigned char AppleAgxWindowsFreeContiguous(
    void *Context, void *AllocationHandle) {
  APPLE_AGX_WINDOWS_MEMORY_ALLOCATOR *allocator = Context;
  APPLE_AGX_WINDOWS_MEMORY_ALLOCATION *allocation = AllocationHandle;
  if (allocator == NULL || allocation == NULL ||
      allocation->Interface != allocator->Interface ||
      !AppleAgxWindowsMemoryCallbacksValid(allocator->Interface))
    return 0u;
  AppleAgxWindowsMemoryCleanup(allocation);
  ExFreePoolWithTag(allocation, APPLE_AGX_POOL_TAG);
  return 1u;
}

_Use_decl_annotations_ NTSTATUS AppleAgxWindowsMemoryInitialize(
    PDXGKRNL_INTERFACE DxgkInterface,
    APPLE_AGX_WINDOWS_MEMORY_ALLOCATOR *Allocator, APPLE_AGX_MEMORY_IO *Io) {
  if (Allocator == NULL || Io == NULL ||
      !AppleAgxWindowsMemoryCallbacksValid(DxgkInterface))
    return STATUS_INVALID_PARAMETER;
  RtlZeroMemory(Allocator, sizeof(*Allocator));
  RtlZeroMemory(Io, sizeof(*Io));
  Allocator->Interface = DxgkInterface;
  Io->Context = Allocator;
  Io->AllocateContiguous = AppleAgxWindowsAllocateContiguous;
  Io->FreeContiguous = AppleAgxWindowsFreeContiguous;
  return STATUS_SUCCESS;
}
