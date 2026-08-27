#include "apple_agx_regionb_memory.h"

#define APPLE_AGX_REGIONB_MEMORY_PAGE_MASK \
  (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)

static const unsigned long long AppleAgxRegionBMemoryContentSizes[
    APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT] = {
    J313_AGX_G2_REGIONB_STATS_TA_SIZE,
    J313_AGX_G2_REGIONB_STATS_3D_SIZE,
    J313_AGX_G2_REGIONB_STATS_CP_SIZE,
    J313_AGX_G2_REGIONB_HWDATA_A_SIZE,
    J313_AGX_G2_REGIONB_FAULT_INFO_SIZE,
    J313_AGX_G2_REGIONB_TIMESTAMP_SIZE,
    J313_AGX_G2_REGIONB_HWDATA_B_SIZE,
    J313_AGX_G2_REGIONB_UNKNOWN_1B8_SIZE,
    J313_AGX_G2_REGIONB_UNKNOWN_1C0_SIZE,
    J313_AGX_G2_REGIONB_UNKNOWN_1C8_SIZE,
    J313_AGX_G2_REGIONB_BUFFER_MGR_CTL_SIZE,
};

static unsigned long long AppleAgxRegionBMemoryAlignUp(
    unsigned long long Value) {
  return (Value + APPLE_AGX_REGIONB_MEMORY_PAGE_MASK) &
         ~APPLE_AGX_REGIONB_MEMORY_PAGE_MASK;
}

static void AppleAgxRegionBMemoryZero(void *Address,
                                      unsigned long long Length) {
  unsigned long long index;
  for (index = 0ULL; index < Length; ++index)
    ((unsigned char *)Address)[index] = 0u;
}

static unsigned char AppleAgxRegionBMemoryStorageIsEmpty(
    const APPLE_AGX_REGIONB_MEMORY_OWNER *Owner) {
  unsigned int index;
  for (index = 0u; index < APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT; ++index) {
    if (Owner->Objects[index].State != AppleAgxMemoryEmpty)
      return 0u;
  }
  return 1u;
}

static unsigned char AppleAgxRegionBMemoryHighRangeIsValid(
    unsigned long long VirtualAddress, unsigned long long Length) {
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_RESULT result = AppleAgxUatValidateRange(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, VirtualAddress, 0ULL, Length,
      AppleAgxUatFirmwareSharedReadWrite, &half);
  return result == AppleAgxUatResultOk && half == AppleAgxUatTtbr1;
}

static void AppleAgxRegionBMemoryClearPublished(
    APPLE_AGX_REGIONB_MEMORY_OWNER *Owner) {
  unsigned char *input = (unsigned char *)&Owner->Input;
  unsigned int index;
  for (index = 0u; index < sizeof(Owner->Input); ++index)
    input[index] = 0u;
  Owner->Built = 0u;
}

static APPLE_AGX_REGIONB_MEMORY_RESULT AppleAgxRegionBMemoryRollback(
    APPLE_AGX_REGIONB_MEMORY_OWNER *Owner,
    APPLE_AGX_REGIONB_MEMORY_RESULT Failure) {
  if (AppleAgxRegionBMemoryDestroy(Owner) !=
      AppleAgxRegionBMemoryResultOk)
    return AppleAgxRegionBMemoryResultReleaseFailed;
  Owner->LastResult = Failure;
  return Failure;
}

APPLE_AGX_REGIONB_MEMORY_RESULT AppleAgxRegionBMemoryBuild(
    APPLE_AGX_REGIONB_MEMORY_OWNER *Owner,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    unsigned long long FirstVirtualAddress,
    unsigned long long FwlogRingAddress) {
  unsigned long long virtual_address = FirstVirtualAddress;
  unsigned int index;

  if (Owner == 0 || MemoryIo == 0 || MemoryIo->AllocateContiguous == 0 ||
      MemoryIo->FreeContiguous == 0 || Owner->Initialized != 0u ||
      Owner->Built != 0u || Owner->ObjectCount != 0u ||
      AppleAgxRegionBMemoryStorageIsEmpty(Owner) == 0u ||
      (FirstVirtualAddress & APPLE_AGX_REGIONB_MEMORY_PAGE_MASK) != 0ULL ||
      AppleAgxRegionBMemoryHighRangeIsValid(
          FirstVirtualAddress, APPLE_AGX_MEMORY_PAGE_SIZE) == 0u ||
      AppleAgxRegionBMemoryHighRangeIsValid(
          FwlogRingAddress, APPLE_AGX_MEMORY_PAGE_SIZE) == 0u)
    return AppleAgxRegionBMemoryResultInvalidArgument;

  Owner->MemoryIo = MemoryIo;
  Owner->Initialized = 1u;
  Owner->LastResult = AppleAgxRegionBMemoryResultOk;
  for (index = 0u; index < APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT; ++index) {
    unsigned long long allocation_size =
        AppleAgxRegionBMemoryAlignUp(AppleAgxRegionBMemoryContentSizes[index]);
    if (AppleAgxRegionBMemoryHighRangeIsValid(virtual_address,
                                              allocation_size) == 0u)
      return AppleAgxRegionBMemoryRollback(
          Owner, AppleAgxRegionBMemoryResultInvalidArgument);
    if (AppleAgxMemoryAllocate(MemoryIo, allocation_size,
                               &Owner->Objects[index]) !=
        AppleAgxMemoryResultOk)
      return AppleAgxRegionBMemoryRollback(
          Owner, AppleAgxRegionBMemoryResultAllocationFailed);
    ++Owner->ObjectCount;
    AppleAgxRegionBMemoryZero(Owner->Objects[index].CpuAddress,
                              allocation_size);
    Owner->VirtualAddresses[index] = virtual_address;
    virtual_address += allocation_size + APPLE_AGX_MEMORY_PAGE_SIZE;
  }

  Owner->Input.StatsTaAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryStatsTa];
  Owner->Input.Stats3dAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryStats3d];
  Owner->Input.StatsCpAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryStatsCp];
  Owner->Input.HwdataAAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryHwdataA];
  Owner->Input.FaultInfoAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryFaultInfo];
  Owner->Input.TimestampAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryTimestamp];
  Owner->Input.HwdataBAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryHwdataB];
  Owner->Input.FwlogRingAddress = FwlogRingAddress;
  Owner->Input.Unknown1b8Address =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryUnknown1b8];
  Owner->Input.Unknown1c0Address =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryUnknown1c0];
  Owner->Input.Unknown1c8Address =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryUnknown1c8];
  Owner->Input.BufferManagerGpuAddress =
      J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA;
  Owner->Input.BufferManagerCpuAddress =
      Owner->VirtualAddresses[AppleAgxRegionBMemoryBufferManager];
  Owner->Built = 1u;
  return Owner->LastResult;
}

APPLE_AGX_REGIONB_MEMORY_RESULT AppleAgxRegionBMemoryDestroy(
    APPLE_AGX_REGIONB_MEMORY_OWNER *Owner) {
  unsigned int index;
  if (Owner == 0)
    return AppleAgxRegionBMemoryResultInvalidArgument;
  if (Owner->Initialized == 0u)
    return AppleAgxRegionBMemoryResultOk;
  AppleAgxRegionBMemoryClearPublished(Owner);
  while (Owner->ObjectCount > 0u) {
    index = Owner->ObjectCount - 1u;
    if (AppleAgxMemoryRelease(Owner->MemoryIo, &Owner->Objects[index]) !=
        AppleAgxMemoryResultOk) {
      Owner->LastResult = AppleAgxRegionBMemoryResultReleaseFailed;
      return Owner->LastResult;
    }
    Owner->VirtualAddresses[index] = 0ULL;
    --Owner->ObjectCount;
  }
  Owner->MemoryIo = 0;
  Owner->Initialized = 0u;
  Owner->LastResult = AppleAgxRegionBMemoryResultOk;
  return Owner->LastResult;
}
