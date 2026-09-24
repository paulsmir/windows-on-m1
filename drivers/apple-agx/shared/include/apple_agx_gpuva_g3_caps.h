#ifndef APPLE_AGX_GPUVA_G3_CAPS_H
#define APPLE_AGX_GPUVA_G3_CAPS_H

/* VidMm sees logical 4-KiB entries. AGX consumes each aligned group of four
 * leaf entries as one native 16-KiB PTE in the first 16 KiB of the allocation. */
typedef struct _APPLE_AGX_GPUVA_G3_LEVEL_CAPS {
  unsigned int IndexBits;
  unsigned int SegmentId;
  unsigned int SizeBytes;
  unsigned int AlignmentBytes;
} APPLE_AGX_GPUVA_G3_LEVEL_CAPS;

typedef struct _APPLE_AGX_GPUVA_G3_CAPS {
  unsigned int VirtualAddressBits;
  unsigned int LevelCount;
  unsigned int Leaf64KBytes;
  APPLE_AGX_GPUVA_G3_LEVEL_CAPS Level[3];
} APPLE_AGX_GPUVA_G3_CAPS;

static inline APPLE_AGX_GPUVA_G3_CAPS AppleAgxGpuvaG3Caps(void) {
  APPLE_AGX_GPUVA_G3_CAPS caps = {39u, 3u, 0u,
      {{13u, 2u, 0x20000u, 0x4000u},
       {11u, 2u, 0x8000u, 0x4000u},
       {3u, 2u, 0x4000u, 0x4000u}}};
  return caps;
}

static inline int AppleAgxGpuvaG3CapsValid(
    const APPLE_AGX_GPUVA_G3_CAPS *caps, unsigned int pte_bytes,
    unsigned int any_64k_segment, unsigned int cpu_virtual_update) {
  unsigned int i, bits = 12u;
  if (caps == 0 || caps->LevelCount != 3u || pte_bytes == 0u ||
      cpu_virtual_update ||
      (any_64k_segment &&
       (caps->Leaf64KBytes == 0u || (caps->Leaf64KBytes & 0xfffu))) ||
      (!any_64k_segment && caps->Leaf64KBytes != 0u)) return 0;
  for (i = 0u; i < caps->LevelCount; ++i) {
    const APPLE_AGX_GPUVA_G3_LEVEL_CAPS *level = &caps->Level[i];
    unsigned long long entries;
    if (level->IndexBits >= 32u || level->SegmentId == 0u ||
        level->SizeBytes == 0u || (level->SizeBytes & 0xfffu) ||
        level->AlignmentBytes < 0x4000u ||
        (level->AlignmentBytes & 0x3fffu)) return 0;
    entries = 1ULL << level->IndexBits;
    if (entries * pte_bytes > level->SizeBytes) return 0;
    bits += level->IndexBits;
  }
  return bits == caps->VirtualAddressBits;
}

#endif
