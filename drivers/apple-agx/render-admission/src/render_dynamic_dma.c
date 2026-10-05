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

static int dma_role_copied(APPLE_AGX_U32 Role, int Native, int Indexed) {
  return Role == AppleAgxWin32RoleVertex ||
         (Indexed && Role == AppleAgxWin32RoleIndex) ||
         Role == AppleAgxWin32RoleShader ||
         Role == AppleAgxWin32RoleShaderRodata ||
         Role == AppleAgxWin32RoleUscPipeline ||
         Role == AppleAgxWin32RoleDescriptor ||
         Role == AppleAgxWin32RoleScissor ||
         Role == AppleAgxWin32RoleDepthBias ||
         Role == AppleAgxWin32RoleSharedGeometry ||
         Role == AppleAgxWin32RoleEncoder ||
         (Native && (Role == AppleAgxWin32RoleUniform ||
                     Role == AppleAgxWin32RoleConstant || Role == AppleAgxWin32RolePppState));
}

static int dma_job_valid(const APPLE_AGX_DYNAMIC_JOB *Job,
                         const void *Storage,
                         APPLE_AGX_U32 StorageBytes,
                         APPLE_AGX_U32 Generation,
                         const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings) {
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 other;
  APPLE_AGX_U32 indexRelocations=0u;
  int indexed;
  int native;
  APPLE_AGX_U32 referenceLimit;
  if (Job == DYNAMIC_DMA_NULL || Storage == DYNAMIC_DMA_NULL ||
      Bindings == DYNAMIC_DMA_NULL ||
      Generation == 0u || Job->Magic != APPLE_AGX_DYNAMIC_JOB_MAGIC ||
      Job->Version != APPLE_AGX_DYNAMIC_JOB_VERSION ||
      Job->Generation != Generation || Job->ObjectCount == 0u ||
      Job->RelocationCount == 0u ||
      Job->StorageBytes != StorageBytes ||
      Job->Reserved[0] != 0u || Job->Reserved[1] != 0u ||
      AppleAgxDynamicDmaBytesHash(Storage, StorageBytes) !=
          Job->MaterializedHash)
    return 0;
  indexed=APPLE_AGX_WIN32_COMMAND_HAS_INDEX(Bindings->CommandVersion);
  native=APPLE_AGX_WIN32_COMMAND_IS_NATIVE(Bindings->CommandVersion);
  referenceLimit=native ?
      APPLE_AGX_WIN32_COMMAND_REFERENCE_LIMIT(Bindings->CommandVersion) :
      APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES;
  if(Job->ObjectCount>(native ? referenceLimit :
       ADMISSION_DYNAMIC_OVERLAY_LEGACY_MAX_ENTRIES) ||
     Job->RelocationCount>(native ?
       APPLE_AGX_WIN32_COMMAND_RELOCATION_LIMIT(Bindings->CommandVersion) :
       APPLE_AGX_WIN32_COMMAND_LEGACY_MAX_RELOCATIONS)) return 0;
  if (Bindings->CommandVersion == APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH) {
    if (Bindings->NativeBatch.DepthReference ==
            APPLE_AGX_WIN32_OPTIONAL_REFERENCE ||
        Bindings->NativeBatch.DepthReference >= referenceLimit ||
        Bindings->DepthGpuVirtualAddress == 0ULL ||
        ((Bindings->NativeBatch.DepthCompressionReference ==
          APPLE_AGX_WIN32_OPTIONAL_REFERENCE) !=
         (Bindings->DepthCompressionGpuVirtualAddress == 0ULL)) ||
        ((Bindings->NativeBatch.StencilReference ==
          APPLE_AGX_WIN32_OPTIONAL_REFERENCE) !=
         (Bindings->StencilGpuVirtualAddress == 0ULL)) ||
        ((Bindings->NativeBatch.StencilCompressionReference ==
          APPLE_AGX_WIN32_OPTIONAL_REFERENCE) !=
         (Bindings->StencilCompressionGpuVirtualAddress == 0ULL)))
      return 0;
  } else if (Bindings->DepthGpuVirtualAddress != 0ULL ||
             Bindings->DepthCompressionGpuVirtualAddress != 0ULL ||
             Bindings->StencilGpuVirtualAddress != 0ULL ||
             Bindings->StencilCompressionGpuVirtualAddress != 0ULL) {
    return 0;
  }
  for (index = 0u; index < Job->ObjectCount; ++index) {
    const APPLE_AGX_DYNAMIC_JOB_OBJECT *object = &Job->Objects[index];
    if (object->ReferenceIndex >= referenceLimit ||
        !dma_role_copied(object->Role,native,indexed) || object->Bytes == 0u ||
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
        relocation->Kind > (APPLE_AGX_U32)(indexed ? AppleAgxWin32RelocationVdmIndexBufferAddress40 :
                            native ? AppleAgxWin32RelocationPbeAddress40 :
                            AppleAgxWin32RelocationPppStateAddress40) ||
        relocation->DestinationReference >= referenceLimit ||
        relocation->TargetReference >= referenceLimit ||
        relocation->Reserved != 0u ||
        relocation->ResolvedAddress == 0ULL ||
        relocation->ResolvedAddress >= DYNAMIC_DMA_40_BIT_LIMIT)
      return 0;
    if(relocation->Kind==AppleAgxWin32RelocationVdmIndexBufferAddress40) {
      const APPLE_AGX_DYNAMIC_JOB_OBJECT *destination=DYNAMIC_DMA_NULL;
      const APPLE_AGX_DYNAMIC_JOB_OBJECT *target=DYNAMIC_DMA_NULL;
      ++indexRelocations;
      for(other=0u;other<Job->ObjectCount;++other) {
        if(Job->Objects[other].ReferenceIndex==relocation->DestinationReference)
          destination=&Job->Objects[other];
        if(Job->Objects[other].ReferenceIndex==relocation->TargetReference)
          target=&Job->Objects[other];
      }
      if(!indexed || !destination || !target ||
         destination->Role!=AppleAgxWin32RoleEncoder ||
         target->Role!=(APPLE_AGX_U32)(Bindings->CommandVersion==
             APPLE_AGX_WIN32_COMMAND_VERSION_MIXED_BATCH ?
               AppleAgxWin32RoleSharedGeometry : AppleAgxWin32RoleIndex) ||
         (Bindings->CommandVersion==APPLE_AGX_WIN32_COMMAND_VERSION_INDEXED_BATCH &&
          target->Bytes<2u) ||
         (relocation->ResolvedAddress&3ULL)!=0ULL)
        return 0;
    }
  }
  if(indexed ? indexRelocations!=1u : indexRelocations!=0u) return 0;
  return 1;
}

ADMISSION_DYNAMIC_DMA_RESULT AdmissionDynamicDmaBuild(
    APPLE_AGX_U32 Generation, APPLE_AGX_U64 CommandHash,
    APPLE_AGX_U64 DestinationGpuVa,
    APPLE_AGX_U32 DestinationAllocationIndex,
    APPLE_AGX_U32 BackgroundColor, APPLE_AGX_U32 ExpectedForegroundColor,
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
      DestinationAllocationIndex >=
          (Bindings ? APPLE_AGX_WIN32_COMMAND_REFERENCE_LIMIT(Bindings->CommandVersion) :
           APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES) ||
      DestinationGpuVa >= DYNAMIC_DMA_40_BIT_LIMIT ||
      Bindings == DYNAMIC_DMA_NULL || Bindings->Reserved != 0u || Job == DYNAMIC_DMA_NULL ||
      Storage == DYNAMIC_DMA_NULL || StorageBytes == 0u ||
      Destination == DYNAMIC_DMA_NULL || BytesWritten == DYNAMIC_DMA_NULL ||
      (ExpectedForegroundColor != 0u &&
       (ExpectedForegroundColor == BackgroundColor ||
        ExpectedForegroundColor == 0xa5a5a5a5u)) ||
      StorageBytes > ADMISSION_DYNAMIC_DMA_MAX_BYTES ||
      storageOffset > ADMISSION_DYNAMIC_DMA_MAX_BYTES ||
      StorageBytes > ADMISSION_DYNAMIC_DMA_MAX_BYTES - storageOffset)
    return AdmissionDynamicDmaArgument;
  total = storageOffset + StorageBytes;
  if (DestinationCapacity < total)
    return AdmissionDynamicDmaCapacity;
  if (!dma_job_valid(Job, Storage, StorageBytes, Generation, Bindings))
    return AdmissionDynamicDmaJob;
  dma_zero(&header, (APPLE_AGX_U32)sizeof(header));
  header.Magic = ADMISSION_DYNAMIC_DMA_MAGIC;
  header.Version = ADMISSION_DYNAMIC_DMA_VERSION;
  header.HeaderBytes = sizeof(header);
  header.TotalBytes = total;
  header.Generation = Generation;
  header.Flags = ExpectedForegroundColor != 0u
                     ? ADMISSION_DYNAMIC_DMA_FLAG_EXPECTED_FOREGROUND
                     : 0u;
  header.JobOffset = jobOffset;
  header.JobBytes = sizeof(*Job);
  header.StorageOffset = storageOffset;
  header.StorageBytes = StorageBytes;
  header.CommandHash = CommandHash;
  header.DestinationGpuVa = DestinationGpuVa;
  header.Bindings = *Bindings;
  header.BackgroundColor = BackgroundColor;
  header.DestinationAllocationIndex = DestinationAllocationIndex;
  header.ExpectedForegroundColor = ExpectedForegroundColor;
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
      (header->Flags & ~ADMISSION_DYNAMIC_DMA_FLAG_EXPECTED_FOREGROUND) != 0u ||
      (((header->Flags & ADMISSION_DYNAMIC_DMA_FLAG_EXPECTED_FOREGROUND) != 0u) !=
       (header->ExpectedForegroundColor != 0u)) ||
      (header->ExpectedForegroundColor != 0u &&
       (header->ExpectedForegroundColor == header->BackgroundColor ||
        header->ExpectedForegroundColor == 0xa5a5a5a5u)) ||
      header->JobOffset != sizeof(*header) ||
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
          APPLE_AGX_WIN32_COMMAND_REFERENCE_LIMIT(header->Bindings.CommandVersion) ||
      header->Reserved != 0u || header->Bindings.Reserved != 0u)
    return AdmissionDynamicDmaLayout;
  if (AppleAgxDynamicDmaRecordHash(Bytes, ByteCount) != header->ContentHash)
    return AdmissionDynamicDmaHash;
  job = (const APPLE_AGX_DYNAMIC_JOB *)(bytes + header->JobOffset);
  if (!dma_job_valid(job, bytes + header->StorageOffset,
                     header->StorageBytes, header->Generation, &header->Bindings))
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
  if(APPLE_AGX_WIN32_COMMAND_IS_NATIVE(header->Bindings.CommandVersion)) {
    APPLE_AGX_DYNAMIC_JOB *job=(APPLE_AGX_DYNAMIC_JOB *)((unsigned char *)Bytes+header->JobOffset);
    unsigned char *storage=(unsigned char *)Bytes+header->StorageOffset;
    APPLE_AGX_U32 i,j,pass;
    if(!header->Bindings.DestinationBytes) return AdmissionDynamicDmaLayout;
    /* Validate all destination-bearing fields before mutating either the
     * immutable DMA copy or its privately held shadow. A retry is idempotent. */
    for(pass=0;pass<2;++pass) for(i=0;i<job->RelocationCount;++i) {
      APPLE_AGX_DYNAMIC_JOB_RELOCATION *r=&job->Relocations[i];
      APPLE_AGX_DYNAMIC_JOB_OBJECT *object=DYNAMIC_DMA_NULL;
      APPLE_AGX_U64 address,raw=0,mask,encoded;
      unsigned char *field;
      if(r->TargetReference!=header->Bindings.DestinationReference) continue;
      if((r->Kind!=AppleAgxWin32RelocationTextureAddress40 &&
          r->Kind!=AppleAgxWin32RelocationPbeAddress40) ||
          r->TargetOffset>=header->Bindings.DestinationBytes ||
          r->TargetOffset>DYNAMIC_DMA_40_BIT_LIMIT-1-DestinationGpuVa)
        return AdmissionDynamicDmaLayout;
      address=DestinationGpuVa+r->TargetOffset;
      if(address&15ULL) return AdmissionDynamicDmaLayout;
      for(j=0;j<job->ObjectCount;++j)
        if(job->Objects[j].ReferenceIndex==r->DestinationReference) object=&job->Objects[j];
      if(!object || object->Role!=AppleAgxWin32RoleDescriptor ||
          r->DestinationOffset>object->Bytes || 8u>object->Bytes-r->DestinationOffset)
        return AdmissionDynamicDmaLayout;
      field=storage+object->StorageOffset+r->DestinationOffset;
      for(j=0;j<8u;++j) raw|=(APPLE_AGX_U64)field[j]<<(8u*j);
      mask=r->Kind==AppleAgxWin32RelocationTextureAddress40 ?
          ((1ULL<<36)-1)<<2 : (1ULL<<36)-1;
      encoded=r->Kind==AppleAgxWin32RelocationTextureAddress40 ? (address>>4)<<2 : address>>4;
      if(pass) {
        encoded|=raw&~mask;
        for(j=0;j<8u;++j) field[j]=(unsigned char)(encoded>>(8u*j));
        r->ResolvedAddress=address; r->EncodedValue=address;
      }
    }
    job->MaterializedHash=AppleAgxDynamicDmaBytesHash(storage,job->StorageBytes);
  }
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
