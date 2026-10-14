#ifndef APPLE_AGX_RENDER_POOL_PROBE_H
#define APPLE_AGX_RENDER_POOL_PROBE_H

/* M2-f1 receipt: how much physically contiguous memory Windows grants the
 * KMD at adapter start (MmAllocatePagesForMdlEx with
 * MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS), and whether every block can back
 * native 16 KiB UAT pages. */
#define ADMISSION_POOL_PROBE_VERSION 2u
#define ADMISSION_POOL_PROBE_ATTEMPTS 8u
#define ADMISSION_POOL_UAT_PAGE 0x4000ULL

typedef struct _ADMISSION_POOL_ATTEMPT {
  unsigned long long SkipBytes;
  unsigned long long RequestedBytes;
  unsigned long long ObtainedBytes;
  unsigned long long LowestPhysical;
  unsigned long long HighestPhysical; /* exclusive end of the last page */
  unsigned long long LargestRunBytes;
  unsigned int Runs;       /* maximal physically contiguous runs */
  unsigned int Misaligned; /* runs not starting on a 16 KiB boundary */
  unsigned int ElapsedUs;
  unsigned int Status;     /* 0 not tried, 1 full, 2 none, 3 partial */
} ADMISSION_POOL_ATTEMPT;

/* M2-f2 pool (version 2): Attempts[0] one block, Attempts[1] 64 KiB blocks
 * (when the block failed), Attempts[2] the hypervisor window: SkipBytes =
 * window IPA, RequestedBytes = capacity, ObtainedBytes = mapped bytes,
 * Runs = MAP_APPEND commands, Misaligned = last AGX_POOL_WINDOW_RESULT,
 * ElapsedUs = mapping time, Status = ADMISSION_POOL_WINDOW_*. */
#define ADMISSION_POOL_ATTEMPT_BLOCK 0u
#define ADMISSION_POOL_ATTEMPT_CHUNKS 1u
#define ADMISSION_POOL_ATTEMPT_WINDOW 2u
#define ADMISSION_POOL_WINDOW_MAPPED 1u
#define ADMISSION_POOL_WINDOW_ABSENT 7u
#define ADMISSION_POOL_WINDOW_REFUSED 8u
#define ADMISSION_POOL_WINDOW_STALE 9u
/* M2-f3: the window maps ranges, segment 3 is backed on demand: no block or
 * chunk attempt runs, ObtainedBytes is 0 and RequestedBytes the segment. */
#define ADMISSION_POOL_WINDOW_DEMAND 10u

typedef struct _ADMISSION_POOL_PROBE {
  unsigned int Version, Bytes, Count, Reserved;
  ADMISSION_POOL_ATTEMPT Attempts[ADMISSION_POOL_PROBE_ATTEMPTS];
} ADMISSION_POOL_PROBE;

/* Summarizes a page-frame array (4 KiB frames, in MDL order) into Attempt's
 * physical fields; RequestedBytes and SkipBytes are kept. */
void AdmissionPoolAnalyzeFrames(const unsigned long long *Frames,
                                unsigned long long FrameCount,
                                ADMISSION_POOL_ATTEMPT *Attempt);

#endif
