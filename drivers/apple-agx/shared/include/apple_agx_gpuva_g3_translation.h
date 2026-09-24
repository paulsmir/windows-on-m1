#ifndef APPLE_AGX_GPUVA_G3_TRANSLATION_H
#define APPLE_AGX_GPUVA_G3_TRANSLATION_H

#define APPLE_AGX_GPUVA_G3_LOGICAL_PAGE 0x1000u
#define APPLE_AGX_GPUVA_G3_NATIVE_PAGE 0x4000u
#define APPLE_AGX_GPUVA_G3_LOGICAL_LEAF_ENTRIES 8192u
#define APPLE_AGX_GPUVA_G3_VALID 1u
#define APPLE_AGX_GPUVA_G3_WRITE 2u

typedef struct _APPLE_AGX_GPUVA_G3_LOGICAL_PTE {
  /* Already resolved and validated guest IPA, never DXGK_PTE.PageAddress. */
  unsigned long long GuestIpa;
  unsigned int SegmentId;
  /* Only VALID and WRITE are representable by the current v5 leaf wire. */
  unsigned int Flags;
} APPLE_AGX_GPUVA_G3_LOGICAL_PTE;

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

APPLE_AGX_GPUVA_G3_RESULT AppleAgxGpuvaG3ResolvePageAddress(
    unsigned int SegmentId, unsigned long long PageAddress,
    unsigned int LocalSegmentId, unsigned long long LocalGuestIpaBase,
    unsigned long long LocalBytes, unsigned long long *GuestIpa);

#endif
