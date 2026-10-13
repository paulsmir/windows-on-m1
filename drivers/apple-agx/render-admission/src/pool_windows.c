#include "render_admission.h"

#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)
/* M2-f2: segment 3 is a CPU-visible memory segment populated from Windows
 * memory this driver allocates at start (DXGK_SEGMENTFLAGS.
 * PopulatedFromSystemMemory). VidMm-managed system memory cannot back
 * 16 KiB UAT pages (EXP1244); one physically contiguous block can (EXP1246:
 * 1 GiB in 123.5 ms). MmAllocatePagesForMdlEx with SkipBytes 0 and
 * MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS returns one contiguous block or
 * nothing; the pages are zeroed (they reach user mode through CPU-visible
 * allocations) and write-combined, the cache type VidMm maps CPU views of
 * this segment with. Without the block the adapter runs on two segments. */
#define ADMISSION_MEMORY_POOL_BYTES 0x40000000ULL
#define ADMISSION_MEMORY_POOL_GPU_VA 0x1700000000ULL
#define ADMISSION_MEMORY_POOL_ALIGNMENT 0x10000ULL

C_ASSERT(sizeof(PFN_NUMBER) == sizeof(unsigned long long));

static VOID AdmissionMemoryPoolRelease(ADMISSION_CONTEXT *Context) {
  if (Context->PoolCpu != NULL && Context->PoolMdl != NULL)
    MmUnmapLockedPages(Context->PoolCpu, Context->PoolMdl);
  if (Context->PoolMdl != NULL) {
    MmFreePagesFromMdl(Context->PoolMdl);
    ExFreePool(Context->PoolMdl);
  }
  Context->PoolMdl = NULL;
  Context->PoolCpu = NULL;
  Context->PoolIpa = 0ULL;
  Context->PoolBytes = 0ULL;
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolStart(ADMISSION_CONTEXT *Context) {
  ADMISSION_POOL_PROBE receipt;
  ADMISSION_POOL_ATTEMPT *attempt = &receipt.Attempts[0];
  PHYSICAL_ADDRESS low, high, skip;
  LARGE_INTEGER frequency, start, end;
  PMDL mdl;
  ULONGLONG bytes;
  if (Context == NULL || Context->PoolMdl != NULL ||
      KeGetCurrentIrql() != PASSIVE_LEVEL ||
      !AdmissionMemoryReady(&Context->Memory))
    return;
  RtlZeroMemory(&receipt, sizeof(receipt));
  receipt.Version = ADMISSION_POOL_PROBE_VERSION;
  receipt.Bytes = sizeof(receipt);
  receipt.Count = 1u;
  attempt->RequestedBytes = ADMISSION_MEMORY_POOL_BYTES;
  low.QuadPart = 0;
  high.QuadPart = (LONGLONG)(ADMISSION_HVC_PHYSICAL_LIMIT - 1ULL);
  skip.QuadPart = 0;
  start = KeQueryPerformanceCounter(&frequency);
  mdl = MmAllocatePagesForMdlEx(low, high, skip,
      (SIZE_T)ADMISSION_MEMORY_POOL_BYTES, MmWriteCombined,
      MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS | MM_ALLOCATE_FULLY_REQUIRED);
  end = KeQueryPerformanceCounter(NULL);
  if (frequency.QuadPart > 0) {
    ULONGLONG us = (ULONGLONG)(end.QuadPart - start.QuadPart) * 1000000ULL /
                   (ULONGLONG)frequency.QuadPart;
    attempt->ElapsedUs = us > 0xffffffffULL ? 0xffffffffu : (ULONG)us;
  }
  if (mdl == NULL) {
    attempt->Status = 2u;
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  Context->PoolMdl = mdl;
  bytes = MmGetMdlByteCount(mdl);
  AdmissionPoolAnalyzeFrames(
      (const unsigned long long *)MmGetMdlPfnArray(mdl), bytes / PAGE_SIZE,
      attempt);
  attempt->Status = bytes >= ADMISSION_MEMORY_POOL_BYTES ? 1u : 3u;
  /* One run, 64 KiB-aligned (64 KB PTEs need it), below the HVC limit. */
  if (attempt->Status != 1u || attempt->Runs != 1u ||
      (attempt->LowestPhysical & (ADMISSION_MEMORY_POOL_ALIGNMENT - 1ULL)) ||
      attempt->HighestPhysical > ADMISSION_HVC_PHYSICAL_LIMIT) {
    if (attempt->Status == 1u) attempt->Status = 4u;
    AdmissionMemoryPoolRelease(Context);
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  Context->PoolCpu = MmMapLockedPagesSpecifyCache(mdl, KernelMode,
      MmWriteCombined, NULL, FALSE, NormalPagePriority | MdlMappingNoExecute);
  if (Context->PoolCpu == NULL) {
    attempt->Status = 5u;
    AdmissionMemoryPoolRelease(Context);
    AdmissionRecordM2PoolProbe(Context, &receipt);
    return;
  }
  Context->PoolIpa = attempt->LowestPhysical;
  Context->PoolBytes = ADMISSION_MEMORY_POOL_BYTES;
  if (AppleAgxPhysicalTopologyAddPool(&Context->Memory.Topology,
          ADMISSION_MEMORY_POOL_GPU_VA, Context->PoolBytes) !=
      AppleAgxPhysicalTopologyOk) {
    attempt->Status = 6u;
    AdmissionMemoryPoolRelease(Context);
  }
  AdmissionRecordM2PoolProbe(Context, &receipt);
}

_Use_decl_annotations_ VOID AdmissionMemoryPoolStop(ADMISSION_CONTEXT *Context) {
  if (Context == NULL) return;
  AdmissionMemoryPoolRelease(Context);
  RtlZeroMemory(&Context->Memory.Topology.Pool,
                sizeof(Context->Memory.Topology.Pool));
  if (Context->Memory.Topology.SegmentCount == 3u)
    Context->Memory.Topology.SegmentCount = 2u;
}

_Use_decl_annotations_ BOOLEAN AdmissionMemoryPoolView(
    const ADMISSION_CONTEXT *Context, ADMISSION_SCANOUT_MEMORY_VIEW *View) {
  if (Context == NULL || View == NULL) return FALSE;
  RtlZeroMemory(View, sizeof(*View));
  if (Context->PoolCpu == NULL || Context->PoolBytes == 0ULL ||
      Context->Memory.Topology.Pool.Id != 3u)
    return FALSE;
  View->CpuAddress = Context->PoolCpu;
  View->GuestIpaAddress = Context->PoolIpa;
  View->GpuVirtualAddress = Context->Memory.Topology.Pool.Base;
  View->Bytes = Context->PoolBytes;
  return TRUE;
}
#endif
