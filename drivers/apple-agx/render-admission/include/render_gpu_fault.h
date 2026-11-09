#ifndef APPLE_AGX_RENDER_GPU_FAULT_H
#define APPLE_AGX_RENDER_GPU_FAULT_H

/* G4-r3 (EXP1296): native records of the firmware's Timeout/Fault events and
 * of the KMD's engine/adapter resets, for offline correlation. Decoder:
 * tools/decode_wom1_gpu_fault.py. */

/* Wom1GpuFault: captured in AdmissionTransportRecover, before the firmware is
 * resumed (Asahi gpu.rs handle_timeout/handle_fault: fault info, then
 * recover()). */
#define ADMISSION_GPU_FAULT_RECEIPT_VERSION 1u
/* FaultInfo when the SGX FAULT_INFO register (0x17030) was not read: the read
 * is opt-in (device value Wom1GpuFaultInfoRead = 1) because it aborted under
 * earlier mappings (EXP592, EXP671). Same sentinel as the queue snapshot. */
#define ADMISSION_GPU_FAULT_INFO_NOT_READ 0xacce5515abad1deaULL
#define ADMISSION_GPU_FAULT_RESUMED 1u     /* halted seen, resume written */
#define ADMISSION_GPU_FAULT_INFO_READ 2u   /* FaultInfo holds the register */
#define ADMISSION_GPU_FAULT_INFO_VALID 4u  /* FAULT_INFO bit 0 (Asahi regs.rs) */
#define ADMISSION_GPU_FAULT_JOB_KNOWN 8u   /* the event's fence owned the slot */

typedef struct _ADMISSION_GPU_FAULT_EVENT {
  unsigned int Sequence;        /* 1-based event number of this boot */
  unsigned int EventKind;       /* firmware event: 0 Fault, 4 Timeout */
  unsigned int Fence;           /* WDDM fence of the pending job */
  unsigned int ProcessId;       /* submitting process; 0 unknown */
  unsigned int SubmitToEventMs; /* job kick to event */
  unsigned int HaltCount;       /* firmware status halt_count (+0x10) */
  unsigned int Flags;           /* ADMISSION_GPU_FAULT_* */
  unsigned int Reserved;
  unsigned long long ContextToken; /* ADMISSION_RENDER_CONTEXT of the job */
  unsigned long long FaultInfo;    /* raw FAULT_INFO or ..._NOT_READ */
} ADMISSION_GPU_FAULT_EVENT;

typedef struct _ADMISSION_GPU_FAULT_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Events;      /* Timeout/Fault events seen */
  unsigned int Resumed;     /* events whose job the firmware dropped */
  unsigned int NotHalted;   /* events without a halt within 100 ms */
  unsigned int ValidFaults; /* events with a valid FAULT_INFO */
  ADMISSION_GPU_FAULT_EVENT First;      /* first event of the boot */
  ADMISSION_GPU_FAULT_EVENT FirstValid; /* first with a valid FAULT_INFO */
  ADMISSION_GPU_FAULT_EVENT Last;
} ADMISSION_GPU_FAULT_RECEIPT;

void AdmissionGpuFaultReceiptInitialize(ADMISSION_GPU_FAULT_RECEIPT *Receipt);
/* Numbers Event and counts it; it becomes First (first event), FirstValid
 * (first with a valid FAULT_INFO) and Last. */
void AdmissionGpuFaultReceiptRecord(ADMISSION_GPU_FAULT_RECEIPT *Receipt,
    ADMISSION_GPU_FAULT_EVENT *Event);

/* Wom1GpuFaultMapping v2: first valid fault, before firmware recovery.
 * CPU metadata only; NativePresent is the acknowledged software graph, not
 * a hardware page-table read. Status preserves unknown versus absent. */
typedef enum _ADMISSION_GPU_FAULT_MAPPING_STATUS {
  AdmissionGpuFaultMappingUnavailable = 0,
  AdmissionGpuFaultMappingBusy,
  AdmissionGpuFaultMappingIdentity,
  AdmissionGpuFaultMappingVm,
  AdmissionGpuFaultMappingUncertain,
  AdmissionGpuFaultMappingNoTable,
  AdmissionGpuFaultMappingNoShadow,
  AdmissionGpuFaultMappingCaptured
} ADMISSION_GPU_FAULT_MAPPING_STATUS;

typedef struct _ADMISSION_GPU_FAULT_MAPPING_PTE {
  unsigned long long GuestIpa, Allocation, AllocationOffset;
  unsigned int Segment, Flags;
} ADMISSION_GPU_FAULT_MAPPING_PTE;

/* EXP1935: freeze the existing paging ring before recovery overwrites it.
 * Historical observations only: a zero Count is unknown, not never-mapped.
 * Match bits: 1 VA overlap, 2 current table/index overlap (whole native group).
 * Flags/Status retain the leaf producer's values, including failed updates.
 * Allocation is an opaque historical token and must never be dereferenced. */
#define ADMISSION_GPU_FAULT_HISTORY_COUNT 8u
typedef struct _ADMISSION_GPU_FAULT_HISTORY {
  unsigned long long Qpc, ProcessId, TableIpa, Allocation, FirstVa, MappingGeneration;
  unsigned int First, Count, ValidCount, Flags, Status, FirstSegment;
  unsigned int Sequence, Match;
} ADMISSION_GPU_FAULT_HISTORY;

typedef struct _ADMISSION_GPU_FAULT_MAPPING {
  unsigned int Version, Bytes, Fence, Status;
  unsigned int OsProcessId, VmSlot, GraphUncertain, CpuOnlyMappings;
  unsigned int JobInFlight, NativePresent, NativeWritable, NativeKind;
  unsigned long long ContextToken, FaultInfo, FaultVa, GraphProcessId;
  unsigned long long RootIpa, ProcessGeneration, MappingGeneration, LeaseToken;
  unsigned long long NativeIpa, TableIpa;
  ADMISSION_GPU_FAULT_MAPPING_PTE Ptes[4];
  /* v2 follows the entire 256-byte v1 prefix. Newest matching records first.
   * HistoryScanned is bounded by the existing ring; HistoryMatches can exceed
   * HistoryCount. Even a complete ring is not a complete mapping lifetime. */
  unsigned int HistoryNext, HistoryScanned, HistoryMatches, HistoryCount;
  ADMISSION_GPU_FAULT_HISTORY History[ADMISSION_GPU_FAULT_HISTORY_COUNT];
} ADMISSION_GPU_FAULT_MAPPING;

/* Wom1GpuReset: the last engine or adapter reset and the state it found;
 * version 2 appends timing and the last ADMISSION_GPU_RESET_HISTORY resets
 * (EXP1303: LONGJOB jobs completed seconds late with four resets, and one
 * receipt could not tell a worker that stopped polling from a job still
 * running on the firmware). Version 5 appends the failure class of the last
 * reset and the firmware restart record. */
#define ADMISSION_GPU_RESET_RECEIPT_VERSION 5u
#define ADMISSION_GPU_RESET_HISTORY 8u
#define ADMISSION_GPU_RESET_ENGINE 1u   /* DxgkDdiResetEngine */
#define ADMISSION_GPU_RESET_ADAPTER 2u  /* DxgkDdiResetFromTimeout */
typedef enum _ADMISSION_GPU_RESET_QUIESCE {
  AdmissionGpuResetQuiesceNone = 0, /* no render job was active */
  AdmissionGpuResetQuiesceEnded,    /* the firmware ended the job */
  AdmissionGpuResetQuiesceTimeout,  /* still owned by the firmware */
  AdmissionGpuResetQuiesceWorker,   /* the worker did not leave */
  AdmissionGpuResetQuiescePoll,     /* provider or RTKit drain failed */
  AdmissionGpuResetQuiesceBackend,  /* backend left Submitted, job still active */
  AdmissionGpuResetQuiesceState,    /* runtime absent, stopping or resetting */
  /* rc102 (EXP2039): the active job never reached the firmware (its backend
   * submit was refused: backend Ready, queues idle); the reset aborts it. */
  AdmissionGpuResetQuiesceUnsubmitted,
} ADMISSION_GPU_RESET_QUIESCE;

/* T10: the last operation checked by this reset. On failure this identifies
 * the refusing gate; Quiesce further describes the active-job wait. */
typedef enum _ADMISSION_GPU_RESET_STAGE {
  AdmissionGpuResetStageNone = 0,
  AdmissionGpuResetStageValidate,
  AdmissionGpuResetStageQuiesce,
  AdmissionGpuResetStagePrivate,
  AdmissionGpuResetStagePaging,
  AdmissionGpuResetStagePacketActive,
  AdmissionGpuResetStagePacketReset,
  AdmissionGpuResetStageBackendRelease,
  AdmissionGpuResetStageScheduler,
  AdmissionGpuResetStageComplete,
} ADMISSION_GPU_RESET_STAGE;

typedef struct _ADMISSION_GPU_RESET_ENTRY {
  unsigned int EntryMs;          /* interrupt time at entry, ms */
  unsigned int Entry, Status, Quiesce, WaitMs;
  unsigned int PacketState, PacketFence, LastAbortedFence;
  unsigned int SchedulerFaulted;
  unsigned int JobAgeMs;         /* active job's kick to entry; 0 none */
  unsigned int PollAgeMs;        /* worker's last poll to entry */
  unsigned int FirmwareRecoveries;
} ADMISSION_GPU_RESET_ENTRY;

/* v4: read-only samples after the reset owns the idle worker, before/after
 * its bounded poll. QueueMask bits 0/1/2 are TA/3D/compute. ReadMask bits
 * 0/1/2 mean Stamp/Done/Write was read successfully; zero is NOT a value.
 * Flags bits 0/1 retain EventSeen/Complete. These are sequential observations
 * of firmware-shared words, not an atomic GPU snapshot or proof of quiescence. */
typedef struct _ADMISSION_GPU_RECOVERY_QUEUE {
  unsigned int EventNumber, Flags, ReadMask;
  unsigned int Stamp, ExpectedStamp, Done, Write, ExpectedDone, Capacity;
} ADMISSION_GPU_RECOVERY_QUEUE;

typedef struct _ADMISSION_GPU_RECOVERY_SNAPSHOT {
  unsigned int Captured, Fence, Phase, RecoveryPending, QueueMask;
  ADMISSION_GPU_RECOVERY_QUEUE Queues[3];
} ADMISSION_GPU_RECOVERY_SNAPSHOT;

/* rc77 (EXP2005/EXP2007): Wom1GpuFaultPostResume, REG_BINARY, written at the
 * end of every reset quiescence wait: what the firmware did after it was
 * resumed from a Timeout/Fault, and the optional KMD RecoverChannel
 * (Wom1GpuFaultRecoverChannel=1). Sequential CPU reads of shared memory, not
 * an atomic GPU state. rc92 (EXP2027): no SGX register is read here (the
 * GPU may be power-gated after the resume; the FAULT_INFO load wedged the
 * guest), so SgxRead, Sgx17620 and Sgx17630 are 0 and FaultInfo is
 * ADMISSION_GPU_FAULT_INFO_NOT_READ. FAULT_INFO is in Wom1GpuFault (read at
 * the event, before resume), the fault-control registers in
 * Wom1SoftFaultState. */
#define ADMISSION_GPU_POST_RESUME_VERSION 4u
/* v2 (rc92): PollFailure (was Reserved, always 0 in v1) names the poll that
 * ended the wait with the Poll outcome: bits 24-31 source (1 RTKit drain,
 * 2 provider poll), bits 0-7 the RTKit session result or the provider's
 * LastPollGuard, bits 8-15 LastDrainGuard, bits 16-23 LastIngestGuard
 * (each saturated at 0xff); 0 = the wait did not end in a failed poll. */
#define ADMISSION_GPU_POST_RESUME_POLL_RTKIT 1u
#define ADMISSION_GPU_POST_RESUME_POLL_PROVIDER 2u
#define ADMISSION_GPU_POST_RESUME_FORCE_DISABLED 0u
#define ADMISSION_GPU_POST_RESUME_FORCE_NOT_REACHED 1u
#define ADMISSION_GPU_POST_RESUME_FORCE_SENT 2u
#define ADMISSION_GPU_POST_RESUME_FORCE_REFUSED 3u
/* Q13: CPU-owned RTKit/session and queue identity at reset. Captured=0 is
 * unknown (v1/v2). LastRx is the last received message, NOT necessarily the
 * cause of a transport error. No crashlog pointer is dereferenced. Kinds use
 * render_job_window.h: 0 absent, 1 render, 2 render+prelude, 3 compute. */
typedef struct _ADMISSION_GPU_RTKIT_SNAPSHOT {
  unsigned int Captured, Running, CpuReady, CrashlogReplySent;
  unsigned int Crashed, ReceivedCount, LastEndpoint, MailboxValid;
  unsigned int Inbox, Outbox, CrashlogCapacity, CrashlogRequested;
  unsigned int HeadFence, NextFence, HeadKind, NextKind;
  unsigned long long LastPayload, CrashlogGpuAddress;
} ADMISSION_GPU_RTKIT_SNAPSHOT;

typedef struct _ADMISSION_GPU_POST_RESUME_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Fence, WaitMs;
  unsigned int RegionCFaultControl;     /* RegionC word 0x10fd8 as published */
  unsigned int SgxRead, Sgx17620, Sgx17630;
  unsigned long long FaultInfo;         /* NOT_READ since rc92 (was SGX 0x17030) */
  unsigned int FirmwareStatus[16];      /* first 64 bytes: halt_count +0x10,
                                           halted +0x20, resume +0x30 */
  unsigned int PendingStampsActive;     /* RegionC pending_stamps with bit 0 */
  unsigned int RegionCFaultInfo[6];     /* RegionC fault_info (Asahi FaultInfo) */
  unsigned int ComputeRptrRead, ComputeRptr, ComputeDone, ComputeWrite;
  unsigned int PostEvents, PostEventKindMask, LastPostEventKind;
  unsigned int ForceState, ForceResult, ForceMask, ForceAtMs;
  unsigned int PollFailure;             /* v2, ADMISSION_GPU_POST_RESUME_POLL_* */
  ADMISSION_GPU_RTKIT_SNAPSHOT Rtkit;    /* v3, 80-byte CPU-only suffix */
  /* Q15 v4: head TA/3D/compute, then successor TA/3D/compute. Zero
   * denotes an unused queue; kind/fence in Rtkit supplies validity.
   * Expected values from CPU-owned publications, NOT observed retirement. */
  unsigned int ExpectedStamps[6], EventSlots[6];
} ADMISSION_GPU_POST_RESUME_RECEIPT;

/* rc82: Wom1SoftFaultState, REG_BINARY, written once when the GPU runtime has
 * started: whether the KMD reports soft faults to the UMD
 * (AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS).  Active = Requested (device value
 * Wom1SoftFaults = 1) and the RegionC fault_control word, read back from the
 * published RegionC, is 0xb (Asahi file.rs:333-335).  The SGX fault-control
 * registers the firmware programs from that word (Asahi fw/initdata.rs:
 * 1154-1155: bit 0 -> 0x17620, bit 1 -> 0x17630) are read only with
 * Wom1GpuFaultControlRead = 1 (SgxRead = 1); otherwise they are 0. */
#define ADMISSION_GPU_SOFT_FAULTS_VERSION 1u
#define ADMISSION_GPU_SOFT_FAULTS_FAULT_CONTROL 0xbu
typedef struct _ADMISSION_GPU_SOFT_FAULTS_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Requested;            /* Wom1SoftFaults = 1 */
  unsigned int RegionCRead;          /* RegionC mapped and word in range */
  unsigned int RegionCFaultControl;  /* RegionC word 0x10fd8 as published */
  unsigned int Active;               /* reported to the UMD */
  unsigned int SgxRead, Sgx17620, Sgx17630;
  unsigned int Reserved;
} ADMISSION_GPU_SOFT_FAULTS_RECEIPT;

/* v5 (TDR-FIRMWARE-RESTART-RECOVERY.md section 6, stage 1): why the last
 * reset's quiescence failed. NONE: it did not fail (or the reset never owned
 * the runtime). F2: the RTKit crash latch is set. F1: the firmware halted for
 * the job or a Timeout/Fault recovery is still pending (halted, resumed,
 * never retires). F3: neither; nothing separates it from a long job but
 * time. F2 wins over F1: a crashed firmware answers no resume. */
#define ADMISSION_GPU_RESET_CLASS_NONE 0u
#define ADMISSION_GPU_RESET_CLASS_F1 1u
#define ADMISSION_GPU_RESET_CLASS_F2 2u
#define ADMISSION_GPU_RESET_CLASS_F3 3u
/* Halted is the firmware status word (+0x20), 0xffffffff when unmapped. */
unsigned int AdmissionGpuResetFailureClass(unsigned int Quiesce,
    unsigned int RtkitCrashed, unsigned int Halted,
    unsigned int RecoveryPending);

/* v5: the warm firmware restart (stage 2, AdmissionPlatformRuntimeRestart).
 * Until stage 3 its only caller is the drill escape below. Stage is the
 * step the last attempt was in when it ended (COMPLETE on success); a
 * refusal before the runtime was owned counts in Refused, sets
 * RefusedReason and leaves the last attempt's fields alone. */
typedef enum _ADMISSION_FW_RESTART_STAGE {
  AdmissionFwRestartStageNone = 0,
  AdmissionFwRestartStageOwned,           /* Resetting held, worker idle */
  AdmissionFwRestartStageStopWarm,        /* AppleAgxBackendRuntimeStopWarm */
  AdmissionFwRestartStageStopProof,       /* RUN reads 0, ISR closed, no context-0 lease */
  AdmissionFwRestartStageProviderDestroy,
  AdmissionFwRestartStageQueueLifetime,   /* job sequence back to 0 */
  AdmissionFwRestartStageGraphRebuild,    /* initdata graph destroyed and prepared */
  AdmissionFwRestartStageProviderInitialize,
  AdmissionFwRestartStageIsrReopen,
  AdmissionFwRestartStageStartWarm,       /* AppleAgxBackendRuntimeStartWarm */
  AdmissionFwRestartStageLifetimeFresh,   /* AppleAgxG13QueueRuntimeLifetimeIsFresh */
  AdmissionFwRestartStageComplete,
} ADMISSION_FW_RESTART_STAGE;
#define ADMISSION_FW_RESTART_STOP_NONE 0u
#define ADMISSION_FW_RESTART_STOP_ORDERLY 1u /* AP 0x10, IOP 0x10, RUN off */
#define ADMISSION_FW_RESTART_STOP_FORCED 2u  /* RUN off without the acks */
#define ADMISSION_FW_RESTART_FLAG_CPU_CONTROL_READ 0x001u /* CpuControlAfterStop valid */
#define ADMISSION_FW_RESTART_FLAG_ISR_CLOSED 0x002u       /* after the ASC stop */
#define ADMISSION_FW_RESTART_FLAG_ISR_REOPENED 0x004u     /* before the CPU start */
#define ADMISSION_FW_RESTART_FLAG_WARM_PUBLISH 0x008u     /* roots republished without ACTIVATE */
#define ADMISSION_FW_RESTART_FLAG_LIFETIME_FRESH 0x010u
#define ADMISSION_FW_RESTART_FLAG_FORCED_AFTER_BOOT 0x020u /* failed boot left RUN on */
#define ADMISSION_FW_RESTART_FLAG_STATUS_BEFORE 0x040u    /* FirmwareStatusBefore valid */
#define ADMISSION_FW_RESTART_FLAG_STATUS_AFTER 0x080u     /* FirmwareStatusAfter valid */
#define ADMISSION_FW_RESTART_FLAG_LINE_LISTED 0x100u      /* ACPI lists the mailbox line */
#define ADMISSION_FW_RESTART_FLAG_INBOX_READ 0x200u       /* InboxControlAfterStop valid */
#define ADMISSION_FW_RESTART_FLAG_OUTBOX_READ 0x400u      /* OutboxControlAfterStop valid */
#define ADMISSION_FW_RESTART_REFUSED_NOT_ALLOWED 1u /* Wom1FwRestartDrillAllowed != 1 */
#define ADMISSION_FW_RESTART_REFUSED_PAYLOAD 2u
#define ADMISSION_FW_RESTART_REFUSED_STATE 3u   /* no started runtime, stopping, IRQL */
#define ADMISSION_FW_RESTART_REFUSED_FAULTED 4u /* SchedulerFaulted already set */
#define ADMISSION_FW_RESTART_REFUSED_BUSY 5u    /* a job, packet or reset in flight */
#define ADMISSION_FW_RESTART_REFUSED_WORKER 6u  /* the worker did not go idle */
typedef struct _ADMISSION_GPU_FW_RESTART_RECEIPT {
  unsigned int Restarts, Succeeded, Refused, RefusedReason;
  unsigned int Stage;             /* ADMISSION_FW_RESTART_STAGE */
  unsigned int Status;            /* NTSTATUS of the last attempt */
  unsigned int StopKind;          /* ADMISSION_FW_RESTART_STOP_* */
  unsigned int StopMs, RebuildMs, BootMs, TotalMs;
  unsigned int BackendStopResult, BackendStartResult;
  /* The firmware state machine and the firmware provider after the attempt. */
  unsigned int FirmwarePhase, FirmwareCompletedMask, FirmwareCleanupMask;
  unsigned int FirmwareLastResult, ProviderState;
  unsigned int CpuControlAfterStop; /* raw ASC CPU_CONTROL, RUN = bit 4 */
  unsigned int Flags;               /* ADMISSION_FW_RESTART_FLAG_* */
  /* Bit n: retained-root command n (AGX_RR_*) went through the KMD's own
   * command path during the attempt. PREPARE (1), ACTIVATE (2) and CLOSE
   * (6) have no other sender, so their bits must stay clear. */
  unsigned int RetainedCommandMask;
  unsigned int RetainedFlags;       /* AGX_RR_FLAG_* at the warm publish */
  /* RTKit session at entry: Inbox*Valid | Outbox*Valid << 1 (these survive
   * a restart and can pair with a later failed read) and the crash latch. */
  unsigned int RtkitMailboxValid, RtkitCrashed;
  /* Raw ASC mailbox control words read once after the proven stop, with the
   * ISR closed (FLAG_MAILBOX_READ bit per word). Recorded, not judged:
   * whether a stopped ASC keeps messages in its outbox is not known. */
  unsigned int InboxControlAfterStop, OutboxControlAfterStop;
  unsigned long long EpochBefore; /* retained-root epoch held at entry */
  unsigned long long EpochAfter;  /* epoch the broker returned at the warm publish */
  /* First 64 bytes of the firmware status block (halt_count +0x10, halted
   * +0x20, resume +0x30) before the stop and after the boot. No expected
   * value: H1 records them (design 9.3). */
  unsigned int FirmwareStatusBefore[16], FirmwareStatusAfter[16];
} ADMISSION_GPU_FW_RESTART_RECEIPT;

typedef struct _ADMISSION_GPU_RESET_RECEIPT {
  unsigned int Version, Bytes;
  unsigned int Resets, Succeeded;
  /* The last attempt. */
  unsigned int Entry;            /* ADMISSION_GPU_RESET_ENGINE/ADAPTER */
  unsigned int Status;           /* NTSTATUS returned */
  unsigned int SchedulerFaulted; /* at entry: file tag << 16 | line */
  unsigned int PacketState;      /* render packet state at entry */
  unsigned int PacketFence;
  unsigned int ActiveFence, CompletedFence, LastSubmittedFence;
  unsigned int PreemptionPhase;
  unsigned int LastAbortedFence;
  unsigned int Quiesce;          /* ADMISSION_GPU_RESET_QUIESCE */
  unsigned int WaitMs;
  unsigned int BackendPhase, ProviderPhase, LastPollGuard; /* at entry */
  unsigned int WorkerBusy;       /* WorkScheduled | WorkersActive << 1 */
  unsigned int Halted;           /* firmware status halted (+0x20) */
  unsigned int FirmwareRecoveries;
  unsigned long long ContextToken;
  /* Version 2. */
  unsigned int EntryMs, JobAgeMs, PollAgeMs;
  unsigned int HistoryCount;     /* valid entries; the last is at
                                    (Resets - 1) % ADMISSION_GPU_RESET_HISTORY */
  ADMISSION_GPU_RESET_ENTRY History[ADMISSION_GPU_RESET_HISTORY];
  /* Version 3, appended after the complete v2 prefix (496 bytes). The stage
   * ring uses the same index as History; old entries are never relabeled. */
  unsigned int Stage;
  unsigned int HistoryStage[ADMISSION_GPU_RESET_HISTORY];
  unsigned int ReservedV3;
  /* v4 follows the complete 536-byte v3 prefix. Latest reset only; samples
   * are cleared even when this attempt never acquires the runtime. */
  ADMISSION_GPU_RECOVERY_SNAPSHOT RetirementBefore, RetirementAfter;
  /* v5 follows the complete 792-byte v4 prefix. The first six words belong
   * to the last reset (cleared at its entry); MailboxValid bit 0/1 says
   * whether Inbox/OutboxControl hold the RTKit session's failure snapshot.
   * Restart is the firmware restart's own record and is never cleared by a
   * reset. */
  unsigned int FailureClass;     /* ADMISSION_GPU_RESET_CLASS_* */
  unsigned int HaltCount;        /* firmware status halt_count (+0x10) */
  unsigned int RtkitCrashed;
  unsigned int MailboxValid, InboxControl, OutboxControl;
  ADMISSION_GPU_FW_RESTART_RECEIPT Restart;
} ADMISSION_GPU_RESET_RECEIPT;

/* TDR drill (DxgkDdiEscape payload): the KMD strands the next Jobs render
 * jobs of the calling process in the worker, as a worker that left its poll
 * loop would (the EXP1296 state): the job stays with the firmware, dispatch
 * freezes, VidSch's TDR runs the reset's quiescence wait. Refused unless the
 * device value Wom1TdrDrillAllowed = 1. Tool: windows/icd/agx_tdr_drill.c. */
#define ADMISSION_TDR_DRILL_MAGIC 0x44524454u /* "TDRD" */
#define ADMISSION_TDR_DRILL_VERSION 1u
#define ADMISSION_TDR_DRILL_MAX_JOBS 4u
typedef struct _ADMISSION_TDR_DRILL {
  unsigned int Magic, Version, Bytes, Jobs;
} ADMISSION_TDR_DRILL;

/* Explicit one-shot diagnostic: a CDM command-fetch fault in the caller's
 * process, not an unbounded shader. Operation 1 arms, 0 queries. GpuVa is
 * chosen/proven by the KMD, never supplied by the caller. */
#define ADMISSION_GPU_FAULT_DRILL_MAGIC 0x52444647u
typedef struct _ADMISSION_GPU_FAULT_DRILL {
  unsigned int Magic, Version, Bytes, Operation, Armed, Fence;
  unsigned long long GpuVa, FaultInfo;
} ADMISSION_GPU_FAULT_DRILL;

#endif /* APPLE_AGX_RENDER_GPU_FAULT_H */
