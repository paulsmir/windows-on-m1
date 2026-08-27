#include "apple_agx_initdata_memory.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ALLOCATION_COUNT 80u

typedef struct _FAKE_ALLOCATION {
  void *Storage;
  unsigned long long Length;
  unsigned long long DeviceBase;
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

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

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
  allocation->DeviceBase = 0x10000000ULL + slot * 0x200000ULL;
  allocation->Slot = slot;
  allocation->Active = 1u;
  *cpu_base = allocation->Storage;
  *device_base = allocation->DeviceBase;
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
                         APPLE_AGX_INITDATA_MEMORY_GRAPH *graph) {
  memset(fake, 0, sizeof(*fake));
  memset(graph, 0, sizeof(*graph));
  io->Context = fake;
  io->AllocateContiguous = allocate_contiguous;
  io->FreeContiguous = free_contiguous;
}

static void assert_no_active_allocations(const FAKE_MEMORY *fake) {
  unsigned int index;
  for (index = 0u; index < TEST_ALLOCATION_COUNT; ++index)
    assert(fake->Allocations[index].Active == 0u);
}

static APPLE_AGX_CONFIG_SNAPSHOT physical_snapshot(void) {
  static const unsigned int frequencies[7] = {
      0u, 396000000u, 528000000u, 720000000u,
      924000000u, 1128000000u, 1278000000u};
  static const unsigned int voltages[7] = {
      400u, 612u, 650u, 687u, 778u, 871u, 943u};
  static const unsigned int scalars[APPLE_AGX_CONFIG_SCALAR_COUNT] = {
      0x000003e8u, 0x40f00000u, 0x40800000u, 0x00000028u,
      0x0000007du, 0x43480000u, 0x00000000u, 0x40a00000u,
      0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
      0x00000005u, 0x00000032u, 0x00000000u, 0x3e4a2121u,
      0x00000000u, 0x00000000u, 0x40db53d0u, 0x00000000u,
      0x00000055u, 0x00000064u, 0x42b70000u, 0x40dccccdu,
      0x00000139u, 0x3ca59586u, 0x00000000u, 0x00000028u,
      0x40a90fdbu, 0x00000000u, 0x00000000u, 0x00000000u,
      0x00000000u};
  APPLE_AGX_CONFIG_SNAPSHOT snapshot;
  unsigned int index;

  memset(&snapshot, 0, sizeof(snapshot));
  snapshot.PerfStateCount = 7u;
  snapshot.PerfStateTableCount = 1u;
  snapshot.BasePstate = 1u;
  snapshot.MaxPstate = 6u;
  snapshot.PowerSamplePeriodMs = 8u;
  snapshot.GpuRegionBase = 0x9fffb8000ULL;
  for (index = 0u; index < 7u; ++index) {
    snapshot.PerfStates[index].FrequencyHz = frequencies[index];
    snapshot.PerfStates[index].VoltageMv = voltages[index];
  }
  snapshot.ScalarPresence = 0x1ff5b8bfULL;
  memcpy(snapshot.ScalarBits, scalars, sizeof(scalars));
  return snapshot;
}

static void test_builds_exact_graph_and_releases(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();
  unsigned char *encoded;

  init_fixture(&fake, &io, &graph);
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultOk);
  assert(graph.Built == 1u);
  assert(graph.DataObjectCount == APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT);
  assert(graph.ChannelMemory.ObjectCount ==
         APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT);
  assert(graph.RegionBMemory.ObjectCount ==
         APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT);
  assert(graph.Inventory.PageCount == 6u);
  assert(graph.Inventory.MappingCount == 54u);
  assert(fake.AllocateCount == 59u);
  assert(graph.VirtualAddresses[AppleAgxInitdataMemoryEnvelope] ==
         J313_AGX_G2_KERNEL_VA_BASE);
  assert(graph.VirtualAddresses[AppleAgxInitdataMemoryRegionA] ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x8000ULL);
  assert(graph.VirtualAddresses[AppleAgxInitdataMemoryRegionB] ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x10000ULL);
  assert(graph.VirtualAddresses[AppleAgxInitdataMemoryRegionC] ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x1c000ULL);
  assert(graph.VirtualAddresses[AppleAgxInitdataMemoryFirmwareStatus] ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x34000ULL);
  assert(graph.VirtualAddresses[AppleAgxInitdataMemoryFwctlState] ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x3c000ULL);
  assert(graph.VirtualAddresses[AppleAgxInitdataMemoryFwctlRing] ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x44000ULL);
  assert(graph.ChannelMemory.VirtualAddresses[0] ==
         J313_AGX_G2_KERNEL_VA_BASE + 0x4c000ULL);
  assert(graph.TtbrPair.Ttbr0 ==
         (graph.Roots.Ttbr0PhysicalAddress | 1ULL));
  assert(graph.TtbrPair.Ttbr1 ==
         (graph.Roots.Ttbr1PhysicalAddress | 1ULL));

  encoded = (unsigned char *)
      graph.DataObjects[AppleAgxInitdataMemoryEnvelope].CpuAddress;
  assert(read_u64(encoded + 0x08u) ==
         graph.VirtualAddresses[AppleAgxInitdataMemoryRegionA]);
  assert(read_u64(encoded + 0x18u) ==
         graph.VirtualAddresses[AppleAgxInitdataMemoryRegionB]);
  assert(read_u64(encoded + 0x20u) ==
         graph.VirtualAddresses[AppleAgxInitdataMemoryRegionC]);
  assert(read_u64(encoded + 0x28u) ==
         graph.VirtualAddresses[AppleAgxInitdataMemoryFirmwareStatus]);
  assert(graph.InitdataVirtualAddress == J313_AGX_G2_KERNEL_VA_BASE);
  assert(graph.InitdataDeviceAddress ==
         graph.DataObjects[AppleAgxInitdataMemoryEnvelope].DeviceAddress);
  encoded = (unsigned char *)
      graph.DataObjects[AppleAgxInitdataMemoryFirmwareStatus].CpuAddress;
  assert(read_u64(encoded + 0x00u) ==
         graph.VirtualAddresses[AppleAgxInitdataMemoryFwctlState]);
  assert(read_u64(encoded + 0x08u) ==
         graph.VirtualAddresses[AppleAgxInitdataMemoryFwctlRing]);
  encoded = (unsigned char *)
      graph.DataObjects[AppleAgxInitdataMemoryRegionB].CpuAddress;
  assert(read_u64(encoded + 0x00u) ==
         graph.ChannelMemory.ChannelInfo.Entries[0].StateAddress);
  assert(read_u64(encoded + 0x08u) ==
         graph.ChannelMemory.ChannelInfo.Entries[0].RingAddress);
  assert(read_u64(encoded + 0x100u) ==
         graph.ChannelMemory.ChannelInfo.Entries[16].StateAddress);
  assert(read_u64(encoded + 0x108u) ==
         graph.ChannelMemory.ChannelInfo.Entries[16].RingAddress);
  assert(read_u64(encoded + J313_AGX_G2_REGIONB_STATS_TA_OFFSET) ==
         graph.RegionBMemory.Input.StatsTaAddress);
  assert(read_u64(encoded + J313_AGX_G2_REGIONB_HWDATA_B_OFFSET) ==
         graph.RegionBMemory.Input.HwdataBAddress);
  assert(read_u64(encoded + J313_AGX_G2_REGIONB_HWDATA_B_REPEAT_OFFSET) ==
         graph.RegionBMemory.Input.HwdataBAddress);
  assert(read_u64(encoded + J313_AGX_G2_REGIONB_FWLOG_RING_OFFSET) ==
         graph.ChannelMemory.RealFwlogRingAddress);
  assert(read_u64(encoded + J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_OFFSET) ==
         J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA);
  assert(read_u64(encoded + J313_AGX_G2_REGIONB_BUFFER_MGR_CPU_OFFSET) ==
         graph.RegionBMemory.Input.BufferManagerCpuAddress);
  assert(graph.ChannelInfoManifest.EncodedSize ==
         J313_AGX_G2_CHANNEL_INFO_SET_SIZE);
  assert(graph.RegionBManifest.PointerCount == 14u);
  assert(graph.RegionCManifest.EncodedSize ==
         J313_AGX_G2_INITDATA_REGION_C_SIZE);
  assert(graph.RegionCManifest.NonzeroWordCount == 81u);
  assert(graph.RegionCManifest.OracleFnv1a64 ==
         0xc3bc91a9acf61290ULL);
  assert(graph.Inventory.Mappings[53].VirtualAddress ==
         J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA);
  assert(graph.Inventory.Mappings[53].PhysicalAddress ==
         graph.RegionBMemory.Objects[AppleAgxRegionBMemoryBufferManager]
             .DeviceAddress);

  assert(AppleAgxInitdataMemoryDestroy(&graph) ==
         AppleAgxInitdataMemoryResultOk);
  assert(graph.Built == 0u && graph.DataObjectCount == 0u);
  assert(fake.FreeCount == 59u);
  assert_no_active_allocations(&fake);
  assert(AppleAgxInitdataMemoryDestroy(&graph) ==
         AppleAgxInitdataMemoryResultOk);
  assert(fake.FreeCount == 59u);
}

static void test_every_allocation_failure_rolls_back(void) {
  unsigned int fail_call;
  for (fail_call = 1u; fail_call <= 59u; ++fail_call) {
    FAKE_MEMORY fake;
    APPLE_AGX_MEMORY_IO io;
    APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
    APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();
    init_fixture(&fake, &io, &graph);
    fake.FailAllocateCall = fail_call;
    assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
           AppleAgxInitdataMemoryResultAllocationFailed);
    assert(graph.Built == 0u && graph.DataObjectCount == 0u);
    assert(graph.ChannelMemory.ObjectCount == 0u);
    assert(graph.RegionBMemory.ObjectCount == 0u);
    assert(fake.FreeCount == fail_call - 1u);
    assert_no_active_allocations(&fake);
  }
}

static void test_invalid_arguments_fail_closed(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();
  init_fixture(&fake, &io, &graph);
  assert(AppleAgxInitdataMemoryBuild(0, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultInvalidArgument);
  assert(AppleAgxInitdataMemoryBuild(&graph, 0, &snapshot) ==
         AppleAgxInitdataMemoryResultInvalidArgument);
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, 0) ==
         AppleAgxInitdataMemoryResultInvalidArgument);
  assert(AppleAgxInitdataMemoryDestroy(0) ==
         AppleAgxInitdataMemoryResultInvalidArgument);

  graph.DataObjects[0].State = AppleAgxMemoryCpuOwned;
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultInvalidArgument);
  assert(fake.AllocateCount == 0u);

  graph.DataObjects[0].State = AppleAgxMemoryEmpty;
  graph.UatMemoryObjects[0].State = AppleAgxMemoryCpuOwned;
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultInvalidArgument);
  assert(fake.AllocateCount == 0u);
}

static void test_double_build_fails_without_disturbing_owned_graph(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();

  init_fixture(&fake, &io, &graph);
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultOk);
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultInvalidArgument);
  assert(fake.AllocateCount == 59u);
  assert(graph.Built == 1u && graph.DataObjectCount == 7u);
  assert(AppleAgxInitdataMemoryDestroy(&graph) ==
         AppleAgxInitdataMemoryResultOk);
  assert_no_active_allocations(&fake);
}

static void test_regionc_mismatch_rolls_back_entire_graph(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();

  init_fixture(&fake, &io, &graph);
  snapshot.ScalarBits[AppleAgxConfigScalarAvgPowerKp] ^= 1u;
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultEncodeFailed);
  assert(graph.Built == 0u && graph.DataObjectCount == 0u);
  assert(graph.RegionCManifest.EncodedSize == 0u);
  assert(fake.FreeCount == 59u);
  assert_no_active_allocations(&fake);
}

static void test_failed_release_is_retryable(void) {
  FAKE_MEMORY fake;
  APPLE_AGX_MEMORY_IO io;
  APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot = physical_snapshot();

  init_fixture(&fake, &io, &graph);
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultOk);
  fake.FailFreeSlot = 59u;
  assert(AppleAgxInitdataMemoryDestroy(&graph) ==
         AppleAgxInitdataMemoryResultReleaseFailed);
  assert(graph.Initialized == 1u && graph.DataObjectCount == 7u);
  fake.FailFreeSlot = 0u;
  assert(AppleAgxInitdataMemoryDestroy(&graph) ==
         AppleAgxInitdataMemoryResultOk);
  assert(fake.FreeCount == 59u);
  assert_no_active_allocations(&fake);

  init_fixture(&fake, &io, &graph);
  assert(AppleAgxInitdataMemoryBuild(&graph, &io, &snapshot) ==
         AppleAgxInitdataMemoryResultOk);
  fake.FailFreeSlot = 53u;
  assert(AppleAgxInitdataMemoryDestroy(&graph) ==
         AppleAgxInitdataMemoryResultReleaseFailed);
  assert(graph.Initialized == 1u && graph.DataObjectCount == 7u);
  fake.FailFreeSlot = 0u;
  assert(AppleAgxInitdataMemoryDestroy(&graph) ==
         AppleAgxInitdataMemoryResultOk);
  assert(fake.FreeCount == 59u);
  assert_no_active_allocations(&fake);
}

int main(void) {
  test_builds_exact_graph_and_releases();
  test_every_allocation_failure_rolls_back();
  test_invalid_arguments_fail_closed();
  test_double_build_fails_without_disturbing_owned_graph();
  test_regionc_mismatch_rolls_back_entire_graph();
  test_failed_release_is_retryable();
  return 0;
}
