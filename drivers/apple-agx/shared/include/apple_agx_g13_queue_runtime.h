#ifndef APPLE_AGX_G13_QUEUE_RUNTIME_H
#define APPLE_AGX_G13_QUEUE_RUNTIME_H

#include "apple_agx_g13_codec.h"

typedef enum _APPLE_AGX_G13_QUEUE_RUNTIME_PHASE {
  AppleAgxG13QueueRuntimeReady = 0,
  AppleAgxG13QueueRuntimeComputeSubmitted,
  AppleAgxG13QueueRuntimeComputeComplete,
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
  /* Firmware Timeout/Fault recovered and actual queue retirement proven. */
  AppleAgxG13QueueCompletionRecovered,
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
  APPLE_AGX_G13_QUEUE_BINDING Compute;
  APPLE_AGX_BACKEND_BOOL HasCompute;
  /* Q6: dependent work after an unproven/dropped head cannot succeed. */
  APPLE_AGX_BACKEND_BOOL AbortFaultSuffix;
  APPLE_AGX_BACKEND_U64 TimeoutTicks;
} APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG;

#define APPLE_AGX_G13_NOTIFIER_COMMIT 1
typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME_IO {
  void *Context;
  APPLE_AGX_BACKEND_BOOL (*FlushForDevice)(void *Context,
                                           const void *Address,
                                           APPLE_AGX_BACKEND_U32 Bytes);
  void (*MemoryBarrier)(void *Context);
  /* FALSE guarantees no store occurred (Windows validates before writing). */
  APPLE_AGX_BACKEND_BOOL (*PublishU32)(
      void *Context, volatile APPLE_AGX_BACKEND_U32 *Address,
      APPLE_AGX_BACKEND_U32 Value);
  APPLE_AGX_BACKEND_BOOL (*ReadU32)(
      void *Context, const volatile APPLE_AGX_BACKEND_U32 *Address,
      APPLE_AGX_BACKEND_U32 *Value);
  APPLE_AGX_BACKEND_BOOL (*SendRunMessage)(
      void *Context, APPLE_AGX_BACKEND_U32 QueueType,
      const unsigned char Message[APPLE_AGX_G13_RUN_MESSAGE_SIZE]);
  /* Optional transaction: after all work/ring flushes, before the first
   * cpu_wptr. Finish(FALSE) restores an unpublished build; TRUE retains it.
   * A failed restore is fatal, never permission to reuse the slot. */
  APPLE_AGX_BACKEND_BOOL (*PreparePublish)(void *Context, APPLE_AGX_BACKEND_U32 Fence);
  APPLE_AGX_BACKEND_BOOL (*FinishPublish)(void *Context, APPLE_AGX_BACKEND_U32 Fence,
                                         APPLE_AGX_BACKEND_BOOL Visible);
  /*
   * Success means the platform has synchronously stopped this fence and made
   * both queue pointer mappings safe to rebase. It must not return before the
   * GPU can no longer access the submitted work bytes.
   */
  APPLE_AGX_BACKEND_BOOL (*Quiesce)(void *Context,
                                    APPLE_AGX_BACKEND_U32 Fence);
  /*
   * Optional (EXP1075). The firmware halts itself after it reports a Timeout
   * or Fault event (FW status halted=1); Asahi recover() resumes it with
   * halted=0, resume=1 after clearing pending-stamp records. Success only
   * acknowledges this resume request. The runtime must still observe actual
   * GPU done pointers reaching every published queue endpoint before releasing
   * the dropped submission. Killed commands need not write success stamps.
   */
  APPLE_AGX_BACKEND_BOOL (*Recover)(void *Context,
                                    APPLE_AGX_BACKEND_U32 Fence,
                                    APPLE_AGX_BACKEND_U32 EventKind);
  /* ChannelError's separate Asahi handshake. Success proves only that the
   * device-control command was consumed, never that DMA ownership returned. */
  APPLE_AGX_BACKEND_BOOL (*RecoverChannel)(
      void *Context, APPLE_AGX_BACKEND_U32 Fence,
      APPLE_AGX_BACKEND_U32 PipeType,
      APPLE_AGX_BACKEND_U64 QueueInfoGpuAddress,
      APPLE_AGX_BACKEND_U32 EventValue);
} APPLE_AGX_G13_QUEUE_RUNTIME_IO;

/* D3D-G1b: a TA submission may lead with a firmware Barrier work item
 * (render waiting on its submission's compute), one entry more than the
 * job's TA/3D work items. */
#define APPLE_AGX_G13_QUEUE_PUBLISH_MAX (APPLE_AGX_BACKEND_QUEUE_WORK_COUNT + 1u)

typedef struct _APPLE_AGX_G13_WORK_PUBLICATION {
  struct {
    const void *Address;
    APPLE_AGX_BACKEND_U32 Bytes;
  } PreparedRanges[APPLE_AGX_G13_QUEUE_PUBLISH_MAX];
  APPLE_AGX_BACKEND_U32 PreparedRangeCount;
  /* Legacy copy path retained for qualification fixtures. Production
   * submissions use PreparedRanges and never copy already-relocated work. */
  const unsigned char *Source;
  unsigned char *Destination;
  APPLE_AGX_BACKEND_U32 Bytes;
  APPLE_AGX_BACKEND_U64
      GpuAddresses[APPLE_AGX_G13_QUEUE_PUBLISH_MAX];
  APPLE_AGX_BACKEND_U32 GpuAddressCount;
  APPLE_AGX_BACKEND_U32 ExpectedStamp;
} APPLE_AGX_G13_WORK_PUBLICATION;

typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION {
  APPLE_AGX_BACKEND_U32 Fence;
  APPLE_AGX_BACKEND_U64 Timestamp;
  APPLE_AGX_BACKEND_U64 NowTicks;
  APPLE_AGX_G13_WORK_PUBLICATION Ta;
  APPLE_AGX_G13_WORK_PUBLICATION D3;
  APPLE_AGX_G13_WORK_PUBLICATION Compute;
  /* G2: a standalone compute job. Its compute completion is the job's
   * completion (no TA/3D follows). */
  APPLE_AGX_BACKEND_BOOL ComputeOnly;
  /* D3D-G1b: Ta.GpuAddresses[0] is a firmware Barrier that holds the TA
   * queue until the submission's compute stamp is written (Asahi
   * queue/mod.rs explicit barrier); only SubmitRenderWithCompute takes it. */
  APPLE_AGX_BACKEND_BOOL TaLeadingBarrier;
  /* rc102 (depth-2 job window, render_job_window.h): this job is the
   * successor of the running one and enters the successor slot while the
   * running job (PendingFence) is still with the firmware. The first entry
   * of the queue it starts on (TA for a render without compute work,
   * compute otherwise) is its leading firmware Barrier on the running
   * job's last firmware stamp, one ring entry more than at depth 1. A
   * successor render needs the current buffer-manager binding and started
   * queues (no InitBM, no NewQueue on TA/3D). */
  APPLE_AGX_BACKEND_BOOL Successor;
  /* rc102: the prepared range that holds the job's leading Barrier
   * (APPLE_AGX_BACKEND_JOB_IMAGE LeadBarrierAddress), set by the
   * submission builder; the provider puts it first in the lead queue. */
  struct {
    const void *Address;
    APPLE_AGX_BACKEND_U32 Bytes;
  } LeadBarrierRange;
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

/* rc102: the job kicked behind the running one (depth 2). Its records are
 * observed with every event like the running job's; it becomes the
 * running job when that job's completion is taken (TakeCompletion), and is
 * then complete at once when the notifier deferred both ends to its own
 * (U4: batched). VerdictValid: a firmware recovery retired the whole
 * window; Verdict is Success when every stamp of the successor was
 * reached, else Recovered (killed with the dropped job). */
typedef struct _APPLE_AGX_G13_QUEUE_SUCCESSOR {
  APPLE_AGX_BACKEND_U32 Fence;
  APPLE_AGX_BACKEND_U64 DeadlineTicks;
  APPLE_AGX_G13_QUEUE_PENDING TaPending;
  APPLE_AGX_G13_QUEUE_PENDING D3Pending;
  APPLE_AGX_G13_QUEUE_PENDING ComputePending;
  APPLE_AGX_BACKEND_BOOL ComputeTerminal;
  APPLE_AGX_BACKEND_BOOL ComputeInRender;
  APPLE_AGX_BACKEND_BOOL VerdictValid;
  APPLE_AGX_G13_QUEUE_COMPLETION_STATUS Verdict;
} APPLE_AGX_G13_QUEUE_SUCCESSOR;

/* Q12: pinned software completion-stall evidence, one pair per runtime.
 * No GPU command or firmware state is modified. All fields are 32-bit wire
 * words; status values are APPLE_AGX_G13_QUEUE_COMPLETION_STATUS. */
typedef struct _APPLE_AGX_G13_DEPTH2_DRILL {
  APPLE_AGX_BACKEND_U32 Version, Bytes, Target, Released;
  APPLE_AGX_BACKEND_U32 HeadFence, NextFence;
  APPLE_AGX_BACKEND_U32 Pipe[2], ExpectedDone[2], ExpectedStamp[2];
  /* head, successor, head again: outstanding at the final bracket read. */
  APPLE_AGX_BACKEND_U32 Done[3], Write[3];
  APPLE_AGX_BACKEND_U32 TerminalCount, TerminalFence[2], TerminalStatus[2];
} APPLE_AGX_G13_DEPTH2_DRILL;

typedef struct _APPLE_AGX_G13_QUEUE_RUNTIME {
  APPLE_AGX_G13_QUEUE_RUNTIME_PHASE Phase;
  APPLE_AGX_G13_DEPTH2_DRILL Drill;
  APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG Config;
  APPLE_AGX_G13_QUEUE_RUNTIME_IO Io;
  APPLE_AGX_BACKEND_U32 PendingFence;
  APPLE_AGX_BACKEND_U64 DeadlineTicks;
  /*
   * InitBM describes the current buffer-manager binding. It is published on
   * queue creation and after an idle manager rebind. Clearing this flag does
   * not reset ring pointers, event ownership or the FirstRun flags.
   */
  APPLE_AGX_BACKEND_BOOL BufferManagerInitialized;
  APPLE_AGX_BACKEND_BOOL TaFirstRun;
  APPLE_AGX_BACKEND_BOOL D3FirstRun;
  APPLE_AGX_BACKEND_BOOL ComputeFirstRun;
  APPLE_AGX_BACKEND_BOOL CompletionAvailable;
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION Completion;
  APPLE_AGX_G13_QUEUE_PENDING TaPending;
  APPLE_AGX_G13_QUEUE_PENDING D3Pending;
  APPLE_AGX_G13_QUEUE_PENDING ComputePending;
  /* The pending compute work is a standalone job (submission ComputeOnly). */
  APPLE_AGX_BACKEND_BOOL ComputeTerminal;
  /* D3D-G1b: the pending render also waits for its compute work (one
   * submission; the notifier fires once all three queues are done). */
  APPLE_AGX_BACKEND_BOOL ComputeInRender;
  APPLE_AGX_BACKEND_U32 RecoveredFaults, LastRecoveredEventKind;
  /* Firmware resume requested; submitted bytes remain owned until retirement. */
  APPLE_AGX_BACKEND_BOOL RecoveryPending;
  APPLE_AGX_BACKEND_BOOL RecoveredTimeoutValid;
  APPLE_AGX_BACKEND_U32 LastRecoveredTimeoutCounter;
  APPLE_AGX_BACKEND_U32 ChannelRecoveryMask;
  APPLE_AGX_BACKEND_BOOL ChannelRecoveryFailed;
  APPLE_AGX_BACKEND_U32 RecoveredChannelMask;
  APPLE_AGX_BACKEND_U32 LastRecoveredChannelValue[3];
  /* rc77: events the firmware sent while RecoveryPending (any kind, also
   * stale Timeouts), and the one KMD-initiated RecoverChannel per job. */
  APPLE_AGX_BACKEND_U32 PostRecoveryEvents, PostRecoveryKindMask;
  APPLE_AGX_BACKEND_U32 LastPostRecoveryKind;
  APPLE_AGX_BACKEND_BOOL ForcedChannelRecovery;
  APPLE_AGX_BACKEND_U32 ForcedChannelMask;
  /* G4-recovery: the pending job outlived its deadline (counted once per
   * job); the deadline is recorded, not terminal. */
  APPLE_AGX_BACKEND_BOOL DeadlinePassed;
  APPLE_AGX_BACKEND_U32 DeadlineExpiries;
  /* rc102: depth-2 successor slot (empty at depth 1) and its receipts:
   * successors published, promoted to running, and promoted already
   * complete (a batched pair, U4). */
  APPLE_AGX_G13_QUEUE_SUCCESSOR Successor;
  APPLE_AGX_BACKEND_U32 SuccessorKicks;
  APPLE_AGX_BACKEND_U32 SuccessorPromotions;
  APPLE_AGX_BACKEND_U32 BatchedPromotions;
} APPLE_AGX_G13_QUEUE_RUNTIME;

APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeInitialize(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG *Config,
    const APPLE_AGX_G13_QUEUE_RUNTIME_IO *Io);
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmit(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission);
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmitCompute(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    const APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *Submission);
/* D3D-G1b: one submission of compute and TA/3D. The TA queue's first entry
 * is a firmware Barrier on the compute stamp, so the firmware orders the
 * render after the compute (Asahi); the shared notifier threshold counts all
 * three notifications, so a CPU wait for the compute alone would deadlock
 * (EXP1278). */
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmitRenderWithCompute(
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
/* Worker-serialized, diagnostic only. Target 1=head, 2=successor; invalid,
 * stale, already retired or second arms are rejected. Release is called only
 * after Resetting has stopped the worker. Poll still proves physical retirement. */
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueRuntimeDrillArm(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, APPLE_AGX_BACKEND_U32 Target);
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueRuntimeDrillRelease(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime);

/* rc77 (EXP2005/EXP2007): after a Timeout/Fault was recovered (firmware
 * resumed) and a queue of the pending job still has done != write, publish
 * Asahi's RecoverChannel (fw/channels.rs:187) once per unretired pipe with
 * that pipe's QueueInfo and the job's expected stamp, as gpu.rs:1459 does for
 * a ChannelError. Once per job; never without a prior firmware recovery.
 * Ownership is still proven only by PollRecovery (done == write), reached
 * through the next CheckTimeout or event; this call never completes the job.
 * *Issued is the mask of pipes the command was sent for (0 when nothing was
 * needed). */
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeForceChannelRecovery(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime, APPLE_AGX_BACKEND_U32 *Issued);
/* rc102: is the running job's completion available with its successor
 * slot occupied? (TakeCompletion then promotes the successor.) */
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueRuntimeHasSuccessor(
    const APPLE_AGX_G13_QUEUE_RUNTIME *Runtime);
/* Takes the running job's completion. rc102: after a Success or Recovered
 * completion an occupied successor slot becomes the running job (its
 * completion is available at once when it already ended). */
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueRuntimeTakeCompletion(
    APPLE_AGX_G13_QUEUE_RUNTIME *Runtime,
    APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION *Completion);

#endif /* APPLE_AGX_G13_QUEUE_RUNTIME_H */
