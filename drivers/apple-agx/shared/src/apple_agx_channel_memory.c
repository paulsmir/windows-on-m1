#include "apple_agx_channel_memory.h"

#define APPLE_AGX_CHANNEL_MEMORY_PAGE_MASK \
  (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)

static unsigned long long AppleAgxChannelMemoryAlignUp(
    unsigned long long Value) {
  return (Value + APPLE_AGX_CHANNEL_MEMORY_PAGE_MASK) &
         ~APPLE_AGX_CHANNEL_MEMORY_PAGE_MASK;
}

static unsigned long long AppleAgxChannelMemoryContentSize(
    unsigned int Index) {
  if (Index <= AppleAgxChannelMemoryCommandStateLast)
    return J313_AGX_G2_CHANNEL_STATE_STRIDE;
  if (Index <= AppleAgxChannelMemoryCommandRingLast)
    return J313_AGX_G2_CMD_QUEUE_RING_SIZE;
  switch (Index) {
    case AppleAgxChannelMemoryDevctrlState:
    case AppleAgxChannelMemoryEventState:
    case AppleAgxChannelMemoryKtraceState:
    case AppleAgxChannelMemoryStatsState:
      return J313_AGX_G2_CHANNEL_STATE_STRIDE;
    case AppleAgxChannelMemoryDevctrlRing:
      return J313_AGX_G2_DEVCTRL_RING_SIZE;
    case AppleAgxChannelMemoryEventRing:
      return J313_AGX_G2_EVENT_RING_SIZE;
    case AppleAgxChannelMemoryFwlogState:
      return J313_AGX_G2_FWLOG_STATE_SIZE;
    case AppleAgxChannelMemoryFwlogRing:
      return J313_AGX_G2_FWLOG_RING_SIZE;
    case AppleAgxChannelMemoryFwlogDummyRing:
      return J313_AGX_G2_FWLOG_DUMMY_RING_SIZE;
    case AppleAgxChannelMemoryKtraceRing:
      return J313_AGX_G2_KTRACE_RING_SIZE;
    case AppleAgxChannelMemoryStatsRing:
      return J313_AGX_G2_STATS_RING_SIZE;
    default:
      return 0ULL;
  }
}

static void AppleAgxChannelMemoryZero(void *Address,
                                      unsigned long long Length) {
  unsigned long long index;
  for (index = 0ULL; index < Length; ++index)
    ((unsigned char *)Address)[index] = 0u;
}

static unsigned char AppleAgxChannelMemoryStorageIsEmpty(
    const APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner) {
  unsigned int index;
  for (index = 0u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index) {
    if (Owner->Objects[index].State != AppleAgxMemoryEmpty)
      return 0u;
  }
  return 1u;
}

static unsigned char AppleAgxChannelMemoryRangeIsValid(
    unsigned long long VirtualAddress, unsigned long long Length) {
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_RESULT result = AppleAgxUatValidateRange(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, VirtualAddress, 0ULL, Length,
      AppleAgxUatFirmwareSharedReadWrite, &half);
  return result == AppleAgxUatResultOk && half == AppleAgxUatTtbr1;
}

static void AppleAgxChannelMemoryClearPublished(
    APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner) {
  unsigned int index;
  for (index = 0u; index < J313_AGX_G2_CHANNEL_INFO_COUNT; ++index) {
    Owner->ChannelInfo.Entries[index].StateAddress = 0ULL;
    Owner->ChannelInfo.Entries[index].RingAddress = 0ULL;
  }
  Owner->RealFwlogRingAddress = 0ULL;
  Owner->Built = 0u;
}

static APPLE_AGX_CHANNEL_MEMORY_RESULT AppleAgxChannelMemoryRollback(
    APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner,
    APPLE_AGX_CHANNEL_MEMORY_RESULT Failure) {
  if (AppleAgxChannelMemoryDestroy(Owner) != AppleAgxChannelMemoryResultOk)
    return AppleAgxChannelMemoryResultReleaseFailed;
  Owner->LastResult = Failure;
  return Failure;
}

APPLE_AGX_CHANNEL_MEMORY_RESULT AppleAgxChannelMemoryBuild(
    APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    unsigned long long FirstVirtualAddress) {
  unsigned long long virtual_address = FirstVirtualAddress;
  unsigned int index;

  if (Owner == 0 || MemoryIo == 0 || MemoryIo->AllocateContiguous == 0 ||
      MemoryIo->FreeContiguous == 0 || Owner->Initialized != 0u ||
      Owner->Built != 0u || Owner->ObjectCount != 0u ||
      AppleAgxChannelMemoryStorageIsEmpty(Owner) == 0u ||
      (FirstVirtualAddress & APPLE_AGX_CHANNEL_MEMORY_PAGE_MASK) != 0ULL ||
      AppleAgxChannelMemoryRangeIsValid(
          FirstVirtualAddress, APPLE_AGX_MEMORY_PAGE_SIZE) == 0u)
    return AppleAgxChannelMemoryResultInvalidArgument;

  Owner->MemoryIo = MemoryIo;
  Owner->Initialized = 1u;
  Owner->LastResult = AppleAgxChannelMemoryResultOk;
  for (index = 0u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index) {
    unsigned long long content_size = AppleAgxChannelMemoryContentSize(index);
    unsigned long long allocation_size =
        AppleAgxChannelMemoryAlignUp(content_size);
    if (content_size == 0ULL ||
        AppleAgxChannelMemoryRangeIsValid(virtual_address,
                                          allocation_size) == 0u)
      return AppleAgxChannelMemoryRollback(
          Owner, AppleAgxChannelMemoryResultInvalidArgument);
    if (AppleAgxMemoryAllocate(MemoryIo, allocation_size,
                               &Owner->Objects[index]) !=
        AppleAgxMemoryResultOk)
      return AppleAgxChannelMemoryRollback(
          Owner, AppleAgxChannelMemoryResultAllocationFailed);
    ++Owner->ObjectCount;
    AppleAgxChannelMemoryZero(Owner->Objects[index].CpuAddress,
                              allocation_size);
    Owner->VirtualAddresses[index] = virtual_address;
    virtual_address += allocation_size + APPLE_AGX_MEMORY_PAGE_SIZE;
  }

  for (index = 0u; index < J313_AGX_G2_CMD_QUEUE_CHANNEL_COUNT; ++index) {
    Owner->ChannelInfo.Entries[index].StateAddress =
        Owner->VirtualAddresses[AppleAgxChannelMemoryCommandStateBase + index];
    Owner->ChannelInfo.Entries[index].RingAddress =
        Owner->VirtualAddresses[AppleAgxChannelMemoryCommandRingBase + index];
  }
  Owner->ChannelInfo.Entries[12].StateAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryDevctrlState];
  Owner->ChannelInfo.Entries[12].RingAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryDevctrlRing];
  Owner->ChannelInfo.Entries[13].StateAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryEventState];
  Owner->ChannelInfo.Entries[13].RingAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryEventRing];
  Owner->ChannelInfo.Entries[14].StateAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryFwlogState];
  Owner->ChannelInfo.Entries[14].RingAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryFwlogDummyRing];
  Owner->RealFwlogRingAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryFwlogRing];
  Owner->ChannelInfo.Entries[15].StateAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryKtraceState];
  Owner->ChannelInfo.Entries[15].RingAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryKtraceRing];
  Owner->ChannelInfo.Entries[16].StateAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryStatsState];
  Owner->ChannelInfo.Entries[16].RingAddress =
      Owner->VirtualAddresses[AppleAgxChannelMemoryStatsRing];
  Owner->Built = 1u;
  return Owner->LastResult;
}

APPLE_AGX_CHANNEL_MEMORY_RESULT AppleAgxChannelMemoryDestroy(
    APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner) {
  unsigned int index;

  if (Owner == 0)
    return AppleAgxChannelMemoryResultInvalidArgument;
  if (Owner->Initialized == 0u)
    return AppleAgxChannelMemoryResultOk;
  AppleAgxChannelMemoryClearPublished(Owner);
  while (Owner->ObjectCount > 0u) {
    index = Owner->ObjectCount - 1u;
    if (AppleAgxMemoryRelease(Owner->MemoryIo, &Owner->Objects[index]) !=
        AppleAgxMemoryResultOk) {
      Owner->LastResult = AppleAgxChannelMemoryResultReleaseFailed;
      return Owner->LastResult;
    }
    Owner->VirtualAddresses[index] = 0ULL;
    --Owner->ObjectCount;
  }
  Owner->MemoryIo = 0;
  Owner->Initialized = 0u;
  Owner->LastResult = AppleAgxChannelMemoryResultOk;
  return Owner->LastResult;
}
