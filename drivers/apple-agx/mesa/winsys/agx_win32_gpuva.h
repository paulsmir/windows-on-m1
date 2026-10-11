#ifndef AGX_WIN32_GPUVA_H
#define AGX_WIN32_GPUVA_H

#include <stdint.h>
#include "apple_agx_g3_private_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AGX_GPUVA_MAP_WRITE 1u
#define AGX_GPUVA_MAP_EXECUTE 2u
/* EXP1203: submissions that may be in flight at once (the newest included).
 * Must equal ADMISSION_UMD_IN_FLIGHT_SETS (umd_internal.h): the UMD lets a
 * slot join a new submission while fewer than this many sets hold it. */
#define AGX_GPUVA_IN_FLIGHT 4u

/* Host-testable boundary around WDDM callbacks. Return 0 on failure, 1 for
 * completed work, 2 for pending work. Map and MakeResident may return 3 after uncertain
 * accepted mapping; Submit may return 2 after an accepted command whose
 * completion signal failed. All calls are serialized. */
typedef struct {
  int (*Reserve)(void *, uint64_t, uint64_t, uint64_t, uint64_t *);
  int (*Map)(void *, uint64_t, uint64_t, uint64_t, unsigned, uint64_t *);
  int (*Free)(void *, uint64_t, uint64_t);
  int (*MakeResident)(void *, const uint64_t *, unsigned, uint64_t *);
  int (*WaitPaging)(void *, uint64_t);
  int (*Submit)(void *, const uint64_t *, unsigned, uint64_t, uint32_t,
                const void *, uint32_t, uint64_t *);
  int (*WaitRender)(void *, uint64_t);
  int (*Evict)(void *, const uint64_t *, unsigned);
  int (*PrivateEscape)(void *, APPLE_AGX_G3_PRIVATE_REQUEST *);
  /* EXP1082 (optional): non-blocking, 1 once the render fence is signalled. */
  int (*QueryRender)(void *, uint64_t);
} AGX_WIN32_GPUVA_OPS;

typedef struct {
  uint64_t Allocation; /* UMD token resolved to a VidMm handle by the adapter */
  uint64_t Va;
  uint64_t Bytes;
  unsigned Bound;
} AGX_WIN32_GPUVA_BO;

/* EXP1093: a submission still in flight behind the newest one. */
typedef struct {
  uint64_t *Handles;
  unsigned Count;
  uint64_t Fence;
} AGX_WIN32_GPUVA_SET;

typedef struct {
  AGX_WIN32_GPUVA_OPS Ops;
  void *Context;
  /* The newest submission's residency set and fence. */
  uint64_t *Held;
  uint64_t RenderFence;
  unsigned HeldCount;
  /* EXP1093: older submissions still in flight behind the newest one,
   * oldest first; they retire in this (GPU) order. EXP1203: up to
   * AGX_GPUVA_IN_FLIGHT submissions in flight (was two): CS 1.6 submits
   * three per frame, so with two every frame waited for its own previous
   * submission. A submission needs a free older slot. */
  AGX_WIN32_GPUVA_SET Older[AGX_GPUVA_IN_FLIGHT - 1u];
  unsigned OlderCount;
  unsigned Terminal;
  unsigned LastFailure; /* diagnostic: source line of the last refused call */
  unsigned LastDetail;  /* diagnostic: MakeResident/Submit callback result */
} AGX_WIN32_GPUVA_SPACE;

int AgxWin32GpuvaInit(AGX_WIN32_GPUVA_SPACE *, const AGX_WIN32_GPUVA_OPS *, void *);
int AgxWin32GpuvaBind(AGX_WIN32_GPUVA_SPACE *, AGX_WIN32_GPUVA_BO *,
                      uint64_t Allocation, uint64_t Bytes, int LowVa,
                      unsigned Protection);
int AgxWin32GpuvaUnbind(AGX_WIN32_GPUVA_SPACE *, AGX_WIN32_GPUVA_BO *);
int AgxWin32GpuvaSubmit(AGX_WIN32_GPUVA_SPACE *,
                        const AGX_WIN32_GPUVA_BO *const *, unsigned Count,
                        const AGX_WIN32_GPUVA_BO *Command, uint32_t CommandBytes,
                        const AGX_WIN32_GPUVA_BO *const *Written,
                        unsigned WrittenCount,
                        const void *PrivateData, uint32_t PrivateBytes,
                        uint64_t *CompletionFence);
int AgxWin32GpuvaRetire(AGX_WIN32_GPUVA_SPACE *, uint64_t CompletionFence);
int AgxWin32GpuvaComplete(AGX_WIN32_GPUVA_SPACE *, uint64_t CompletionFence);
/* EXP1093: whether an in-flight submission names this allocation, and the
 * fence of the newest one that does (0: none). */
uint64_t AgxWin32GpuvaHoldingFence(const AGX_WIN32_GPUVA_SPACE *, uint64_t Allocation);
/* Whether CompletionFence is an in-flight (held) submission's fence. */
int AgxWin32GpuvaFenceHeld(const AGX_WIN32_GPUVA_SPACE *, uint64_t CompletionFence);

#ifdef __cplusplus
}
#endif

#endif
