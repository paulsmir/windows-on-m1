#include "apple_agx_memory.h"

static void AppleAgxMemoryZero(APPLE_AGX_MEMORY_OBJECT *Object) {
  Object->AllocationCpuBase = 0;
  Object->CpuAddress = 0;
  Object->AllocationHandle = 0;
  Object->AllocationDeviceBase = 0ULL;
  Object->DeviceAddress = 0ULL;
  Object->AllocationLength = 0ULL;
  Object->Length = 0ULL;
  Object->DevicePages = 0;
  Object->DevicePageCount = 0u;
  Object->GpuVirtualAddress = 0ULL;
  Object->SubmittedFence = 0ULL;
  Object->Context = 0u;
  Object->State = AppleAgxMemoryEmpty;
}

static unsigned char AppleAgxMemoryIoValid(const APPLE_AGX_MEMORY_IO *Io) {
  return Io != 0 && Io->AllocateContiguous != 0 && Io->FreeContiguous != 0 &&
         ((Io->AllocatePageList == 0 && Io->FreePageList == 0) ||
          (Io->AllocatePageList != 0 && Io->FreePageList != 0));
}

static unsigned char AppleAgxMemoryFenceAfter(unsigned long long Candidate,
                                               unsigned long long Reference) {
  unsigned long long distance = Candidate - Reference;
  return distance != 0ULL && distance < (1ULL << 63) ? 1u : 0u;
}

APPLE_AGX_MEMORY_RESULT AppleAgxMemoryAllocateAligned(
    const APPLE_AGX_MEMORY_IO *Io, unsigned long long Length,
    unsigned long long Alignment, APPLE_AGX_MEMORY_OBJECT *Object) {
  unsigned long long allocation_length;
  unsigned long long alignment_offset;
  void *cpu_base = 0;
  void *handle = 0;
  unsigned long long device_base = 0ULL;
  const unsigned long long *device_pages = 0;
  unsigned int device_page_count = 0u;

  if (Object == 0 || AppleAgxMemoryIoValid(Io) == 0u || Length == 0ULL ||
      (Length & (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL ||
      Alignment < APPLE_AGX_MEMORY_PAGE_SIZE ||
      (Alignment & (Alignment - 1ULL)) != 0ULL ||
      (Alignment & (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL ||
      Length > ~0ULL - Alignment) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State != AppleAgxMemoryEmpty) {
    return AppleAgxMemoryResultBusy;
  }

  if (Io->AllocatePageList != 0) {
    if (Io->AllocatePageList(Io->Context, Length, &cpu_base, &device_pages,
                             &device_page_count, &handle) == 0u ||
        cpu_base == 0 || handle == 0 || device_pages == 0 ||
        device_page_count != Length / APPLE_AGX_MEMORY_PAGE_SIZE ||
        device_pages[0] == 0ULL) {
      if (handle != 0)
        (void)Io->FreePageList(Io->Context, handle);
      AppleAgxMemoryZero(Object);
      return AppleAgxMemoryResultAllocationFailed;
    }
    Object->AllocationCpuBase = cpu_base;
    Object->CpuAddress = cpu_base;
    Object->AllocationHandle = handle;
    Object->AllocationDeviceBase = device_pages[0];
    Object->DeviceAddress = device_pages[0];
    Object->AllocationLength = Length;
    Object->Length = Length;
    Object->DevicePages = device_pages;
    Object->DevicePageCount = device_page_count;
    Object->GpuVirtualAddress = 0ULL;
    Object->SubmittedFence = 0ULL;
    Object->Context = 0u;
    Object->State = AppleAgxMemoryCpuOwned;
    return AppleAgxMemoryResultOk;
  }

  /* A full extra alignment unit guarantees an aligned contained view. */
  allocation_length = Length + Alignment;
  if (Io->AllocateContiguous(Io->Context, allocation_length, &cpu_base,
                             &device_base, &handle) == 0u) {
    AppleAgxMemoryZero(Object);
    return AppleAgxMemoryResultAllocationFailed;
  }
  if (cpu_base == 0 || handle == 0 || device_base == 0ULL ||
      device_base > ~0ULL - allocation_length) {
    if (handle != 0)
      (void)Io->FreeContiguous(Io->Context, handle);
    AppleAgxMemoryZero(Object);
    return AppleAgxMemoryResultAllocationFailed;
  }

  alignment_offset =
      (Alignment - (device_base & (Alignment - 1ULL))) & (Alignment - 1ULL);
  if (device_base + alignment_offset >= APPLE_AGX_MEMORY_DEVICE_ADDRESS_LIMIT ||
      Length > APPLE_AGX_MEMORY_DEVICE_ADDRESS_LIMIT -
                   (device_base + alignment_offset)) {
    (void)Io->FreeContiguous(Io->Context, handle);
    AppleAgxMemoryZero(Object);
    return AppleAgxMemoryResultOutOfRange;
  }

  Object->AllocationCpuBase = cpu_base;
  Object->CpuAddress = (void *)((unsigned char *)cpu_base + alignment_offset);
  Object->AllocationHandle = handle;
  Object->AllocationDeviceBase = device_base;
  Object->DeviceAddress = device_base + alignment_offset;
  Object->AllocationLength = allocation_length;
  Object->Length = Length;
  Object->DevicePages = 0;
  Object->DevicePageCount = 0u;
  Object->GpuVirtualAddress = 0ULL;
  Object->SubmittedFence = 0ULL;
  Object->Context = 0u;
  Object->State = AppleAgxMemoryCpuOwned;
  return AppleAgxMemoryResultOk;
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryAllocate(const APPLE_AGX_MEMORY_IO *Io, unsigned long long Length,
                       APPLE_AGX_MEMORY_OBJECT *Object) {
  return AppleAgxMemoryAllocateAligned(Io, Length, APPLE_AGX_MEMORY_PAGE_SIZE,
                                       Object);
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkCpuWritten(APPLE_AGX_MEMORY_OBJECT *Object) {
  if (Object == 0 || Object->State == AppleAgxMemoryEmpty) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State == AppleAgxMemoryInFlight ||
      Object->State == AppleAgxMemoryGpuMapped) {
    return AppleAgxMemoryResultBusy;
  }
  if (Object->GpuVirtualAddress != 0ULL) {
    return AppleAgxMemoryResultBusy;
  }
  Object->State = AppleAgxMemoryCpuOwned;
  Object->SubmittedFence = 0ULL;
  return AppleAgxMemoryResultOk;
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkPrepared(APPLE_AGX_MEMORY_OBJECT *Object) {
  if (Object == 0 || Object->State == AppleAgxMemoryEmpty) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State != AppleAgxMemoryCpuOwned) {
    return AppleAgxMemoryResultBusy;
  }
  Object->State = AppleAgxMemoryPrepared;
  return AppleAgxMemoryResultOk;
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkGpuMapped(APPLE_AGX_MEMORY_OBJECT *Object,
                            unsigned int Context,
                            unsigned long long GpuVirtualAddress) {
  if (Object == 0 || GpuVirtualAddress == 0ULL ||
      (GpuVirtualAddress & (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL ||
      Context >= 64u) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State != AppleAgxMemoryPrepared) {
    return AppleAgxMemoryResultNotPrepared;
  }
  Object->Context = Context;
  Object->GpuVirtualAddress = GpuVirtualAddress;
  Object->State = AppleAgxMemoryGpuMapped;
  return AppleAgxMemoryResultOk;
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkSubmitted(APPLE_AGX_MEMORY_OBJECT *Object,
                            unsigned long long Fence) {
  if (Object == 0 || Fence == 0ULL) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State != AppleAgxMemoryGpuMapped &&
      Object->State != AppleAgxMemoryCompleted) {
    return AppleAgxMemoryResultBusy;
  }
  if (Object->State == AppleAgxMemoryCompleted &&
      AppleAgxMemoryFenceAfter(Fence, Object->SubmittedFence) == 0u) {
    return AppleAgxMemoryResultStaleFence;
  }
  Object->SubmittedFence = Fence;
  Object->State = AppleAgxMemoryInFlight;
  return AppleAgxMemoryResultOk;
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkCompleted(APPLE_AGX_MEMORY_OBJECT *Object,
                            unsigned long long Fence) {
  if (Object == 0 || Fence == 0ULL) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State != AppleAgxMemoryInFlight) {
    return AppleAgxMemoryResultBusy;
  }
  if (Fence != Object->SubmittedFence) {
    return AppleAgxMemoryResultStaleFence;
  }
  Object->State = AppleAgxMemoryCompleted;
  return AppleAgxMemoryResultOk;
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkAborted(APPLE_AGX_MEMORY_OBJECT *Object,
                          unsigned long long Fence) {
  /* Completion and abort have the same ownership transition.  Their Windows
   * notification semantics remain deliberately separate in the scheduler. */
  return AppleAgxMemoryMarkCompleted(Object, Fence);
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkGpuUnmapped(APPLE_AGX_MEMORY_OBJECT *Object) {
  if (Object == 0 || Object->State == AppleAgxMemoryEmpty) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State == AppleAgxMemoryInFlight) {
    return AppleAgxMemoryResultBusy;
  }
  if (Object->State != AppleAgxMemoryGpuMapped &&
      Object->State != AppleAgxMemoryCompleted) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  Object->Context = 0u;
  Object->GpuVirtualAddress = 0ULL;
  Object->SubmittedFence = 0ULL;
  Object->State = AppleAgxMemoryPrepared;
  return AppleAgxMemoryResultOk;
}

APPLE_AGX_MEMORY_RESULT AppleAgxMemoryRelease(const APPLE_AGX_MEMORY_IO *Io,
                                              APPLE_AGX_MEMORY_OBJECT *Object) {
  if (Object == 0 || AppleAgxMemoryIoValid(Io) == 0u) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State == AppleAgxMemoryEmpty) {
    return AppleAgxMemoryResultOk;
  }
  if (Object->State != AppleAgxMemoryCpuOwned &&
      Object->State != AppleAgxMemoryPrepared) {
    return AppleAgxMemoryResultBusy;
  }
  if ((Object->DevicePages != 0
           ? Io->FreePageList(Io->Context, Object->AllocationHandle)
           : Io->FreeContiguous(Io->Context, Object->AllocationHandle)) ==
      0u) {
    return AppleAgxMemoryResultAllocationFailed;
  }
  AppleAgxMemoryZero(Object);
  return AppleAgxMemoryResultOk;
}
