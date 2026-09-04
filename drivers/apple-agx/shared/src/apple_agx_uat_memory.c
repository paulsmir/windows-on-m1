#include "apple_agx_uat_memory.h"

static void AppleAgxUatMemoryZeroObject(APPLE_AGX_MEMORY_OBJECT *Object) {
  Object->AllocationCpuBase = 0;
  Object->CpuAddress = 0;
  Object->AllocationHandle = 0;
  Object->AllocationDeviceBase = 0ULL;
  Object->DeviceAddress = 0ULL;
  Object->AllocationLength = 0ULL;
  Object->Length = 0ULL;
  Object->GpuVirtualAddress = 0ULL;
  Object->SubmittedFence = 0ULL;
  Object->Context = 0u;
  Object->State = AppleAgxMemoryEmpty;
}

static unsigned char AppleAgxUatMemoryOwnerValid(
    const APPLE_AGX_UAT_MEMORY_OWNER *Owner) {
  return Owner != 0 && Owner->MemoryIo != 0 && Owner->Objects != 0 &&
         Owner->ObjectCapacity != 0u &&
         Owner->ObjectCount <= Owner->ObjectCapacity &&
         Owner->MemoryIo->AllocateContiguous != 0 &&
         Owner->MemoryIo->FreeContiguous != 0;
}

static APPLE_AGX_UAT_MEMORY_RESULT AppleAgxUatMemoryResultFromAllocation(
    APPLE_AGX_MEMORY_RESULT Result) {
  if (Result == AppleAgxMemoryResultOk)
    return AppleAgxUatMemoryResultOk;
  return AppleAgxUatMemoryResultAllocationFailed;
}

static unsigned char AppleAgxUatMemoryAllocatePage(
    void *Context, APPLE_AGX_UAT_PAGE *Page) {
  APPLE_AGX_UAT_MEMORY_OWNER *owner =
      (APPLE_AGX_UAT_MEMORY_OWNER *)Context;
  APPLE_AGX_MEMORY_OBJECT *object;
  APPLE_AGX_MEMORY_RESULT memory_result;
  unsigned long long index;

  if (Page == 0 || AppleAgxUatMemoryOwnerValid(owner) == 0u) {
    if (owner != 0)
      owner->LastResult = AppleAgxUatMemoryResultInvalidArgument;
    return 0u;
  }
  if (owner->ObjectCount >= owner->ObjectCapacity) {
    owner->LastResult = AppleAgxUatMemoryResultCapacity;
    return 0u;
  }

  object = &owner->Objects[owner->ObjectCount];
  AppleAgxUatMemoryZeroObject(object);
  memory_result =
      AppleAgxMemoryAllocate(owner->MemoryIo, APPLE_AGX_MEMORY_PAGE_SIZE, object);
  if (memory_result != AppleAgxMemoryResultOk) {
    owner->LastResult = AppleAgxUatMemoryResultFromAllocation(memory_result);
    return 0u;
  }
  if (object->CpuAddress == 0 || object->DeviceAddress == 0ULL ||
      (((unsigned long long)(void *)object->CpuAddress) &
       (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL ||
      (object->DeviceAddress & (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL) {
    memory_result = AppleAgxMemoryRelease(owner->MemoryIo, object);
    if (memory_result != AppleAgxMemoryResultOk) {
      ++owner->ObjectCount;
      owner->LastResult = AppleAgxUatMemoryResultReleaseFailed;
    } else {
      owner->LastResult = AppleAgxUatMemoryResultAllocationFailed;
    }
    return 0u;
  }

  for (index = 0ULL; index < APPLE_AGX_MEMORY_PAGE_SIZE; ++index)
    ((unsigned char *)object->CpuAddress)[index] = 0u;
  Page->PhysicalAddress = object->DeviceAddress;
  Page->Entries = (unsigned long long *)object->CpuAddress;
  ++owner->ObjectCount;
  owner->LastResult = AppleAgxUatMemoryResultOk;
  return 1u;
}

static void AppleAgxUatMemoryReleasePage(
    void *Context, const APPLE_AGX_UAT_PAGE *Page) {
  APPLE_AGX_UAT_MEMORY_OWNER *owner =
      (APPLE_AGX_UAT_MEMORY_OWNER *)Context;
  unsigned int index;

  if (Page == 0 || AppleAgxUatMemoryOwnerValid(owner) == 0u) {
    if (owner != 0)
      owner->LastResult = AppleAgxUatMemoryResultInvalidArgument;
    return;
  }
  for (index = owner->ObjectCount; index > 0u; --index) {
    APPLE_AGX_MEMORY_OBJECT *object = &owner->Objects[index - 1u];
    unsigned int move_index;
    if (object->CpuAddress != (void *)Page->Entries ||
        object->DeviceAddress != Page->PhysicalAddress)
      continue;
    if (AppleAgxMemoryRelease(owner->MemoryIo, object) !=
        AppleAgxMemoryResultOk) {
      owner->LastResult = AppleAgxUatMemoryResultReleaseFailed;
      return;
    }
    for (move_index = index; move_index < owner->ObjectCount; ++move_index)
      owner->Objects[move_index - 1u] = owner->Objects[move_index];
    --owner->ObjectCount;
    AppleAgxUatMemoryZeroObject(&owner->Objects[owner->ObjectCount]);
    return;
  }
  owner->LastResult = AppleAgxUatMemoryResultNotOwned;
}

APPLE_AGX_UAT_MEMORY_RESULT AppleAgxUatMemoryOwnerInitialize(
    APPLE_AGX_UAT_MEMORY_OWNER *Owner, const APPLE_AGX_MEMORY_IO *MemoryIo,
    APPLE_AGX_MEMORY_OBJECT *Objects, unsigned int ObjectCapacity) {
  unsigned int index;

  if (Owner == 0 || MemoryIo == 0 || Objects == 0 || ObjectCapacity == 0u ||
      MemoryIo->AllocateContiguous == 0 || MemoryIo->FreeContiguous == 0)
    return AppleAgxUatMemoryResultInvalidArgument;
  for (index = 0u; index < ObjectCapacity; ++index) {
    if (Objects[index].State != AppleAgxMemoryEmpty)
      return AppleAgxUatMemoryResultInvalidArgument;
  }
  Owner->MemoryIo = MemoryIo;
  Owner->Objects = Objects;
  Owner->ObjectCapacity = ObjectCapacity;
  Owner->ObjectCount = 0u;
  Owner->LastResult = AppleAgxUatMemoryResultOk;
  return AppleAgxUatMemoryResultOk;
}

APPLE_AGX_UAT_MEMORY_RESULT AppleAgxUatMemoryOwnerGetAllocator(
    APPLE_AGX_UAT_MEMORY_OWNER *Owner, APPLE_AGX_UAT_ALLOCATOR *Allocator) {
  if (Allocator == 0 || AppleAgxUatMemoryOwnerValid(Owner) == 0u)
    return AppleAgxUatMemoryResultInvalidArgument;
  Allocator->Context = Owner;
  Allocator->AllocatePage = AppleAgxUatMemoryAllocatePage;
  Allocator->ReleasePage = AppleAgxUatMemoryReleasePage;
  return AppleAgxUatMemoryResultOk;
}

APPLE_AGX_UAT_MEMORY_RESULT
AppleAgxUatMemoryOwnerDestroy(APPLE_AGX_UAT_MEMORY_OWNER *Owner) {
  if (AppleAgxUatMemoryOwnerValid(Owner) == 0u)
    return AppleAgxUatMemoryResultInvalidArgument;
  while (Owner->ObjectCount > 0u) {
    APPLE_AGX_MEMORY_OBJECT *object =
        &Owner->Objects[Owner->ObjectCount - 1u];
    if (AppleAgxMemoryRelease(Owner->MemoryIo, object) !=
        AppleAgxMemoryResultOk) {
      Owner->LastResult = AppleAgxUatMemoryResultReleaseFailed;
      return Owner->LastResult;
    }
    --Owner->ObjectCount;
  }
  Owner->LastResult = AppleAgxUatMemoryResultOk;
  return Owner->LastResult;
}
