#ifndef APPLE_AGX_MEMORY_H
#define APPLE_AGX_MEMORY_H

#define APPLE_AGX_MEMORY_PAGE_SIZE 0x4000ULL
#define APPLE_AGX_MEMORY_DEVICE_ADDRESS_LIMIT (1ULL << 40)

typedef enum _APPLE_AGX_MEMORY_RESULT {
  AppleAgxMemoryResultOk = 0,
  AppleAgxMemoryResultInvalidArgument,
  AppleAgxMemoryResultAllocationFailed,
  AppleAgxMemoryResultOutOfRange,
  AppleAgxMemoryResultBusy,
  AppleAgxMemoryResultNotPrepared,
  AppleAgxMemoryResultStaleFence,
} APPLE_AGX_MEMORY_RESULT;

typedef enum _APPLE_AGX_MEMORY_STATE {
  AppleAgxMemoryEmpty = 0,
  AppleAgxMemoryCpuOwned,
  AppleAgxMemoryPrepared,
  AppleAgxMemoryGpuMapped,
  AppleAgxMemoryInFlight,
  AppleAgxMemoryCompleted,
} APPLE_AGX_MEMORY_STATE;

typedef struct _APPLE_AGX_MEMORY_OBJECT {
  void *AllocationCpuBase;
  void *CpuAddress;
  void *AllocationHandle;
  /* Address consumed by Apple UAT; the platform adapter proves its semantics.
   */
  unsigned long long AllocationDeviceBase;
  unsigned long long DeviceAddress;
  unsigned long long AllocationLength;
  unsigned long long Length;
  unsigned long long GpuVirtualAddress;
  unsigned long long SubmittedFence;
  unsigned int Context;
  APPLE_AGX_MEMORY_STATE State;
} APPLE_AGX_MEMORY_OBJECT;

typedef struct _APPLE_AGX_MEMORY_IO {
  void *Context;
  unsigned char (*AllocateContiguous)(void *Context, unsigned long long Bytes,
                                      void **CpuBase,
                                      unsigned long long *DeviceBase,
                                      void **AllocationHandle);
  unsigned char (*FreeContiguous)(void *Context, void *AllocationHandle);
} APPLE_AGX_MEMORY_IO;

APPLE_AGX_MEMORY_RESULT AppleAgxMemoryAllocate(const APPLE_AGX_MEMORY_IO *Io,
                                               unsigned long long Length,
                                               APPLE_AGX_MEMORY_OBJECT *Object);
APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkCpuWritten(APPLE_AGX_MEMORY_OBJECT *Object);
APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkPrepared(APPLE_AGX_MEMORY_OBJECT *Object);
APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkGpuMapped(APPLE_AGX_MEMORY_OBJECT *Object,
                            unsigned int Context,
                            unsigned long long GpuVirtualAddress);
APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkSubmitted(APPLE_AGX_MEMORY_OBJECT *Object,
                            unsigned long long Fence);
APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkCompleted(APPLE_AGX_MEMORY_OBJECT *Object,
                            unsigned long long Fence);
APPLE_AGX_MEMORY_RESULT
AppleAgxMemoryMarkGpuUnmapped(APPLE_AGX_MEMORY_OBJECT *Object);
APPLE_AGX_MEMORY_RESULT AppleAgxMemoryRelease(const APPLE_AGX_MEMORY_IO *Io,
                                              APPLE_AGX_MEMORY_OBJECT *Object);

#endif /* APPLE_AGX_MEMORY_H */
