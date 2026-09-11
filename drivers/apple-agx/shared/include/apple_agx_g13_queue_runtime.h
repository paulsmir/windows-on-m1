#ifndef APPLE_AGX_G13_QUEUE_RUNTIME_H
#define APPLE_AGX_G13_QUEUE_RUNTIME_H

#include "apple_agx_g13_codec.h"

typedef enum _APPLE_AGX_G13_QUEUE_RUNTIME_PHASE {
  AppleAgxG13QueueRuntimeReady = 0,
  AppleAgxG13QueueRuntimeSubmitted,
  AppleAgxG13QueueRuntimeCompletionPending,
  AppleAgxG13QueueRuntimeFaulted,
} APPLE_AGX_G13_QUEUE_RUNTIME_PHASE;

typedef enum _APPLE_AGX_G13_QUEUE_RUNTIME_RESULT {
  AppleAgxG13QueueRuntimeResultOk = 0,
  AppleAgxG13QueueRuntimeResultInvalidArgument,
  AppleAgxG13QueueRuntimeResultInvalidState,
  AppleAgxG13QueueRuntimeResultBusy,
  AppleAgxG13QueueRuntimeResultMemoryFailed,
  AppleAgxG13QueueRuntimeResultTransportFailed,
  AppleAgxG13QueueRuntimeResultFaulted,
  AppleAgxG13QueueRuntimeResultTimedOut,
  AppleAgxG13QueueRuntimeResultCancelled,
  AppleAgxG13QueueRuntimeResultResetFailed,
} APPLE_AGX_G13_QUEUE_RUNTIME_RESULT;

typedef enum _APPLE_AGX_G13_QUEUE_COMPLETION_STATUS {
  AppleAgxG13QueueCompletionSuccess = 0,
  AppleAgxG13QueueCompletionFaulted,
  AppleAgxG13QueueCompletionTimedOut,
  AppleAgxG13QueueCompletionCancelled,
} APPLE_AGX_G13_QUEUE_COMPLETION_STATUS;

typedef struct _APPLE_AGX_G13_QUEUE_BINDING {
  APPLE_AGX_BACKEND_U32 QueueType;
  APPLE_AGX_BACKEND_U64 QueueInfoGpuAddress;
  APPLE_AGX_BACKEND_U64 *RingCpuAddress;
  APPLE_AGX_BACKEND_U32 RingCapacity;
  volatile APPLE_AGX_BACKEND_U32 *CpuWritePointer;
  volatile APPLE_AGX_BACKEND_U32 *GpuDonePointer;
  volatile APPLE_AGX_BACKEND_U32 *Stamp;
  APPLE_AGX_BACKEND_U32 EventNumber;
} APPLE_AGX_G13_QUEUE_BINDING;

typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG {
  APPLE_AGX_G13_QUEUE_BINDING Ta;
  APPLE_AGX_G13_QUEUE_BINDING D3;
  APPLE_AGX_BACKEND_U64 TimeoutTicks;
} APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG;

typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME_IO {
  void *Context;
  APPLE_AGX_BACKEND_BOOL (*FlushForDevice)(void *Context,
                                           const void *Address,
                                           APPLE_AGX_BACKEND_U32 Bytes);
  void (*MemoryBarrier)(void *Context);
  APPLE_AGX_BACKEND_BOOL (*PublishU32)(
      void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
      APPLE_AGX_BACKEND_U32 Value);
  APPLE_AGX_BACKEND_BOOL (*ReadU32)(
      void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
      APPLE_AGX_BACKEND_U32 *Value);
  APPLE_AGX_BACKEND_BOOL (*SendRunMessage)(
      void *Context, APPLE_AGX_BACKEND_U32 QueueType,
      const unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]);
  /*
   * Success means the platform has synchronously stopped this fence and made
   * both queue pointer mappings safe to rebase. It must not return before the
   * GPU can no longer access the submitted work bytes.
   */
  APPLE_AGX_BACKEND_BOOL (*Quiesce)(void *Context,
                                    APPLE_AGX_BACKEND_U32 Fence);
} APPLE_AGX_G13_QUEUE_RUNTIME_IO;

typedef struct _APPLE_AGX_G13_WORK_PUBLICATION {
  struct {
    const void *Address;
    APPLE_AGX_BACKEND_U32 Bytes;
  } PreparedRanges[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  APPLE_AGX_BACKEND_U32 PreparedRangeCount;
  /* Legacy copy path retained for qualification fixtures. Production
   * submissions use PreparedRanges and never copy already-relocated work. */
  const unsigned char *Source;
  unsigned char *Destination;
  APPLE_AGX_BACKEND_U32 Bytes;
  APPLE_AGX_BACKEND_U64
      GpuAddresses[APPLE_AGX_BACKEND_QUEUE_WORK_COUNT];
  APPLE_AGX_BACKEND_U32 GpuAddressCount;
  APPLE_AGX_BACKEND_U32 ExpectedStamp;
} APPLE_AGX_G13_WORK_PUBLICATION;

typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION {
  APPLE_AGX_BACKEND_U32 Fence;
  APPLE_AGX_BACKEND_U64 Timestamp;
  APPLE_AGX_BACKEND_U64 NowTicks;
  APPLE_AGX_G13_WORK_PUBLICATION Ta;
  APPLE_AGX_G13_WORK_PUBLICATION D3;
} APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION;

typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION {
  APPLE_AGX_BACKEND_U32 Fence;
  APPLE_AGX_G13_QUEUE_COMPLETION_STATUS Status;
} APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION;

typedef struct _APPLE_AGX_G13_QUEUE_PENDING {
  APPLE_AGX_BACKEND_U32 EventNumber;
  APPLE_AGX_BACKEND_U32 ExpectedStamp;
  APPLE_AGX_BACKEND_U32 ExpectedDonePointer;
  APPLE_AGX_BACKEND_BOOL EventSeen;
  APPLE_AGX_BACKEND_BOOL Complete;
} APPLE_AGX_G13_QUEUE_PENDING;

typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME {
  APPLE_AGX_G13_QUEUE_RUNTIME_PHASE Phase;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG Config;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO Io;
  APPLE_AGX_BACKEND_U32 PendingFence;
  APPLE_AGX_BACKEND_U64 DeadlineTicks;
  /*
   * InitBM is a queue-lifetime command.  It is published once after queue
   * creation, remains initialized across successful jobs, and becomes false
   * again only when the queue runtime is reset/recreated.
   */
  APPLE_AGX_BACKEND_BOOL BufferManagerInitialized;
  APPLE_AGX_BACKEND_BOOL TaFirstRun;
  APPLE_AGX_BACKEND_BOOL D3FirstRun;
  APPLE_AGX_BACKEND_BOOL CompletionAvailable;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION Completion;
  APPLE_AGX_G13_QUEUE_PENDING TaPending;
  APPLE_AGX_G13_QUEUE_PENDING D3Pending;
} APPLE_AGX_G13_QUEUE_RUNTIME;

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeInitialize(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *Io);
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmit(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission);
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeHandleEvent(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, const unsigned char *Message,
    APPLE_AGX_BACKEND_U32 MessageBytes);
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeCheckTimeout(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_BACKEND_U64 NowTicks);
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeCancel(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, APPLE_AGX_BACKEND_U32 Fence);
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT
AppleAgxG13QueueRuntimeReset(APPLE_AGX_G13_QUEUE_RUNTIME *Runtime);
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueRuntimeTakeCompletion(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION *Completion);

#endif /* APPLE_AGX_G13_QUEUE_RUNTIME_H */
