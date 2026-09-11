#include "apple_agx_backend_memory_bridge.h"

static void AppleAgxBackendBridgeZero(void *Address, unsigned int Bytes) {
  unsigned char *out = (unsigned char *)Address;
  unsigned int index;
  for (index = 0u; index < Bytes; ++index)
    out[index] = 0u;
}

static unsigned char AppleAgxBackendBridgePoolValid(
    const APPLE_AGX_MEMORY_OBJECT *Pool) {
  return Pool != 0 && Pool->CpuAddress != 0 &&
                 Pool->GpuVirtualAddress ==
                     APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE &&
                 Pool->Length == APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES &&
                 (Pool->State == AppleAgxMemoryGpuMapped ||
                  Pool->State == AppleAgxMemoryCompleted)
             ? 1u
             : 0u;
}

unsigned char AppleAgxBackendMemoryBridgeRootsValid(
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots) {
  return Roots != 0 && Roots->Ta[0] == 0x1503880000ULL &&
                 Roots->Ta[1] == 0x1503898000ULL &&
                 Roots->D3[0] == 0x1503870000ULL &&
                 Roots->D3[1] == 0x1503890000ULL
             ? 1u
             : 0u;
}

unsigned char AppleAgxBackendMemoryBridgeInitialize(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, APPLE_AGX_MEMORY_OBJECT *Pool,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const APPLE_AGX_BACKEND_MEMORY_SYNC_IO *Sync) {
  if (Bridge == 0 || !AppleAgxBackendBridgePoolValid(Pool) ||
      !AppleAgxBackendMemoryBridgeRootsValid(Roots) || Sync == 0 ||
      Sync->Synchronize == 0)
    return 0u;
  AppleAgxBackendBridgeZero(Bridge, sizeof(*Bridge));
  Bridge->Pool = Pool;
  Bridge->Roots = *Roots;
  Bridge->Sync = *Sync;
  return 1u;
}

unsigned char AppleAgxBackendMemoryBridgeBorrow(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, void **CpuAddress,
    unsigned long long *GpuAddress, unsigned int *Bytes) {
  if (CpuAddress != 0)
    *CpuAddress = 0;
  if (GpuAddress != 0)
    *GpuAddress = 0ULL;
  if (Bytes != 0)
    *Bytes = 0u;
  if (Bridge == 0 || CpuAddress == 0 || GpuAddress == 0 || Bytes == 0 ||
      Bridge->Borrowed || !AppleAgxBackendBridgePoolValid(Bridge->Pool) ||
      !AppleAgxBackendMemoryBridgeRootsValid(&Bridge->Roots))
    return 0u;
  *CpuAddress = Bridge->Pool->CpuAddress;
  *GpuAddress = Bridge->Pool->GpuVirtualAddress;
  *Bytes = (unsigned int)Bridge->Pool->Length;
  Bridge->Borrowed = 1u;
  return 1u;
}

unsigned char AppleAgxBackendMemoryBridgeRelease(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, void *CpuAddress,
    unsigned int Bytes) {
  if (Bridge == 0 || !Bridge->Borrowed ||
      !AppleAgxBackendBridgePoolValid(Bridge->Pool) ||
      CpuAddress != Bridge->Pool->CpuAddress ||
      Bytes != APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES ||
      Bridge->SubmittedFence != 0ULL)
    return 0u;
  Bridge->Borrowed = 0u;
  return 1u;
}

unsigned char AppleAgxBackendMemoryBridgeResolve(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, const void *PrivateData,
    unsigned int PrivateDataBytes, unsigned int PrivateDataStart,
    unsigned int PrivateDataEnd, unsigned int DmaSubmissionStart,
    unsigned int DmaSubmissionEnd, const unsigned char **Bytes,
    unsigned int *ByteCount) {
  APPLE_AGX_DMA_SHADOW shadow;
  unsigned int copied = 0u;
  if (Bytes != 0)
    *Bytes = 0;
  if (ByteCount != 0)
    *ByteCount = 0u;
  if (Bridge == 0 || !Bridge->Borrowed || PrivateData == 0 || Bytes == 0 ||
      ByteCount == 0 || PrivateDataStart > PrivateDataEnd ||
      PrivateDataEnd > PrivateDataBytes ||
      DmaSubmissionStart >= DmaSubmissionEnd ||
      DmaSubmissionEnd - DmaSubmissionStart > sizeof(Bridge->DmaScratch) ||
      !AppleAgxDmaShadowOpen(&shadow, (void *)PrivateData, PrivateDataBytes) ||
      !AppleAgxDmaShadowIsSealed(shadow.Storage, shadow.BytesUsed) ||
      PrivateDataStart != 0u || PrivateDataEnd < shadow.BytesUsed ||
      !AppleAgxDmaShadowCopySubmission(
          shadow.Storage, shadow.BytesUsed, DmaSubmissionStart,
          DmaSubmissionEnd, Bridge->DmaScratch, sizeof(Bridge->DmaScratch),
          &copied) ||
      copied != DmaSubmissionEnd - DmaSubmissionStart)
    return 0u;
  *Bytes = Bridge->DmaScratch;
  *ByteCount = copied;
  return 1u;
}

static unsigned char AppleAgxBackendMemoryBridgeFlush(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, const void *Address,
    unsigned int Bytes, APPLE_AGX_BACKEND_MEMORY_SYNC_DIRECTION Direction) {
  if (Bridge == 0 || !Bridge->Borrowed ||
      !AppleAgxBackendBridgePoolValid(Bridge->Pool) ||
      Address != Bridge->Pool->CpuAddress ||
      Bytes != APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES ||
      Bridge->Sync.Synchronize == 0)
    return 0u;
  return Bridge->Sync.Synchronize(Bridge->Sync.Context, Bridge->Pool, Address,
                                  Bytes, Direction);
}

unsigned char AppleAgxBackendMemoryBridgeFlushForDevice(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, const void *Address,
    unsigned int Bytes) {
  return AppleAgxBackendMemoryBridgeFlush(
      Bridge, Address, Bytes, AppleAgxBackendMemorySyncForDevice);
}

unsigned char AppleAgxBackendMemoryBridgeFlushForCpu(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, const void *Address,
    unsigned int Bytes) {
  return AppleAgxBackendMemoryBridgeFlush(
      Bridge, Address, Bytes, AppleAgxBackendMemorySyncForCpu);
}

unsigned char AppleAgxBackendMemoryBridgeMarkSubmitted(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, unsigned long long Fence) {
  if (Bridge == 0 || !Bridge->Borrowed || Fence == 0ULL ||
      Bridge->SubmittedFence != 0ULL ||
      AppleAgxMemoryMarkSubmitted(Bridge->Pool, Fence) !=
          AppleAgxMemoryResultOk)
    return 0u;
  Bridge->SubmittedFence = Fence;
  return 1u;
}

static unsigned char AppleAgxBackendMemoryBridgeRetire(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, unsigned long long Fence,
    unsigned char Aborted) {
  APPLE_AGX_MEMORY_RESULT result;
  if (Bridge == 0 || !Bridge->Borrowed || Fence == 0ULL ||
      Fence != Bridge->SubmittedFence)
    return 0u;
  result = Aborted ? AppleAgxMemoryMarkAborted(Bridge->Pool, Fence)
                   : AppleAgxMemoryMarkCompleted(Bridge->Pool, Fence);
  if (result != AppleAgxMemoryResultOk)
    return 0u;
  Bridge->SubmittedFence = 0ULL;
  AppleAgxBackendBridgeZero(Bridge->DmaScratch,
                            sizeof(Bridge->DmaScratch));
  return 1u;
}

unsigned char AppleAgxBackendMemoryBridgeMarkCompleted(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, unsigned long long Fence) {
  return AppleAgxBackendMemoryBridgeRetire(Bridge, Fence, 0u);
}

unsigned char AppleAgxBackendMemoryBridgeMarkAborted(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, unsigned long long Fence) {
  return AppleAgxBackendMemoryBridgeRetire(Bridge, Fence, 1u);
}
