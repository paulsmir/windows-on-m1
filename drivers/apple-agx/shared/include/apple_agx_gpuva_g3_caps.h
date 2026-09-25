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

static inline APPLE_AGX_GPUVA_G3_CAPS AppleAgxGpuvaG3Caps(
    unsigned int any_64k_segment) {
  APPLE_AGX_GPUVA_G3_CAPS caps = {39u, 3u,
      any_64k_segment ? 0x4000u : 0u,
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
       (caps->Leaf64KBytes == 0u || (caps->Leaf64KBytes & 0xfffu) ||
        caps->Leaf64KBytes < 0x4000u ||
        caps->Leaf64KBytes < (1u << (13u - 4u)) * pte_bytes)) ||
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

#define APPLE_AGX_GPUVA_G3_INVALID_MMU_ID 0xffffu

/* One source for the adapter, execution-node, MMU and segment declarations.
 * No Windows types are used so the exact contract runs in the host validator. */
typedef struct _APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT {
  unsigned int Enabled;
  unsigned int VirtualAddressingSupported;
  unsigned int AdapterGpuMmuSupported;
  unsigned int AdapterIoMmuSupported;
  unsigned int NodeCount;
  unsigned int PagingNode;
  unsigned int VirtualSubmissionNodeMask;
  unsigned int NodeGpuMmuMask;
  unsigned int NodeIoMmuMask;
  unsigned int MmuCount;
  unsigned long long MmuSizeBytes;
  unsigned int DisplayMmuId;
  unsigned int SegmentCount;
  unsigned int ApertureCount;
  unsigned int ApertureSegmentId;
  unsigned int LocalSegmentId;
  unsigned int LocalUse64KBPages;
  unsigned int PagingBufferSegmentId;
  unsigned int PagingBufferBytes;
  unsigned int PagingPrivateBytes;
  unsigned int VirtualAddressBits;
  unsigned int Leaf64KBytes;
} APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT;

static inline APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT
AppleAgxGpuvaG3AdmissionContract(unsigned int enabled,
                                  unsigned int page_profile) {
  APPLE_AGX_GPUVA_G3_CAPS tables = AppleAgxGpuvaG3Caps(
      enabled && page_profile == 64u);
  APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT caps = {
      enabled ? 1u : 0u, enabled ? 1u : 0u, enabled ? 1u : 0u, 0u,
      1u, 0u, enabled ? 1u : 0u, enabled ? 1u : 0u, 0u,
      enabled ? 1u : 0u, enabled ? (1ULL << 39) : 0ULL,
      APPLE_AGX_GPUVA_G3_INVALID_MMU_ID,
      2u, 1u, 1u, 2u, page_profile == 64u ? 1u : 0u,
      1u, 0x1000u, 0x1000u, tables.VirtualAddressBits,
      enabled ? tables.Leaf64KBytes : 0u};
  return caps;
}

static inline int AppleAgxGpuvaG3AdmissionContractValid(
    const APPLE_AGX_GPUVA_G3_ADMISSION_CONTRACT *caps,
    unsigned int pte_bytes) {
  unsigned int nodes;
  if (caps == 0 || pte_bytes == 0u || caps->NodeCount == 0u ||
      caps->NodeCount >= 32u || caps->PagingNode >= caps->NodeCount ||
      caps->SegmentCount < 2u || caps->ApertureCount != 1u ||
      caps->ApertureSegmentId == 0u ||
      caps->ApertureSegmentId > caps->SegmentCount ||
      caps->LocalSegmentId == 0u ||
      caps->LocalSegmentId > caps->SegmentCount ||
      caps->LocalSegmentId == caps->ApertureSegmentId ||
      caps->PagingBufferSegmentId != caps->ApertureSegmentId ||
      caps->PagingBufferBytes == 0u ||
      (caps->PagingBufferBytes & 0xfffu) ||
      (caps->PagingPrivateBytes & 0xfffu) ||
      caps->VirtualAddressBits != 39u)
    return 0;
  nodes = (1u << caps->NodeCount) - 1u;
  if ((caps->VirtualSubmissionNodeMask & ~nodes) ||
      (caps->NodeGpuMmuMask & ~nodes) ||
      (caps->NodeIoMmuMask & ~nodes) ||
      caps->AdapterIoMmuSupported || caps->NodeIoMmuMask)
    return 0;
  if (!caps->Enabled)
    return !caps->VirtualAddressingSupported &&
           !caps->AdapterGpuMmuSupported &&
           !caps->VirtualSubmissionNodeMask && !caps->NodeGpuMmuMask &&
           caps->MmuCount == 0u && caps->MmuSizeBytes == 0ULL &&
           caps->Leaf64KBytes == 0u &&
           caps->DisplayMmuId == APPLE_AGX_GPUVA_G3_INVALID_MMU_ID;
  return caps->VirtualAddressingSupported &&
         caps->AdapterGpuMmuSupported &&
         (caps->VirtualSubmissionNodeMask & (1u << caps->PagingNode)) &&
         (caps->NodeGpuMmuMask & caps->VirtualSubmissionNodeMask) ==
             caps->VirtualSubmissionNodeMask &&
         caps->MmuCount >= 1u && caps->MmuCount <= 16u &&
         caps->MmuSizeBytes >= (1ULL << caps->VirtualAddressBits) &&
         (caps->DisplayMmuId == APPLE_AGX_GPUVA_G3_INVALID_MMU_ID ||
          caps->DisplayMmuId < caps->MmuCount) &&
         (caps->LocalUse64KBPages ?
          (caps->Leaf64KBytes >= 0x4000u &&
           caps->Leaf64KBytes >= (1u << (13u - 4u)) * pte_bytes &&
           (caps->Leaf64KBytes & 0xfffu) == 0u) :
          caps->Leaf64KBytes == 0u);
}

#endif
