#include "render_admission.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* M2-f1 (receipt only): what Windows grants this KMD at adapter start for a
 * driver-populated memory segment (DXGK_SEGMENTFLAGS.PopulatedFromSystemMemory,
 * "system memory that the driver allocated during initialization").
 * MmAllocatePagesForMdlEx with MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS: SkipBytes 0
 * returns one contiguous block or nothing; a power-of-two SkipBytes returns
 * blocks of exactly that size aligned on it (Microsoft wdm documentation).
 * The pages are never mapped or exposed, and every block is freed again before
 * the memory runtime starts. */
C_ASSERT(sizeof(PFN_NUMBER) == sizeof(unsigned long long));

static VOID AdmissionPoolTry(ADMISSION_POOL_ATTEMPT *Attempt,
                             ULONGLONG SkipBytes, ULONGLONG TotalBytes) {
  PHYSICAL_ADDRESS low, high, skip;
  LARGE_INTEGER frequency, start, end;
  PMDL mdl;
  ULONGLONG bytes, elapsed;
  Attempt->SkipBytes = SkipBytes;
  Attempt->RequestedBytes = TotalBytes;
  low.QuadPart = 0;
  high.QuadPart = -1;
  skip.QuadPart = (LONGLONG)SkipBytes;
  start = KeQueryPerformanceCounter(&frequency);
  mdl = MmAllocatePagesForMdlEx(low, high, skip, (SIZE_T)TotalBytes,
      MmWriteCombined,
      MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_DONT_ZERO_ALLOCATION |
          (SkipBytes == 0ULL ? MM_ALLOCATE_FULLY_REQUIRED : 0u));
  end = KeQueryPerformanceCounter(NULL);
  elapsed = frequency.QuadPart > 0
      ? (ULONGLONG)(end.QuadPart - start.QuadPart) * 1000000ULL /
            (ULONGLONG)frequency.QuadPart
      : 0ULL;
  Attempt->ElapsedUs = elapsed > 0xffffffffULL ? 0xffffffffu : (ULONG)elapsed;
  if (mdl == NULL) {
    Attempt->Status = 2u;
    return;
  }
  bytes = MmGetMdlByteCount(mdl);
  AdmissionPoolAnalyzeFrames(
      (const unsigned long long *)MmGetMdlPfnArray(mdl), bytes / PAGE_SIZE,
      Attempt);
  Attempt->Status = bytes >= TotalBytes ? 1u : 3u;
  MmFreePagesFromMdl(mdl);
  ExFreePool(mdl);
}

_Use_decl_annotations_ VOID AdmissionProbeContiguousPool(
    ADMISSION_CONTEXT *Context) {
  static const ULONGLONG single[] = {0x40000000ULL, 0x20000000ULL,
                                     0x10000000ULL, 0x8000000ULL,
                                     0x4000000ULL};
  ADMISSION_POOL_PROBE probe;
  ULONG i, n = 0u;
  if (Context == NULL || KeGetCurrentIrql() != PASSIVE_LEVEL) return;
  RtlZeroMemory(&probe, sizeof(probe));
  probe.Version = ADMISSION_POOL_PROBE_VERSION;
  probe.Bytes = sizeof(probe);
  /* The largest single block first, then 2 MiB and 64 KiB blocks for 1 GiB. */
  for (i = 0u; i < RTL_NUMBER_OF(single); ++i) {
    AdmissionPoolTry(&probe.Attempts[n++], 0ULL, single[i]);
    if (probe.Attempts[n - 1u].Status == 1u) break;
  }
  AdmissionPoolTry(&probe.Attempts[n++], 0x200000ULL, 0x40000000ULL);
  AdmissionPoolTry(&probe.Attempts[n++], 0x10000ULL, 0x40000000ULL);
  probe.Count = n;
  AdmissionRecordM2PoolProbe(Context, &probe);
}
#endif
