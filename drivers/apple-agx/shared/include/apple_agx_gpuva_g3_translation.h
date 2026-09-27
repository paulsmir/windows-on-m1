#ifndef APPLE_AGX_GPUVA_G3_TRANSLATION_H
#define APPLE_AGX_GPUVA_G3_TRANSLATION_H

#define APPLE_AGX_GPUVA_G3_LOGICAL_PAGE 0x1000u
#define APPLE_AGX_GPUVA_G3_NATIVE_PAGE 0x4000u
#define APPLE_AGX_GPUVA_G3_LOGICAL_LEAF_ENTRIES 8192u
#define APPLE_AGX_GPUVA_G3_VALID 1u
#define APPLE_AGX_GPUVA_G3_WRITE 2u

/* DXGK_UPDATEPAGETABLEFLAGS.Repeat provides one PTE for the entire span. */
static inline unsigned int AppleAgxGpuvaG3PteInputIndex(
    unsigned int Index, unsigned int Repeat) {
  return Repeat ? 0u : Index;
}

/* DXGK_PTE stores address bits 63:12; G3 segment and IPA helpers use bytes. */
static inline int AppleAgxGpuvaG3PteAddressBytes(
    unsigned long long PtePageNumber, unsigned long long *Bytes) {
  if (Bytes == 0 || PtePageNumber > (~0ULL >> 12)) return 0;
  *Bytes = PtePageNumber << 12;
  return 1;
}

static inline int AppleAgxGpuvaG3TableSpanWithinLocal(
    unsigned long long LocalGuestIpa, unsigned long long LocalBytes,
    unsigned long long TableGuestIpa, unsigned long long TableBytes) {
  return TableBytes != 0ULL && LocalBytes >= TableBytes &&
         LocalGuestIpa <= (~0ULL) - LocalBytes &&
         TableGuestIpa >= LocalGuestIpa &&
         TableGuestIpa - LocalGuestIpa <= LocalBytes - TableBytes;
}

typedef struct _APPLE_AGX_GPUVA_G3_LOGICAL_PTE {
  /* Already resolved and validated guest IPA, never DXGK_PTE.PageAddress. */
  unsigned long long GuestIpa;
  unsigned int SegmentId;
  /* Only VALID and WRITE are representable by the current v5 leaf wire. */
  unsigned int Flags;
} APPLE_AGX_GPUVA_G3_LOGICAL_PTE;

/* A 64-KiB leaf update replaces sixteen 4-KiB logical slots. Until that
 * mapping has an explicit CPU-envelope representation, invalidate any old
 * 4-KiB shadow before publishing the new native leaves. */
static inline int AppleAgxGpuvaG3InvalidateLogical64K(
    APPLE_AGX_GPUVA_G3_LOGICAL_PTE *entries,
    unsigned int start_index, unsigned int count) {
  unsigned int i, first, end;
  if (entries == 0 || count == 0u || start_index >= 512u ||
      count > 512u - start_index) return 0;
  first = start_index * 16u;
  end = (start_index + count) * 16u;
  for (i = first; i < end; ++i) {
    entries[i].GuestIpa = 0ULL;
    entries[i].SegmentId = 0u;
    entries[i].Flags = 0u;
  }
  return 1;
}

typedef struct _APPLE_AGX_GPUVA_G3_NATIVE_LEAF {
  unsigned long long GpuVa;
  unsigned long long GuestIpa;
  unsigned int SegmentId;
  unsigned int ValidMask;
  unsigned int WritableMask;
} APPLE_AGX_GPUVA_G3_NATIVE_LEAF;

typedef enum _APPLE_AGX_GPUVA_G3_RESULT {
  AppleAgxGpuvaG3Ok = 0,
  AppleAgxGpuvaG3Unmap,
  AppleAgxGpuvaG3Invalid,
  AppleAgxGpuvaG3Unrepresentable
} APPLE_AGX_GPUVA_G3_RESULT;

APPLE_AGX_GPUVA_G3_RESULT AppleAgxGpuvaG3PlanSpan(
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *Entries,
    unsigned int StartIndex, unsigned int Count,
    unsigned long long FirstGpuVa, unsigned int LocalSegmentId,
    unsigned int SegmentPageBytes, APPLE_AGX_GPUVA_G3_NATIVE_LEAF *Leaves,
    unsigned int LeafCapacity, unsigned int *LeafCount);

/* A 64-KiB VidMm leaf entry expands to four native 16-KiB AGX leaves. */
APPLE_AGX_GPUVA_G3_RESULT AppleAgxGpuvaG3Plan64KSpan(
    const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *Entries,
    unsigned int StartIndex, unsigned int Count,
    unsigned long long FirstGpuVa, unsigned int LocalSegmentId,
    APPLE_AGX_GPUVA_G3_NATIVE_LEAF *Leaves,
    unsigned int LeafCapacity, unsigned int *LeafCount);

APPLE_AGX_GPUVA_G3_RESULT AppleAgxGpuvaG3ResolvePageAddress(
    unsigned int SegmentId, unsigned long long PageAddress,
    unsigned int LocalSegmentId, unsigned long long LocalGuestIpaBase,
    unsigned long long LocalBytes, unsigned long long *GuestIpa);

#endif
