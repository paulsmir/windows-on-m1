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
#define OVERLAY_VERTEX_SHADER_GPU_VA 0x1100064000ULL
#define OVERLAY_FRAGMENT_SHADER_GPU_VA 0x110006c000ULL

static const ADMISSION_DYNAMIC_OVERLAY_ALIAS OverlayShaderAliases[
    ADMISSION_DYNAMIC_OVERLAY_SHADER_ALIAS_COUNT] = {
    {OVERLAY_SHADER_OBJECT, 0x10000u, 0x4000u, 0u,
     OVERLAY_VERTEX_SHADER_GPU_VA},
    {OVERLAY_SHADER_OBJECT, 0x14000u, 0x4000u, 0u,
     OVERLAY_FRAGMENT_SHADER_GPU_VA},
};

typedef struct _ADMISSION_DYNAMIC_OVERLAY_LOCATION {
  APPLE_AGX_U32 ObjectIndex;
  APPLE_AGX_U32 ObjectOffset;
  APPLE_AGX_U32 Capacity;
  APPLE_AGX_BOOL OriginalGpuAddress;
  APPLE_AGX_U64 FixedGpuVirtualAddress;
} ADMISSION_DYNAMIC_OVERLAY_LOCATION;

const ADMISSION_DYNAMIC_OVERLAY_ALIAS *AdmissionDynamicOverlayShaderAliases(
    APPLE_AGX_U32 *Count) {
  if (Count == OVERLAY_NULL)
    return OVERLAY_NULL;
  *Count = ADMISSION_DYNAMIC_OVERLAY_SHADER_ALIAS_COUNT;
  return OverlayShaderAliases;
}

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

static APPLE_AGX_U64 overlay_hash(const void *Data, APPLE_AGX_U32 Bytes) {
  const unsigned char *data = (const unsigned char *)Data;
  APPLE_AGX_U64 hash = 14695981039346656037ULL;
  APPLE_AGX_U32 index;
  if (Data == OVERLAY_NULL || Bytes == 0u)
    return 0ULL;
  for (index = 0u; index < Bytes; ++index) {
    hash ^= data[index];
    hash *= 1099511628211ULL;
  }
  return hash;
}

static APPLE_AGX_U64 overlay_read_u64(const unsigned char *Data) {
  APPLE_AGX_U64 value = 0ULL;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    value |= (APPLE_AGX_U64)Data[index] << (index * 8u);
  return value;
}

static APPLE_AGX_U32 overlay_read_u32(const unsigned char *Data) {
  APPLE_AGX_U32 value = 0u;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 4u; ++index)
    value |= (APPLE_AGX_U32)Data[index] << (index * 8u);
  return value;
}

static void overlay_write_u64(unsigned char *Data, APPLE_AGX_U64 Value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    Data[index] = (unsigned char)(Value >> (index * 8u));
}
static void overlay_write_u32(unsigned char *Data, APPLE_AGX_U32 Value) {
  APPLE_AGX_U32 i;
  for(i=0;i<4u;++i) Data[i]=(unsigned char)(Value>>(i*8u));
}

/* Native objects occupy only the already mapped, zero-owned windows used by
 * the legacy overlay. No new GPU address or backing allocation is created. */
static ADMISSION_DYNAMIC_OVERLAY_RESULT overlay_native_add(
    const ADMISSION_BACKEND_IMAGE *image, APPLE_AGX_U32 reference,
    APPLE_AGX_U32 role, APPLE_AGX_U64 sourceOffset, APPLE_AGX_U64 bytes,
    int low, APPLE_AGX_U32 *lowUsed, APPLE_AGX_U32 *generalUsed,
    ADMISSION_DYNAMIC_OVERLAY_PLAN *plan) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts = AppleAgxRenderTemplateObjectLayouts();
  APPLE_AGX_U32 object, offset, capacity;
  APPLE_AGX_U64 base;
  ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry;
  if (!bytes || bytes > 0xffffffffULL || !layouts ||
      plan->EntryCount >= ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES)
    return AdmissionDynamicOverlayRange;
  if (role == AppleAgxWin32RoleEncoder) {
    object = OVERLAY_ENCODER_OBJECT; offset = 0; capacity = 0x180u;
  } else if (role == AppleAgxWin32RoleScissor) {
    object = OVERLAY_SCISSOR_OBJECT; offset = 0; capacity = 0x4000u;
  } else if (role == AppleAgxWin32RoleDepthBias) {
    object = OVERLAY_DEPTH_BIAS_OBJECT; offset = 0; capacity = 0x4000u;
  } else if (low) {
    APPLE_AGX_U32 aligned = (*lowUsed + 255u) & ~255u;
    if (aligned < *lowUsed || aligned > 0x10000u || bytes > 0x10000u - aligned)
      return AdmissionDynamicOverlayRange;
    object = OVERLAY_PIPELINE_OBJECT; offset = 0x20000u + aligned;
    capacity = 0x10000u - aligned; *lowUsed = aligned + (APPLE_AGX_U32)bytes;
  } else {
    APPLE_AGX_U32 aligned = (*generalUsed + 255u) & ~255u;
    if (aligned < *generalUsed || aligned > 0x8000u || bytes > 0x8000u - aligned)
      return AdmissionDynamicOverlayRange;
    object = OVERLAY_DESCRIPTOR_OBJECT; offset = 0x8000u + aligned;
    capacity = 0x8000u - aligned; *generalUsed = aligned + (APPLE_AGX_U32)bytes;
  }
  if (bytes > capacity || !image->Objects[object].Data ||
      offset > image->Objects[object].Size || bytes > image->Objects[object].Size - offset)
    return AdmissionDynamicOverlayRange;
  base = low ? layouts[object].OriginalGpuVa : image->Objects[object].GpuVa;
  if (!base || base > ((1ULL << 40) - 1) - offset || bytes > (1ULL << 40) - base - offset)
    return AdmissionDynamicOverlayRange;
  entry = &plan->Entries[plan->EntryCount++];
  *entry = (ADMISSION_DYNAMIC_OVERLAY_ENTRY){reference, role, object, offset,
      sourceOffset, (APPLE_AGX_U32)bytes, 0, base + offset};
  return AdmissionDynamicOverlaySuccess;
}

static int overlay_native_copied(APPLE_AGX_U32 role) {
  return role == AppleAgxWin32RoleVertex || role == AppleAgxWin32RoleConstant ||
      role == AppleAgxWin32RoleUniform || role == AppleAgxWin32RoleShader ||
      role == AppleAgxWin32RoleShaderRodata || role == AppleAgxWin32RoleUscPipeline ||
      role == AppleAgxWin32RoleDescriptor || role == AppleAgxWin32RolePppState ||
      role == AppleAgxWin32RoleEncoder || role == AppleAgxWin32RoleScissor ||
      role == AppleAgxWin32RoleDepthBias;
}

static ADMISSION_DYNAMIC_OVERLAY_RESULT overlay_native_plan_view(
    const ADMISSION_BACKEND_IMAGE *image, const APPLE_AGX_WIN32_COMMAND_VIEW *view,
    ADMISSION_DYNAMIC_OVERLAY_PLAN *plan) {
  APPLE_AGX_U32 lowUsed = 0, generalUsed = 0, i, j;
  if (!view->NativeBatch || !view->Relocations || !view->References ||
      !view->Header->ReferenceCount || view->Header->ReferenceCount > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES)
    return AdmissionDynamicOverlayArgument;
  plan->Magic=ADMISSION_DYNAMIC_OVERLAY_MAGIC; plan->Version=ADMISSION_DYNAMIC_OVERLAY_VERSION;
  plan->Generation=view->Header->Generation; plan->CommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
  for (i=0;i<view->Header->ReferenceCount;++i) {
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *r=&view->References[i];
    int low=r->Role==AppleAgxWin32RoleShader || r->Role==AppleAgxWin32RoleShaderRodata ||
        r->Role==AppleAgxWin32RoleUscPipeline;
    ADMISSION_DYNAMIC_OVERLAY_RESULT result;
    if (r->Role==AppleAgxWin32RoleRenderTarget) continue;
    if (!overlay_native_copied(r->Role)) return AdmissionDynamicOverlayLayout;
    for (j=0;j<view->Draw->RelocationCount;++j)
      if (view->Relocations[j].Kind==AppleAgxWin32RelocationPppCfBindingsOffset32 &&
          view->Relocations[j].TargetReference==i) low=1;
    result=overlay_native_add(image,i,r->Role,r->Offset,r->Bytes,low,&lowUsed,&generalUsed,plan);
    if (result!=AdmissionDynamicOverlaySuccess) return result;
  }
  return AdmissionDynamicOverlaySuccess;
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
        OVERLAY_VERTEX_OBJECT, 0x20000u, 0x10000u, APPLE_AGX_TRUE, 0ULL};
  } else if (ReferenceIndex == Draw->VertexShaderReference) {
    *ExpectedRole = AppleAgxWin32RoleShader;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_SHADER_OBJECT, 0x10000u, 0x4000u, APPLE_AGX_TRUE,
        OVERLAY_VERTEX_SHADER_GPU_VA};
  } else if (ReferenceIndex == Draw->FragmentShaderReference) {
    *ExpectedRole = AppleAgxWin32RoleShader;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_SHADER_OBJECT, 0x14000u, 0x4000u, APPLE_AGX_TRUE,
        OVERLAY_FRAGMENT_SHADER_GPU_VA};
  } else if (Draw->VertexRodataReference !=
                 APPLE_AGX_WIN32_OPTIONAL_REFERENCE &&
             ReferenceIndex == Draw->VertexRodataReference) {
    *ExpectedRole = AppleAgxWin32RoleShaderRodata;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_RODATA_OBJECT, 0x3000u, 0x400u, APPLE_AGX_TRUE, 0ULL};
  } else if (Draw->FragmentRodataReference !=
                 APPLE_AGX_WIN32_OPTIONAL_REFERENCE &&
             ReferenceIndex == Draw->FragmentRodataReference) {
    *ExpectedRole = AppleAgxWin32RoleShaderRodata;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_RODATA_OBJECT, 0x3400u, 0xc00u, APPLE_AGX_TRUE, 0ULL};
  } else if (ReferenceIndex == Draw->UscPipelineReference) {
    *ExpectedRole = AppleAgxWin32RoleUscPipeline;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_PIPELINE_OBJECT, 0u, 0x2000u, APPLE_AGX_TRUE, 0ULL};
  } else if (ReferenceIndex == Draw->DescriptorReference) {
    *ExpectedRole = AppleAgxWin32RoleDescriptor;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_DESCRIPTOR_OBJECT, 0x8000u, 0x8000u, APPLE_AGX_FALSE, 0ULL};
  } else if (ReferenceIndex == Draw->ScissorReference) {
    *ExpectedRole = AppleAgxWin32RoleScissor;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_SCISSOR_OBJECT, 0u, 0x4000u, APPLE_AGX_FALSE, 0ULL};
  } else if (ReferenceIndex == Draw->DepthBiasReference) {
    *ExpectedRole = AppleAgxWin32RoleDepthBias;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_DEPTH_BIAS_OBJECT, 0u, 0x4000u, APPLE_AGX_FALSE, 0ULL};
  } else if (ReferenceIndex == Draw->EncoderReference) {
    *ExpectedRole = AppleAgxWin32RoleEncoder;
    *Location = (ADMISSION_DYNAMIC_OVERLAY_LOCATION){
        OVERLAY_ENCODER_OBJECT, 0u, 0x180u, APPLE_AGX_FALSE, 0ULL};
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
  base = location.FixedGpuVirtualAddress != 0ULL
             ? location.FixedGpuVirtualAddress
             : (location.OriginalGpuAddress
                    ? layouts[location.ObjectIndex].OriginalGpuVa
                    : object->GpuVa);
  if (base == 0ULL ||
      (location.FixedGpuVirtualAddress == 0ULL &&
       base > ~0ULL - location.ObjectOffset) ||
      (location.FixedGpuVirtualAddress != 0ULL
           ? base
           : base + location.ObjectOffset) >= (1ULL << 40u))
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
  entry->GpuVirtualAddress = location.FixedGpuVirtualAddress != 0ULL
                                 ? base
                                 : base + location.ObjectOffset;
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
      (View->Header->Version != APPLE_AGX_WIN32_COMMAND_VERSION &&
       View->Header->Version != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH) ||
      View->Header->Generation == 0u)
    return AdmissionDynamicOverlayArgument;
  if (View->Header->Version == APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH) {
    ADMISSION_DYNAMIC_OVERLAY_RESULT result=overlay_native_plan_view(Image,View,Plan);
    if(result!=AdmissionDynamicOverlaySuccess) overlay_zero(Plan,sizeof(*Plan));
    return result;
  }
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
  if (count > ADMISSION_DYNAMIC_OVERLAY_LEGACY_MAX_ENTRIES)
    return AdmissionDynamicOverlayLayout;
  Plan->Magic = ADMISSION_DYNAMIC_OVERLAY_MAGIC;
  Plan->Version = ADMISSION_DYNAMIC_OVERLAY_VERSION;
  Plan->Generation = View->Header->Generation;
  Plan->CommandVersion = View->Header->Version;
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
      (View->Header->Version != APPLE_AGX_WIN32_COMMAND_VERSION &&
       View->Header->Version != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH) ||
      View->Header->ReferenceCount == 0u ||
      View->Header->ReferenceCount > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES)
    return AdmissionDynamicOverlayArgument;
  Bindings->VertexReference=View->Draw->VertexReference;
  Bindings->VertexShaderReference=View->Draw->VertexShaderReference;
  Bindings->FragmentShaderReference=View->Draw->FragmentShaderReference;
  Bindings->VertexRodataReference=View->Draw->VertexRodataReference;
  Bindings->FragmentRodataReference=View->Draw->FragmentRodataReference;
  Bindings->UscPipelineReference=View->Draw->UscPipelineReference;
  Bindings->DescriptorReference=View->Draw->DescriptorReference;
  Bindings->ScissorReference=View->Draw->ScissorReference;
  Bindings->DepthBiasReference=View->Draw->DepthBiasReference;
  Bindings->EncoderReference=View->Draw->EncoderReference;
  Bindings->CommandVersion=View->Header->Version;
  Bindings->DestinationReference=View->Draw->DestinationReference;
  Bindings->SurfaceWidth=View->Draw->SurfaceWidth;
  Bindings->SurfaceHeight=View->Draw->SurfaceHeight;
  Bindings->SurfacePitch=View->Draw->SurfacePitch;
  if (View->References && View->Draw->DestinationReference<View->Header->ReferenceCount)
    Bindings->DestinationBytes=View->References[View->Draw->DestinationReference].Bytes;
  if (View->Header->Version==APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH) {
    if (!View->NativeBatch) return AdmissionDynamicOverlayArgument;
    Bindings->NativeBatch=*View->NativeBatch;
  }
  return AdmissionDynamicOverlaySuccess;
}

static ADMISSION_DYNAMIC_OVERLAY_RESULT overlay_legacy_plan_from_job(
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
      Job->ObjectCount > ADMISSION_DYNAMIC_OVERLAY_LEGACY_MAX_ENTRIES)
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
  header.Version = APPLE_AGX_WIN32_COMMAND_VERSION;
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

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayPlanFromJob(
    const ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings,
    const APPLE_AGX_DYNAMIC_JOB *Job, ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan) {
  APPLE_AGX_U32 lowUsed=0,generalUsed=0,i,j;
  if (!Bindings || Bindings->CommandVersion!=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH)
    return overlay_legacy_plan_from_job(Image,Bindings,Job,Plan);
  if (!Image || !Job || !Plan || Image->Ready!=APPLE_AGX_TRUE ||
      Job->Magic!=APPLE_AGX_DYNAMIC_JOB_MAGIC || Job->Version!=APPLE_AGX_DYNAMIC_JOB_VERSION ||
      !Job->Generation || !Job->ObjectCount || Job->ObjectCount>ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES ||
      !Job->RelocationCount || Job->RelocationCount>APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS)
    return AdmissionDynamicOverlayArgument;
  overlay_zero(Plan,sizeof(*Plan));
  Plan->Magic=ADMISSION_DYNAMIC_OVERLAY_MAGIC; Plan->Version=ADMISSION_DYNAMIC_OVERLAY_VERSION;
  Plan->Generation=Job->Generation; Plan->CommandVersion=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
  for(i=0;i<Job->ObjectCount;++i) {
    const APPLE_AGX_DYNAMIC_JOB_OBJECT *r=&Job->Objects[i];
    int low=r->Role==AppleAgxWin32RoleShader || r->Role==AppleAgxWin32RoleShaderRodata ||
        r->Role==AppleAgxWin32RoleUscPipeline;
    ADMISSION_DYNAMIC_OVERLAY_RESULT result;
    if (!overlay_native_copied(r->Role) || r->ReferenceIndex>=APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
        (i && r->ReferenceIndex<=Job->Objects[i-1].ReferenceIndex)) return AdmissionDynamicOverlayLayout;
    for(j=0;j<Job->RelocationCount;++j)
      if(Job->Relocations[j].Kind==AppleAgxWin32RelocationPppCfBindingsOffset32 &&
          Job->Relocations[j].TargetReference==r->ReferenceIndex) low=1;
    result=overlay_native_add(Image,r->ReferenceIndex,r->Role,0,r->Bytes,low,&lowUsed,&generalUsed,Plan);
    if(result!=AdmissionDynamicOverlaySuccess) return result;
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
    if (Plan->CommandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH &&
        entry->Role == AppleAgxWin32RoleUscPipeline &&
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

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayRouteNative(
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects, APPLE_AGX_U32 Count) {
  const APPLE_AGX_WIN32_NATIVE_BATCH_METADATA *n;
  const APPLE_AGX_WIN32_NATIVE_PIPELINE_ROOT *roots[3];
  APPLE_AGX_U32 pipeline[3],i,j,blocks,tileConfig,utile;
  APPLE_AGX_U64 scissor=0,dbias=0;
  unsigned char *work,*ta,*micro;
  if(!Plan || !Bindings || !Objects || Count<APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      Plan->Magic!=ADMISSION_DYNAMIC_OVERLAY_MAGIC || Plan->Version!=ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      !Plan->Generation || !Plan->EntryCount || Plan->EntryCount>ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES ||
      Plan->CommandVersion!=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH ||
      Bindings->CommandVersion!=Plan->CommandVersion ||
      Bindings->SurfaceWidth!=16u || Bindings->SurfaceHeight!=16u || Bindings->SurfacePitch!=64u ||
      !Bindings->DestinationBytes || Bindings->DestinationBytes>0xffffffffULL-127ULL)
    return AdmissionDynamicOverlayArgument;
  n=&Bindings->NativeBatch;
  /* First native producer shares the proven one-tile geometry. The current
   * Asahi tilebuffer contract derives the sample/utile scalars; no old shader
   * or pipeline payload is substituted. */
  if(n->StructBytes!=sizeof(*n) || n->Samples!=1 || n->Layers!=1 ||
      (n->SampleSizeBytes!=8 && n->SampleSizeBytes!=16) ||
      n->UtileWidth!=32 || n->UtileHeight!=32 || n->PppControl!=0x202u ||
      n->PppMultisampleControl!=0x88u ||
      (n->RenderFlags&~APPLE_AGX_WIN32_NATIVE_RENDER_PROCESS_EMPTY_TILES))
    return AdmissionDynamicOverlayLayout;
  roots[0]=&n->Background; roots[1]=&n->PartialBackground; roots[2]=&n->EndOfTile;
  for(i=0;i<3u;++i) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry=OVERLAY_NULL;
    for(j=0;j<Plan->EntryCount;++j)
      if(Plan->Entries[j].ReferenceIndex==roots[i]->UscReference) entry=&Plan->Entries[j];
    if(!entry || entry->Role!=AppleAgxWin32RoleUscPipeline || roots[i]->UscFlags!=4u ||
        entry->GpuVirtualAddress<0x1100000000ULL ||
        entry->GpuVirtualAddress-0x1100000000ULL>0xffffffffULL ||
        ((entry->GpuVirtualAddress-0x1100000000ULL)&63ULL)) return AdmissionDynamicOverlayLayout;
    pipeline[i]=(APPLE_AGX_U32)(entry->GpuVirtualAddress-0x1100000000ULL)|roots[i]->UscFlags;
  }
  for(i=0;i<Plan->EntryCount;++i) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *e=&Plan->Entries[i];
    if(e->ReferenceIndex==Bindings->ScissorReference && e->Role==AppleAgxWin32RoleScissor)
      scissor=e->GpuVirtualAddress;
    if(e->ReferenceIndex==Bindings->DepthBiasReference && e->Role==AppleAgxWin32RoleDepthBias)
      dbias=e->GpuVirtualAddress;
  }
  if(!scissor || (Bindings->DepthBiasReference!=APPLE_AGX_WIN32_OPTIONAL_REFERENCE && !dbias) ||
      !Objects[18].Data || Objects[18].Size<0x770u ||
      !Objects[19].Data || Objects[19].Size<0x3d0u ||
      !Objects[15].Data || Objects[15].Size<160u ||
      Objects[40].Size!=Bindings->DestinationBytes)
    return AdmissionDynamicOverlayRange;
  work=Objects[18].Data; ta=Objects[19].Data; micro=Objects[15].Data;
  if(overlay_read_u64(work+0x1c0u)!=0x1100000000ULL ||
      overlay_read_u64(ta+0x120u)!=0x1100000000ULL)
    return AdmissionDynamicOverlayContent;
  if(AdmissionDynamicOverlayRouteEncoder(Plan,Objects,Count)!=AdmissionDynamicOverlaySuccess)
    return AdmissionDynamicOverlayContent;
  /* G13/V13_5 fields from m1n1 microsequence.py; corresponding current Asahi
   * queue/render.rs JobParameters1/2/3 owns all duplicated BG/EOT fields. */
  overlay_write_u64(work+0x88u,roots[0]->PackedCounts);
  overlay_write_u64(work+0x90u,pipeline[0]);
  overlay_write_u32(work+0x3c8u,roots[2]->PackedCounts);
  overlay_write_u32(work+0x3ccu,pipeline[2]);
  overlay_write_u64(work+0x610u,roots[1]->PackedCounts);
  overlay_write_u64(work+0x618u,pipeline[1]);
  overlay_write_u64(work+0x640u,roots[1]->PackedCounts);
  overlay_write_u64(work+0x648u,pipeline[1]);
  overlay_write_u32(work+0x70cu,roots[2]->PackedCounts);
  overlay_write_u32(work+0x714u,pipeline[2]);
  overlay_write_u32(work+0x72cu,roots[2]->PackedCounts);
  overlay_write_u32(work+0x734u,pipeline[2]);
  overlay_write_u64(work+0xa0u,scissor); overlay_write_u64(work+0x4c8u,scissor);
  overlay_write_u64(work+0xa8u,dbias); overlay_write_u64(work+0x4b8u,dbias);
  utile=((n->UtileWidth/16u)<<12)|((n->UtileHeight/16u)<<14);
  blocks=(n->SampleSizeBytes*n->UtileWidth*n->UtileHeight+2047u)/2048u;
  tileConfig=0x280u|((n->RenderFlags&APPLE_AGX_WIN32_NATIVE_RENDER_PROCESS_EMPTY_TILES)?0x10000u:0u);
  overlay_write_u32(work+0x80u,utile); overlay_write_u32(ta+0x88u,utile);
  overlay_write_u64(work+0x48u,n->PppMultisampleControl);
  overlay_write_u64(work+0x98u,n->PppMultisampleControl);
  overlay_write_u64(ta+0x90u,n->PppMultisampleControl);
  overlay_write_u32(work+0x50u,n->Samples);
  overlay_write_u32(ta+0x3ccu,n->PppControl);
  overlay_write_u32(work+0x3f4u,blocks); overlay_write_u32(work+0x6d0u,blocks);
  overlay_write_u32(work+0x748u,n->SampleSizeBytes);
  overlay_write_u64(work+0x180u,tileConfig); overlay_write_u64(work+0x6f0u,tileConfig);
  /* No depth/stencil attachment: native isp_bgobjvals is 0x300. */
  overlay_write_u32(work+0x3fcu,0x300u); overlay_write_u32(work+0x744u,0x300u);
  overlay_write_u32(micro+156u,(APPLE_AGX_U32)((Bindings->DestinationBytes+127u)/128u));
  return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayCaptureNativeGraph(
    const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings,
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan, const APPLE_AGX_DYNAMIC_JOB *Job,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects, APPLE_AGX_U32 ObjectCount,
    APPLE_AGX_U64 CommandHash, APPLE_AGX_U32 Fence, ADMISSION_NATIVE_GRAPH_RECEIPT *Receipt) {
  const APPLE_AGX_U32 refs[6] = {Bindings ? Bindings->EncoderReference : 0u,
      Bindings ? Bindings->VertexShaderReference : 0u,
      Bindings ? Bindings->FragmentShaderReference : 0u,
      Bindings ? Bindings->NativeBatch.Background.UscReference : 0u,
      Bindings ? Bindings->NativeBatch.PartialBackground.UscReference : 0u,
      Bindings ? Bindings->NativeBatch.EndOfTile.UscReference : 0u};
  ADMISSION_DYNAMIC_OVERLAY_ENTRY const *entries[6] = {0};
  APPLE_AGX_U32 i,j;
  if (!Receipt) return AdmissionDynamicOverlayArgument;
  overlay_zero(Receipt,(APPLE_AGX_U32)sizeof(*Receipt));
  if (!Bindings || !Plan || !Job || !Objects || !Fence || !CommandHash ||
      Plan->CommandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH ||
      Plan->Magic!=ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      Plan->Version!=ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      !Plan->Generation || Plan->EntryCount>ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES ||
      Job->Magic!=APPLE_AGX_DYNAMIC_JOB_MAGIC || Job->Version!=APPLE_AGX_DYNAMIC_JOB_VERSION ||
      Job->ObjectCount>APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
      Job->RelocationCount>APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS ||
      Job->Generation != Plan->Generation)
    return AdmissionDynamicOverlayArgument;
  for(i=0;i<6u;++i) for(j=0;j<Plan->EntryCount;++j)
    if(Plan->Entries[j].ReferenceIndex==refs[i]) entries[i]=&Plan->Entries[j];
  for(i=0;i<6u;++i) if(!entries[i] || !entries[i]->GpuVirtualAddress)
    return AdmissionDynamicOverlayLayout;
  Receipt->Version=1u; Receipt->Bytes=sizeof(*Receipt); Receipt->Fence=Fence;
  Receipt->Generation=Plan->Generation; Receipt->CommandHash=CommandHash; Receipt->GraphObjectCount=Job->ObjectCount;
  Receipt->GraphEdgeCount=Job->RelocationCount; Receipt->EncoderReference=refs[0];
  Receipt->VertexShaderReference=refs[1]; Receipt->FragmentShaderReference=refs[2];
  Receipt->BackgroundReference=refs[3]; Receipt->PartialBackgroundReference=refs[4];
  Receipt->EndOfTileReference=refs[5]; Receipt->EncoderGpuVa=entries[0]->GpuVirtualAddress;
  Receipt->VertexShaderGpuVa=entries[1]->GpuVirtualAddress;
  Receipt->FragmentShaderGpuVa=entries[2]->GpuVirtualAddress;
  Receipt->BackgroundGpuVa=entries[3]->GpuVirtualAddress;
  Receipt->PartialBackgroundGpuVa=entries[4]->GpuVirtualAddress;
  Receipt->EndOfTileGpuVa=entries[5]->GpuVirtualAddress;
  Receipt->BackgroundCounts=Bindings->NativeBatch.Background.PackedCounts;
  Receipt->PartialBackgroundCounts=Bindings->NativeBatch.PartialBackground.PackedCounts;
  Receipt->EndOfTileCounts=Bindings->NativeBatch.EndOfTile.PackedCounts;
  Receipt->BackgroundFlags=Bindings->NativeBatch.Background.UscFlags;
  Receipt->PartialBackgroundFlags=Bindings->NativeBatch.PartialBackground.UscFlags;
  Receipt->EndOfTileFlags=Bindings->NativeBatch.EndOfTile.UscFlags;
  for(i=0;i<Job->ObjectCount;++i) {
    const APPLE_AGX_DYNAMIC_JOB_OBJECT *o=&Job->Objects[i];
    if(o->ReferenceIndex==refs[0]) Receipt->EncoderFnv1a=o->SourceHash;
    if(o->ReferenceIndex==refs[1]) Receipt->VertexShaderFnv1a=o->SourceHash;
    if(o->ReferenceIndex==refs[2]) Receipt->FragmentShaderFnv1a=o->SourceHash;
  }
  if (APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT >= ObjectCount ||
      !Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].GpuVa ||
      !Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].PhysicalAddress ||
      Bindings->DestinationBytes > Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Size)
    return AdmissionDynamicOverlayRange;
  Receipt->RenderTargetReference=Bindings->DestinationReference;
  Receipt->RenderTargetBytes=(APPLE_AGX_U32)Bindings->DestinationBytes;
  Receipt->RenderTargetGpuVa=Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].GpuVa;
  Receipt->RenderTargetPhysical=Objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].PhysicalAddress;
  Receipt->Valid=1u; return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayCaptureNativeOutput(
    ADMISSION_NATIVE_GRAPH_RECEIPT *Receipt, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 SnapshotGeneration, APPLE_AGX_U64 GpuVa,
    APPLE_AGX_U64 Physical, const void *Data, APPLE_AGX_U32 Bytes) {
  if(!Receipt || !Receipt->Valid || !Fence || Fence!=Receipt->Fence ||
     !SnapshotGeneration || !GpuVa || GpuVa!=Receipt->RenderTargetGpuVa ||
     !Physical || Physical!=Receipt->RenderTargetPhysical || !Data ||
     Bytes!=Receipt->RenderTargetBytes || Bytes>sizeof(Receipt->ReadbackData))
    return AdmissionDynamicOverlayArgument;
  overlay_copy(Receipt->ReadbackData,Data,Bytes);
  Receipt->SnapshotGeneration=SnapshotGeneration;
  Receipt->ReadbackBytes=Bytes;
  Receipt->ReadbackFnv1a=overlay_hash(Receipt->ReadbackData,Bytes);
  Receipt->ReadbackAvailable=Receipt->ReadbackFnv1a ? 1u : 0u;
  return Receipt->ReadbackAvailable ? AdmissionDynamicOverlaySuccess : AdmissionDynamicOverlayContent;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayCaptureGraph(
    const ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    const ADMISSION_DYNAMIC_OVERLAY_STATE *State,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_U32 ActiveObjectCount, APPLE_AGX_U32 Fence,
    ADMISSION_DYNAMIC_GRAPH_RECEIPT *Receipt) {
  const ADMISSION_DYNAMIC_OVERLAY_ENTRY *encoder = OVERLAY_NULL;
  const ADMISSION_DYNAMIC_OVERLAY_ENTRY *pipeline = OVERLAY_NULL;
  const ADMISSION_DYNAMIC_OVERLAY_ENTRY *vertexShader = OVERLAY_NULL;
  const ADMISSION_DYNAMIC_OVERLAY_ENTRY *fragmentShader = OVERLAY_NULL;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *work;
  const unsigned char *pipelineData;
  APPLE_AGX_U32 index;
  if (Receipt != OVERLAY_NULL)
    overlay_zero(Receipt, (APPLE_AGX_U32)sizeof(*Receipt));
  if (Image == OVERLAY_NULL || Plan == OVERLAY_NULL ||
      State == OVERLAY_NULL || ActiveObjects == OVERLAY_NULL ||
      Receipt == OVERLAY_NULL || Fence == 0u ||
      Plan->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      Plan->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      State->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      State->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      State->Applied != 1u || State->Fence != Fence ||
      ActiveObjectCount <= OVERLAY_TA_WORK_OBJECT)
    return AdmissionDynamicOverlayArgument;
  for (index = 0u; index < Plan->EntryCount; ++index) {
    const ADMISSION_DYNAMIC_OVERLAY_ENTRY *entry = &Plan->Entries[index];
    if (entry->Role == AppleAgxWin32RoleEncoder) {
      if (encoder != OVERLAY_NULL)
        return AdmissionDynamicOverlayLayout;
      encoder = entry;
    } else if (entry->Role == AppleAgxWin32RoleUscPipeline) {
      if (pipeline != OVERLAY_NULL)
        return AdmissionDynamicOverlayLayout;
      pipeline = entry;
    } else if (entry->Role == AppleAgxWin32RoleShader &&
               entry->GpuVirtualAddress == OVERLAY_VERTEX_SHADER_GPU_VA) {
      if (vertexShader != OVERLAY_NULL)
        return AdmissionDynamicOverlayLayout;
      vertexShader = entry;
    } else if (entry->Role == AppleAgxWin32RoleShader &&
               entry->GpuVirtualAddress == OVERLAY_FRAGMENT_SHADER_GPU_VA) {
      if (fragmentShader != OVERLAY_NULL)
        return AdmissionDynamicOverlayLayout;
      fragmentShader = entry;
    }
  }
  if (encoder == OVERLAY_NULL || pipeline == OVERLAY_NULL ||
      vertexShader == OVERLAY_NULL || fragmentShader == OVERLAY_NULL ||
      pipeline->Bytes <= OVERLAY_PIPELINE_COMPACT_SPLIT ||
      encoder->ObjectIndex >= APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      pipeline->ObjectIndex >= APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      vertexShader->ObjectIndex >=
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      fragmentShader->ObjectIndex >=
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)
    return AdmissionDynamicOverlayLayout;
  work = &ActiveObjects[OVERLAY_TA_WORK_OBJECT];
  if (work->Data == OVERLAY_NULL ||
      work->Size < OVERLAY_TA_ENCODER_OFFSET + 8u ||
      overlay_read_u64(work->Data + OVERLAY_TA_ENCODER_OFFSET) !=
          encoder->GpuVirtualAddress)
    return AdmissionDynamicOverlayContent;
  pipelineData = Image->Objects[pipeline->ObjectIndex].Data +
                 pipeline->ObjectOffset;
  Receipt->Version = 1u;
  Receipt->Bytes = (APPLE_AGX_U32)sizeof(*Receipt);
  Receipt->Fence = Fence;
  Receipt->ActiveEncoderAddress = encoder->GpuVirtualAddress;
  Receipt->VertexPipelineAddress = pipeline->GpuVirtualAddress;
  Receipt->FragmentPipelineAddress =
      pipeline->GpuVirtualAddress + OVERLAY_PIPELINE_NATIVE_SPLIT;
  Receipt->VertexShaderAddress = vertexShader->GpuVirtualAddress;
  Receipt->FragmentShaderAddress = fragmentShader->GpuVirtualAddress;
  Receipt->EncoderFnv1a = overlay_hash(
      Image->Objects[encoder->ObjectIndex].Data + encoder->ObjectOffset,
      encoder->Bytes);
  Receipt->VertexPipelineFnv1a =
      overlay_hash(pipelineData, OVERLAY_PIPELINE_COMPACT_SPLIT);
  Receipt->FragmentPipelineFnv1a = overlay_hash(
      pipelineData + OVERLAY_PIPELINE_NATIVE_SPLIT,
      pipeline->Bytes - OVERLAY_PIPELINE_COMPACT_SPLIT);
  Receipt->VertexShaderFnv1a = overlay_hash(
      Image->Objects[vertexShader->ObjectIndex].Data +
          vertexShader->ObjectOffset,
      vertexShader->Bytes);
  Receipt->FragmentShaderFnv1a = overlay_hash(
      Image->Objects[fragmentShader->ObjectIndex].Data +
          fragmentShader->ObjectOffset,
      fragmentShader->Bytes);
  if (Receipt->EncoderFnv1a == 0ULL ||
      Receipt->VertexPipelineFnv1a == 0ULL ||
      Receipt->FragmentPipelineFnv1a == 0ULL ||
      Receipt->VertexShaderFnv1a == 0ULL ||
      Receipt->FragmentShaderFnv1a == 0ULL)
    return AdmissionDynamicOverlayContent;
  Receipt->Valid = 1u;
  return AdmissionDynamicOverlaySuccess;
}

ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayCaptureStoreGraph(
    const ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_STATE *State,
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *ActiveObjects,
    APPLE_AGX_U32 ActiveObjectCount, APPLE_AGX_U32 Fence,
    ADMISSION_DYNAMIC_STORE_RECEIPT *Receipt) {
  enum {
    microsequenceIndex = 15u,
    workIndex = 18u,
    stateIndex = 36u,
    outputIndex = 40u,
    pipelineIndex = 73u,
    shaderIndex = 74u,
  };
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *microsequence;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *work;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *stateObject;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *output;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *pipeline;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *shader;
  ADMISSION_DYNAMIC_STORE_RECEIPT candidate;
  const unsigned char *renderTarget;
  const unsigned char *companion;
  if (Image == OVERLAY_NULL || State == OVERLAY_NULL ||
      ActiveObjects == OVERLAY_NULL || Receipt == OVERLAY_NULL ||
      Fence == 0u || Image->Ready != APPLE_AGX_TRUE ||
      Image->BoundFence != Fence ||
      State->Magic != ADMISSION_DYNAMIC_OVERLAY_MAGIC ||
      State->Version != ADMISSION_DYNAMIC_OVERLAY_VERSION ||
      State->Applied != 1u || State->Fence != Fence ||
      State->Generation == 0u ||
      ActiveObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT)
    return AdmissionDynamicOverlayState;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  if (layouts == OVERLAY_NULL)
    return AdmissionDynamicOverlayLayout;
  microsequence = &ActiveObjects[microsequenceIndex];
  work = &ActiveObjects[workIndex];
  stateObject = &ActiveObjects[stateIndex];
  output = &ActiveObjects[outputIndex];
  pipeline = &ActiveObjects[pipelineIndex];
  shader = &ActiveObjects[shaderIndex];
  if (microsequence->Data == OVERLAY_NULL ||
      microsequence->Size < 156u || work->Data == OVERLAY_NULL ||
      work->Size < 0x738u || stateObject->Data == OVERLAY_NULL ||
      stateObject->Size < 0x4020u || output->Data == OVERLAY_NULL ||
      output->Size == 0u || pipeline->Data == OVERLAY_NULL ||
      pipeline->Size < 0x5000u || shader->Data == OVERLAY_NULL ||
      shader->Size < 0x500u)
    return AdmissionDynamicOverlayRange;
  if (stateObject->Data != Image->Objects[stateIndex].Data ||
      stateObject->GpuVa != Image->Objects[stateIndex].GpuVa ||
      stateObject->PhysicalAddress !=
          Image->Objects[stateIndex].PhysicalAddress ||
      pipeline->Data != Image->Objects[pipelineIndex].Data ||
      pipeline->GpuVa != Image->Objects[pipelineIndex].GpuVa ||
      pipeline->PhysicalAddress !=
          Image->Objects[pipelineIndex].PhysicalAddress ||
      shader->Data != Image->Objects[shaderIndex].Data ||
      shader->PhysicalAddress != Image->Objects[shaderIndex].PhysicalAddress ||
      output->Data != Image->Objects[outputIndex].Data ||
      output->GpuVa != Image->Objects[outputIndex].GpuVa ||
      output->PhysicalAddress != Image->Objects[outputIndex].PhysicalAddress ||
      output->GpuVa != Image->Binding.DestinationGpuVa ||
      output->PhysicalAddress != Image->Binding.DestinationPhysical ||
      output->Size != Image->Binding.DestinationBytes)
    return AdmissionDynamicOverlayContent;
  renderTarget = stateObject->Data + 0x3000u;
  companion = stateObject->Data + 0x4000u;
  overlay_zero(&candidate, (APPLE_AGX_U32)sizeof(candidate));
  candidate.Version = ADMISSION_DYNAMIC_STORE_RECEIPT_VERSION;
  candidate.Bytes = (APPLE_AGX_U32)sizeof(candidate);
  candidate.Fence = Fence;
  candidate.Generation = State->Generation;
  candidate.DestinationBytes = output->Size;
  candidate.StorePipeline = overlay_read_u32(work->Data + 0x3ccu);
  candidate.PartialStorePipeline0 =
      overlay_read_u32(work->Data + 0x714u);
  candidate.PartialStorePipeline1 =
      overlay_read_u32(work->Data + 0x734u);
  candidate.DestinationGpuVa = output->GpuVa;
  candidate.DestinationPhysical = output->PhysicalAddress;
  candidate.AttachmentGpuVa =
      overlay_read_u64(microsequence->Data + 148u);
  candidate.PipelineBaseRaw = overlay_read_u64(work->Data + 0x170u);
  candidate.LoadPipeline = overlay_read_u64(work->Data + 0x90u);
  candidate.ReloadPipeline0 = overlay_read_u64(work->Data + 0x618u);
  candidate.ReloadPipeline1 = overlay_read_u64(work->Data + 0x648u);
  candidate.ClearPageFnv1a =
      overlay_hash(pipeline->Data + 0x2000u, 0x1000u);
  candidate.ReloadPageFnv1a =
      overlay_hash(pipeline->Data + 0x3000u, 0x1000u);
  candidate.StorePageFnv1a =
      overlay_hash(pipeline->Data + 0x4000u, 0x1000u);
  candidate.ClearUniformWord =
      overlay_read_u64(pipeline->Data + 0x2000u);
  candidate.StoreTextureWord =
      overlay_read_u64(pipeline->Data + 0x4000u);
  candidate.StoreUniformWord =
      overlay_read_u64(pipeline->Data + 0x4008u);
  candidate.StoreShaderGpuVa = layouts[shaderIndex].OriginalGpuVa + 0x400u;
  candidate.StoreShaderFnv1a =
      overlay_hash(shader->Data + 0x400u, 256u);
  candidate.RenderTargetGpuVa = stateObject->GpuVa + 0x3000u;
  candidate.RenderTargetQword0 = overlay_read_u64(renderTarget);
  candidate.RenderTargetQword1 = overlay_read_u64(renderTarget + 8u);
  candidate.RenderTargetQword2 = overlay_read_u64(renderTarget + 16u);
  candidate.RenderTargetFnv1a = overlay_hash(renderTarget, 24u);
  candidate.CompanionGpuVa = stateObject->GpuVa + 0x4000u;
  candidate.CompanionQword0 = overlay_read_u64(companion);
  candidate.CompanionQword1 = overlay_read_u64(companion + 8u);
  candidate.CompanionQword2 = overlay_read_u64(companion + 16u);
  candidate.CompanionQword3 = overlay_read_u64(companion + 24u);
  candidate.CompanionFnv1a = overlay_hash(companion, 32u);
  candidate.Valid = 1u;
  *Receipt = candidate;
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
    if (Plan->CommandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH &&
        entry->Role == AppleAgxWin32RoleUscPipeline &&
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
    if (Plan->CommandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH &&
        entry->Role == AppleAgxWin32RoleUscPipeline &&
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
    if (Plan->CommandVersion != APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH &&
        entry->Role == AppleAgxWin32RoleUscPipeline &&
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
