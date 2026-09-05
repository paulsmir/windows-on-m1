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
    APPLE_AGX_RTKIT_CRASHLOG_BYTES,
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
  if (Graph->RegionBMemory.Initialized != 0u ||
      Graph->RegionBMemory.Built != 0u ||
      Graph->RegionBMemory.ObjectCount != 0u)
    return 0u;
  for (index = 0u; index < APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT; ++index) {
    if (Graph->RegionBMemory.Objects[index].State != AppleAgxMemoryEmpty)
      return 0u;
  }
  if (Graph->RenderSharedMemory.Initialized ||
      Graph->RenderSharedMemory.Built ||
      Graph->RenderSharedMemory.ObjectCount != 0u)
    return 0u;
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    if (Graph->RenderSharedMemory.Objects[index].State != AppleAgxMemoryEmpty)
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
  Graph->RegionBManifest.PointerCount = 0u;
  Graph->RegionBManifest.FirstOffset = 0u;
  Graph->RegionBManifest.LastOffset = 0u;
  Graph->RegionCManifest.EncodedSize = 0u;
  Graph->RegionCManifest.NonzeroWordCount = 0u;
  Graph->RegionCManifest.OracleFnv1a64 = 0ULL;
  for (index = 0u; index < 4u; ++index) {
    Graph->Manifest.VersionWords[index] = 0u;
    Graph->Manifest.ReferencedAddresses[index] = 0ULL;
  }
  Graph->InitdataVirtualAddress = 0ULL;
  Graph->InitdataDeviceAddress = 0ULL;
  Graph->Built = 0u;
  Graph->MappingsReady = 0u;
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

static APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryPrepareInternal(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot, unsigned char BrokerOnly) {
  APPLE_AGX_INITDATA_INPUT input;
#ifndef APPLE_AGX_FULL_CONTEXT0_BROKER
  APPLE_AGX_UAT_RESULT uat_result;
#endif
  APPLE_AGX_INITDATA_RESULT initdata_result;
  APPLE_AGX_FIRMWARE_STATUS_INPUT firmware_status_input;
  APPLE_AGX_FIRMWARE_STATUS_RESULT firmware_status_result;
  APPLE_AGX_CHANNEL_INFO_RESULT channel_info_result;
  APPLE_AGX_REGIONB_RESULT regionb_result;
  APPLE_AGX_REGIONC_RESULT regionc_result;
  unsigned long long virtual_address;
  unsigned int index;

  if (Graph == 0 || MemoryIo == 0 || Snapshot == 0 ||
      MemoryIo->AllocateContiguous == 0 ||
      MemoryIo->FreeContiguous == 0 || Graph->Initialized != 0u ||
      Graph->Built != 0u || Graph->DataObjectCount != 0u ||
      AppleAgxInitdataMemoryStorageIsEmpty(Graph) == 0u)
    return AppleAgxInitdataMemoryResultInvalidArgument;

#ifdef APPLE_AGX_FULL_CONTEXT0_BROKER
  if (!BrokerOnly) return AppleAgxInitdataMemoryResultInvalidArgument;
#endif
  Graph->BrokerOnly = BrokerOnly;
  Graph->MemoryIo = MemoryIo;
  Graph->Initialized = 1u;
  Graph->LastResult = AppleAgxInitdataMemoryResultOk;
  Graph->Inventory.Pages = Graph->UatPages;
  Graph->Inventory.PageCapacity = APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY;
  Graph->Inventory.PageCount = 0u;
  Graph->Inventory.Mappings = Graph->UatMappings;
  Graph->Inventory.MappingCapacity = APPLE_AGX_INITDATA_MEMORY_MAPPING_CAPACITY;
  Graph->Inventory.MappingCount = 0u;

#ifndef APPLE_AGX_FULL_CONTEXT0_BROKER
  if (!BrokerOnly && (AppleAgxUatMemoryOwnerInitialize(
          &Graph->UatMemoryOwner, MemoryIo, Graph->UatMemoryObjects,
          APPLE_AGX_INITDATA_MEMORY_UAT_PAGE_CAPACITY) !=
          AppleAgxUatMemoryResultOk ||
      AppleAgxUatMemoryOwnerGetAllocator(&Graph->UatMemoryOwner,
                                         &Graph->UatAllocator) !=
          AppleAgxUatMemoryResultOk))
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultInvalidArgument);

#endif
  /* A guard page between objects makes an overrun fault deterministic. */
  virtual_address = J313_AGX_G2_KERNEL_VA_BASE;
  for (index = 0u; index < APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT; ++index) {
    unsigned long long allocation_size =
        AppleAgxInitdataMemoryAlignUp(
            AppleAgxInitdataMemoryContentSizes[index]);
    if (BrokerOnly && index == AppleAgxInitdataMemoryCrashlog) {
      Graph->VirtualAddresses[index] = APPLE_AGX_RTKIT_CRASHLOG_GPU_VA;
      continue;
    }
    if (AppleAgxMemoryAllocate(MemoryIo, allocation_size,
                               &Graph->DataObjects[index]) !=
        AppleAgxMemoryResultOk)
      return AppleAgxInitdataMemoryRollback(
          Graph, AppleAgxInitdataMemoryResultAllocationFailed);
    ++Graph->DataObjectCount;
    AppleAgxInitdataMemoryZero(Graph->DataObjects[index].CpuAddress,
                               allocation_size);
    if (index == AppleAgxInitdataMemoryCrashlog) {
      Graph->VirtualAddresses[index] = APPLE_AGX_RTKIT_CRASHLOG_GPU_VA;
    } else {
      Graph->VirtualAddresses[index] = virtual_address;
      virtual_address += allocation_size + APPLE_AGX_MEMORY_PAGE_SIZE;
    }
  }

  if (AppleAgxChannelMemoryBuild(&Graph->ChannelMemory, MemoryIo,
                                 virtual_address) !=
      AppleAgxChannelMemoryResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultAllocationFailed);
  virtual_address =
      Graph->ChannelMemory.VirtualAddresses[AppleAgxChannelMemoryStatsRing] +
      Graph->ChannelMemory.Objects[AppleAgxChannelMemoryStatsRing].Length +
      APPLE_AGX_MEMORY_PAGE_SIZE;
  if (AppleAgxRegionBMemoryBuild(
          &Graph->RegionBMemory, MemoryIo, virtual_address,
          Graph->ChannelMemory.RealFwlogRingAddress) !=
      AppleAgxRegionBMemoryResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultAllocationFailed);
  virtual_address = AppleAgxInitdataMemoryAlignUp(
      Graph->RegionBMemory.VirtualAddresses[
          APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT - 1u] +
      Graph->RegionBMemory.Objects[
          APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT - 1u].Length +
      APPLE_AGX_MEMORY_PAGE_SIZE);
  virtual_address = (virtual_address + 0x7fffULL) & ~0x7fffULL;
  if (AppleAgxRenderSharedMemoryBuild(&Graph->RenderSharedMemory, MemoryIo,
                                      virtual_address) !=
      AppleAgxRenderSharedMemoryResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultAllocationFailed);

#ifndef APPLE_AGX_FULL_CONTEXT0_BROKER
  if (!BrokerOnly) {
  uat_result = AppleAgxUatCreateAddressSpace(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->UatAllocator,
      &Graph->Inventory, &Graph->Roots);
  if (uat_result != AppleAgxUatResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultAllocationFailed);

  }
#endif
  channel_info_result = AppleAgxChannelInfoEncodeG13V13_5(
      &Graph->ChannelMemory.ChannelInfo,
      (unsigned char *)
          Graph->DataObjects[AppleAgxInitdataMemoryRegionB].CpuAddress,
      J313_AGX_G2_CHANNEL_INFO_SET_SIZE, &Graph->ChannelInfoManifest);
  if (channel_info_result != AppleAgxChannelInfoResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultEncodeFailed);

  regionb_result = AppleAgxRegionBEncodePointersG13V13_5(
      &Graph->RegionBMemory.Input,
      (unsigned char *)
          Graph->DataObjects[AppleAgxInitdataMemoryRegionB].CpuAddress,
      J313_AGX_G2_INITDATA_REGION_B_SIZE, &Graph->RegionBManifest);
  if (regionb_result != AppleAgxRegionBResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultEncodeFailed);

  /* InitData_RegionB's sole nonzero non-pointer/channel default on G13/V13_5. */
  ((unsigned char *)Graph->DataObjects[AppleAgxInitdataMemoryRegionB].CpuAddress)[0x6b38u]=0xffu;

  regionc_result = AppleAgxRegionCEncodeJ313G13V13_5(
      Snapshot,
      (unsigned char *)
          Graph->DataObjects[AppleAgxInitdataMemoryRegionC].CpuAddress,
      J313_AGX_G2_INITDATA_REGION_C_SIZE, &Graph->RegionCManifest);
  if (regionc_result != AppleAgxRegionCResultOk)
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

#ifndef APPLE_AGX_FULL_CONTEXT0_BROKER
  if (!BrokerOnly) {
  uat_result = AppleAgxUatEncodeTtbrPair(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->Roots, &Graph->TtbrPair);
  if (uat_result != AppleAgxUatResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, AppleAgxInitdataMemoryResultUatFailed);

  }
#endif
  /* CPU content is complete. Prepared is not GPU-mapped: provider may build
   * address/configuration bindings, but no queue can run before live import. */
  for (index = 0; index < APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT; ++index)
    if (AppleAgxMemoryMarkPrepared(&Graph->ChannelMemory.Objects[index]) !=
        AppleAgxMemoryResultOk)
      return AppleAgxInitdataMemoryRollback(Graph, AppleAgxInitdataMemoryResultUatFailed);
  for (index = 0; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT; ++index)
    if (AppleAgxMemoryMarkPrepared(&Graph->RenderSharedMemory.Objects[index]) !=
        AppleAgxMemoryResultOk)
      return AppleAgxInitdataMemoryRollback(Graph, AppleAgxInitdataMemoryResultUatFailed);
  /* The caller owns publication into the fixed GPU region and ASC startup. */
  Graph->InitdataVirtualAddress =
      Graph->VirtualAddresses[AppleAgxInitdataMemoryEnvelope];
  Graph->InitdataDeviceAddress =
      Graph->DataObjects[AppleAgxInitdataMemoryEnvelope].DeviceAddress;
  Graph->Built = 1u;
  Graph->LastResult = AppleAgxInitdataMemoryResultOk;
  return Graph->LastResult;
}



static unsigned char AppleAgxInitdataRecordRange(APPLE_AGX_INITDATA_MEMORY_GRAPH *g,
    APPLE_AGX_MEMORY_OBJECT *object, unsigned long long va) {
  unsigned int i = g->Inventory.MappingCount;
  APPLE_AGX_UAT_HALF half;
  if (i >= g->Inventory.MappingCapacity || !object || object->State == AppleAgxMemoryEmpty ||
      AppleAgxUatValidateRange(0,va,object->DeviceAddress,object->Length,
          AppleAgxUatFirmwareSharedReadWrite,&half) != AppleAgxUatResultOk)
    return 0;
  g->UatMappings[i] = (APPLE_AGX_UAT_MAPPING){0,va,object->DeviceAddress,object->Length,
      AppleAgxUatFirmwareSharedReadWrite,APPLE_AGX_MEMORY_PAGE_SIZE};
  g->MappingObjects[i] = object;
  ++g->Inventory.MappingCount;
  return 1;
}

#ifndef APPLE_AGX_FULL_CONTEXT0_BROKER
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryPrepare(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph, const APPLE_AGX_MEMORY_IO *Io,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot) {
  return AppleAgxInitdataMemoryPrepareInternal(Graph,Io,Snapshot,0);
}

#endif
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryPrepareBroker(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph, const APPLE_AGX_MEMORY_IO *Io,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot) {
  unsigned int i;
  APPLE_AGX_INITDATA_MEMORY_RESULT result =
      AppleAgxInitdataMemoryPrepareInternal(Graph,Io,Snapshot,1);
  if (result != AppleAgxInitdataMemoryResultOk) return result;
  for(i=0;i<APPLE_AGX_INITDATA_MEMORY_OBJECT_COUNT;++i)
    if (i != AppleAgxInitdataMemoryCrashlog &&
        !AppleAgxInitdataRecordRange(Graph,&Graph->DataObjects[i],Graph->VirtualAddresses[i]))
      goto Fail;
  for(i=0;i<APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT;++i)
    if (!AppleAgxInitdataRecordRange(Graph,&Graph->ChannelMemory.Objects[i],Graph->ChannelMemory.VirtualAddresses[i]))
      goto Fail;
  for(i=0;i<APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT;++i)
    if (!AppleAgxInitdataRecordRange(Graph,&Graph->RegionBMemory.Objects[i],Graph->RegionBMemory.VirtualAddresses[i]))
      goto Fail;
  for(i=0;i<APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;++i)
    if (!AppleAgxInitdataRecordRange(Graph,&Graph->RenderSharedMemory.Objects[i],Graph->RenderSharedMemory.VirtualAddresses[i]))
      goto Fail;
  if (!AppleAgxInitdataRecordRange(Graph,
      &Graph->RegionBMemory.Objects[AppleAgxRegionBMemoryBufferManager],
      J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA)) goto Fail;
  return AppleAgxInitdataMemoryResultOk;
Fail:
  return AppleAgxInitdataMemoryRollback(Graph,AppleAgxInitdataMemoryResultUatFailed);
}

#ifndef APPLE_AGX_FULL_CONTEXT0_BROKER
static APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryMapPrepared(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph) {
  unsigned int index;
  APPLE_AGX_UAT_RESULT uat_result;
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
    if (AppleAgxMemoryMarkGpuMapped(
            &Graph->ChannelMemory.Objects[index],
            J313_AGX_G2_UAT_FIRMWARE_CONTEXT,
            Graph->ChannelMemory.VirtualAddresses[index]) !=
            AppleAgxMemoryResultOk)
      return AppleAgxInitdataMemoryRollback(
          Graph, AppleAgxInitdataMemoryResultUatFailed);
  }
  for (index = 0u; index < APPLE_AGX_REGIONB_MEMORY_OBJECT_COUNT; ++index) {
    uat_result = AppleAgxUatMap(
        J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->Roots,
        Graph->RegionBMemory.VirtualAddresses[index],
        Graph->RegionBMemory.Objects[index].DeviceAddress,
        Graph->RegionBMemory.Objects[index].Length,
        AppleAgxUatFirmwareSharedReadWrite, &Graph->UatAllocator,
        &Graph->Inventory);
    if (uat_result != AppleAgxUatResultOk)
      return AppleAgxInitdataMemoryRollback(
          Graph, uat_result == AppleAgxUatResultAllocationFailed
                     ? AppleAgxInitdataMemoryResultAllocationFailed
                     : AppleAgxInitdataMemoryResultUatFailed);
  }
  for (index = 0u; index < APPLE_AGX_RENDER_SHARED_MEMORY_OBJECT_COUNT;
       ++index) {
    uat_result = AppleAgxUatMap(
        J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->Roots,
        Graph->RenderSharedMemory.VirtualAddresses[index],
        Graph->RenderSharedMemory.Objects[index].DeviceAddress,
        Graph->RenderSharedMemory.Objects[index].Length,
        AppleAgxUatFirmwareSharedReadWrite, &Graph->UatAllocator,
        &Graph->Inventory);
    if (uat_result != AppleAgxUatResultOk ||
        AppleAgxMemoryMarkGpuMapped(
            &Graph->RenderSharedMemory.Objects[index],
            J313_AGX_G2_UAT_FIRMWARE_CONTEXT,
            Graph->RenderSharedMemory.VirtualAddresses[index]) !=
            AppleAgxMemoryResultOk)
      return AppleAgxInitdataMemoryRollback(
          Graph, uat_result == AppleAgxUatResultAllocationFailed
                     ? AppleAgxInitdataMemoryResultAllocationFailed
                     : AppleAgxInitdataMemoryResultUatFailed);
  }
  uat_result = AppleAgxUatMap(
      J313_AGX_G2_UAT_FIRMWARE_CONTEXT, &Graph->Roots,
      J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA,
      Graph->RegionBMemory.Objects[AppleAgxRegionBMemoryBufferManager]
          .DeviceAddress,
      Graph->RegionBMemory.Objects[AppleAgxRegionBMemoryBufferManager].Length,
      AppleAgxUatFirmwareSharedReadWrite, &Graph->UatAllocator,
      &Graph->Inventory);
  if (uat_result != AppleAgxUatResultOk)
    return AppleAgxInitdataMemoryRollback(
        Graph, uat_result == AppleAgxUatResultAllocationFailed
                   ? AppleAgxInitdataMemoryResultAllocationFailed
                   : AppleAgxInitdataMemoryResultUatFailed);


  Graph->MappingsReady = 1u;
  return AppleAgxInitdataMemoryResultOk;
}

APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryBuild(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    const APPLE_AGX_CONFIG_SNAPSHOT *Snapshot) {
  APPLE_AGX_INITDATA_MEMORY_RESULT result =
      AppleAgxInitdataMemoryPrepare(Graph, MemoryIo, Snapshot);
  return result == AppleAgxInitdataMemoryResultOk
      ? AppleAgxInitdataMemoryMapPrepared(Graph) : result;
}

APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryImportAndMap(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph, const AGX_FW_PREFIX *Prefix) {
  unsigned int index;
  unsigned long long *root = 0;
  APPLE_AGX_INITDATA_MEMORY_RESULT result;
  if (!Graph || Graph->BrokerOnly || !Graph->Built || Graph->MappingsReady ||
      Graph->Inventory.MappingCount || !Prefix ||
      !AgxFwPrefixValid(Prefix, sizeof(*Prefix), Prefix->Epoch))
    return AppleAgxInitdataMemoryResultInvalidArgument;
  for (index = 0; index < Graph->Inventory.PageCount; ++index)
    if (Graph->Inventory.Pages[index].PhysicalAddress == Graph->Roots.Ttbr1PhysicalAddress)
      root = Graph->Inventory.Pages[index].Entries;
  if (!root)
    return AppleAgxInitdataMemoryResultInvalidArgument;
  for (index = 0; index < 2048; ++index)
    if (root[index])
      return AppleAgxInitdataMemoryResultInvalidArgument;
  /* Only the owned root contains these borrowed references. Do not add their
   * physical addresses to Inventory: firmware retains the private subtrees. */
  root[0] = Prefix->Entries[0];
  root[1] = Prefix->Entries[1];
  result = AppleAgxInitdataMemoryMapPrepared(Graph);
  if (result != AppleAgxInitdataMemoryResultOk)
    return result; /* mapping failure has already rolled back owned storage */
  if (root[0] != Prefix->Entries[0] || root[1] != Prefix->Entries[1])
    return AppleAgxInitdataMemoryRollback(Graph, AppleAgxInitdataMemoryResultUatFailed);
  return result;
}

#endif
APPLE_AGX_INITDATA_MEMORY_RESULT AppleAgxInitdataMemoryDestroy(
    APPLE_AGX_INITDATA_MEMORY_GRAPH *Graph) {
  unsigned int index;

  if (Graph == 0)
    return AppleAgxInitdataMemoryResultInvalidArgument;
  if (Graph->Initialized == 0u)
    return AppleAgxInitdataMemoryResultOk;

  if (Graph->BrokerOutstanding) return AppleAgxInitdataMemoryResultReleaseFailed;
  AppleAgxInitdataMemoryClearPublished(Graph);
#ifndef APPLE_AGX_FULL_CONTEXT0_BROKER
  if (!Graph->BrokerOnly) {
  AppleAgxUatDestroy(&Graph->UatAllocator, &Graph->Inventory);
  if (AppleAgxUatMemoryOwnerDestroy(&Graph->UatMemoryOwner) !=
      AppleAgxUatMemoryResultOk) {
    Graph->LastResult = AppleAgxInitdataMemoryResultReleaseFailed;
    return Graph->LastResult;
  }
  }
#endif
  Graph->Inventory.MappingCount = 0;
  if (AppleAgxRenderSharedMemoryDestroy(&Graph->RenderSharedMemory) !=
      AppleAgxRenderSharedMemoryResultOk) {
    Graph->LastResult = AppleAgxInitdataMemoryResultReleaseFailed;
    return Graph->LastResult;
  }
  if (AppleAgxRegionBMemoryDestroy(&Graph->RegionBMemory) !=
      AppleAgxRegionBMemoryResultOk) {
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
  Graph->BrokerOnly = 0u;
  Graph->LastResult = AppleAgxInitdataMemoryResultOk;
  return Graph->LastResult;
}
