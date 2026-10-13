#ifndef APPLE_AGX_RENDER_POOL_PROBE_H
#define APPLE_AGX_RENDER_POOL_PROBE_H

/* M2-f1 receipt: how much physically contiguous memory Windows grants the
 * KMD at adapter start (MmAllocatePagesForMdlEx with
 * MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS), and whether every block can back
 * native 16 KiB UAT pages. */
#define ADMISSION_POOL_PROBE_VERSION 3u
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
/* M2-f2c-3 (version 3): Attempts[3] is the sizing policy: RequestedBytes =
 * the pool target, ObtainedBytes = the RAM Windows manages, SkipBytes = the
 * registry override in MiB (0 = none). */
#define ADMISSION_POOL_ATTEMPT_POLICY 3u
#define ADMISSION_POOL_WINDOW_MAPPED 1u
#define ADMISSION_POOL_WINDOW_ABSENT 7u
#define ADMISSION_POOL_WINDOW_REFUSED 8u
#define ADMISSION_POOL_WINDOW_STALE 9u

typedef struct _ADMISSION_POOL_PROBE {
  unsigned int Version, Bytes, Count, Reserved;
  ADMISSION_POOL_ATTEMPT Attempts[ADMISSION_POOL_PROBE_ATTEMPTS];
} ADMISSION_POOL_PROBE;

/* M2-f2c-3: the KMD pool (segment 3) is a quarter of the RAM Windows manages,
 * in 256 MiB steps, at least 512 MiB and at most the hypervisor window and
 * 1.5 GiB: every published pool page is one m1n1 broker system record, and
 * the broker holds 2 GiB of them (HV_AGX_GPUVA_V5_BACKINGS) for the pool and
 * VidMm's system pages together. A nonzero override (MiB, from the registry)
 * replaces the quarter, in 64 MiB steps, within the same bounds. 0 when the
 * window cannot hold the minimum. */
#define ADMISSION_POOL_MIN_BYTES 0x20000000ULL
#define ADMISSION_POOL_MAX_BYTES 0x60000000ULL
#define ADMISSION_POOL_STEP 0x10000000ULL
#define ADMISSION_POOL_OVERRIDE_STEP 0x4000000ULL
unsigned long long AdmissionPoolPolicyBytes(unsigned long long RamBytes,
                                            unsigned long long WindowBytes,
                                            unsigned int OverrideMegabytes);

/* Summarizes a page-frame array (4 KiB frames, in MDL order) into Attempt's
 * physical fields; RequestedBytes and SkipBytes are kept. */
void AdmissionPoolAnalyzeFrames(const unsigned long long *Frames,
                                unsigned long long FrameCount,
                                ADMISSION_POOL_ATTEMPT *Attempt);

#endif
