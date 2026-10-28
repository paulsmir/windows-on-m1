#ifndef APPLE_AGX_G3_COPY_ABI_H
#define APPLE_AGX_G3_COPY_ABI_H
#include "apple_agx_win32_abi.h"

/* Buffered CPU staging transfer, never a user pointer or physical address. */
#define APPLE_AGX_G3_COPY_MAGIC 0x43565041u
#define APPLE_AGX_G3_COPY_VERSION 1u
#define APPLE_AGX_G3_COPY_CAPACITY 65536u
#define APPLE_AGX_G3_COPY_QUERY 0u
#define APPLE_AGX_G3_COPY_UPLOAD 1u
#define APPLE_AGX_G3_COPY_DOWNLOAD 2u
/* RC68: Reserved carries APPLE_AGX_G3_COPY_FLAG_UNREFERENCED in and the
 * job-gate outcome out (apple_agx_g3_copy_gate.h); bits outside
 * APPLE_AGX_G3_COPY_RESERVED_MASK and Reserved2 must be zero. */
typedef struct _APPLE_AGX_G3_COPY_REQUEST {
  APPLE_AGX_U32 Magic, Version, Bytes, Operation;
  APPLE_AGX_U32 Allocation, Reserved;
  APPLE_AGX_U64 Offset, GpuVa, MappingGeneration, ProcessGeneration;
  APPLE_AGX_U32 TransferBytes, Reserved2;
  unsigned char Data[APPLE_AGX_G3_COPY_CAPACITY];
} APPLE_AGX_G3_COPY_REQUEST;

/* EXP1933 containment: read-only native coverage of one held allocation.
 * Runtime escape handles authenticate process/device/context and Allocation.
 * AllocationBytes is the logical extent on input, full KMD description extent
 * on output. Ready/Reason/FailedVa are output; no GPU command or paging result.
 * Separate magic and fixed 64-byte layout avoid the 64-KiB copy payload. */
#define APPLE_AGX_G3_NATIVE_QUERY_MAGIC 0x4e565041u
#define APPLE_AGX_G3_NATIVE_QUERY_VERSION 1u
/* RC25: v2 requires the whole allocation in the native pool. Old KMDs
 * reject this version rather than silently returning a weaker v1 proof. */
#define APPLE_AGX_G3_NATIVE_QUERY_POOL_VERSION 2u
/* RC47: same 64-byte wire size. Reserved2 is a KMD-issued proof token,
 * input/output; zero requests a full proof. Reserved may be POOL_ONLY.
 * Older versions still require Reserved2=0 and always walk. */
#define APPLE_AGX_G3_NATIVE_QUERY_CACHED_VERSION 3u
#define APPLE_AGX_G3_NATIVE_QUERY_POOL_ONLY 1u
#define APPLE_AGX_G3_NATIVE_QUERY_NOT_POOL 0x102u
#define APPLE_AGX_G3_NATIVE_QUERY_IDENTITY 0x100u
#define APPLE_AGX_G3_NATIVE_QUERY_TRANSLATION 0x101u
/* rc107: read-only placement census of the logical (VidMm) mapping, never a
 * readiness proof: the batch refuses this version. Reserved and Reserved2
 * must be zero in. Ready=1 iff every 4-KiB page is a valid page of this
 * allocation in the native pool segment. Otherwise Reason is IDENTITY (a
 * valid page of another owner/offset), NOT_POOL (a valid page outside the
 * pool, e.g. VidMm's segment-0 system backing) or NOT_RESIDENT (only
 * invalid pages besides pool pages), FailedVa its first page. Reserved2
 * out = pool pages | (valid non-pool pages << 32). */
#define APPLE_AGX_G3_NATIVE_QUERY_PLACEMENT_VERSION 4u
#define APPLE_AGX_G3_NATIVE_QUERY_NOT_RESIDENT 0x103u
typedef struct _APPLE_AGX_G3_NATIVE_QUERY {
  APPLE_AGX_U32 Magic, Version, Bytes, Allocation;
  APPLE_AGX_U64 GpuVa, AllocationBytes;
  APPLE_AGX_U32 Write, Ready, Reason, Reserved; /* v2: POOL_ONLY input */
  APPLE_AGX_U64 FailedVa, Reserved2;
} APPLE_AGX_G3_NATIVE_QUERY;

/* RC45: one buffered escape for the entire held allocation set. Exactly
 * Count ordinary queries follow this header (no pointers, handles widened
 * to native pointers, or partial array). v1 retains the v1/v2 proof above;
 * v2 also accepts cached v3 entries and returns hit/full-proof counts in
 * Reserved[0]/Reserved[1]. Both versions require zero input counters.
 * Ready is set only after every entry succeeds; FailedIndex selects the
 * first not-ready entry. All output fields must be zero on input. */
#define APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAGIC 0x42565041u
#define APPLE_AGX_G3_NATIVE_QUERY_BATCH_VERSION 1u
#define APPLE_AGX_G3_NATIVE_QUERY_BATCH_CACHED_VERSION 2u
#define APPLE_AGX_G3_NATIVE_QUERY_BATCH_MAX 4096u
typedef struct _APPLE_AGX_G3_NATIVE_QUERY_BATCH {
  APPLE_AGX_U32 Magic, Version, Bytes, Count;
  APPLE_AGX_U32 Ready, FailedIndex, Reserved[2];
} APPLE_AGX_G3_NATIVE_QUERY_BATCH;

/* Diagnostic only; not part of the buffered request or its version. */
typedef struct _APPLE_AGX_G3_COPY_TRANSFER_FAILURE {
  APPLE_AGX_U32 Version, Bytes, Operation, Predicate;
  APPLE_AGX_U32 Status, TransferBytes, Pid, Reserved;
  APPLE_AGX_U64 GpuVa, Offset, FailedPage, Allocation, Context;
  APPLE_AGX_U64 RequestProcessGeneration, RequestMappingGeneration;
  APPLE_AGX_U64 CurrentProcessGeneration, CurrentMappingGeneration;
} APPLE_AGX_G3_COPY_TRANSFER_FAILURE;
#endif
