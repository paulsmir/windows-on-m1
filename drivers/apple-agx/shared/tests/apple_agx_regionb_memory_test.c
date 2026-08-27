#include "apple_agx_regionb_memory.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ALLOCATION_COUNT 16u

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
  *device_base = 0x50000000ULL + slot * 0x200000ULL;
  *allocation_handle = allocation;
  return 1u;
}

static unsigned char free_contiguous(void *context, void *handle) {
  FAKE_MEMORY *fake = (FAKE_MEMORY *)context;
  FAKE_ALLOCATION *allocation = (FAKE_ALLOCATION *)handle;
  assert(allocation->Active != 0u);
  free(allocation->Storage);
  allocation->Storage = 0;
  allocation->Active = 0u;
  ++fake->FreeCount;
  return 1u;
}

static void init_fixture(FAKE_MEMORY *fake, APPLE_AGX_MEMORY_IO *io,
                         APPLE_AGX_REGIONB_MEMORY_OWNER *owner) {
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

static void test_builds_exact_owned_children(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_REGIONB_MEMORY_OWNER owner;
  unsigned long long first = J313_AGX_G2_KERNEL_VA_BASE + 0x4000000ULL;
  unsigned int index;
  init_fixture(&fake, &io, &owner);
  assert(AppleAgxRegionBMemoryBuild(&owner, &io, first,
                                   J313_AGX_G2_KERNEL_VA_BASE + 0x800000ULL) ==
         AppleAgxRegionBMemoryResultOk);
  assert(owner.Built == 1u && owner.ObjectCount == 11u);
  assert(fake.AllocateCount == 11u);
  assert(owner.VirtualAddresses[0] == first);
  assert(owner.Input.StatsTaAddress == owner.VirtualAddresses[0]);
  assert(owner.Input.Stats3dAddress == owner.VirtualAddresses[1]);
  assert(owner.Input.StatsCpAddress == owner.VirtualAddresses[2]);
  assert(owner.Input.HwdataAAddress == owner.VirtualAddresses[3]);
  assert(owner.Input.FaultInfoAddress == owner.VirtualAddresses[4]);
  assert(owner.Input.TimestampAddress == owner.VirtualAddresses[5]);
  assert(owner.Input.HwdataBAddress == owner.VirtualAddresses[6]);
  assert(owner.Input.FwlogRingAddress ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x800000ULL);
  assert(owner.Input.Unknown1b8Address == owner.VirtualAddresses[7]);
  assert(owner.Input.Unknown1c0Address == owner.VirtualAddresses[8]);
  assert(owner.Input.Unknown1c8Address == owner.VirtualAddresses[9]);
  assert(owner.Input.BufferManagerCpuAddress == owner.VirtualAddresses[10]);
  assert(owner.Input.BufferManagerGpuAddress ==
         J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA);
  for (index = 0u; index < owner.ObjectCount; ++index) {
    unsigned long long byte;
    for (byte = 0ULL; byte < owner.Objects[index].Length; ++byte)
      assert(((unsigned char *)owner.Objects[index].CpuAddress)[byte] == 0u);
  }
  assert(AppleAgxRegionBMemoryDestroy(&owner) ==
         AppleAgxRegionBMemoryResultOk);
  assert(fake.FreeCount == 11u);
  assert_no_active_allocations(&fake);
}

static void test_every_allocation_failure_rolls_back(void) {
  unsigned int fail_call;
  for (fail_call = 1u; fail_call <= 11u; ++fail_call) {
    FAKE_MEMORY fake;
    APPLE_AGX_MEMORY_IO io;
    APPLE_AGX_REGIONB_MEMORY_OWNER owner;
    init_fixture(&fake, &io, &owner);
    fake.FailAllocateCall = fail_call;
    assert(AppleAgxRegionBMemoryBuild(
               &owner, &io, J313_AGX_G2_KERNEL_VA_BASE + 0x4000000ULL,
               J313_AGX_G2_KERNEL_VA_BASE + 0x800000ULL) ==
           AppleAgxRegionBMemoryResultAllocationFailed);
    assert(owner.Built == 0u && owner.ObjectCount == 0u);
    assert(fake.FreeCount == fail_call - 1u);
    assert_no_active_allocations(&fake);
  }
}

int main(void) {
  test_builds_exact_owned_children();
  test_every_allocation_failure_rolls_back();
  return 0;
}
