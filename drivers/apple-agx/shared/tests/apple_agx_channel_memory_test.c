#include "apple_agx_channel_memory.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ALLOCATION_COUNT 40u

typedef struct _FAKE_ALLOCATION {
  void *Storage;
  unsigned long long Length;
  unsigned int Slot;
  unsigned char Active;
} FAKE_ALLOCATION;

typedef struct _FAKE_MEMORY {
  FAKE_ALLOCATION Allocations[TEST_ALLOCATION_COUNT];
  unsigned int AllocateCount;
  unsigned int FailAllocateCall;
  unsigned int FailFreeSlot;
  unsigned int FreeCount;
} FAKE_MEMORY;

static unsigned char allocate_contiguous(
    void *context, unsigned long long bytes, void **cpu_base,
    unsigned long long *device_base, void **allocation_handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  unsigned int slot = fake->AllocateCount++;
  FAKE_ALLOCATION *allocation;

  assert(slot < TEST_ALLOCATION_COUNT);
  if (fake->FailAllocateCall != 0u &&
      fake->AllocateCount == fake->FailAllocateCall)
    return 0u;
  allocation = &fake->Allocations[slot];
  allocation->Storage = aligned_alloc(APPLE_AGX_MEMORY_PAGE_SIZE, bytes);
  assert(allocation->Storage != 0);
  memset(allocation->Storage, 0xa5, (size_t)bytes);
  allocation->Length = bytes;
  allocation->Slot = slot;
  allocation->Active = 1u;
  *cpu_base = allocation->Storage;
  *device_base = 0x10000000ULL + slot * 0x200000ULL;
  *allocation_handle = allocation;
  return 1u;
}

static unsigned char free_contiguous(void *context, void *handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  FAKE_ALLOCATION *allocation = (FAKE_ALLOCATION *)handle;
  assert(allocation >= &fake->Allocations[0]);
  assert(allocation < &fake->Allocations[TEST_ALLOCATION_COUNT]);
  assert(allocation->Active != 0u);
  if (fake->FailFreeSlot != 0u &&
      allocation->Slot + 1u == fake->FailFreeSlot)
    return 0u;
  free(allocation->Storage);
  allocation->Storage = 0;
  allocation->Active = 0u;
  ++fake->FreeCount;
  return 1u;
}

static void init_fixture(FAKE_MEMORY *fake, APPLE_AGX_MEMORY_IO *io,
                         APPLE_AGX_CHANNEL_MEMORY_OWNER *owner) {
  memset(fake, 0, sizeof(*fake));
  memset(owner, 0, sizeof(*owner));
  io->Context = fake;
  io->AllocateContiguous = allocate_contiguous;
  io->FreeContiguous = free_contiguous;
}

static void assert_no_active_allocations(const FAKE_MEMORY *fake) {
  unsigned int index;
  for (index = 0u; index < TEST_ALLOCATION_COUNT; ++index)
    assert(fake->Allocations[index].Active == 0u);
}

static void assert_all_zero(const APPLE_AGX_MEMORY_OBJECT *object) {
  unsigned long long index;
  const unsigned char *bytes = (const unsigned char *)object->CpuAddress;
  for (index = 0ULL; index < object->Length; ++index)
    assert(bytes[index] == 0u);
}

static void test_builds_exact_owned_channel_set(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  unsigned int index;

  init_fixture(&fake, &io, &owner);
  assert(AppleAgxChannelMemoryBuild(
             &owner, &io, J313_AGX_G2_KERNEL_VA_BASE + 0x800000ULL) ==
         AppleAgxChannelMemoryResultOk);
  assert(owner.Built == 1u);
  assert(owner.ObjectCount == APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT);
  assert(fake.AllocateCount == APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT);
  for (index = 0u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index)
    assert_all_zero(&owner.Objects[index]);
  for (index = 0u; index < J313_AGX_G2_CMD_QUEUE_CHANNEL_COUNT; ++index) {
    assert(owner.ChannelInfo.Entries[index].StateAddress ==
           owner.VirtualAddresses[AppleAgxChannelMemoryCommandStateBase +
                                  index]);
    assert(owner.ChannelInfo.Entries[index].RingAddress ==
           owner.VirtualAddresses[AppleAgxChannelMemoryCommandRingBase +
                                  index]);
  }
  assert(owner.ChannelInfo.Entries[12].StateAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryDevctrlState]);
  assert(owner.ChannelInfo.Entries[12].RingAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryDevctrlRing]);
  assert(owner.ChannelInfo.Entries[13].StateAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryEventState]);
  assert(owner.ChannelInfo.Entries[13].RingAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryEventRing]);
  assert(owner.ChannelInfo.Entries[14].StateAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryFwlogState]);
  assert(owner.ChannelInfo.Entries[14].RingAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryFwlogDummyRing]);
  assert(owner.RealFwlogRingAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryFwlogRing]);
  assert(owner.ChannelInfo.Entries[15].StateAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryKtraceState]);
  assert(owner.ChannelInfo.Entries[15].RingAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryKtraceRing]);
  assert(owner.ChannelInfo.Entries[16].StateAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryStatsState]);
  assert(owner.ChannelInfo.Entries[16].RingAddress ==
         owner.VirtualAddresses[AppleAgxChannelMemoryStatsRing]);
  for (index = 1u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index)
    assert(owner.VirtualAddresses[index] > owner.VirtualAddresses[index - 1u] +
           owner.Objects[index - 1u].Length);

  assert(AppleAgxChannelMemoryDestroy(&owner) ==
         AppleAgxChannelMemoryResultOk);
  assert(owner.Built == 0u && owner.ObjectCount == 0u);
  assert(fake.FreeCount == APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT);
  assert_no_active_allocations(&fake);
}

static void test_every_allocation_failure_rolls_back(void) {
  unsigned int fail_call;
  for (fail_call = 1u;
       fail_call <= APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++fail_call) {
    FAKE_MEMORY fake;
    APPLE_AGX_MEMORY_IO io;
    APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
    init_fixture(&fake, &io, &owner);
    fake.FailAllocateCall = fail_call;
    assert(AppleAgxChannelMemoryBuild(
               &owner, &io, J313_AGX_G2_KERNEL_VA_BASE + 0x800000ULL) ==
           AppleAgxChannelMemoryResultAllocationFailed);
    assert(owner.Built == 0u && owner.ObjectCount == 0u);
    assert(fake.FreeCount == fail_call - 1u);
    assert_no_active_allocations(&fake);
  }
}

static void test_invalid_and_retryable_release(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_CHANNEL_MEMORY_OWNER owner;
  init_fixture(&fake, &io, &owner);
  assert(AppleAgxChannelMemoryBuild(0, &io, J313_AGX_G2_KERNEL_VA_BASE) ==
         AppleAgxChannelMemoryResultInvalidArgument);
  assert(AppleAgxChannelMemoryBuild(&owner, 0, J313_AGX_G2_KERNEL_VA_BASE) ==
         AppleAgxChannelMemoryResultInvalidArgument);
  assert(AppleAgxChannelMemoryBuild(&owner, &io, 0x10000ULL) ==
         AppleAgxChannelMemoryResultInvalidArgument);
  assert(fake.AllocateCount == 0u);
  assert(AppleAgxChannelMemoryBuild(
             &owner, &io, J313_AGX_G2_KERNEL_VA_BASE + 0x800000ULL) ==
         AppleAgxChannelMemoryResultOk);
  assert(AppleAgxChannelMemoryBuild(
             &owner, &io, J313_AGX_G2_KERNEL_VA_BASE + 0x800000ULL) ==
         AppleAgxChannelMemoryResultInvalidArgument);
  fake.FailFreeSlot = APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT;
  assert(AppleAgxChannelMemoryDestroy(&owner) ==
         AppleAgxChannelMemoryResultReleaseFailed);
  fake.FailFreeSlot = 0u;
  assert(AppleAgxChannelMemoryDestroy(&owner) ==
         AppleAgxChannelMemoryResultOk);
  assert_no_active_allocations(&fake);
}

int main(void) {
  test_builds_exact_owned_channel_set();
  test_every_allocation_failure_rolls_back();
  test_invalid_and_retryable_release();
  return 0;
}
