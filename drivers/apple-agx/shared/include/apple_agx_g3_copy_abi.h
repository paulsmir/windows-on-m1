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
#define APPLE_AGX_G3_NATIVE_QUERY_IDENTITY 0x100u
#define APPLE_AGX_G3_NATIVE_QUERY_TRANSLATION 0x101u
typedef struct _APPLE_AGX_G3_NATIVE_QUERY {
  APPLE_AGX_U32 Magic, Version, Bytes, Allocation;
  APPLE_AGX_U64 GpuVa, AllocationBytes;
  APPLE_AGX_U32 Write, Ready, Reason, Reserved;
  APPLE_AGX_U64 FailedVa, Reserved2;
} APPLE_AGX_G3_NATIVE_QUERY;

/* Diagnostic only; not part of the buffered request or its version. */
typedef struct _APPLE_AGX_G3_COPY_TRANSFER_FAILURE {
  APPLE_AGX_U32 Version, Bytes, Operation, Predicate;
  APPLE_AGX_U32 Status, TransferBytes, Pid, Reserved;
  APPLE_AGX_U64 GpuVa, Offset, FailedPage, Allocation, Context;
  APPLE_AGX_U64 RequestProcessGeneration, RequestMappingGeneration;
  APPLE_AGX_U64 CurrentProcessGeneration, CurrentMappingGeneration;
} APPLE_AGX_G3_COPY_TRANSFER_FAILURE;
#endif
