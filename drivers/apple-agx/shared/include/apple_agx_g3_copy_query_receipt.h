#ifndef APPLE_AGX_G3_COPY_QUERY_RECEIPT_H
#define APPLE_AGX_G3_COPY_QUERY_RECEIPT_H

/* Fixed little-endian registry ABI. No pointers to transient storage. */
#define APPLE_AGX_G3_COPY_QUERY_RECEIPT_BYTES 168u
enum {
  AppleAgxG3QueryLocked = 1u,
  AppleAgxG3QueryProcess = 2u,
  AppleAgxG3QueryContext = 4u,
  AppleAgxG3QueryRequest = 8u,
  AppleAgxG3QueryRange = 16u,
  AppleAgxG3QueryRootIsBootstrap = 32u,
  AppleAgxG3QueryResidentGroupAvailable = 64u,
  AppleAgxG3QueryCanonicalAllocationAvailable = 128u
};
enum {
  AppleAgxG3WalkNone = 0u,
  AppleAgxG3WalkNoRoot = 1u,
  AppleAgxG3WalkNoTable = 2u,
  AppleAgxG3WalkLeafAbsent = 3u,
  AppleAgxG3WalkLeafNotPublished = 4u,
  AppleAgxG3WalkTailShort = 5u,
  AppleAgxG3WalkLogicalShadowAbsent = 6u
};
typedef struct {
  unsigned int Level, Index, Reason, ComponentReason;
  unsigned long long Va;
} APPLE_AGX_GPUVA_G3_WALK_FAILURE;
typedef struct {
  unsigned int Version, Bytes, Predicate, Status;
  unsigned int Flags, Write, MissingLevel, MissingIndex;
  unsigned int MissingReason, ComponentReason;
  unsigned int ProcessSetRootCount, ContextSetRootCount;
  unsigned long long GraphRootIpa, BootstrapIpa;
  unsigned long long ProcessLastSetRootIpa, ContextLastSetRootIpa;
  unsigned long long QueryVa, QueryBytes, MissingVa;
  unsigned long long ProcessGeneration, MappingGeneration, ContextRootIpa;
  unsigned long long ProcessId, ContextToken;
  /* v3 extension. Numeric identities only; never dereferenced by consumers.
   * Canonical fields require validated local allocation ownership under lock.
   * ResidentGroupAvailable distinguishes lookup failure from zero valid PTEs. */
  unsigned long long RequestAllocationHandle, CanonicalAllocationIdentity;
  unsigned long long CanonicalAllocationBytes;
} APPLE_AGX_G3_COPY_QUERY_RECEIPT;
typedef char APPLE_AGX_G3_COPY_QUERY_RECEIPT_SIZE_CHECK[
    sizeof(APPLE_AGX_G3_COPY_QUERY_RECEIPT) ==
        APPLE_AGX_G3_COPY_QUERY_RECEIPT_BYTES ? 1 : -1];
#endif
