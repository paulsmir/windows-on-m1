#ifndef AGX_WIN32_RELOC_CAPTURE_H
#define AGX_WIN32_RELOC_CAPTURE_H
#include "agx_win32_transport.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Local UMD/BO identity only. No physical address or persistent GPUVA. */
typedef struct {
  APPLE_AGX_U64 Owner, Token, Serial, Bytes;
  APPLE_AGX_U32 Generation, AllocationIndex, Access;
} AGX_WIN32_RELOC_ALLOCATION;
typedef struct {
  int (*Query)(void *, APPLE_AGX_U64, AGX_WIN32_RELOC_ALLOCATION *);
  int (*Retain)(void *, APPLE_AGX_U64, APPLE_AGX_U64);
  void (*Release)(void *, APPLE_AGX_U64, APPLE_AGX_U64);
  /* Owner-side atomic acquire for a previously observed exact identity. */
  int (*RetainExact)(void *, const AGX_WIN32_RELOC_ALLOCATION *);
} AGX_WIN32_RELOC_OPERATIONS;
typedef enum {
  AgxRelocOk, AgxRelocArgument, AgxRelocState, AgxRelocStale,
  AgxRelocRange, AgxRelocCapacity, AgxRelocOverlap, AgxRelocCallback,
  AgxRelocCommand
} AGX_WIN32_RELOC_RESULT;
typedef struct {
  APPLE_AGX_U64 Owner, Request, LastRequest;
  APPLE_AGX_U32 Generation, State, Fence, ReferenceCount, RelocationCount;
  APPLE_AGX_U16 CommandVersion;
  APPLE_AGX_U16 MaxReferences, MaxRelocations;
  void *Context;
  AGX_WIN32_RELOC_OPERATIONS Operations;
  AGX_WIN32_RELOC_ALLOCATION Allocations[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE References[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  APPLE_AGX_WIN32_RELOCATION Relocations[APPLE_AGX_WIN32_COMMAND_STORAGE_MAX_RELOCATIONS];
} AGX_WIN32_RELOC_CAPTURE;

/* Zero-initialized caller storage; serialized non-reentrant device-context use.
 * Retain/Release preserve BO identity, NOT residency or physical placement. */
AGX_WIN32_RELOC_RESULT AgxWin32RelocBegin(AGX_WIN32_RELOC_CAPTURE *,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U64 Request,
    const AGX_WIN32_RELOC_OPERATIONS *, void *Context);
AGX_WIN32_RELOC_RESULT AgxWin32RelocBeginVersion(AGX_WIN32_RELOC_CAPTURE *,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U64 Request,
    APPLE_AGX_U16 CommandVersion, const AGX_WIN32_RELOC_OPERATIONS *,
    void *Context);
AGX_WIN32_RELOC_RESULT AgxWin32RelocPromoteIndexed(
    AGX_WIN32_RELOC_CAPTURE *);
AGX_WIN32_RELOC_RESULT AgxWin32RelocReference(AGX_WIN32_RELOC_CAPTURE *,
    APPLE_AGX_U64 Token, APPLE_AGX_U32 Role, APPLE_AGX_U32 Access,
    APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes, APPLE_AGX_U32 *Index);
AGX_WIN32_RELOC_RESULT AgxWin32RelocReferenceExpected(
    AGX_WIN32_RELOC_CAPTURE *, const AGX_WIN32_RELOC_ALLOCATION *Expected,
    APPLE_AGX_U32 Role, APPLE_AGX_U32 Access, APPLE_AGX_U64 Offset,
    APPLE_AGX_U64 Bytes, APPLE_AGX_U32 *Index);
AGX_WIN32_RELOC_RESULT AgxWin32RelocField(AGX_WIN32_RELOC_CAPTURE *,
    APPLE_AGX_U32 Kind, APPLE_AGX_U32 Destination, APPLE_AGX_U64 DestinationOffset,
    APPLE_AGX_U32 Target, APPLE_AGX_U64 TargetOffset);
AGX_WIN32_RELOC_RESULT AgxWin32RelocSeal(AGX_WIN32_RELOC_CAPTURE *,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *, void *Command,
    APPLE_AGX_U32 Capacity, APPLE_AGX_U32 *Bytes);
/* Legacy seal emits v1. Native stage paths choose v2 explicitly. */
AGX_WIN32_RELOC_RESULT AgxWin32RelocSealVersion(
    AGX_WIN32_RELOC_CAPTURE *, APPLE_AGX_U16 CommandVersion,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *, void *Command,
    APPLE_AGX_U32 Capacity, APPLE_AGX_U32 *Bytes);
/* Seal typed source ownership for a single Windows request without producing
 * a second command buffer. The composer owns the only BuildDraw operation. */
AGX_WIN32_RELOC_RESULT AgxWin32RelocPrepareDraw(
    AGX_WIN32_RELOC_CAPTURE *, APPLE_AGX_U16 CommandVersion,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *, AGX_WIN32_DRAW_REQUEST *);
AGX_WIN32_RELOC_RESULT AgxWin32RelocSubmitted(AGX_WIN32_RELOC_CAPTURE *, APPLE_AGX_U32 Fence);
AGX_WIN32_RELOC_RESULT AgxWin32RelocRetire(AGX_WIN32_RELOC_CAPTURE *,
    APPLE_AGX_U64 Owner, APPLE_AGX_U32 Generation, APPLE_AGX_U64 Request, APPLE_AGX_U32 Fence);
AGX_WIN32_RELOC_RESULT AgxWin32RelocAbort(AGX_WIN32_RELOC_CAPTURE *);
#ifdef __cplusplus
}
#endif
#endif
