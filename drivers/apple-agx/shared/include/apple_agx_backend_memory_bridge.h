#ifndef APPLE_AGX_BACKEND_MEMORY_BRIDGE_H
#define APPLE_AGX_BACKEND_MEMORY_BRIDGE_H

#include "apple_agx_dma_shadow.h"
#include "apple_agx_memory.h"
#include "apple_agx_render_template.h"

#define APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES (64u * 1024u * 1024u)
#define APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE 0x1500000000ULL
#define APPLE_AGX_BACKEND_BRIDGE_DMA_SCRATCH_BYTES 8192u

typedef enum _APPLE_AGX_BACKEND_MEMORY_SYNC_DIRECTION {
  AppleAgxBackendMemorySyncForDevice = 1,
  AppleAgxBackendMemorySyncForCpu,
} APPLE_AGX_BACKEND_MEMORY_SYNC_DIRECTION;

typedef struct _APPLE_AGX_BACKEND_MEMORY_SYNC_IO {
  void *Context;
  unsigned char (*Synchronize)(
      void *Context, const APPLE_AGX_MEMORY_OBJECT *Pool,
      const void *Address, unsigned int Bytes,
      APPLE_AGX_BACKEND_MEMORY_SYNC_DIRECTION Direction);
} APPLE_AGX_BACKEND_MEMORY_SYNC_IO;

typedef struct _APPLE_AGX_BACKEND_MEMORY_BRIDGE {
  APPLE_AGX_MEMORY_OBJECT *Pool;
  APPLE_AGX_RENDER_TEMPLATE_ROOTS Roots;
  APPLE_AGX_BACKEND_MEMORY_SYNC_IO Sync;
  unsigned char DmaScratch[APPLE_AGX_BACKEND_BRIDGE_DMA_SCRATCH_BYTES];
  unsigned long long SubmittedFence;
  unsigned char Borrowed;
} APPLE_AGX_BACKEND_MEMORY_BRIDGE;

unsigned char AppleAgxBackendMemoryBridgeRootsValid(
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots);
unsigned char AppleAgxBackendMemoryBridgeInitialize(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, APPLE_AGX_MEMORY_OBJECT *Pool,
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots,
    const APPLE_AGX_BACKEND_MEMORY_SYNC_IO *Sync);
unsigned char AppleAgxBackendMemoryBridgeBorrow(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, void **CpuAddress,
    unsigned long long *GpuAddress, unsigned int *Bytes);
unsigned char AppleAgxBackendMemoryBridgeRelease(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, void *CpuAddress,
    unsigned int Bytes);
unsigned char AppleAgxBackendMemoryBridgeResolve(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, const void *PrivateData,
    unsigned int PrivateDataBytes, unsigned int PrivateDataStart,
    unsigned int PrivateDataEnd, unsigned int DmaSubmissionStart,
    unsigned int DmaSubmissionEnd, const unsigned char **Bytes,
    unsigned int *ByteCount);
unsigned char AppleAgxBackendMemoryBridgeFlushForDevice(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, const void *Address,
    unsigned int Bytes);
unsigned char AppleAgxBackendMemoryBridgeFlushForCpu(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, const void *Address,
    unsigned int Bytes);
unsigned char AppleAgxBackendMemoryBridgeMarkSubmitted(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, unsigned long long Fence);
unsigned char AppleAgxBackendMemoryBridgeMarkCompleted(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, unsigned long long Fence);
unsigned char AppleAgxBackendMemoryBridgeMarkAborted(
    APPLE_AGX_BACKEND_MEMORY_BRIDGE *Bridge, unsigned long long Fence);

#endif /* APPLE_AGX_BACKEND_MEMORY_BRIDGE_H */
