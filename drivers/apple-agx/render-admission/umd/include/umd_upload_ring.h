#ifndef APPLE_AGX_UMD_UPLOAD_RING_H
#define APPLE_AGX_UMD_UPLOAD_RING_H

#include "umd_staging_ring.h"

/* RC65 (EXP1985): persistent CPU-written D3D12 heaps that native staging
 * cannot hold (over the 16 MiB slice limit, or refused for space) kept the
 * write-watch path: one UPLOAD copy escape per dirty run of up to 64 KiB,
 * and each escape waits in the KMD until the process has no job in flight
 * (gpuva_g3_windows.c, "node 0 is serialized"). GravityMark's 3 x 32 MiB and
 * 3 x 128 MiB UPLOAD heaps paid 44-48 such escapes per frame.
 *
 * At Execute start the dirty runs are copied into a transient slice of one
 * native-staged ring BO and GPU copies from that slice into the canonical BO
 * are recorded before the replay. The GPU runs submissions in order
 * (agx_win32_gpuva_batch.c, EXP1195), so the copy runs after every earlier
 * reader of the canonical BO (no WAR) and before every later one. The ring
 * metadata is the RC52 FIFO ring: a slice is reused only after the fence of
 * the submission that read it has completed.
 *
 * Plain C, no CRT, header-only: C and the x86 /TP C++ build. */
#define ADMISSION_UMD_UPLOAD_RING_BYTES (8u * 1024u * 1024u)
/* One snapshot never needs more pages than the ring holds; a full page list
 * means the dirty set may be larger and is left to the escape path. */
#define ADMISSION_UMD_UPLOAD_RING_PAGES (ADMISSION_UMD_UPLOAD_RING_BYTES / 4096u + 1u)
/* A clean gap up to this size is copied with its neighbours: its staging
 * bytes equal the canonical bytes, and one GPU copy is cheaper than two. */
#define ADMISSION_UMD_UPLOAD_RING_GAP (64u * 1024u)
/* GPU copies recorded per Execute (one compute dispatch each). */
#define ADMISSION_UMD_UPLOAD_RING_COPIES 128u
#define ADMISSION_UMD_UPLOAD_RING_ALIGN 256u
/* Fallback reasons (receipt bit mask). */
#define ADMISSION_UMD_UPLOAD_FALLBACK_SPACE 1u   /* ring bytes or slice records */
#define ADMISSION_UMD_UPLOAD_FALLBACK_COPIES 2u  /* runs over the copy capacity */
#define ADMISSION_UMD_UPLOAD_FALLBACK_LARGE 4u   /* dirty set over the ring */
#define ADMISSION_UMD_UPLOAD_FALLBACK_PENDING 8u /* unsubmitted work recorded */

typedef struct _ADMISSION_UMD_UPLOAD_COPY {
  unsigned long long Offset; /* destination offset in the target BO */
  unsigned int RingOffset;   /* source offset in the ring BO */
  unsigned int Bytes;
  unsigned int Target;       /* the caller's target index */
} ADMISSION_UMD_UPLOAD_COPY;

typedef struct _ADMISSION_UMD_UPLOAD_RING {
  ADMISSION_UMD_STAGING_RING Ring;
  unsigned char *Cpu;   /* persistent map of the native ring BO */
  void *Scratch;        /* page list of one snapshot (process heap) */
  unsigned int Disabled;
  /* Per-Execute receipt counters, reset by the owner. */
  unsigned int Slots, Copies, Fallbacks, FallbackReasons, Invalidated;
  unsigned long long Bytes, FallbackBytes;
} ADMISSION_UMD_UPLOAD_RING;

/* Pages: ascending, unique offsets of the dirty pages (multiples of the
 * power-of-two Granularity, below SlotBytes). Runs receive the copies with
 * 16-byte aligned ring offsets relative to the slice; *SliceBytes their
 * span. Returns the run count, 0 for an empty set, or ~0u for invalid input
 * or more than Capacity runs (never a truncated plan). */
static inline unsigned int AdmissionUmdUploadPlan(const unsigned long long *Pages,
    unsigned int Count, unsigned int Granularity, unsigned long long SlotBytes,
    unsigned int Gap, unsigned int Target, ADMISSION_UMD_UPLOAD_COPY *Runs,
    unsigned int Capacity, unsigned long long *SliceBytes) {
  unsigned int i, n = 0u;
  unsigned long long end = 0ULL, cursor = 0ULL;
  if (!SliceBytes) return ~0u;
  *SliceBytes = 0ULL;
  if (!Count) return 0u;
  if (!Pages || !Runs || !Granularity || (Granularity & (Granularity - 1u)) ||
      !SlotBytes || SlotBytes > 0xffffffffULL) return ~0u;
  for (i = 0u; i < Count; ++i) {
    const unsigned long long at = Pages[i];
    unsigned long long stop;
    if (at >= SlotBytes || (at & (Granularity - 1u)) || (i && at <= Pages[i - 1u])) return ~0u;
    stop = SlotBytes - at < Granularity ? SlotBytes : at + Granularity;
    if (n && at - end <= Gap) {
      Runs[n - 1u].Bytes = (unsigned int)(stop - Runs[n - 1u].Offset);
    } else {
      if (n == Capacity) return ~0u;
      Runs[n].Offset = at;
      Runs[n].Bytes = (unsigned int)(stop - at);
      Runs[n].RingOffset = 0u;
      Runs[n].Target = Target;
      ++n;
    }
    end = stop;
  }
  for (i = 0u; i < n; ++i) {
    Runs[i].RingOffset = (unsigned int)cursor;
    cursor = (cursor + Runs[i].Bytes + 15ULL) & ~15ULL;
    if (cursor > 0xffffffffULL) return ~0u;
  }
  *SliceBytes = cursor;
  return n;
}

#endif
