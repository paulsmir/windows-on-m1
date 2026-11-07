#ifndef APPLE_AGX_RENDER_LIVE_PAGING_WAIT_H
#define APPLE_AGX_RENDER_LIVE_PAGING_WAIT_H

/* Diagnostic ABI v1. Never used to authorize a mapping or completion. */
enum {
  AdmissionLiveWaitEligible = 0u,
  AdmissionLiveWaitNoProcess = 1u,
  AdmissionLiveWaitDisabled = 2u,
  AdmissionLiveWaitPoisoned = 3u,
  AdmissionLiveWaitUncertain = 4u,
  AdmissionLiveWaitCpuOnly = 5u,
  AdmissionLiveWaitNotifyEviction = 6u,
  AdmissionLiveWaitInputShape = 7u,
  AdmissionLiveWaitPendingShadow = 8u,
  AdmissionLiveWaitTableLevel = 9u,
  AdmissionLiveWaitLocalView = 10u,
  AdmissionLiveWaitNativeStaging = 11u,
  AdmissionLiveWaitGeneration = 12u,
  AdmissionLiveWaitTableRange = 13u,
  AdmissionLiveWaitLeafVa = 14u,
  AdmissionLiveWaitRootChange = 15u,
  AdmissionLiveWaitParentPte = 16u,
  AdmissionLiveWaitChildResolve = 17u,
  AdmissionLiveWaitChildSelf = 18u,
  AdmissionLiveWaitPartialGroup = 19u,
  AdmissionLiveWaitLeafRange = 20u,
  AdmissionLiveWaitLogicalOccupied = 21u,
  AdmissionLiveWaitResidentOccupied = 22u,
  AdmissionLiveWaitInvalidPte = 23u,
  AdmissionLiveWaitPteFlags = 24u,
  AdmissionLiveWaitPteAddress = 25u,
  AdmissionLiveWaitSegment = 26u,
  AdmissionLiveWaitBackingRange = 27u,
  AdmissionLiveWaitBackingOverflow = 28u,
  AdmissionLiveWaitPage64Alignment = 29u,
  AdmissionLiveWaitNativeAlignment = 30u,
  AdmissionLiveWaitNativeOccupied = 31u,
  AdmissionLiveWaitNativeSpan = 32u,
  AdmissionLiveWaitNoncontiguous = 33u,
  AdmissionLiveWaitMixedSegment = 34u,
  AdmissionLiveWaitMixedPermission = 35u,
  AdmissionLiveWaitCpuMutation = 36u,
  AdmissionLiveWaitReasonCount = 37u
};
enum { AdmissionLiveWaitIdle = 1u, AdmissionLiveWaitAdmitted = 2u,
       AdmissionLiveWaitInvalid = 3u, AdmissionLiveWaitTimeout = 4u };
#define ADMISSION_LIVE_WAIT_ENTRIES 16u
/* QWORD-only wire layout. Operation is DXGK_OPERATION_*; worker physical
 * operations use 0x100 + APPLE_AGX_PHYSICAL_PAGING_KIND. AddressKind: 1 VA,
 * 2 physical-plan offsets. Index is the failing input PTE, ~0ULL if none.
 * Reason is the FIRST refusal; LastReason is the final preflight result.
 * JobState: bit0 in-flight, bit1 lease, bit2 nested. Exit is orthogonal to
 * refusal (in particular timeout is not a publication-eligibility guard). */
typedef struct _ADMISSION_G3_LIVE_WAIT_ENTRY {
  unsigned long long Sequence, Qpc, QpcFrequency, EndQpc, Iterations, Exit;
  unsigned long long Reason, LastReason, Operation, ProcessId, ActiveProcessId;
  unsigned long long ActiveFence, CompletedFence, JobState;
  unsigned long long Va, RangeBytes, SourceVa, TableIpa, Level, StartIndex;
  unsigned long long EntryCount, UpdateFlags, Index, PteFlags, PteAddress;
  unsigned long long Allocation, AllocationOffset, AddressKind;
} ADMISSION_G3_LIVE_WAIT_ENTRY;
typedef struct _ADMISSION_G3_LIVE_WAIT_RECEIPT {
  unsigned long long Version, Bytes, Total, Overwritten;
  unsigned long long Histogram[AdmissionLiveWaitReasonCount];
  ADMISSION_G3_LIVE_WAIT_ENTRY Entries[ADMISSION_LIVE_WAIT_ENTRIES];
} ADMISSION_G3_LIVE_WAIT_RECEIPT;
#endif
