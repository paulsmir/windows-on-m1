#include "apple_agx_residency.h"

#define APPLE_AGX_RESIDENCY_NULL ((void *)0)

static void AppleAgxResidencySetStatus(
    APPLE_AGX_RESIDENCY_STATUS *Status,
    APPLE_AGX_MEMORY_RESULT MemoryResult,
    APPLE_AGX_UAT_RESULT UatResult) {
  if (Status != APPLE_AGX_RESIDENCY_NULL) {
    Status->MemoryResult = MemoryResult;
    Status->UatResult = UatResult;
    Status->UatMemoryResult = AppleAgxUatMemoryResultOk;
    Status->ContextResult = AppleAgxResidencyContextResultOk;
  }
}

static void AppleAgxResidencySetContextStatus(
    APPLE_AGX_RESIDENCY_STATUS *Status,
    APPLE_AGX_RESIDENCY_CONTEXT_RESULT ContextResult,
    APPLE_AGX_UAT_MEMORY_RESULT UatMemoryResult,
    APPLE_AGX_UAT_RESULT UatResult) {
  if (Status != APPLE_AGX_RESIDENCY_NULL) {
    Status->MemoryResult = AppleAgxMemoryResultOk;
    Status->UatResult = UatResult;
    Status->UatMemoryResult = UatMemoryResult;
    Status->ContextResult = ContextResult;
  }
}

static APPLE_AGX_BOOL AppleAgxResidencyAligned64K(unsigned long long Value) {
  return Value != 0ULL &&
         (Value & (APPLE_AGX_RESIDENCY_WDDM_PAGE_SIZE - 1ULL)) == 0ULL;
}

APPLE_AGX_BOOL AppleAgxResidencyContextCreate(
    APPLE_AGX_RESIDENCY_CONTEXT *ResidencyContext, unsigned int Context,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    APPLE_AGX_MEMORY_OBJECT *PageObjects, unsigned int PageObjectCapacity,
    APPLE_AGX_UAT_PAGE *Pages, unsigned int PageCapacity,
    APPLE_AGX_UAT_MAPPING *Mappings, unsigned int MappingCapacity,
    APPLE_AGX_RESIDENCY_STATUS *Status) {
  APPLE_AGX_UAT_MEMORY_RESULT memory_result;
  APPLE_AGX_UAT_RESULT uat_result;

  AppleAgxResidencySetContextStatus(
      Status, AppleAgxResidencyContextResultInvalidArgument,
      AppleAgxUatMemoryResultInvalidArgument,
      AppleAgxUatResultInvalidArgument);
  if (ResidencyContext == APPLE_AGX_RESIDENCY_NULL ||
      Status == APPLE_AGX_RESIDENCY_NULL || MemoryIo == APPLE_AGX_RESIDENCY_NULL ||
      PageObjects == APPLE_AGX_RESIDENCY_NULL ||
      Pages == APPLE_AGX_RESIDENCY_NULL || Mappings == APPLE_AGX_RESIDENCY_NULL ||
      Context == 0u || ResidencyContext->Initialized != APPLE_AGX_FALSE ||
      PageObjectCapacity == 0u || PageCapacity == 0u ||
      MappingCapacity == 0u || PageObjectCapacity < PageCapacity)
    return APPLE_AGX_FALSE;

  memory_result = AppleAgxUatMemoryOwnerInitialize(
      &ResidencyContext->MemoryOwner, MemoryIo, PageObjects,
      PageObjectCapacity);
  if (memory_result != AppleAgxUatMemoryResultOk) {
    AppleAgxResidencySetContextStatus(
        Status, AppleAgxResidencyContextResultMemoryOwner, memory_result,
        AppleAgxUatResultOk);
    return APPLE_AGX_FALSE;
  }
  ResidencyContext->Initialized = APPLE_AGX_TRUE;
  ResidencyContext->Context = Context;
  memory_result = AppleAgxUatMemoryOwnerGetAllocator(
      &ResidencyContext->MemoryOwner, &ResidencyContext->Allocator);
  if (memory_result != AppleAgxUatMemoryResultOk) {
    AppleAgxResidencySetContextStatus(
        Status, AppleAgxResidencyContextResultMemoryOwner, memory_result,
        AppleAgxUatResultOk);
    return APPLE_AGX_FALSE;
  }

  ResidencyContext->Inventory.Pages = Pages;
  ResidencyContext->Inventory.PageCapacity = PageCapacity;
  ResidencyContext->Inventory.PageCount = 0u;
  ResidencyContext->Inventory.Mappings = Mappings;
  ResidencyContext->Inventory.MappingCapacity = MappingCapacity;
  ResidencyContext->Inventory.MappingCount = 0u;
  uat_result = AppleAgxUatCreateAddressSpace(
      Context, &ResidencyContext->Allocator, &ResidencyContext->Inventory,
      &ResidencyContext->Roots);
  if (uat_result != AppleAgxUatResultOk) {
    memory_result = AppleAgxUatMemoryOwnerDestroy(
        &ResidencyContext->MemoryOwner);
    if (memory_result == AppleAgxUatMemoryResultOk) {
      ResidencyContext->Initialized = APPLE_AGX_FALSE;
      ResidencyContext->Context = 0u;
    }
    AppleAgxResidencySetContextStatus(
        Status, AppleAgxResidencyContextResultUat, memory_result, uat_result);
    return APPLE_AGX_FALSE;
  }

  AppleAgxResidencySetContextStatus(
      Status, AppleAgxResidencyContextResultOk,
      AppleAgxUatMemoryResultOk, AppleAgxUatResultOk);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxResidencyContextDestroy(
    APPLE_AGX_RESIDENCY_CONTEXT *ResidencyContext,
    APPLE_AGX_RESIDENCY_STATUS *Status) {
  APPLE_AGX_UAT_MEMORY_RESULT memory_result;

  AppleAgxResidencySetContextStatus(
      Status, AppleAgxResidencyContextResultInvalidArgument,
      AppleAgxUatMemoryResultInvalidArgument,
      AppleAgxUatResultInvalidArgument);
  if (ResidencyContext == APPLE_AGX_RESIDENCY_NULL ||
      Status == APPLE_AGX_RESIDENCY_NULL ||
      ResidencyContext->Initialized == APPLE_AGX_FALSE)
    return APPLE_AGX_FALSE;
  if (ResidencyContext->Inventory.MappingCount != 0u) {
    AppleAgxResidencySetContextStatus(
        Status, AppleAgxResidencyContextResultBusy,
        AppleAgxUatMemoryResultOk, AppleAgxUatResultAlreadyMapped);
    return APPLE_AGX_FALSE;
  }

  AppleAgxUatDestroy(&ResidencyContext->Allocator,
                     &ResidencyContext->Inventory);
  memory_result = AppleAgxUatMemoryOwnerDestroy(
      &ResidencyContext->MemoryOwner);
  if (memory_result != AppleAgxUatMemoryResultOk) {
    AppleAgxResidencySetContextStatus(
        Status, AppleAgxResidencyContextResultMemoryOwner, memory_result,
        AppleAgxUatResultOk);
    return APPLE_AGX_FALSE;
  }

  ResidencyContext->Context = 0u;
  ResidencyContext->Initialized = APPLE_AGX_FALSE;
  ResidencyContext->Roots.Ttbr0PhysicalAddress = 0ULL;
  ResidencyContext->Roots.Ttbr1PhysicalAddress = 0ULL;
  AppleAgxResidencySetContextStatus(
      Status, AppleAgxResidencyContextResultOk,
      AppleAgxUatMemoryResultOk, AppleAgxUatResultOk);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxResidencyMap64K(
    APPLE_AGX_MEMORY_OBJECT *Object, unsigned int Context,
    const APPLE_AGX_UAT_ROOTS *Roots, unsigned long long GpuVirtualAddress,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_RESIDENCY_STATUS *Status) {
  APPLE_AGX_MEMORY_RESULT memory_result;
  APPLE_AGX_UAT_RESULT uat_result;

  AppleAgxResidencySetStatus(Status, AppleAgxMemoryResultInvalidArgument,
                             AppleAgxUatResultInvalidArgument);
  if (Object == APPLE_AGX_RESIDENCY_NULL ||
      Status == APPLE_AGX_RESIDENCY_NULL ||
      Object->State != AppleAgxMemoryPrepared || Context == 0u ||
      !AppleAgxResidencyAligned64K(Object->Length) ||
      !AppleAgxResidencyAligned64K(GpuVirtualAddress))
    return APPLE_AGX_FALSE;

  if (Object->DevicePages != APPLE_AGX_RESIDENCY_NULL) {
    if (Object->DevicePageCount !=
        Object->Length / APPLE_AGX_MEMORY_PAGE_SIZE)
      return APPLE_AGX_FALSE;
    uat_result = AppleAgxUatMapPageList(
        Context, Roots, GpuVirtualAddress, Object->DevicePages,
        Object->DevicePageCount, AppleAgxUatGpuSharedReadWrite, Allocator,
        Inventory);
  } else {
    if (!AppleAgxResidencyAligned64K(Object->DeviceAddress))
      return APPLE_AGX_FALSE;
    uat_result = AppleAgxUatMap(
        Context, Roots, GpuVirtualAddress, Object->DeviceAddress,
        Object->Length, AppleAgxUatGpuSharedReadWrite, Allocator, Inventory);
  }
  if (uat_result != AppleAgxUatResultOk) {
    AppleAgxResidencySetStatus(Status, AppleAgxMemoryResultOk, uat_result);
    return APPLE_AGX_FALSE;
  }

  memory_result = AppleAgxMemoryMarkGpuMapped(Object, Context,
                                              GpuVirtualAddress);
  if (memory_result != AppleAgxMemoryResultOk) {
    uat_result = AppleAgxUatUnmap(Context, Roots, GpuVirtualAddress,
                                  Object->Length, Allocator, Inventory);
    AppleAgxResidencySetStatus(Status, memory_result, uat_result);
    return APPLE_AGX_FALSE;
  }

  AppleAgxResidencySetStatus(Status, AppleAgxMemoryResultOk,
                             AppleAgxUatResultOk);
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxResidencyUnmap64K(
    APPLE_AGX_MEMORY_OBJECT *Object, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_RESIDENCY_STATUS *Status) {
  APPLE_AGX_MEMORY_RESULT memory_result;
  APPLE_AGX_UAT_RESULT uat_result;
  unsigned int context;
  unsigned long long gpu_virtual_address;

  AppleAgxResidencySetStatus(Status, AppleAgxMemoryResultInvalidArgument,
                             AppleAgxUatResultInvalidArgument);
  if (Object == APPLE_AGX_RESIDENCY_NULL ||
      Status == APPLE_AGX_RESIDENCY_NULL)
    return APPLE_AGX_FALSE;
  if (Object->State == AppleAgxMemoryInFlight) {
    AppleAgxResidencySetStatus(Status, AppleAgxMemoryResultBusy,
                               AppleAgxUatResultOk);
    return APPLE_AGX_FALSE;
  }
  if ((Object->State != AppleAgxMemoryGpuMapped &&
       Object->State != AppleAgxMemoryCompleted) ||
      Object->Context == 0u ||
      !AppleAgxResidencyAligned64K(Object->GpuVirtualAddress) ||
      !AppleAgxResidencyAligned64K(Object->Length))
    return APPLE_AGX_FALSE;
  if (Object->DevicePages == APPLE_AGX_RESIDENCY_NULL &&
      !AppleAgxResidencyAligned64K(Object->DeviceAddress))
    return APPLE_AGX_FALSE;

  context = Object->Context;
  gpu_virtual_address = Object->GpuVirtualAddress;
  uat_result = AppleAgxUatUnmap(context, Roots, gpu_virtual_address,
                                Object->Length, Allocator, Inventory);
  if (uat_result != AppleAgxUatResultOk) {
    AppleAgxResidencySetStatus(Status, AppleAgxMemoryResultOk, uat_result);
    return APPLE_AGX_FALSE;
  }

  memory_result = AppleAgxMemoryMarkGpuUnmapped(Object);
  if (memory_result != AppleAgxMemoryResultOk) {
    /* Preserve atomic ownership if an unexpected memory-state invariant fails. */
    if (Object->DevicePages != APPLE_AGX_RESIDENCY_NULL)
      uat_result = AppleAgxUatMapPageList(
          context, Roots, gpu_virtual_address, Object->DevicePages,
          Object->DevicePageCount, AppleAgxUatGpuSharedReadWrite, Allocator,
          Inventory);
    else
      uat_result = AppleAgxUatMap(
          context, Roots, gpu_virtual_address, Object->DeviceAddress,
          Object->Length, AppleAgxUatGpuSharedReadWrite, Allocator, Inventory);
    AppleAgxResidencySetStatus(Status, memory_result, uat_result);
    return APPLE_AGX_FALSE;
  }

  AppleAgxResidencySetStatus(Status, AppleAgxMemoryResultOk,
                             AppleAgxUatResultOk);
  return APPLE_AGX_TRUE;
}
