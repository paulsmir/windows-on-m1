#include "render_dynamic_dma.h"

#include <stddef.h>

#define DYNAMIC_DMA_NULL ((void *)0)
#define DYNAMIC_DMA_40_BIT_LIMIT (1ULL << 40u)

static void dma_zero(void *Data, APPLE_AGX_U32 Bytes) {
  unsigned char *data = (unsigned char *)Data;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    data[index] = 0u;
}

static void dma_copy(void *Destination, const void *Source,
                     APPLE_AGX_U32 Bytes) {
  unsigned char *destination = (unsigned char *)Destination;
  const unsigned char *source = (const unsigned char *)Source;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    destination[index] = source[index];
}

APPLE_AGX_U64 AppleAgxDynamicDmaBytesHash(const void *Bytes,
                                          APPLE_AGX_U32 ByteCount) {
  const unsigned char *bytes = (const unsigned char *)Bytes;
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  APPLE_AGX_U32 index;
  if (bytes == DYNAMIC_DMA_NULL || ByteCount == 0u)
    return 0ULL;
  for (index = 0u; index < ByteCount; ++index) {
    hash ^= bytes[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

APPLE_AGX_U64 AppleAgxDynamicDmaRecordHash(const void *Bytes,
                                           APPLE_AGX_U32 ByteCount) {
  const unsigned char *bytes = (const unsigned char *)Bytes;
  const APPLE_AGX_U32 hashOffset =
      (APPLE_AGX_U32)offsetof(ADMISSION_DYNAMIC_DMA_HEADER, ContentHash);
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  APPLE_AGX_U32 index;
  if (bytes == DYNAMIC_DMA_NULL ||
      ByteCount < sizeof(ADMISSION_DYNAMIC_DMA_HEADER) ||
      ByteCount > ADMISSION_DYNAMIC_DMA_MAX_BYTES)
    return 0ULL;
  for (index = 0u; index < ByteCount; ++index) {
    unsigned char value =
        index >= hashOffset && index < hashOffset + sizeof(APPLE_AGX_U64)
            ? 0u
            : bytes[index];
    hash ^= value;
    hash *= 1099511628211ULL;
  }
  return hash;
}

APPLE_AGX_U32 AdmissionDynamicDmaDestinationPatchOffset(void) {
  return (APPLE_AGX_U32)offsetof(ADMISSION_DYNAMIC_DMA_HEADER,
                                DestinationGpuVa);
}

static int dma_role_copied(APPLE_AGX_U32 Role) {
  return Role == AppleAgxWin32RoleVertex ||
         Role == AppleAgxWin32RoleShader ||
         Role == AppleAgxWin32RoleShaderRodata ||
         Role == AppleAgxWin32RoleUscPipeline ||
         Role == AppleAgxWin32RoleDescriptor ||
         Role == AppleAgxWin32RoleScissor ||
         Role == AppleAgxWin32RoleDepthBias ||
         Role == AppleAgxWin32RoleEncoder;
}

static int dma_job_valid(const APPLE_AGX_DYNAMIC_JOB *Job,
                         const void *Storage,
                         APPLE_AGX_U32 StorageBytes,
                         APPLE_AGX_U32 Generation) {
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 other;
  if (Job == DYNAMIC_DMA_NULL || Storage == DYNAMIC_DMA_NULL ||
      Generation == 0u || Job->Magic != APPLE_AGX_DYNAMIC_JOB_MAGIC ||
      Job->Version != APPLE_AGX_DYNAMIC_JOB_VERSION ||
      Job->Generation != Generation || Job->ObjectCount == 0u ||
      Job->ObjectCount > ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES ||
      Job->RelocationCount == 0u ||
      Job->RelocationCount > APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS ||
      Job->StorageBytes != StorageBytes ||
      Job->Reserved[0] != 0u || Job->Reserved[1] != 0u ||
      AppleAgxDynamicDmaBytesHash(Storage, StorageBytes) !=
          Job->MaterializedHash)
    return 0;
  for (index = 0u; index < Job->ObjectCount; ++index) {
    const APPLE_AGX_DYNAMIC_JOB_OBJECT *object = &Job->Objects[index];
    if (object->ReferenceIndex >= APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
        !dma_role_copied(object->Role) || object->Bytes == 0u ||
        object->StorageOffset > StorageBytes ||
        object->Bytes > StorageBytes - object->StorageOffset)
      return 0;
    for (other = 0u; other < index; ++other) {
      const APPLE_AGX_DYNAMIC_JOB_OBJECT *prior = &Job->Objects[other];
      if (prior->ReferenceIndex == object->ReferenceIndex ||
          (prior->StorageOffset < object->StorageOffset + object->Bytes &&
           object->StorageOffset < prior->StorageOffset + prior->Bytes))
        return 0;
    }
  }
  for (index = 0u; index < Job->RelocationCount; ++index) {
    const APPLE_AGX_DYNAMIC_JOB_RELOCATION *relocation =
        &Job->Relocations[index];
    if (relocation->Kind < AppleAgxWin32RelocationEncoderAddress ||
        relocation->Kind > AppleAgxWin32RelocationPppStateAddress40 ||
        relocation->DestinationReference >=
            APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
        relocation->TargetReference >=
            APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
        relocation->Reserved != 0u ||
        relocation->ResolvedAddress == 0ULL ||
        relocation->ResolvedAddress >= DYNAMIC_DMA_40_BIT_LIMIT)
      return 0;
  }
  return 1;
}

ADMISSION_DYNAMIC_DMA_RESULT AdmissionDynamicDmaBuild(
    APPLE_AGX_U32 Generation, APPLE_AGX_U64 CommandHash,
    APPLE_AGX_U64 DestinationGpuVa,
    APPLE_AGX_U32 DestinationAllocationIndex,
    APPLE_AGX_U32 BackgroundColor,
    const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings,
    const APPLE_AGX_DYNAMIC_JOB *Job, const void *Storage,
    APPLE_AGX_U32 StorageBytes, void *Destination,
    APPLE_AGX_U32 DestinationCapacity, APPLE_AGX_U32 *BytesWritten) {
  ADMISSION_DYNAMIC_DMA_HEADER header;
  APPLE_AGX_U32 jobOffset = (APPLE_AGX_U32)sizeof(header);
  APPLE_AGX_U32 storageOffset = jobOffset + (APPLE_AGX_U32)sizeof(*Job);
  APPLE_AGX_U32 total;
  unsigned char *destination = (unsigned char *)Destination;
  if (BytesWritten != DYNAMIC_DMA_NULL)
    *BytesWritten = 0u;
  if (Generation == 0u || CommandHash == 0ULL ||
      DestinationGpuVa == 0ULL || (DestinationGpuVa & 0x3fffULL) != 0ULL ||
      DestinationAllocationIndex >= APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
      DestinationGpuVa >= DYNAMIC_DMA_40_BIT_LIMIT ||
      Bindings == DYNAMIC_DMA_NULL || Job == DYNAMIC_DMA_NULL ||
      Storage == DYNAMIC_DMA_NULL || StorageBytes == 0u ||
      Destination == DYNAMIC_DMA_NULL || BytesWritten == DYNAMIC_DMA_NULL ||
      StorageBytes > ADMISSION_DYNAMIC_DMA_MAX_BYTES ||
      storageOffset > ADMISSION_DYNAMIC_DMA_MAX_BYTES ||
      StorageBytes > ADMISSION_DYNAMIC_DMA_MAX_BYTES - storageOffset)
    return AdmissionDynamicDmaArgument;
  total = storageOffset + StorageBytes;
  if (DestinationCapacity < total)
    return AdmissionDynamicDmaCapacity;
  if (!dma_job_valid(Job, Storage, StorageBytes, Generation))
    return AdmissionDynamicDmaJob;
  dma_zero(&header, (APPLE_AGX_U32)sizeof(header));
  header.Magic = ADMISSION_DYNAMIC_DMA_MAGIC;
  header.Version = ADMISSION_DYNAMIC_DMA_VERSION;
  header.HeaderBytes = sizeof(header);
  header.TotalBytes = total;
  header.Generation = Generation;
  header.JobOffset = jobOffset;
  header.JobBytes = sizeof(*Job);
  header.StorageOffset = storageOffset;
  header.StorageBytes = StorageBytes;
  header.CommandHash = CommandHash;
  header.DestinationGpuVa = DestinationGpuVa;
  header.Bindings = *Bindings;
  header.BackgroundColor = BackgroundColor;
  header.DestinationAllocationIndex = DestinationAllocationIndex;
  dma_copy(destination, &header, (APPLE_AGX_U32)sizeof(header));
  dma_copy(destination + jobOffset, Job, (APPLE_AGX_U32)sizeof(*Job));
  dma_copy(destination + storageOffset, Storage, StorageBytes);
  ((ADMISSION_DYNAMIC_DMA_HEADER *)destination)->ContentHash =
      AppleAgxDynamicDmaRecordHash(destination, total);
  if (((ADMISSION_DYNAMIC_DMA_HEADER *)destination)->ContentHash == 0ULL)
    return AdmissionDynamicDmaHash;
  *BytesWritten = total;
  return AdmissionDynamicDmaSuccess;
}

ADMISSION_DYNAMIC_DMA_RESULT AdmissionDynamicDmaOpen(
    const void *Bytes, APPLE_AGX_U32 ByteCount,
    ADMISSION_DYNAMIC_DMA_VIEW *View) {
  const unsigned char *bytes = (const unsigned char *)Bytes;
  const ADMISSION_DYNAMIC_DMA_HEADER *header;
  const APPLE_AGX_DYNAMIC_JOB *job;
  if (View != DYNAMIC_DMA_NULL)
    dma_zero(View, (APPLE_AGX_U32)sizeof(*View));
  if (bytes == DYNAMIC_DMA_NULL || View == DYNAMIC_DMA_NULL ||
      ByteCount < sizeof(ADMISSION_DYNAMIC_DMA_HEADER) ||
      ByteCount > ADMISSION_DYNAMIC_DMA_MAX_BYTES)
    return AdmissionDynamicDmaArgument;
  header = (const ADMISSION_DYNAMIC_DMA_HEADER *)bytes;
  if (header->Magic != ADMISSION_DYNAMIC_DMA_MAGIC ||
      header->Version != ADMISSION_DYNAMIC_DMA_VERSION ||
      header->HeaderBytes != sizeof(*header) ||
      header->TotalBytes != ByteCount || header->Generation == 0u ||
      header->Flags != 0u || header->JobOffset != sizeof(*header) ||
      header->JobBytes != sizeof(APPLE_AGX_DYNAMIC_JOB) ||
      header->StorageOffset != header->JobOffset + header->JobBytes ||
      header->StorageBytes == 0u ||
      header->StorageOffset > header->TotalBytes ||
      header->StorageBytes > header->TotalBytes - header->StorageOffset ||
      header->StorageOffset + header->StorageBytes != header->TotalBytes ||
      header->CommandHash == 0ULL || header->DestinationGpuVa == 0ULL ||
      (header->DestinationGpuVa & 0x3fffULL) != 0ULL ||
      header->DestinationGpuVa >= DYNAMIC_DMA_40_BIT_LIMIT ||
      header->DestinationAllocationIndex >=
          APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
      header->Reserved != 0u)
    return AdmissionDynamicDmaLayout;
  if (AppleAgxDynamicDmaRecordHash(Bytes, ByteCount) != header->ContentHash)
    return AdmissionDynamicDmaHash;
  job = (const APPLE_AGX_DYNAMIC_JOB *)(bytes + header->JobOffset);
  if (!dma_job_valid(job, bytes + header->StorageOffset,
                     header->StorageBytes, header->Generation))
    return AdmissionDynamicDmaJob;
  View->Header = header;
  View->Bindings = &header->Bindings;
  View->Job = job;
  View->Storage = bytes + header->StorageOffset;
  View->StorageBytes = header->StorageBytes;
  return AdmissionDynamicDmaSuccess;
}

ADMISSION_DYNAMIC_DMA_RESULT AdmissionDynamicDmaPatchDestination(
    void *Bytes, APPLE_AGX_U32 ByteCount,
    APPLE_AGX_U64 DestinationGpuVa) {
  ADMISSION_DYNAMIC_DMA_VIEW view;
  ADMISSION_DYNAMIC_DMA_HEADER *header;
  ADMISSION_DYNAMIC_DMA_RESULT result;
  if (Bytes == DYNAMIC_DMA_NULL || DestinationGpuVa == 0ULL ||
      (DestinationGpuVa & 0x3fffULL) != 0ULL ||
      DestinationGpuVa >= DYNAMIC_DMA_40_BIT_LIMIT)
    return AdmissionDynamicDmaArgument;
  result = AdmissionDynamicDmaOpen(Bytes, ByteCount, &view);
  if (result != AdmissionDynamicDmaSuccess)
    return result;
  header = (ADMISSION_DYNAMIC_DMA_HEADER *)Bytes;
  header->DestinationGpuVa = DestinationGpuVa;
  header->ContentHash = 0ULL;
  header->ContentHash = AppleAgxDynamicDmaRecordHash(Bytes, ByteCount);
  return header->ContentHash != 0ULL ? AdmissionDynamicDmaSuccess
                                    : AdmissionDynamicDmaHash;
}

int AdmissionDynamicDmaDescribePreparedRecord(
    const unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_U32 DmaOffset, ADMISSION_GDI_PREPARED *Prepared) {
  ADMISSION_DYNAMIC_DMA_VIEW view;
  ADMISSION_GDI_PREPARED candidate;
  APPLE_AGX_U32 patchOffset;
  if (Prepared == DYNAMIC_DMA_NULL || DmaOffset > ~0u - DmaBytes ||
      AdmissionDynamicDmaOpen(DmaBuffer, DmaBytes, &view) !=
          AdmissionDynamicDmaSuccess)
    return 0;
  patchOffset = AdmissionDynamicDmaDestinationPatchOffset();
  if (DmaOffset > ~0u - patchOffset)
    return 0;
  dma_zero(&candidate, (APPLE_AGX_U32)sizeof(candidate));
  candidate.Magic = ADMISSION_GDI_PREPARED_MAGIC;
  candidate.Version = ADMISSION_GDI_PREPARED_VERSION;
  candidate.DmaOffset = DmaOffset;
  candidate.DmaBytes = DmaBytes;
  candidate.PatchCount = 1u;
  candidate.Patches[0].AllocationIndex =
      view.Header->DestinationAllocationIndex;
  candidate.Patches[0].SlotId = ADMISSION_GDI_DESTINATION_SLOT;
  candidate.Patches[0].PatchOffset = DmaOffset + patchOffset;
  candidate.Patches[0].SplitOffset = DmaOffset;
  *Prepared = candidate;
  return 1;
}

#undef DYNAMIC_DMA_NULL
