#ifndef AGX_WIN32_NATIVE_POOL_BRIDGE_H
#define AGX_WIN32_NATIVE_POOL_BRIDGE_H

#include "agx_win32_reloc_capture.h"

/* This is the small Windows-facing projection of the result of
 * agx_pool_alloc_aligned_with_bo().  The native emitter supplies the exact
 * BO identity and the allocation's CPU/GPU-relative slice.  The bridge never
 * serializes either address: RelocReference records only Token and Offset. */
typedef struct _AGX_WIN32_NATIVE_POOL_SLICE {
  APPLE_AGX_U64 Owner;
  APPLE_AGX_U64 Token;
  APPLE_AGX_U64 Serial;
  APPLE_AGX_U32 Generation;
  const void *CpuBase;
  APPLE_AGX_U64 GpuBase;
  APPLE_AGX_U64 BoBytes;
  const void *SliceCpu;
  APPLE_AGX_U64 SliceGpu;
  APPLE_AGX_U64 SliceBytes;
} AGX_WIN32_NATIVE_POOL_SLICE;

/* Requires caller serialization with the native pool.  A BO retain is a
 * source/lifetime hold only; it is not a VidMm residency or mapping fence. */
AGX_WIN32_RELOC_RESULT AgxWin32NativePoolReference(
    AGX_WIN32_RELOC_CAPTURE *Capture,
    const AGX_WIN32_NATIVE_POOL_SLICE *Slice, APPLE_AGX_U32 Role,
    APPLE_AGX_U32 Access, APPLE_AGX_U32 *Index);

#endif /* AGX_WIN32_NATIVE_POOL_BRIDGE_H */
