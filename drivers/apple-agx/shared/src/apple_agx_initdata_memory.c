#include "apple_agx_initdata_memory.h"

#define APPLE_AGX_INITDATA_MEMORY_PAGE_MASK \
  (APPLE_AGX_MEMORY_PAGE_SIZE - 1ULL)

static const unsigned long long AppleAgxInitdataMemoryContentSizes[
    APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT] = {
    /* Generated from the executable G13/V13_5 m1n1 layouts. */
    J313_AGX_G2_INITDATA_SIZE,
    J313_AGX_G2_INITDATA_REGION_A_SIZE,
    J313_AGX_G2_INITDATA_REGION_B_SIZE,
    J313_AGX_G2_INITDATA_REGION_C_SIZE,
    J313_AGX_G2_INITDATA_FW_STATUS_SIZE,
    J313_AGX_G2_FWCTL_STATE_SIZE,
    J313_AGX_G2_FWCTL_RING_SIZE,
};

static unsigned char AppleAgxInitdataMemoryStorageIsEmpty(
    const APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph) {
  unsigned int index;

  for (index = 0u; index < APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT; ++index) {
    if (Graph->DataObjects[index].State != AppleAgxMemoryEmpty)
      return 0u;
  }
  for (index = 0u; index < APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY;
       ++index) {
    if (Graph->UatMemoryObjects[index].State != AppleAgxMemoryEmpty)
      return 0u;
  }
  if (Graph->ChannelMemory.Initialized != 0u ||
      Graph->ChannelMemory.Built != 0u ||
      Graph->ChannelMemory.ObjectCount != 0u)
    return 0u;
  for (index = 0u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index) {
    if (Graph->ChannelMemory.Objects[index].State != AppleAgxMemoryEmpty)
      return 0u;
  }
  return 1u;
}

static unsigned long long AppleAgxInitdataMemoryAlignUp(
    unsigned long long Value) {
  return (Value + APPLE_AGX_INITDATA_MEMORY_PAGE_MASK) &
         ~APPLE_AGX_INITDATA_MEMORY_PAGE_MASK;
}

static void AppleAgxInitdataMemoryZero(void *Address,
                                       unsigned long long Length) {
  unsigned long long index;
  for (index = 0ULL; index < Length; ++index)
    ((unsigned char *)Address)[index] = 0u;
}

static void AppleAgxInitdataMemoryClearPublished(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph) {
  unsigned int index;
  Graph->Roots.Ttbr0PhysicalAddress = 0ULL;
  Graph->Roots.Ttbr1PhysicalAddress = 0ULL;
  AppleAgxUatClearTtbrPair(&Graph->TtbrPair);
  Graph->Manifest.EncodedSize = 0u;
  Graph->FirmwareStatusManifest.EncodedSize = 0u;
  Graph->FirmwareStatusManifest.StateAddress = 0ULL;
  Graph->FirmwareStatusManifest.RingAddress = 0ULL;
  Graph->ChannelInfoManifest.EncodedSize = 0u;
  Graph->ChannelInfoManifest.ChannelCount = 0u;
  for (index = 0u; index < 4u; ++index) {
    Graph->Manifest.VersionWords[index] = 0u;
    Graph->Manifest.ReferencedAddresses[index] = 0ULL;
  }
  Graph->InitdataVirtualAddress = 0ULL;
  Graph->InitdataDeviceAddress = 0ULL;
  Graph->Built = 0u;
}

static APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryRollback(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    APPLE_AGX_INITDATA_MEMORY_RESULT Failure) {
  if (AppleAgxInitdataMemoryDestroy(Graph) !=
      AppleAgxInitdataMemoryResultOk)
    return AppleAgxInitdataMemoryResultReleaseFailed;
  Graph->LastResult = Failure;
  return Failure;
}

APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryBuild(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    const APPLE_AGX_MEMORY_IO *MemoryIo) {
  APPLE_AGX_INITDATA_INPUT input;
  APPLE_AGX_UAT_RESULT uat_result;
  APPLE_AGX_INITDATA_RESULT initdata_result;
  APPLE_AGX_FIRMWARE_STATUS_INPUT firmware_status_input;
  APPLE_AGX_FIRMWARE_STATUS_RESULT firmware_status_result;
  APPLE_AGX_CHANNEL_INFO_RESULT channel_info_result;
  unsigned long long virtual_address;
  unsigned int index;

  if (Graph == 0 || MemoryIo == 0 || MemoryIo->AllocateContiguous == 0 ||
      MemoryIo->FreeContiguous == 0 || Graph->Initialized != 0u ||
      Graph->Built != 0u || Graph->DataObjectCount != 0u ||
      AppleAgxInitdataMemoryStorageIsEmpty(Graph) == 0u)
    return AppleAgxInitdataMemoryResultInvalidArgument;

  Graph->MemoryIo = MemoryIo;
  Graph->Initialized = 1u;
  Graph->LastResult = AppleAgxInitdataMemoryResultOk;
  Graph->Inventory.Pages = Graph->UatPages;
  Graph->Inventory.PageCapacity = APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY;
  Graph->Inventory.PageCount = 0u;
  Graph->Inventory.Mappings = Graph->UatMappings;
  Graph->Inventory.MappingCapacity = APPLE_AGX_INITDATA_MEMORY_MAPPING_CAPACITY;
  Graph->Inventory.MappingCount = 0u;

  if (AppleAgxUatMemoryOwnerInitialize(
          &Graph->UatMemoryOwner, MemoryIo, Graph->UatMemoryObjects,
          APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY) !=
          AppleAgxUatMemoryResultOk ||
      AppleAgxUatMemoryOwnerGetAllocator(&Graph->UatMemoryOwner,
                                         &Graph->UatAllocator) !=
          AppleAgxUatMemoryResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultInvalidArgument);

  /* A guard page between objects makes an overrun fault deterministic. */
  virtual_address = J313_AGX_G2_KERNEL_VA_BASE;
  for (index = 0u; index < APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT; ++index) {
    unsigned long long allocation_size =
        AppleAgxInitdataMemoryAlignUp(
            AppleAgxInitdataMemoryContentSizes[index]);
    if (AppleAgxMemoryAllocate(MemoryIo, allocation_size,
                               &Graph->DataObjects[index]) !=
        AppleAgxMemoryResultOk)
      return AppleAgxInitdataMemoryRollback(
          Graph, AppleAgxInitdataMemoryResultAllocationFailed);
    ++Graph->DataObjectCount;
    AppleAgxInitdataMemoryZero(Graph->DataObjects[index].CpuAddress,
                               allocation_size);
    Graph->VirtualAddresses[index] = virtual_address;
    virtual_address += allocation_size + APPLE_AGX_MEMORY_PAGE_SIZE;
  }

  if (AppleAgxChannelMemoryBuild(&Graph->ChannelMemory, MemoryIo,
                                 virtual_address) !=
      AppleAgxChannelMemoryResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultAllocationFailed);

  uat_result = AppleAgxUatCreateAddressSpace(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->UatAllocator,
      &Graph->Inventory, &Graph->Roots);
  if (uat_result != AppleAgxUatResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultAllocationFailed);

  for (index = 0u; index < APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT; ++index) {
    uat_result = AppleAgxUatMap(
        J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->Roots,
        Graph->VirtualAddresses[index], Graph->DataObjects[index].DeviceAddress,
        Graph->DataObjects[index].Length,
        AppleAgxUatFirmwareSharedReadWrite, &Graph->UatAllocator,
        &Graph->Inventory);
    if (uat_result != AppleAgxUatResultOk)
      return AppleAgxInitdataMemoryRollback(
          Graph, uat_result == AppleAgxUatResultAllocationFailed
                     ? AppleAgxInitdataMemoryResultAllocationFailed
                     : AppleAgxInitdataMemoryResultUatFailed);
  }
  for (index = 0u; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index) {
    uat_result = AppleAgxUatMap(
        J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->Roots,
        Graph->ChannelMemory.VirtualAddresses[index],
        Graph->ChannelMemory.Objects[index].DeviceAddress,
        Graph->ChannelMemory.Objects[index].Length,
        AppleAgxUatFirmwareSharedReadWrite, &Graph->UatAllocator,
        &Graph->Inventory);
    if (uat_result != AppleAgxUatResultOk)
      return AppleAgxInitdataMemoryRollback(
          Graph, uat_result == AppleAgxUatResultAllocationFailed
                     ? AppleAgxInitdataMemoryResultAllocationFailed
                     : AppleAgxInitdataMemoryResultUatFailed);
  }

  channel_info_result = AppleAgxChannelInfoEncodeG13V13_5(
      &Graph->ChannelMemory.ChannelInfo,
      (unsigned char *)
          Graph->DataObjects[AppleAgxInitdataMemoryRegionB].CpuAddress,
      J313_AGX_G2_CHANNEL_INFO_SET_SIZE, &Graph->ChannelInfoManifest);
  if (channel_info_result != AppleAgxChannelInfoResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultEncodeFailed);

  firmware_status_input.StateAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryFwctlState];
  firmware_status_input.RingAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryFwctlRing];
  firmware_status_result = AppleAgxFirmwareStatusEncodeG13V13_5(
      &firmware_status_input,
      (unsigned char *)
          Graph->DataObjects[AppleAgxInitdataMemoryFirmwareStatus].CpuAddress,
      J313_AGX_G2_INITDATA_FW_STATUS_SIZE,
      &Graph->FirmwareStatusManifest);
  if (firmware_status_result != AppleAgxFirmwareStatusResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultEncodeFailed);

  input.TaggedBufferAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryRegionA];
  input.RuntimePointersAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryRegionB];
  input.GlobalsAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryRegionC];
  input.FirmwareStatusAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryFirmwareStatus];
  initdata_result = AppleAgxInitdataEncodeG13V13_5(
      &input,
      (unsigned char *)
          Graph->DataObjects[AppleAgxInitdataMemoryEnvelope].CpuAddress,
      J313_AGX_G2_INITDATA_SIZE, &Graph->Manifest);
  if (initdata_result != AppleAgxInitdataResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultEncodeFailed);

  uat_result = AppleAgxUatEncodeTtbrPair(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->Roots, &Graph->TtbrPair);
  if (uat_result != AppleAgxUatResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultUatFailed);

  /* The caller owns publication into the fixed GPU region and ASC startup. */
  Graph->InitdataVirtualAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryEnvelope];
  Graph->InitdataDeviceAddress =
      Graph->DataObjects[AppleAgxInitdataMemoryEnvelope].DeviceAddress;
  Graph->Built = 1u;
  Graph->LastResult = AppleAgxInitdataMemoryResultOk;
  return Graph->LastResult;
}

APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryDestroy(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph) {
  unsigned int index;

  if (Graph == 0)
    return AppleAgxInitdataMemoryResultInvalidArgument;
  if (Graph->Initialized == 0u)
    return AppleAgxInitdataMemoryResultOk;

  AppleAgxInitdataMemoryClearPublished(Graph);
  AppleAgxUatDestroy(&Graph->UatAllocator, &Graph->Inventory);
  if (AppleAgxUatMemoryOwnerDestroy(&Graph->UatMemoryOwner) !=
      AppleAgxUatMemoryResultOk) {
    Graph->LastResult = AppleAgxInitdataMemoryResultReleaseFailed;
    return Graph->LastResult;
  }
  if (AppleAgxChannelMemoryDestroy(&Graph->ChannelMemory) !=
      AppleAgxChannelMemoryResultOk) {
    Graph->LastResult = AppleAgxInitdataMemoryResultReleaseFailed;
    return Graph->LastResult;
  }
  while (Graph->DataObjectCount > 0u) {
    index = Graph->DataObjectCount - 1u;
    if (AppleAgxMemoryRelease(Graph->MemoryIo,
                              &Graph->DataObjects[index]) !=
        AppleAgxMemoryResultOk) {
      Graph->LastResult = AppleAgxInitdataMemoryResultReleaseFailed;
      return Graph->LastResult;
    }
    Graph->VirtualAddresses[index] = 0ULL;
    --Graph->DataObjectCount;
  }
  Graph->MemoryIo = 0;
  Graph->Initialized = 0u;
  Graph->LastResult = AppleAgxInitdataMemoryResultOk;
  return Graph->LastResult;
}
