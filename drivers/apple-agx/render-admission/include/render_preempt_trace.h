#pragma once

/* P1 receipt-only (Wom1JobDiagnostics): the last 32 node-0 preemptions,
 * exported as REG_BINARY Wom1PreemptTrace beside Wom1JobTiming971. It
 * separates the post-preemption bubble hypotheses of
 * investigation/analysis/P1-present-serialization.md: H1 DMA_PREEMPTED
 * deferred / commit late, H2 the next submit waits in AwaitWork, H3 the
 * next job's worker pickup lags a timely commit. No behaviour change: the
 * hooks only take QPC stamps and plain or interlocked writes where the
 * existing code already runs (any IRQL, no new lock).
 *
 * Callers guard the hooks with ADMISSION_PREEMPT_TRACE_ON; host tests that
 * slice the hooked functions do not include this header, so the hooks
 * vanish there. Include after render_admission.h. Decoder:
 * tools/agx_preempt_trace.py. */

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
#define ADMISSION_PREEMPT_TRACE_ON 1
#endif

#define ADMISSION_PREEMPT_TRACE_VERSION 1u
#define ADMISSION_PREEMPT_TRACE_CAPACITY 32u

/* DeferMask: why a DMA_PREEMPTED report did not happen at once. */
#define ADMISSION_PREEMPT_DEFER_WORKER 0x1u      /* render worker scheduled */
#define ADMISSION_PREEMPT_DEFER_PAGING 0x2u      /* paging worker / CPU packet */
#define ADMISSION_PREEMPT_DEFER_IDLE 0x4u        /* phase idle at TryNotify */
#define ADMISSION_PREEMPT_DEFER_CLAIMED 0x8u     /* another caller owns it */
#define ADMISSION_PREEMPT_DEFER_BOUNDARY 0x10u   /* running packet not done */
#define ADMISSION_PREEMPT_DEFER_FAULTED 0x20u    /* SchedulerFaulted */
#define ADMISSION_PREEMPT_DEFER_NO_CLAIM 0x40u   /* claim refused */
/* PreemptCommand's own notifyNow refusal (DecideFlags bit 0 clear). */
#define ADMISSION_PREEMPT_DEFER_CMD_PHASE 0x100u      /* phase != ReadyToNotify */
#define ADMISSION_PREEMPT_DEFER_CMD_DISPATCHED 0x200u /* DispatchedFence != 0 */
#define ADMISSION_PREEMPT_DEFER_CMD_PAGING 0x400u     /* CPU packet unreported */
/* AdmissionPreemptTraceDefer Why: a DEFER bit, or one of these. */
#define ADMISSION_PREEMPT_WHY_BUSY 0x10000u /* worker or paging: refined */
#define ADMISSION_PREEMPT_WHY_PHASE(phase) (0x20000u | (ULONG)(phase))

/* DecideFlags. */
#define ADMISSION_PREEMPT_DECIDE_NOTIFY_NOW 0x1u
#define ADMISSION_PREEMPT_DECIDE_PAGING_PENDING 0x2u
#define ADMISSION_PREEMPT_DECIDE_PAGING_DPC_PENDING 0x4u
#define ADMISSION_PREEMPT_DECIDE_PAGING_DPCS_ACTIVE 0x8u
#define ADMISSION_PREEMPT_DECIDE_WORKER_SCHEDULED 0x10u
#define ADMISSION_PREEMPT_DECIDE_ENTERED 0x80000000u /* BeginLocked ran */

/* SubmitFlags. */
#define ADMISSION_PREEMPT_SUBMIT_DEFERRED 0x1u   /* Phase 5a pending queue */
#define ADMISSION_PREEMPT_SUBMIT_IMMEDIATE 0x2u  /* envelope path, not deferred */

/* Retry sources (RetryCount bytes: 0 worker-finished, 1 DPC). */
#define ADMISSION_PREEMPT_RETRY_WORKER 0u
#define ADMISSION_PREEMPT_RETRY_DPC 1u

/* ClaimSource: who claimed the notification. */
#define ADMISSION_PREEMPT_CLAIM_COMMAND 1u   /* inside PreemptCommand */
#define ADMISSION_PREEMPT_CLAIM_PASSIVE 2u   /* another thread, PASSIVE */
#define ADMISSION_PREEMPT_CLAIM_DISPATCH 3u  /* another thread, >= DISPATCH */

/* All stamps are KeQueryPerformanceCounter ticks, 0 = not reached. */
typedef struct _ADMISSION_PREEMPT_EVENT {
  ULONGLONG EnterQpc;        /* DxgkDdiPreemptCommand entry */
  ULONGLONG ExitQpc;         /* DxgkDdiPreemptCommand return */
  ULONGLONG FirstDeferQpc;   /* first deferred TryNotify / notifyNow refusal */
  ULONGLONG LastDeferQpc;
  ULONGLONG WorkerRetryQpc;  /* first WorkerFinished retry */
  ULONGLONG DpcRetryQpc;     /* first SchedulerDpc retry */
  ULONGLONG ClaimQpc;        /* notification claimed */
  ULONGLONG NotifyQpc;       /* DxgkCbNotifyInterrupt(DMA_PREEMPTED) returned */
  ULONGLONG CommitQpc;       /* phase back to idle (commit) */
  ULONGLONG SubmitQpc;       /* first non-resubmission SubmitCommandVirtual */
  ULONGLONG AwaitEnterQpc;   /* that submit's AwaitWork entry */
  ULONGLONG AwaitWaitQpc;    /* its first wait (0 = never waited) */
  ULONGLONG AwaitExitQpc;
  ULONGLONG PickupQpc;       /* worker pickup of SubmitFence */
  ULONGLONG FirstPickupQpc;  /* first worker pickup of any fence */
  ULONGLONG ResubmitQpc;     /* first resubmission */
  ULONG PreemptionFence, CutoffFence, ActiveFence;
  ULONG DroppedFence;        /* queued render packet discarded */
  ULONG PendingDropped;      /* Phase 5a pending entries dropped */
  ULONG PendingHeadFence;    /* their head fence */
  ULONG CpuDropped;          /* CPU-queue packets dropped */
  ULONG DecidePhase;         /* scheduler phase at notifyNow */
  ULONG DecideDispatchedFence;
  ULONG DecideFlags;
  ULONG DeferMask, DeferCount;
  ULONG RetryCount;          /* worker-finished | DPC << 8 (saturating) */
  ULONG ClaimSource;         /* | IRQL << 8 */
  ULONG NotifyLastCompletedFence;
  ULONG SubmitFence, SubmitPid, SubmitFlags;
  ULONG SubmitCount;         /* submits (resubmissions too) since entry */
  ULONG AwaitFirstReason;    /* AwaitWork predicate of the first wait */
  ULONG AwaitResult;         /* its final predicate (0 = ready) */
  ULONG FirstPickupFence;
  ULONG ResubmitFence;
  ULONG SubmitOrdinal;       /* SubmitCount at SubmitFence */
} ADMISSION_PREEMPT_EVENT;

typedef struct _ADMISSION_PREEMPT_TRACE {
  ULONG Version, Bytes;
  ULONG Count; /* preemptions recorded since start (wraps the ring) */
  ULONG Next;  /* ring index the next preemption takes */
  ULONGLONG QpcFrequency;
  ADMISSION_PREEMPT_EVENT Events[ADMISSION_PREEMPT_TRACE_CAPACITY];
} ADMISSION_PREEMPT_TRACE;

C_ASSERT(sizeof(ADMISSION_PREEMPT_EVENT) == 16u * 8u + 24u * 4u);
C_ASSERT(sizeof(ADMISSION_PREEMPT_TRACE) ==
         24u + ADMISSION_PREEMPT_TRACE_CAPACITY * sizeof(ADMISSION_PREEMPT_EVENT));

#if defined(ADMISSION_PREEMPT_TRACE_ON)
VOID AdmissionPreemptTraceEnter(ADMISSION_CONTEXT *Context,
                                ULONG PreemptionFence);
VOID AdmissionPreemptTraceBeginLocked(ADMISSION_CONTEXT *Context,
    ULONG PreemptionFence, ULONG CutoffFence, ULONG ActiveFence,
    ULONG DroppedFence);
VOID AdmissionPreemptTraceDecideLocked(ADMISSION_CONTEXT *Context,
                                       BOOLEAN NotifyNow);
VOID AdmissionPreemptTraceExit(ADMISSION_CONTEXT *Context,
                               ULONG PreemptionFence);
VOID AdmissionPreemptTraceDefer(ADMISSION_CONTEXT *Context, ULONG Node,
                                ULONG Why);
VOID AdmissionPreemptTraceRetry(ADMISSION_CONTEXT *Context,
                                ULONG Source);
VOID AdmissionPreemptTraceClaim(ADMISSION_CONTEXT *Context, ULONG Node,
                                ULONG PreemptionFence);
VOID AdmissionPreemptTraceNotified(ADMISSION_CONTEXT *Context,
    ULONG Node, ULONG PreemptionFence, ULONG LastCompletedFence);
VOID AdmissionPreemptTraceCommitted(ADMISSION_CONTEXT *Context,
                                    ULONG Node, ULONG PreemptionFence);
VOID AdmissionPreemptTraceSubmit(ADMISSION_CONTEXT *Context,
    ULONG ProcessId, ULONG Fence, BOOLEAN Resubmission);
VOID AdmissionPreemptTraceSubmitPath(ADMISSION_CONTEXT *Context,
                                     ULONG Fence, BOOLEAN Deferred);
VOID AdmissionPreemptTraceAwaitEnter(ADMISSION_CONTEXT *Context);
VOID AdmissionPreemptTraceAwaitExit(ADMISSION_CONTEXT *Context,
    LONGLONG WaitStartQpc, ULONG FirstReason, ULONG Reason);
#endif
