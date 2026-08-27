#include "apple_agx_memory.h"

static void AppleAgxMemoryZero(APPLE_AGX_MEMORY_OBJECT *Object) {
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

static unsigned char AppleAgxMemoryIoValid(const APPLE_AGX_MEMORY_IO *Io) {
  return Io != 0 && Io->AllocateContiguous != 0 && Io->FreeContiguous != 0;
}

APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryAllocate(const APPLE_AGX_MEMORY_IO *Io, unsigned long long Length,
                       APPLE_AGX_MEMORY_OBJECT *Object) {
  unsigned long long allocation_length;
  unsigned long long alignment_offset;
  void *cpu_base = 0;
  void *handle = 0;
  unsigned long long device_base = 0ULL;

  if (Object == 0 || AppleAgxMemoryIoValid(Io) == 0u || Length == 0ULL ||
      (Length & (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)) != 0ULL ||
      Length > ~0ULL - APPLE_AGX_MEMORY_PAGE_SIZE) {
    return AppleAgxMemoryResultInvalidArgument;
  }
  if (Object->State != AppleAgxMemoryEmpty) {
    return AppleAgxMemoryResultBusy;
  }

  /* A full extra device page guarantees an aligned contained view. */
  allocation_length = Length + APPLE_AGX_MEMORY_PAGE_SIZE;
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

  alignment_offset = (APPLE_AGX_MEMORY_PAGE_SIZE -
                      (device_base & (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL))) &
                     (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL);
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
  Object->GpuVirtualAddress = 0ULL;
  Object->SubmittedFence = 0ULL;
  Object->Context = 0u;
  Object->State = AppleAgxMemoryCpuOwned;
  return AppleAgxMemoryResultOk;
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
      Context == 0u || Context >= 63u) {
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
  if (Object->State != AppleAgxMemoryGpuMapped) {
    return AppleAgxMemoryResultBusy;
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
  if (Io->FreeContiguous(Io->Context, Object->AllocationHandle) == 0u) {
    return AppleAgxMemoryResultAllocationFailed;
  }
  AppleAgxMemoryZero(Object);
  return AppleAgxMemoryResultOk;
}
