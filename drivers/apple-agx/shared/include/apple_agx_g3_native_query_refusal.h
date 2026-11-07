#ifndef APPLE_AGX_G3_NATIVE_QUERY_REFUSAL_H
#define APPLE_AGX_G3_NATIVE_QUERY_REFUSAL_H
/* EXP2206 receipt-only: which predicate refused a native readiness query.
 *
 * EXP2206 (CS 1.6): 431 of DWM's 432 reject-native-allocation rows were
 * readiness escapes failing with STATUS_INVALID_PARAMETER (UMD hr
 * 0x80070057) for one 64 KiB written allocation in a new DWM device, each
 * followed by a refused batch and a DWM device re-creation about 184 ms
 * later. AdmissionGpuvaG3NativeQueryEscape and its batch wrapper share that
 * status between about 60 predicates; a well-formed not-ready answer is
 * returned as data with STATUS_SUCCESS and never reaches this receipt.
 *
 * Every refusing exit of the two escapes passes its own code (the numbers
 * below; 0 = none). The KMD notes the refusal and exports the receipt as
 * REG_BINARY Wom1G3NativeQueryRefusal. Statuses, outputs and lock order of
 * the escapes are unchanged. Codes 2-27 and 49-67 are noted without the G3
 * lock; 28-48 under it (the record then also carries the process/context
 * observations). Code 1 (no adapter) is counted only in the in-memory copy.
 *
 * Plain C only: this header is part of the WDK kernel build and of host
 * tests. */

#define APPLE_AGX_G3_NQ_REFUSAL_VERSION 1u
#define APPLE_AGX_G3_NQ_REFUSAL_CODES 128u

/* AdmissionGpuvaG3NativeQueryEscape, in source order. */
#define APPLE_AGX_G3_NQ_NO_ADAPTER 1u
#define APPLE_AGX_G3_NQ_NOT_STARTED 2u
#define APPLE_AGX_G3_NQ_NO_ARGS 3u
#define APPLE_AGX_G3_NQ_IRQL 4u
#define APPLE_AGX_G3_NQ_ESCAPE_FLAGS 5u
#define APPLE_AGX_G3_NQ_NO_DATA 6u
#define APPLE_AGX_G3_NQ_DATA_SIZE 7u
#define APPLE_AGX_G3_NQ_NO_ACQUIRE_CB 8u
#define APPLE_AGX_G3_NQ_NO_RELEASE_CB 9u
#define APPLE_AGX_G3_NQ_NO_STATE 10u
#define APPLE_AGX_G3_NQ_MAGIC 11u
#define APPLE_AGX_G3_NQ_VERSION 12u
#define APPLE_AGX_G3_NQ_BYTES 13u
#define APPLE_AGX_G3_NQ_NO_ALLOCATION 14u
#define APPLE_AGX_G3_NQ_NO_GPUVA 15u
#define APPLE_AGX_G3_NQ_GPUVA_ALIGN 16u
#define APPLE_AGX_G3_NQ_GPUVA_RANGE 17u
#define APPLE_AGX_G3_NQ_NO_BYTES 18u
#define APPLE_AGX_G3_NQ_BYTES_32BIT 19u
#define APPLE_AGX_G3_NQ_BYTES_RANGE 20u
#define APPLE_AGX_G3_NQ_WRITE 21u
#define APPLE_AGX_G3_NQ_READY_IN 22u
#define APPLE_AGX_G3_NQ_REASON_IN 23u
#define APPLE_AGX_G3_NQ_FAILED_VA_IN 24u
#define APPLE_AGX_G3_NQ_RESERVED2_IN 25u
#define APPLE_AGX_G3_NQ_NO_HANDLE_DATA 26u
#define APPLE_AGX_G3_NQ_NO_RELEASE_HANDLE 27u
#define APPLE_AGX_G3_NQ_NO_PROCESS 28u
#define APPLE_AGX_G3_NQ_PROCESS_POISONED 29u
#define APPLE_AGX_G3_NQ_PROCESS_CPU_ONLY 30u
#define APPLE_AGX_G3_NQ_GRAPH_NOT_CREATED 31u
#define APPLE_AGX_G3_NQ_GRAPH_UNCERTAIN 32u
#define APPLE_AGX_G3_NQ_NO_CONTEXT 33u
#define APPLE_AGX_G3_NQ_CONTEXT_NO_TRANSPORT 34u
#define APPLE_AGX_G3_NQ_CONTEXT_CLOSING 35u
#define APPLE_AGX_G3_NQ_CONTEXT_POISONED 36u
#define APPLE_AGX_G3_NQ_CONTEXT_NO_DEVICE 37u
#define APPLE_AGX_G3_NQ_DEVICE_HANDLE 38u
#define APPLE_AGX_G3_NQ_DEVICE_ADAPTER 39u
#define APPLE_AGX_G3_NQ_OPENED_MAGIC 40u
#define APPLE_AGX_G3_NQ_OPENED_DEVICE 41u
#define APPLE_AGX_G3_NQ_OPENED_RUNTIME 42u
#define APPLE_AGX_G3_NQ_OPENED_NO_OBJECT 43u
#define APPLE_AGX_G3_NQ_OBJECT_MAGIC 44u
/* 45 READ_ONLY retired (EXP2208): a write query on a read-only open is now
 * answered as data, APPLE_AGX_G3_NATIVE_QUERY_READ_ONLY. */
#define APPLE_AGX_G3_NQ_READ_ONLY 45u
#define APPLE_AGX_G3_NQ_DESCRIPTION_SHORT 46u
#define APPLE_AGX_G3_NQ_DESCRIPTION_32BIT 47u
#define APPLE_AGX_G3_NQ_DESCRIPTION_RANGE 48u
/* AdmissionGpuvaG3NativeQueryBatchEscape (an entry refused by the single
 * query keeps that query's code). */
#define APPLE_AGX_G3_NQ_BATCH_NO_ADAPTER 49u
#define APPLE_AGX_G3_NQ_BATCH_NOT_STARTED 50u
#define APPLE_AGX_G3_NQ_BATCH_NO_ARGS 51u
#define APPLE_AGX_G3_NQ_BATCH_IRQL 52u
#define APPLE_AGX_G3_NQ_BATCH_ESCAPE_FLAGS 53u
#define APPLE_AGX_G3_NQ_BATCH_NO_DATA 54u
#define APPLE_AGX_G3_NQ_BATCH_DATA_SIZE 55u
#define APPLE_AGX_G3_NQ_BATCH_MAGIC 56u
#define APPLE_AGX_G3_NQ_BATCH_VERSION 57u
#define APPLE_AGX_G3_NQ_BATCH_NO_COUNT 58u
#define APPLE_AGX_G3_NQ_BATCH_COUNT 59u
#define APPLE_AGX_G3_NQ_BATCH_BYTES 60u
#define APPLE_AGX_G3_NQ_BATCH_DATA_BYTES 61u
#define APPLE_AGX_G3_NQ_BATCH_READY_IN 62u
#define APPLE_AGX_G3_NQ_BATCH_FAILED_INDEX_IN 63u
#define APPLE_AGX_G3_NQ_BATCH_RESERVED0_IN 64u
#define APPLE_AGX_G3_NQ_BATCH_RESERVED1_IN 65u
#define APPLE_AGX_G3_NQ_BATCH_LEGACY_CACHED 66u
#define APPLE_AGX_G3_NQ_BATCH_PLACEMENT 67u
#define APPLE_AGX_G3_NQ_LAST_CODE 67u

/* Record flags. */
#define APPLE_AGX_G3_NQ_FLAG_LOCKED 0x1u        /* noted under the G3 lock */
#define APPLE_AGX_G3_NQ_FLAG_BATCH 0x2u         /* batch wrapper exit */
#define APPLE_AGX_G3_NQ_FLAG_QUERY 0x4u         /* Query* fields copied */
/* ProcessFlags (from the process FindProcess returned; Found clear when it
 * returned none). PidMatch: a G3 process of the caller's OS process exists
 * (noted for NO_PROCESS only), its handle in PidProcess. */
#define APPLE_AGX_G3_NQ_PROCESS_FOUND 0x1u
#define APPLE_AGX_G3_NQ_PROCESS_POISONED_BIT 0x2u
#define APPLE_AGX_G3_NQ_PROCESS_CPU_ONLY_BIT 0x4u
#define APPLE_AGX_G3_NQ_PROCESS_CREATED_BIT 0x8u
#define APPLE_AGX_G3_NQ_PROCESS_UNCERTAIN_BIT 0x10u
#define APPLE_AGX_G3_NQ_PROCESS_PID_MATCH 0x20u
/* ContextFlags (the context matching hContext in that process). Foreign:
 * hContext belongs to another G3 process (NO_CONTEXT only), whose OS
 * process id is ForeignOsProcessId. */
#define APPLE_AGX_G3_NQ_CONTEXT_FOUND 0x1u
#define APPLE_AGX_G3_NQ_CONTEXT_TRANSPORT_BIT 0x2u
#define APPLE_AGX_G3_NQ_CONTEXT_CLOSING_BIT 0x4u
#define APPLE_AGX_G3_NQ_CONTEXT_POISONED_BIT 0x8u
#define APPLE_AGX_G3_NQ_CONTEXT_DEVICE_BIT 0x10u
#define APPLE_AGX_G3_NQ_CONTEXT_DEVICE_MATCH 0x20u
#define APPLE_AGX_G3_NQ_CONTEXT_ADAPTER_MATCH 0x40u
#define APPLE_AGX_G3_NQ_CONTEXT_FOREIGN 0x80u
/* OpenedFlags (the allocation handle data of q.Allocation). */
#define APPLE_AGX_G3_NQ_OPENED_FOUND 0x1u
#define APPLE_AGX_G3_NQ_OPENED_MAGIC_BIT 0x2u
#define APPLE_AGX_G3_NQ_OPENED_DEVICE_MATCH 0x4u
#define APPLE_AGX_G3_NQ_OPENED_RUNTIME_MATCH 0x8u
#define APPLE_AGX_G3_NQ_OPENED_OBJECT_BIT 0x10u
#define APPLE_AGX_G3_NQ_OPENED_OBJECT_MAGIC_BIT 0x20u
#define APPLE_AGX_G3_NQ_OPENED_READ_ONLY_BIT 0x40u

typedef struct _APPLE_AGX_G3_NQ_REFUSAL_RECORD {
  unsigned int Code, Status, Flags, Sequence;
  unsigned int OsProcessId, ProcessFlags, ContextFlags, OpenedFlags;
  unsigned long long Qpc;
  /* DXGKARG_ESCAPE handles. */
  unsigned long long Device, Context, KmdProcess;
  /* The caller's query (Flags & QUERY). */
  unsigned long long QueryAllocation, QueryGpuVa, QueryAllocationBytes;
  unsigned int QueryVersion, QueryReserved, QueryWrite, QueryBytes;
  /* Observations: the context's device, the opened allocation's device and
   * runtime handle, its description size; process context count; the
   * process found by OS process id or the foreign owner of hContext. */
  unsigned long long ContextDevice, OpenedDevice, DescriptionSize;
  unsigned long long PidProcess;
  unsigned int OpenedRuntimeAllocation, ProcessContexts;
  unsigned int ForeignOsProcessId, Reserved;
} APPLE_AGX_G3_NQ_REFUSAL_RECORD;

typedef struct _APPLE_AGX_G3_NQ_REFUSAL_RECEIPT {
  unsigned int Version, Bytes, Total, Dropped;
  unsigned int Counts[APPLE_AGX_G3_NQ_REFUSAL_CODES];
  /* The first refusal since start and the newest one. */
  APPLE_AGX_G3_NQ_REFUSAL_RECORD First, Last;
} APPLE_AGX_G3_NQ_REFUSAL_RECEIPT;

/* Count one refusal; with a record (the caller owns the receipt), also keep
 * it as Last (and First when none yet). Without one (the receipt is busy),
 * count it in Dropped only. Out-of-range codes count in Counts[0]. */
static inline void AppleAgxG3NqRefusalNote(APPLE_AGX_G3_NQ_REFUSAL_RECEIPT *r,
    unsigned int code, const APPLE_AGX_G3_NQ_REFUSAL_RECORD *record) {
  if (!r) return;
  r->Version = APPLE_AGX_G3_NQ_REFUSAL_VERSION;
  r->Bytes = (unsigned int)sizeof(*r);
  if (!record) {
    if (r->Dropped != 0xffffffffu) ++r->Dropped;
    return;
  }
  if (r->Total != 0xffffffffu) ++r->Total;
  if (code > APPLE_AGX_G3_NQ_LAST_CODE) code = 0u;
  if (r->Counts[code] != 0xffffffffu) ++r->Counts[code];
  r->Last = *record;
  r->Last.Code = code;
  r->Last.Sequence = r->Total;
  if (!r->First.Code) r->First = r->Last;
}
#endif
