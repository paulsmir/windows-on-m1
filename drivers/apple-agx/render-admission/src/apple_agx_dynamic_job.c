#include "apple_agx_dynamic_job.h"

#include <stddef.h>

#define DYNAMIC_NULL ((void *)0)
#define DYNAMIC_40_BIT_LIMIT (1ULL << 40)

static void dynamic_zero(void *Data, APPLE_AGX_U32 Bytes) {
  unsigned char *data = (unsigned char *)Data;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    data[index] = 0u;
}

static APPLE_AGX_U64 dynamic_hash(const void *Data, APPLE_AGX_U32 Bytes) {
  const unsigned char *data = (const unsigned char *)Data;
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  APPLE_AGX_U32 index;
  if (Data == DYNAMIC_NULL || Bytes == 0u)
    return 0ULL;
  for (index = 0u; index < Bytes; ++index) {
    hash ^= data[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static void dynamic_write_u64(void *Destination, APPLE_AGX_U64 Value) {
  unsigned char *destination = (unsigned char *)Destination;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    destination[index] = (unsigned char)(Value >> (index * 8u));
}

static APPLE_AGX_U64 dynamic_read_le(const void *Source,
                                     APPLE_AGX_U32 Bytes) {
  const unsigned char *source = (const unsigned char *)Source;
  APPLE_AGX_U64 value = 0ULL;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    value |= (APPLE_AGX_U64)source[index] << (index * 8u);
  return value;
}

static void dynamic_write_le(void *Destination, APPLE_AGX_U64 Value,
                             APPLE_AGX_U32 Bytes) {
  unsigned char *destination = (unsigned char *)Destination;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    destination[index] = (unsigned char)(Value >> (index * 8u));
}

static int dynamic_patch(void *Destination,
                         const APPLE_AGX_WIN32_RELOCATION *Relocation,
                         APPLE_AGX_U64 GpuAddress,
                         APPLE_AGX_U64 ShaderBase,
                         APPLE_AGX_U64 *EncodedValue) {
  APPLE_AGX_U64 current;
  APPLE_AGX_U64 relative;
  APPLE_AGX_U64 encoded;
  if (Destination == DYNAMIC_NULL || Relocation == DYNAMIC_NULL ||
      EncodedValue == DYNAMIC_NULL)
    return 0;
  switch (Relocation->Kind) {
  case AppleAgxWin32RelocationEncoderAddress:
  case AppleAgxWin32RelocationPipelineAddress:
  case AppleAgxWin32RelocationDescriptorAddress:
    if (Relocation->WidthBytes != 8u)
      return 0;
    dynamic_write_u64(Destination, GpuAddress);
    *EncodedValue = GpuAddress;
    return 1;
  case AppleAgxWin32RelocationUscShaderOffset32:
    if (Relocation->WidthBytes != 6u || GpuAddress < ShaderBase ||
        GpuAddress - ShaderBase > 0xffffffffULL)
      return 0;
    relative = GpuAddress - ShaderBase;
    current = dynamic_read_le(Destination, 6u);
    encoded = (current & 0xffffULL) | (relative << 16u);
    dynamic_write_le(Destination, encoded, 6u);
    *EncodedValue = relative;
    return 1;
  case AppleAgxWin32RelocationUscBufferAddress40:
    if (Relocation->WidthBytes != 8u || (GpuAddress & 3ULL) != 0ULL ||
        GpuAddress >= DYNAMIC_40_BIT_LIMIT)
      return 0;
    current = dynamic_read_le(Destination, 8u);
    encoded = (current & 0xffffffULL) | (GpuAddress << 24u);
    dynamic_write_le(Destination, encoded, 8u);
    *EncodedValue = GpuAddress;
    return 1;
  case AppleAgxWin32RelocationVdmPipelineOffset32:
    if (Relocation->WidthBytes != 4u || GpuAddress < ShaderBase ||
        GpuAddress - ShaderBase > 0xffffffffULL ||
        ((GpuAddress - ShaderBase) & 0x3fULL) != 0ULL)
      return 0;
    relative = GpuAddress - ShaderBase;
    current = dynamic_read_le(Destination, 4u);
    encoded = (current & 0x3fULL) | relative;
    dynamic_write_le(Destination, encoded, 4u);
    *EncodedValue = relative;
    return 1;
  default:
    return 0;
  }
}

static int dynamic_destination_role(APPLE_AGX_U32 Role) {
  return Role == AppleAgxWin32RoleEncoder ||
         Role == AppleAgxWin32RoleUscPipeline ||
         Role == AppleAgxWin32RoleDescriptor;
}

static APPLE_AGX_DYNAMIC_JOB_OBJECT *dynamic_object(
    APPLE_AGX_DYNAMIC_JOB *Job, APPLE_AGX_U32 ReferenceIndex) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < Job->ObjectCount; ++index)
    if (Job->Objects[index].ReferenceIndex == ReferenceIndex)
      return &Job->Objects[index];
  return DYNAMIC_NULL;
}

static APPLE_AGX_DYNAMIC_JOB_RESULT dynamic_fail(
    APPLE_AGX_DYNAMIC_JOB_RESULT Result, void *Storage,
    APPLE_AGX_U32 StorageBytes, APPLE_AGX_DYNAMIC_JOB *Job) {
  if (Storage != DYNAMIC_NULL && StorageBytes != 0u)
    dynamic_zero(Storage, StorageBytes);
  if (Job != DYNAMIC_NULL)
    dynamic_zero(Job, (APPLE_AGX_U32)sizeof(*Job));
  return Result;
}

APPLE_AGX_DYNAMIC_JOB_RESULT AppleAgxDynamicJobMaterialize(
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    const ADMISSION_WIN32_ALLOCATION_FACT *Facts,
    APPLE_AGX_U32 FactCount, APPLE_AGX_U64 ShaderBase,
    APPLE_AGX_DYNAMIC_JOB_READ Read,
    APPLE_AGX_DYNAMIC_JOB_RESOLVE Resolve, void *CallbackContext,
    void *Storage, APPLE_AGX_U32 StorageCapacity,
    APPLE_AGX_DYNAMIC_JOB *Job) {
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 storageBytes = 0u;
  unsigned char *storage = (unsigned char *)Storage;
  if (View == DYNAMIC_NULL || View->Header == DYNAMIC_NULL ||
      View->References == DYNAMIC_NULL || View->Draw == DYNAMIC_NULL ||
      View->Relocations == DYNAMIC_NULL || Facts == DYNAMIC_NULL ||
      Read == DYNAMIC_NULL || Resolve == DYNAMIC_NULL ||
      CallbackContext == DYNAMIC_NULL || Storage == DYNAMIC_NULL ||
      Job == DYNAMIC_NULL ||
      View->Header->Opcode != AppleAgxWin32OpcodeDraw ||
      View->Header->Generation == 0u ||
      View->Header->ReferenceCount == 0u ||
      View->Header->ReferenceCount > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
      View->Header->ReferenceCount > FactCount ||
      View->Draw->RelocationCount == 0u ||
      View->Draw->RelocationCount > APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS ||
      StorageCapacity == 0u ||
      StorageCapacity > APPLE_AGX_DYNAMIC_JOB_MAX_STORAGE_BYTES ||
      ShaderBase == 0ULL || ShaderBase >= DYNAMIC_40_BIT_LIMIT)
    return dynamic_fail(AppleAgxDynamicJobArgument, Storage, 0u, Job);
  dynamic_zero(Job, (APPLE_AGX_U32)sizeof(*Job));

  for (index = 0u; index < View->Draw->RelocationCount; ++index) {
    const APPLE_AGX_WIN32_RELOCATION *relocation = &View->Relocations[index];
    APPLE_AGX_U32 referenceIndex = relocation->DestinationReference;
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *reference;
    const ADMISSION_WIN32_ALLOCATION_FACT *fact;
    APPLE_AGX_DYNAMIC_JOB_OBJECT *object;
    APPLE_AGX_U32 alignedStorage;
    if (referenceIndex >= View->Header->ReferenceCount)
      return dynamic_fail(AppleAgxDynamicJobLayout, Storage, storageBytes, Job);
    reference = &View->References[referenceIndex];
    fact = &Facts[referenceIndex];
    if (!dynamic_destination_role(reference->Role))
      return dynamic_fail(AppleAgxDynamicJobRelocation, Storage, storageBytes,
                          Job);
    object = dynamic_object(Job, referenceIndex);
    if (object != DYNAMIC_NULL)
      continue;
    if (reference->Bytes == 0ULL ||
        reference->Bytes > APPLE_AGX_DYNAMIC_JOB_MAX_OBJECT_BYTES ||
        reference->Bytes > 0xffffffffULL ||
        fact->AllocationToken == 0ULL ||
        reference->Offset > fact->Bytes ||
        reference->Bytes > fact->Bytes - reference->Offset)
      return dynamic_fail(AppleAgxDynamicJobRange, Storage, storageBytes, Job);
    alignedStorage = (storageBytes + 15u) & ~15u;
    if (alignedStorage < storageBytes || alignedStorage > StorageCapacity ||
        reference->Bytes > StorageCapacity - alignedStorage)
      return dynamic_fail(AppleAgxDynamicJobRange, Storage, storageBytes, Job);
    object = &Job->Objects[Job->ObjectCount++];
    object->ReferenceIndex = referenceIndex;
    object->Role = reference->Role;
    object->StorageOffset = alignedStorage;
    object->Bytes = (APPLE_AGX_U32)reference->Bytes;
    object->AllocationToken = fact->AllocationToken;
    if (!Read(CallbackContext, fact->AllocationToken, reference->Offset,
              object->Bytes, storage + object->StorageOffset))
      return dynamic_fail(AppleAgxDynamicJobRead, Storage,
                          alignedStorage + object->Bytes, Job);
    object->SourceHash = dynamic_hash(
        storage + object->StorageOffset, object->Bytes);
    storageBytes = alignedStorage + object->Bytes;
  }

  for (index = 0u; index < View->Draw->RelocationCount; ++index) {
    const APPLE_AGX_WIN32_RELOCATION *relocation = &View->Relocations[index];
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *target;
    const ADMISSION_WIN32_ALLOCATION_FACT *targetFact;
    APPLE_AGX_DYNAMIC_JOB_OBJECT *destination;
    APPLE_AGX_U64 absoluteOffset;
    APPLE_AGX_U64 gpuAddress = 0ULL;
    APPLE_AGX_DYNAMIC_JOB_RELOCATION *resolved;
    destination = dynamic_object(Job, relocation->DestinationReference);
    if (destination == DYNAMIC_NULL || relocation->WidthBytes == 0u ||
        relocation->DestinationOffset > destination->Bytes ||
        relocation->WidthBytes >
            destination->Bytes - relocation->DestinationOffset ||
        relocation->TargetReference >= View->Header->ReferenceCount)
      return dynamic_fail(AppleAgxDynamicJobRelocation, Storage, storageBytes,
                          Job);
    target = &View->References[relocation->TargetReference];
    targetFact = &Facts[relocation->TargetReference];
    if (target->Offset > targetFact->Bytes ||
        relocation->TargetOffset >= target->Bytes ||
        target->Bytes > targetFact->Bytes - target->Offset)
      return dynamic_fail(AppleAgxDynamicJobRange, Storage, storageBytes, Job);
    absoluteOffset = target->Offset + relocation->TargetOffset;
    if (!Resolve(CallbackContext, targetFact->AllocationToken,
                 targetFact->ClassId, absoluteOffset, 1u, &gpuAddress) ||
        gpuAddress == 0ULL || gpuAddress >= DYNAMIC_40_BIT_LIMIT)
      return dynamic_fail(AppleAgxDynamicJobResolve, Storage, storageBytes,
                          Job);
    resolved = &Job->Relocations[index];
    if (!dynamic_patch(storage + destination->StorageOffset +
                           (APPLE_AGX_U32)relocation->DestinationOffset,
                       relocation, gpuAddress, ShaderBase,
                       &resolved->EncodedValue))
      return dynamic_fail(AppleAgxDynamicJobRelocation, Storage, storageBytes,
                          Job);
    resolved->Kind = relocation->Kind;
    resolved->DestinationReference = relocation->DestinationReference;
    resolved->TargetReference = relocation->TargetReference;
    resolved->DestinationOffset = relocation->DestinationOffset;
    resolved->ResolvedAddress = gpuAddress;
  }

  Job->Magic = APPLE_AGX_DYNAMIC_JOB_MAGIC;
  Job->Version = APPLE_AGX_DYNAMIC_JOB_VERSION;
  Job->Generation = View->Header->Generation;
  Job->RelocationCount = View->Draw->RelocationCount;
  Job->StorageBytes = storageBytes;
  Job->MaterializedHash = dynamic_hash(Storage, storageBytes);
  return AppleAgxDynamicJobSuccess;
}
