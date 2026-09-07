#ifndef APPLE_AGX_CHANNEL_MEMORY_H
#define APPLE_AGX_CHANNEL_MEMORY_H

#include "apple_agx_channel_info.h"
#include "apple_agx_memory.h"

#define APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT 35u

typedef enum _APPLE_AGX_CHANNEL_MEMORY_OBJECT_INDEX {
  AppleAgxChannelMemoryCommandStateBase = 0,
  AppleAgxChannelMemoryCommandStateLast = 11,
  AppleAgxChannelMemoryCommandRingBase = 12,
  AppleAgxChannelMemoryCommandRingLast = 23,
  AppleAgxChannelMemoryDevctrlState = 24,
  AppleAgxChannelMemoryDevctrlRing = 25,
  AppleAgxChannelMemoryEventState = 26,
  AppleAgxChannelMemoryEventRing = 27,
  AppleAgxChannelMemoryFwlogState = 28,
  AppleAgxChannelMemoryFwlogRing = 29,
  AppleAgxChannelMemoryFwlogDummyRing = 30,
  AppleAgxChannelMemoryKtraceState = 31,
  AppleAgxChannelMemoryKtraceRing = 32,
  AppleAgxChannelMemoryStatsState = 33,
  AppleAgxChannelMemoryStatsRing = 34,
} APPLE_AGX_CHANNEL_MEMORY_OBJECT_INDEX;

typedef enum _APPLE_AGX_CHANNEL_MEMORY_RESULT {
  AppleAgxChannelMemoryResultOk = 0,
  AppleAgxChannelMemoryResultInvalidArgument,
  AppleAgxChannelMemoryResultAllocationFailed,
  AppleAgxChannelMemoryResultReleaseFailed,
} APPLE_AGX_CHANNEL_MEMORY_RESULT;

typedef struct _APPLE_AGX_CHANNEL_MEMORY_OWNER {
  const APPLE_AGX_MEMORY_IO *MemoryIo;
  APPLE_AGX_MEMORY_OBJECT Objects[APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT];
  unsigned long long VirtualAddresses[APPLE_AGX_CHANNEL_MEMORY_OBJECT_COUNT];
  APPLE_AGX_CHANNEL_INFO_INPUT ChannelInfo;
  unsigned long long RealFwlogRingAddress;
  unsigned int ObjectCount;
  unsigned char Initialized;
  unsigned char Built;
  APPLE_AGX_CHANNEL_MEMORY_RESULT LastResult;
} APPLE_AGX_CHANNEL_MEMORY_OWNER;

APPLE_AGX_CHANNEL_MEMORY_RESULT AppleAgxChannelMemoryBuild(
    APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner,
    const APPLE_AGX_MEMORY_IO *MemoryIo,
    unsigned long long FirstVirtualAddress);
APPLE_AGX_CHANNEL_MEMORY_RESULT AppleAgxChannelMemoryDestroy(
    APPLE_AGX_CHANNEL_MEMORY_OWNER *Owner);

#endif /* APPLE_AGX_CHANNEL_MEMORY_H */
