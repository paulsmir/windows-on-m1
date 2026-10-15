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

/* Wom1GpuReset: the last engine or adapter reset and the state it found. */
#define ADMISSION_GPU_RESET_RECEIPT_VERSION 1u
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
} ADMISSION_GPU_RESET_QUIESCE;

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
} ADMISSION_GPU_RESET_RECEIPT;

#endif /* APPLE_AGX_RENDER_GPU_FAULT_H */
