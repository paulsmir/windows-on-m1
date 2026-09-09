#include "render_dynamic_overlay.h"

#define OVERLAY_NULL ((void *)0)
#define OVERLAY_ENCODER_OBJECT 71u
#define OVERLAY_PIPELINE_OBJECT 73u
#define OVERLAY_VERTEX_OBJECT 73u
#define OVERLAY_SHADER_OBJECT 73u
#define OVERLAY_RODATA_OBJECT 74u
#define OVERLAY_DESCRIPTOR_OBJECT 36u
#define OVERLAY_SCISSOR_OBJECT 38u
#define OVERLAY_DEPTH_BIAS_OBJECT 39u
#define OVERLAY_TA_WORK_OBJECT 19u
#define OVERLAY_CAPTURED_ENCODER_OBJECT 37u
#define OVERLAY_TA_ENCODER_OFFSET 0xd0u
#define OVERLAY_PIPELINE_COMPACT_SPLIT 0x40u
#define OVERLAY_PIPELINE_NATIVE_SPLIT 0x1000u

typedef struct _ADMISSION_DYNAMIC_OVERLAY_LOCATION {
  APPLE_AGX_U32 ObjectIndex;
  APPLE_AGX_U32 ObjectOffset;
  APPLE_AGX_U32 Capacity;
  APPLE_AGX_BOOL OriginalGpuAddress;
} ADMISSION_DYNAMIC_OVERLAY_LOCATION;

static void overlay_zero(void *Data, APPLE_AGX_U32 Bytes) {
  unsigned char *data = (unsigned char *)Data;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    data[index] = 0u;
}

static void overlay_copy(void *Destination, const void *Source,
                         APPLE_AGX_U32 Bytes) {
  unsigned char *destination = (unsigned char *)Destination;
  const unsigned char *source = (const unsigned char *)Source;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    destination[index] = source[index];
}

static int overlay_equal(const void *Left, const void *Right,
                         APPLE_AGX_U32 Bytes) {
  const unsigned char *left = (const unsigned char *)Left;
  const unsigned char *right = (const unsigned char *)Right;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    if (left[index] != right[index])
      return 0;
  return 1;
}

static int overlay_is_zero(const void *Data, APPLE_AGX_U32 Bytes) {
  const unsigned char *data = (const unsigned char *)Data;
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    if (data[index] != 0u)
      return 0;
  return 1;
}

static APPLE_AGX_U64 overlay_read_u64(const unsigned char *Data) {
  APPLE_AGX_U64 value = 0ULL;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    value |= (APPLE_AGX_U64)Data[index] << (index * 8u);
  return value;
}

static void overlay_write_u64(unsigned char *Data, APPLE_AGX_U64 Value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    Data[index] = (unsigned char)(Value >> (index * 8u));
}

static int overlay_location(APPLE_AGX_U32 ReferenceIndex,
                            const APPLE_AGX_WIN32_DRAW_PAYLOAD *Draw,
                            ADMISSION_DYNAMIC_OVERLAY_LOCATION *Location,
                            APPLE_AGX_U32 *ExpectedRole) {
  if (Draw == OVERLAY_NULL || Location == OVERLAY_NULL ||
      ExpectedRole == OVERLAY_NULL)
    return 0;
  overlay_zero(Location, (APPLE_AGX_U32)sizeof(*Location));
  *ExpectedRole = 0u;
  if (ReferenceIndex == Draw->VertexReference) {
    *ExpectedRole = AppleAgxWin32RoleVertex;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_VERTEX_OBJECT, 0x20000u, 0x10000u, APPLE_AGX_TRUE};
  } else if (ReferenceIndex == Draw->VertexShaderReference) {
    *ExpectedRole = AppleAgxWin32RoleShader;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_SHADER_OBJECT, 0x4000u, 0x4000u, APPLE_AGX_TRUE};
  } else if (ReferenceIndex == Draw->FragmentShaderReference) {
    *ExpectedRole = AppleAgxWin32RoleShader;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_SHADER_OBJECT, 0x8000u, 0x4000u, APPLE_AGX_TRUE};
  } else if (Draw->VertexRodataReference !=
                 APPLE_AGX_WIN32_OPTIONAL_REFERENCE &&
             ReferenceIndex == Draw->VertexRodataReference) {
    *ExpectedRole = AppleAgxWin32RoleShaderRodata;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_RODATA_OBJECT, 0x3000u, 0x400u, APPLE_AGX_TRUE};
  } else if (Draw->FragmentRodataReference !=
                 APPLE_AGX_WIN32_OPTIONAL_REFERENCE &&
             ReferenceIndex == Draw->FragmentRodataReference) {
    *ExpectedRole = AppleAgxWin32RoleShaderRodata;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_RODATA_OBJECT, 0x3400u, 0xc00u, APPLE_AGX_TRUE};
  } else if (ReferenceIndex == Draw->UscPipelineReference) {
    *ExpectedRole = AppleAgxWin32RoleUscPipeline;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_PIPELINE_OBJECT, 0x10000u, 0x10000u, APPLE_AGX_TRUE};
  } else if (ReferenceIndex == Draw->DescriptorReference) {
    *ExpectedRole = AppleAgxWin32RoleDescriptor;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_DESCRIPTOR_OBJECT, 0x8000u, 0x8000u, APPLE_AGX_FALSE};
  } else if (ReferenceIndex == Draw->ScissorReference) {
    *ExpectedRole = AppleAgxWin32RoleScissor;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_SCISSOR_OBJECT, 0u, 0x4000u, APPLE_AGX_FALSE};
  } else if (ReferenceIndex == Draw->DepthBiasReference) {
    *ExpectedRole = AppleAgxWin32RoleDepthBias;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_DEPTH_BIAS_OBJECT, 0u, 0x4000u, APPLE_AGX_FALSE};
  } else if (ReferenceIndex == Draw->EncoderReference) {
    *ExpectedRole = AppleAgxWin32RoleEncoder;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_ENCODER_OBJECT, 0u, 0x180u, APPLE_AGX_FALSE};
  } else {
    return 0;
  }
  return 1;
}

static ADMISSION_DYNAMIC_OVERLAY_RESULT overlay_add(
    const ADMISSION_BACKEND_IMAGE *Image,
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    APPLE_AGX_U32 ReferenceIndex, ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts =
      AppleAgxRenderTemplateObjectLayouts();
  ADMISSION_DYNAMIC_OVERLAY_LOCATION location;
  const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *reference;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *object;
  ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry;
  APPLE_AGX_U32 expectedRole;
  APPLE_AGX_U64 base;
  APPLE_AGX_U32 index;
  if (Image == OVERLAY_NULL || View == OVERLAY_NULL ||
      View->Header == OVERLAY_NULL || View->References == OVERLAY_NULL ||
      View->Draw == OVERLAY_NULL || Plan == OVERLAY_NULL || layouts == OVERLAY_NULL ||
      ReferenceIndex >= View->Header->ReferenceCount ||
      !overlay_location(ReferenceIndex, View->Draw, &location,
                        &expectedRole) ||
      location.ObjectIndex >= APPLE_AGX_RENDER_TEMPLATE_OBJECT_COUNT ||
      Plan->EntryCount >= ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES)
    return AdmissionDynamicOverlayArgument;
  reference = &View->References[ReferenceIndex];
  object = &Image->Objects[location.ObjectIndex];
  if (reference->Role != expectedRole)
    return AdmissionDynamicOverlayLayout;
  if (reference->Bytes == 0ULL || reference->Bytes > location.Capacity ||
      reference->Bytes > 0xffffffffULL ||
      location.ObjectOffset > object->Size ||
      reference->Bytes > object->Size - location.ObjectOffset ||
      object->Data == OVERLAY_NULL)
    return AdmissionDynamicOverlayRange;
  if (expectedRole == AppleAgxWin32RoleUscPipeline &&
      reference->Bytes > OVERLAY_PIPELINE_COMPACT_SPLIT &&
      (location.ObjectOffset > object->Size ||
       OVERLAY_PIPELINE_NATIVE_SPLIT >
           object->Size - location.ObjectOffset ||
       reference->Bytes - OVERLAY_PIPELINE_COMPACT_SPLIT >
           object->Size - location.ObjectOffset -
               OVERLAY_PIPELINE_NATIVE_SPLIT))
    return AdmissionDynamicOverlayRange;
  base = location.OriginalGpuAddress ? layouts[location.ObjectIndex].OriginalGpuVa
                                     : object->GpuVa;
  if (base == 0ULL || base > ~0ULL - location.ObjectOffset ||
      base + location.ObjectOffset >= (1ULL << 40u))
    return AdmissionDynamicOverlayLayout;
  for (index = 0u; index < Plan->EntryCount; ++index) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *other = &Plan->Entries[index];
    if (other->ReferenceIndex == ReferenceIndex)
      return AdmissionDynamicOverlayLayout;
    if (other->ObjectIndex == location.ObjectIndex &&
        other->ObjectOffset < location.ObjectOffset + reference->Bytes &&
        location.ObjectOffset < other->ObjectOffset + other->Bytes)
      return AdmissionDynamicOverlayLayout;
  }
  entry = &Plan->Entries[Plan->EntryCount++];
  entry->ReferenceIndex = ReferenceIndex;
  entry->Role = reference->Role;
  entry->ObjectIndex = location.ObjectIndex;
  entry->ObjectOffset = location.ObjectOffset;
  entry->SourceOffset = reference->Offset;
  entry->Bytes = (APPLE_AGX_U32)reference->Bytes;
  entry->Reserved = 0u;
  entry->GpuVirtualAddress = base + location.ObjectOffset;
  return AdmissionDynamicOverlaySuccess;
}

void AdmissionDynamicOverlayStateInitialize(
    ADMISSION_DYNAMIC_OVERLAY_STATE *State) {
  if (State == OVERLAY_NULL)
    return;
  overlay_zero(State, (APPLE_AGX_U32)sizeof(*State));
  State->Magic = ADMISSION_DYNAMIC_OVERLAY_MAGIC;
  State->Version = ADMISSION_DYNAMIC_OVERLAY_VERSION;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayPlan(
    const ADMISSION_BACKEND_IMAGE *Image,
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan) {
  APPLE_AGX_U32 references[ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES];
  APPLE_AGX_U32 count = 0u;
  APPLE_AGX_U32 index;
  int includeVertex = 0;
  if (Plan != OVERLAY_NULL)
    overlay_zero(Plan, (APPLE_AGX_U32)sizeof(*Plan));
  if (Image == OVERLAY_NULL || View == OVERLAY_NULL ||
      View->Header == OVERLAY_NULL || View->References == OVERLAY_NULL ||
      View->Draw == OVERLAY_NULL || Plan == OVERLAY_NULL ||
      Image->Ready != APPLE_AGX_TRUE ||
      View->Header->Opcode != AppleAgxWin32OpcodeDraw ||
      View->Header->Generation == 0u)
    return AdmissionDynamicOverlayArgument;
  if (View->Relocations != OVERLAY_NULL) {
    for (index = 0u; index < View->Draw->RelocationCount; ++index)
      if (View->Relocations[index].TargetReference ==
          View->Draw->VertexReference) {
        includeVertex = 1;
        break;
      }
  }
#define ADD_REFERENCE(Value) references[count++] = (Value)
  if (includeVertex)
    ADD_REFERENCE(View->Draw->VertexReference);
  ADD_REFERENCE(View->Draw->VertexShaderReference);
  ADD_REFERENCE(View->Draw->FragmentShaderReference);
  if (View->Draw->VertexRodataReference !=
      APPLE_AGX_WIN32_OPTIONAL_REFERENCE)
    ADD_REFERENCE(View->Draw->VertexRodataReference);
  if (View->Draw->FragmentRodataReference !=
      APPLE_AGX_WIN32_OPTIONAL_REFERENCE)
    ADD_REFERENCE(View->Draw->FragmentRodataReference);
  ADD_REFERENCE(View->Draw->UscPipelineReference);
  ADD_REFERENCE(View->Draw->DescriptorReference);
  ADD_REFERENCE(View->Draw->ScissorReference);
  ADD_REFERENCE(View->Draw->DepthBiasReference);
  ADD_REFERENCE(View->Draw->EncoderReference);
#undef ADD_REFERENCE
  if (count > ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES)
    return AdmissionDynamicOverlayLayout;
  Plan->Magic = ADMISSION_DYNAMIC_OVERLAY_MAGIC;
  Plan->Version = ADMISSION_DYNAMIC_OVERLAY_VERSION;
  Plan->Generation = View->Header->Generation;
  for (index = 0u; index < count; ++index) {
    ADMISSION_DYNAMIC_OVERLAY_RESULT result =
        overlay_add(Image, View, references[index], Plan);
    if (result != AdmissionDynamicOverlaySuccess) {
      overlay_zero(Plan, (APPLE_AGX_U32)sizeof(*Plan));
      return result;
    }
  }
  return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayBindingsFromView(
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings) {
  if (Bindings != OVERLAY_NULL)
    overlay_zero(Bindings, (APPLE_AGX_U32)sizeof(*Bindings));
  if (View == OVERLAY_NULL || View->Header == OVERLAY_NULL ||
      View->Draw == OVERLAY_NULL || Bindings == OVERLAY_NULL ||
      View->Header->Opcode != AppleAgxWin32OpcodeDraw ||
      View->Header->ReferenceCount == 0u ||
      View->Header->ReferenceCount > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES)
    return AdmissionDynamicOverlayArgument;
  *Bindings = (ADMISSION_DYNAMIC_OVERLAY_BINDINGS){
      View->Draw->VertexReference,
      View->Draw->VertexShaderReference,
      View->Draw->FragmentShaderReference,
      View->Draw->VertexRodataReference,
      View->Draw->FragmentRodataReference,
      View->Draw->UscPipelineReference,
      View->Draw->DescriptorReference,
      View->Draw->ScissorReference,
      View->Draw->DepthBiasReference,
      View->Draw->EncoderReference};
  return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayPlanFromJob(
    const ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings,
    const APPLE_AGX_DYNAMIC_JOB *Job,
    ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan) {
  APPLE_AGX_WIN32_COMMAND_HEADER header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE
      references[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw;
  APPLE_AGX_WIN32_RELOCATION vertexRelocation;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  APPLE_AGX_U32 highest = 0u;
  APPLE_AGX_U32 index;
  int hasVertex = 0;
  ADMISSION_DYNAMIC_OVERLAY_RESULT result;
  if (Plan != OVERLAY_NULL)
    overlay_zero(Plan, (APPLE_AGX_U32)sizeof(*Plan));
  if (Image == OVERLAY_NULL || Bindings == OVERLAY_NULL ||
      Job == OVERLAY_NULL || Plan == OVERLAY_NULL ||
      Job->Magic != APPLE_AGX_DYNAMIC_JOB_MAGIC ||
      Job->Version != APPLE_AGX_DYNAMIC_JOB_VERSION ||
      Job->Generation == 0u || Job->ObjectCount == 0u ||
      Job->ObjectCount > ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES)
    return AdmissionDynamicOverlayArgument;
  overlay_zero(&header, (APPLE_AGX_U32)sizeof(header));
  overlay_zero(references, (APPLE_AGX_U32)sizeof(references));
  overlay_zero(&draw, (APPLE_AGX_U32)sizeof(draw));
  overlay_zero(&vertexRelocation,
               (APPLE_AGX_U32)sizeof(vertexRelocation));
  overlay_zero(&view, (APPLE_AGX_U32)sizeof(view));
  draw.VertexReference = Bindings->VertexReference;
  draw.VertexShaderReference = Bindings->VertexShaderReference;
  draw.FragmentShaderReference = Bindings->FragmentShaderReference;
  draw.VertexRodataReference = Bindings->VertexRodataReference;
  draw.FragmentRodataReference = Bindings->FragmentRodataReference;
  draw.UscPipelineReference = Bindings->UscPipelineReference;
  draw.DescriptorReference = Bindings->DescriptorReference;
  draw.ScissorReference = Bindings->ScissorReference;
  draw.DepthBiasReference = Bindings->DepthBiasReference;
  draw.EncoderReference = Bindings->EncoderReference;
  for (index = 0u; index < Job->ObjectCount; ++index) {
    const APPLE_AGX_DYNAMIC_JOB_OBJECT *object = &Job->Objects[index];
    if (object->ReferenceIndex >= APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
        object->Bytes == 0u || references[object->ReferenceIndex].Bytes != 0ULL)
      return AdmissionDynamicOverlayLayout;
    references[object->ReferenceIndex].Role = object->Role;
    references[object->ReferenceIndex].Bytes = object->Bytes;
    if (object->ReferenceIndex == Bindings->VertexReference &&
        object->Role == AppleAgxWin32RoleVertex)
      hasVertex = 1;
    if (object->ReferenceIndex > highest)
      highest = object->ReferenceIndex;
  }
  header.Opcode = AppleAgxWin32OpcodeDraw;
  header.Generation = Job->Generation;
  header.ReferenceCount = highest + 1u;
  view.Header = &header;
  view.References = references;
  view.Draw = &draw;
  if (hasVertex) {
    draw.RelocationCount = 1u;
    vertexRelocation.TargetReference = Bindings->VertexReference;
    view.Relocations = &vertexRelocation;
  }
  result = AdmissionDynamicOverlayPlan(Image, &view, Plan);
  if (result != AdmissionDynamicOverlaySuccess)
    return result;
  if (Plan->EntryCount != Job->ObjectCount) {
    overlay_zero(Plan, (APPLE_AGX_U32)sizeof(*Plan));
    return AdmissionDynamicOverlayLayout;
  }
  return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayResolve(
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    APPLE_AGX_U32 ReferenceIndex, APPLE_AGX_U64 ReferenceOffset,
    APPLE_AGX_U32 Bytes, APPLE_AGX_U64 *GpuVirtualAddress) {
  APPLE_AGX_U32 index;
  if (GpuVirtualAddress != OVERLAY_NULL)
    *GpuVirtualAddress = 0ULL;
  if (Plan == OVERLAY_NULL || GpuVirtualAddress == OVERLAY_NULL ||
      Plan->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      Plan->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      Plan->Generation == 0u ||
      Plan->EntryCount == 0u ||
      Plan->EntryCount > ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES || Bytes == 0u)
    return AdmissionDynamicOverlayArgument;
  for (index = 0u; index < Plan->EntryCount; ++index) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry = &Plan->Entries[index];
    APPLE_AGX_U64 relative;
    if (entry->ReferenceIndex != ReferenceIndex)
      continue;
    if (ReferenceOffset < entry->SourceOffset)
      return AdmissionDynamicOverlayRange;
    relative = ReferenceOffset - entry->SourceOffset;
    if (relative >= entry->Bytes || Bytes > entry->Bytes - relative ||
        entry->GpuVirtualAddress > ~0ULL - relative)
      return AdmissionDynamicOverlayRange;
    if (entry->Role == AppleAgxWin32RoleUscPipeline &&
        entry->Bytes > OVERLAY_PIPELINE_COMPACT_SPLIT) {
      if (relative < OVERLAY_PIPELINE_COMPACT_SPLIT) {
        if (Bytes > OVERLAY_PIPELINE_COMPACT_SPLIT - relative)
          return AdmissionDynamicOverlayRange;
      } else {
        relative = OVERLAY_PIPELINE_NATIVE_SPLIT +
                   relative - OVERLAY_PIPELINE_COMPACT_SPLIT;
        if (entry->GpuVirtualAddress > ~0ULL - relative)
          return AdmissionDynamicOverlayRange;
      }
    }
    *GpuVirtualAddress = entry->GpuVirtualAddress + relative;
    return AdmissionDynamicOverlaySuccess;
  }
  return AdmissionDynamicOverlayLayout;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayRouteEncoder(
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_U32 ActiveObjectCount) {
  const ADMISSION_DYNAMIC_OVERLAY_ENTRY *encoder = OVERLAY_NULL;
  APPLE_AGX_EXP208_RELOCATION_OBJECT *taWork;
  APPLE_AGX_U32 index;
  if (Plan == OVERLAY_NULL || ActiveObjects == OVERLAY_NULL ||
      Plan->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      Plan->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      Plan->Generation == 0u || Plan->EntryCount == 0u ||
      Plan->EntryCount > ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES ||
      ActiveObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)
    return AdmissionDynamicOverlayArgument;
  for (index = 0u; index < Plan->EntryCount; ++index) {
    if (Plan->Entries[index].Role != AppleAgxWin32RoleEncoder)
      continue;
    if (encoder != OVERLAY_NULL)
      return AdmissionDynamicOverlayLayout;
    encoder = &Plan->Entries[index];
  }
  if (encoder == OVERLAY_NULL ||
      encoder->ObjectIndex != OVERLAY_ENCODER_OBJECT ||
      encoder->ObjectOffset != 0u ||
      encoder->ObjectIndex >= ActiveObjectCount ||
      ActiveObjects[encoder->ObjectIndex].GpuVa == 0ULL ||
      ActiveObjects[encoder->ObjectIndex].GpuVa >
          ~0ULL - encoder->ObjectOffset ||
      encoder->GpuVirtualAddress !=
          ActiveObjects[encoder->ObjectIndex].GpuVa +
              encoder->ObjectOffset ||
      ActiveObjects[OVERLAY_CAPTURED_ENCODER_OBJECT].GpuVa == 0ULL)
    return AdmissionDynamicOverlayLayout;
  taWork = &ActiveObjects[OVERLAY_TA_WORK_OBJECT];
  if (taWork->Data == OVERLAY_NULL ||
      taWork->Size < OVERLAY_TA_ENCODER_OFFSET + 8u)
    return AdmissionDynamicOverlayRange;
  if (overlay_read_u64(taWork->Data + OVERLAY_TA_ENCODER_OFFSET) !=
      ActiveObjects[OVERLAY_CAPTURED_ENCODER_OBJECT].GpuVa)
    return AdmissionDynamicOverlayContent;
  overlay_write_u64(taWork->Data + OVERLAY_TA_ENCODER_OFFSET,
                    encoder->GpuVirtualAddress);
  return AdmissionDynamicOverlaySuccess;
}

static const APPLE_AGX_DYNAMIC_JOB_OBJECT *overlay_job_object(
    const APPLE_AGX_DYNAMIC_JOB *Job, APPLE_AGX_U32 ReferenceIndex) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < Job->ObjectCount; ++index)
    if (Job->Objects[index].ReferenceIndex == ReferenceIndex)
      return &Job->Objects[index];
  return OVERLAY_NULL;
}

static ADMISSION_DYNAMIC_OVERLAY_RESULT overlay_validate_content(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    const APPLE_AGX_DYNAMIC_JOB *Job, const void *Storage,
    APPLE_AGX_U32 StorageBytes, int ExpectZero) {
  const unsigned char *storage = (const unsigned char *)Storage;
  APPLE_AGX_U32 index;
  if (Image == OVERLAY_NULL || Plan == OVERLAY_NULL || Job == OVERLAY_NULL ||
      Storage == OVERLAY_NULL ||
      Plan->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      Plan->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      Job->Magic != APPLE_AGX_DYNAMIC_JOB_MAGIC ||
      Job->Version != APPLE_AGX_DYNAMIC_JOB_VERSION ||
      Plan->Generation != Job->Generation ||
      Plan->EntryCount != Job->ObjectCount ||
      StorageBytes < Job->StorageBytes)
    return AdmissionDynamicOverlayArgument;
  for (index = 0u; index < Plan->EntryCount; ++index) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry = &Plan->Entries[index];
    const APPLE_AGX_DYNAMIC_JOB_OBJECT *jobObject =
        overlay_job_object(Job, entry->ReferenceIndex);
    APPLE_AGX_EXP208_RELOCATION_OBJECT *target;
    const unsigned char *source;
    unsigned char *destination;
    if (jobObject == OVERLAY_NULL || jobObject->Role != entry->Role ||
        jobObject->Bytes != entry->Bytes ||
        jobObject->StorageOffset > Job->StorageBytes ||
        jobObject->Bytes > Job->StorageBytes - jobObject->StorageOffset ||
        entry->ObjectIndex >= APPLE_AGX_RENDER_TEMPLATE_OBJECT_COUNT)
      return AdmissionDynamicOverlayLayout;
    target = &Image->Objects[entry->ObjectIndex];
    if (target->Data == OVERLAY_NULL || entry->ObjectOffset > target->Size ||
        entry->Bytes > target->Size - entry->ObjectOffset)
      return AdmissionDynamicOverlayRange;
    source = storage + jobObject->StorageOffset;
    destination = target->Data + entry->ObjectOffset;
    if (entry->Role == AppleAgxWin32RoleUscPipeline &&
        entry->Bytes > OVERLAY_PIPELINE_COMPACT_SPLIT) {
      APPLE_AGX_U32 tail =
          entry->Bytes - OVERLAY_PIPELINE_COMPACT_SPLIT;
      unsigned char *tailDestination =
          destination + OVERLAY_PIPELINE_NATIVE_SPLIT;
      if (OVERLAY_PIPELINE_NATIVE_SPLIT >
              target->Size - entry->ObjectOffset ||
          tail > target->Size - entry->ObjectOffset -
                     OVERLAY_PIPELINE_NATIVE_SPLIT)
        return AdmissionDynamicOverlayRange;
      if (ExpectZero
              ? (!overlay_is_zero(
                     destination, OVERLAY_PIPELINE_COMPACT_SPLIT) ||
                 !overlay_is_zero(tailDestination, tail))
              : (!overlay_equal(
                     destination, source,
                     OVERLAY_PIPELINE_COMPACT_SPLIT) ||
                 !overlay_equal(
                     tailDestination,
                     source + OVERLAY_PIPELINE_COMPACT_SPLIT, tail)))
        return ExpectZero ? AdmissionDynamicOverlayOccupied
                          : AdmissionDynamicOverlayContent;
    } else if (ExpectZero ? !overlay_is_zero(destination, entry->Bytes)
                          : !overlay_equal(destination, source,
                                           entry->Bytes)) {
      return ExpectZero ? AdmissionDynamicOverlayOccupied
                        : AdmissionDynamicOverlayContent;
    }
  }
  return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayApply(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    const APPLE_AGX_DYNAMIC_JOB *Job, const void *Storage,
    APPLE_AGX_U32 StorageBytes, APPLE_AGX_U32 Fence,
    ADMISSION_DYNAMIC_OVERLAY_STATE *State) {
  const unsigned char *storage = (const unsigned char *)Storage;
  APPLE_AGX_U32 index;
  ADMISSION_DYNAMIC_OVERLAY_RESULT result;
  if (State == OVERLAY_NULL || Fence == 0u ||
      State->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      State->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION || State->Applied)
    return AdmissionDynamicOverlayState;
  result = overlay_validate_content(Image, Plan, Job, Storage, StorageBytes, 1);
  if (result != AdmissionDynamicOverlaySuccess)
    return result;
  for (index = 0u; index < Plan->EntryCount; ++index) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry = &Plan->Entries[index];
    const APPLE_AGX_DYNAMIC_JOB_OBJECT *jobObject =
        overlay_job_object(Job, entry->ReferenceIndex);
    if (entry->Role == AppleAgxWin32RoleUscPipeline &&
        entry->Bytes > OVERLAY_PIPELINE_COMPACT_SPLIT) {
      overlay_copy(
          Image->Objects[entry->ObjectIndex].Data + entry->ObjectOffset,
          storage + jobObject->StorageOffset,
          OVERLAY_PIPELINE_COMPACT_SPLIT);
      overlay_copy(
          Image->Objects[entry->ObjectIndex].Data + entry->ObjectOffset +
              OVERLAY_PIPELINE_NATIVE_SPLIT,
          storage + jobObject->StorageOffset +
              OVERLAY_PIPELINE_COMPACT_SPLIT,
          entry->Bytes - OVERLAY_PIPELINE_COMPACT_SPLIT);
    } else {
      overlay_copy(
          Image->Objects[entry->ObjectIndex].Data + entry->ObjectOffset,
          storage + jobObject->StorageOffset, entry->Bytes);
    }
  }
  State->Applied = 1u;
  State->Fence = Fence;
  State->Generation = Plan->Generation;
  State->EntryCount = Plan->EntryCount;
  State->MaterializedHash = Job->MaterializedHash;
  return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayRelease(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    const APPLE_AGX_DYNAMIC_JOB *Job, const void *Storage,
    APPLE_AGX_U32 StorageBytes, APPLE_AGX_U32 Fence,
    ADMISSION_DYNAMIC_OVERLAY_STATE *State) {
  APPLE_AGX_U32 index;
  ADMISSION_DYNAMIC_OVERLAY_RESULT result;
  if (State == OVERLAY_NULL || Fence == 0u ||
      State->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      State->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      State->Applied != 1u || State->Fence != Fence ||
      State->Generation != Plan->Generation ||
      State->EntryCount != Plan->EntryCount ||
      State->MaterializedHash != Job->MaterializedHash)
    return AdmissionDynamicOverlayState;
  result = overlay_validate_content(Image, Plan, Job, Storage, StorageBytes, 0);
  if (result != AdmissionDynamicOverlaySuccess)
    return result;
  for (index = 0u; index < Plan->EntryCount; ++index) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry = &Plan->Entries[index];
    if (entry->Role == AppleAgxWin32RoleUscPipeline &&
        entry->Bytes > OVERLAY_PIPELINE_COMPACT_SPLIT) {
      overlay_zero(
          Image->Objects[entry->ObjectIndex].Data + entry->ObjectOffset,
          OVERLAY_PIPELINE_COMPACT_SPLIT);
      overlay_zero(
          Image->Objects[entry->ObjectIndex].Data + entry->ObjectOffset +
              OVERLAY_PIPELINE_NATIVE_SPLIT,
          entry->Bytes - OVERLAY_PIPELINE_COMPACT_SPLIT);
    } else {
      overlay_zero(
          Image->Objects[entry->ObjectIndex].Data + entry->ObjectOffset,
          entry->Bytes);
    }
  }
  AdmissionDynamicOverlayStateInitialize(State);
  return AdmissionDynamicOverlaySuccess;
}

#undef OVERLAY_NULL
