#include "render_dynamic_overlay.h"

#include <assert.h>
#include <string.h>

#define REFERENCE_COUNT 11u

static unsigned char descriptor_bytes[0x10000];
static unsigned char scissor_bytes[0x40];
static unsigned char depth_bytes[0x40];
static unsigned char encoder_bytes[0x180];
static unsigned char pipeline_bytes[0x40000];
static unsigned char shader_bytes[0x4000];
static APPLE_AGX_WIN32_RELOCATION vertex_relocation;

static void initialize_image(ADMISSION_BACKEND_IMAGE *image) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts =
      AppleAgxRenderTemplateObjectLayouts();
  memset(image, 0, sizeof(*image));
  memset(descriptor_bytes, 0, sizeof(descriptor_bytes));
  memset(scissor_bytes, 0, sizeof(scissor_bytes));
  memset(depth_bytes, 0, sizeof(depth_bytes));
  memset(encoder_bytes, 0, sizeof(encoder_bytes));
  memset(pipeline_bytes, 0, sizeof(pipeline_bytes));
  memset(shader_bytes, 0, sizeof(shader_bytes));
  image->Ready = APPLE_AGX_TRUE;
#define OBJECT(index, storage)                                               \
  do {                                                                        \
    image->Objects[index].Data = storage;                                     \
    image->Objects[index].Size = layouts[index].Size;                         \
    image->Objects[index].GpuVa = layouts[index].PackedGpuVa;                 \
  } while (0)
  OBJECT(36u, descriptor_bytes);
  OBJECT(38u, scissor_bytes);
  OBJECT(39u, depth_bytes);
  OBJECT(71u, encoder_bytes);
  OBJECT(73u, pipeline_bytes);
  OBJECT(74u, shader_bytes);
#undef OBJECT
}

static void initialize_view(
    APPLE_AGX_WIN32_COMMAND_VIEW *view,
    APPLE_AGX_WIN32_COMMAND_HEADER *header,
    APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[REFERENCE_COUNT],
    APPLE_AGX_WIN32_DRAW_PAYLOAD *draw) {
  memset(view, 0, sizeof(*view));
  memset(header, 0, sizeof(*header));
  memset(references, 0, REFERENCE_COUNT * sizeof(*references));
  memset(draw, 0, sizeof(*draw));
  memset(&vertex_relocation, 0, sizeof(vertex_relocation));
  header->Opcode = AppleAgxWin32OpcodeDraw;
  header->Generation = 7u;
  header->ReferenceCount = REFERENCE_COUNT;
  for (unsigned index = 0u; index < REFERENCE_COUNT; ++index) {
    references[index].AllocationIndex = index;
    references[index].Offset = 0u;
    references[index].Bytes = 0x40u;
  }
  references[0].Role = AppleAgxWin32RoleRenderTarget;
  references[1].Role = AppleAgxWin32RoleVertex;
  references[2].Role = AppleAgxWin32RoleShader;
  references[3].Role = AppleAgxWin32RoleShader;
  references[4].Role = AppleAgxWin32RoleUscPipeline;
  references[5].Role = AppleAgxWin32RoleDescriptor;
  references[6].Role = AppleAgxWin32RoleScissor;
  references[7].Role = AppleAgxWin32RoleDepthBias;
  references[8].Role = AppleAgxWin32RoleEncoder;
  references[9].Role = AppleAgxWin32RoleShaderRodata;
  references[10].Role = AppleAgxWin32RoleShaderRodata;
  draw->DestinationReference = 0u;
  draw->VertexReference = 1u;
  draw->IndexReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  draw->ConstantReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  draw->TextureReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  draw->VertexShaderReference = 2u;
  draw->FragmentShaderReference = 3u;
  draw->VertexRodataReference = 9u;
  draw->FragmentRodataReference = 10u;
  draw->UscPipelineReference = 4u;
  draw->DescriptorReference = 5u;
  draw->ScissorReference = 6u;
  draw->DepthBiasReference = 7u;
  draw->EncoderReference = 8u;
  draw->RelocationCount = 1u;
  vertex_relocation.Kind = AppleAgxWin32RelocationDescriptorAddress;
  vertex_relocation.WidthBytes = 8u;
  vertex_relocation.DestinationReference = 5u;
  vertex_relocation.TargetReference = 1u;
  view->Header = header;
  view->References = references;
  view->Draw = draw;
  view->Relocations = &vertex_relocation;
}

static void initialize_job(APPLE_AGX_DYNAMIC_JOB *job,
                           unsigned char storage[0x300],
                           const APPLE_AGX_WIN32_COMMAND_VIEW *view) {
  static const unsigned refs[] = {1u, 2u, 3u, 9u, 10u,
                                  4u, 5u, 6u, 7u, 8u};
  memset(job, 0, sizeof(*job));
  memset(storage, 0, 0x300u);
  job->Magic = APPLE_AGX_DYNAMIC_JOB_MAGIC;
  job->Version = APPLE_AGX_DYNAMIC_JOB_VERSION;
  job->Generation = view->Header->Generation;
  job->ObjectCount = sizeof(refs) / sizeof(refs[0]);
  for (unsigned index = 0u; index < job->ObjectCount; ++index) {
    unsigned reference = refs[index];
    job->Objects[index].ReferenceIndex = reference;
    job->Objects[index].Role = view->References[reference].Role;
    job->Objects[index].StorageOffset = index * 0x40u;
    job->Objects[index].Bytes = 0x40u;
    memset(storage + job->Objects[index].StorageOffset,
           0x20 + (int)reference, 0x40u);
  }
  job->StorageBytes = job->ObjectCount * 0x40u;
  job->MaterializedHash = 0x12345678ULL;
}

static const ADMISSION_DYNAMIC_OVERLAY_ENTRY *find_entry(
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *plan, unsigned reference) {
  for (unsigned index = 0u; index < plan->EntryCount; ++index)
    if (plan->Entries[index].ReferenceIndex == reference)
      return &plan->Entries[index];
  return NULL;
}

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  for (unsigned index = 0u; index < 8u; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

static void write_u64(unsigned char *bytes, unsigned long long value) {
  for (unsigned index = 0u; index < 8u; ++index)
    bytes[index] = (unsigned char)(value >> (index * 8u));
}

int main(void) {
  ADMISSION_BACKEND_IMAGE image;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  APPLE_AGX_WIN32_COMMAND_HEADER header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[REFERENCE_COUNT];
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw;
  APPLE_AGX_DYNAMIC_JOB job;
  unsigned char storage[0x300];
  ADMISSION_DYNAMIC_OVERLAY_PLAN plan;
  ADMISSION_DYNAMIC_OVERLAY_PLAN restored_plan;
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS bindings;
  ADMISSION_DYNAMIC_OVERLAY_STATE state;
  APPLE_AGX_U64 address = 0ULL;

  initialize_image(&image);
  initialize_view(&view, &header, references, &draw);
  initialize_job(&job, storage, &view);
  pipeline_bytes[0x2000] = 0x5au;
  assert(AdmissionDynamicOverlayPlan(&image, &view, &plan) ==
         AdmissionDynamicOverlaySuccess);
  assert(AdmissionDynamicOverlayBindingsFromView(&view, &bindings) ==
         AdmissionDynamicOverlaySuccess);
  assert(AdmissionDynamicOverlayPlanFromJob(
             &image, &bindings, &job, &restored_plan) ==
         AdmissionDynamicOverlaySuccess);
  assert(restored_plan.EntryCount == plan.EntryCount);
  for (unsigned index = 0u; index < plan.EntryCount; ++index) {
    assert(restored_plan.Entries[index].ReferenceIndex ==
           plan.Entries[index].ReferenceIndex);
    assert(restored_plan.Entries[index].Role == plan.Entries[index].Role);
    assert(restored_plan.Entries[index].ObjectIndex ==
           plan.Entries[index].ObjectIndex);
    assert(restored_plan.Entries[index].ObjectOffset ==
           plan.Entries[index].ObjectOffset);
    assert(restored_plan.Entries[index].Bytes == plan.Entries[index].Bytes);
    assert(restored_plan.Entries[index].GpuVirtualAddress ==
           plan.Entries[index].GpuVirtualAddress);
  }
  assert(plan.EntryCount == 10u && plan.Generation == 7u);
  assert(find_entry(&plan, 1u)->ObjectIndex == 73u);
  assert(find_entry(&plan, 1u)->ObjectOffset == 0x20000u);
  assert(find_entry(&plan, 1u)->GpuVirtualAddress == 0x1100040000ULL);
  assert(find_entry(&plan, 8u)->ObjectIndex == 71u);
  assert(find_entry(&plan, 8u)->GpuVirtualAddress == 0x1503d78000ULL);
  assert(find_entry(&plan, 4u)->ObjectIndex == 73u);
  assert(find_entry(&plan, 4u)->GpuVirtualAddress == 0x1100030000ULL);
  assert(find_entry(&plan, 2u)->ObjectIndex == 73u);
  assert(find_entry(&plan, 2u)->ObjectOffset == 0x4000u);
  assert(find_entry(&plan, 2u)->GpuVirtualAddress == 0x1100024000ULL);
  assert(find_entry(&plan, 3u)->ObjectIndex == 73u);
  assert(find_entry(&plan, 3u)->ObjectOffset == 0x8000u);
  assert(find_entry(&plan, 3u)->GpuVirtualAddress == 0x1100028000ULL);
  assert(find_entry(&plan, 9u)->GpuVirtualAddress == 0x1100013000ULL);
  assert(find_entry(&plan, 10u)->GpuVirtualAddress == 0x1100013400ULL);
  assert(AdmissionDynamicOverlayResolve(&plan, 4u, 0x20u, 1u, &address) ==
         AdmissionDynamicOverlaySuccess);
  assert(address == 0x1100030020ULL);
  assert(AdmissionDynamicOverlayResolve(&plan, 4u, 0x40u, 1u, &address) ==
         AdmissionDynamicOverlayRange);

  {
    APPLE_AGX_EXP208_RELOCATION_OBJECT active[76];
    const APPLE_AGX_EXP208_RELOCATION *relocations =
        AppleAgxRenderTemplateRelocations();
    unsigned relocation_matches = 0u;
    unsigned char ta_work[0x100];
    for (unsigned index = 0u;
         index < AppleAgxRenderTemplateRelocationCount(); ++index)
      if (relocations[index].SourceObject == 19u &&
          relocations[index].SourceOffset == 0xd0u &&
          relocations[index].TargetObject == 37u &&
          relocations[index].TargetOffset == 0u &&
          relocations[index].AddressSpace == AppleAgxExp208RelocationGpuVa &&
          relocations[index].Encoding ==
              AppleAgxExp208RelocationExactU64)
        ++relocation_matches;
    assert(relocation_matches == 1u);
    memset(active, 0, sizeof(active));
    memset(ta_work, 0, sizeof(ta_work));
    active[19u].Data = ta_work;
    active[19u].Size = sizeof(ta_work);
    active[37u].GpuVa = 0x1500044000ULL;
    active[71u].GpuVa = 0x1503d78000ULL;
    write_u64(ta_work + 0xd0u, active[37u].GpuVa);
    assert(AdmissionDynamicOverlayRouteEncoder(
               &plan, active, 76u) == AdmissionDynamicOverlaySuccess);
    assert(read_u64(ta_work + 0xd0u) == active[71u].GpuVa);
    assert(AdmissionDynamicOverlayRouteEncoder(
               &plan, active, 76u) == AdmissionDynamicOverlayContent);
    write_u64(ta_work + 0xd0u, active[37u].GpuVa + 0x40u);
    assert(AdmissionDynamicOverlayRouteEncoder(
               &plan, active, 76u) == AdmissionDynamicOverlayContent);
    assert(read_u64(ta_work + 0xd0u) == active[37u].GpuVa + 0x40u);
  }

  AdmissionDynamicOverlayStateInitialize(&state);
  assert(AdmissionDynamicOverlayApply(&image, &plan, &job, storage,
                                      sizeof(storage), 256u, &state) ==
         AdmissionDynamicOverlaySuccess);
  assert(state.Applied == 1u && state.Fence == 256u);
  assert(pipeline_bytes[0x20000] == 0x21u);
  assert(encoder_bytes[0] == 0x28u);
  assert(pipeline_bytes[0x10000] == 0x24u);
  assert(pipeline_bytes[0x4000] == 0x22u);
  assert(pipeline_bytes[0x8000] == 0x23u);
  assert(shader_bytes[0x3000] == 0x29u);
  assert(shader_bytes[0x3400] == 0x2au);
  assert(descriptor_bytes[0x8000] == 0x25u);
  assert(scissor_bytes[0] == 0x26u);
  assert(depth_bytes[0] == 0x27u);
  assert(pipeline_bytes[0x2000] == 0x5au);
  assert(AdmissionDynamicOverlayApply(&image, &plan, &job, storage,
                                      sizeof(storage), 257u, &state) ==
         AdmissionDynamicOverlayState);
  assert(AdmissionDynamicOverlayRelease(&image, &plan, &job, storage,
                                        sizeof(storage), 257u, &state) ==
         AdmissionDynamicOverlayState);
  assert(encoder_bytes[0] == 0x28u);
  assert(AdmissionDynamicOverlayRelease(&image, &plan, &job, storage,
                                        sizeof(storage), 256u, &state) ==
         AdmissionDynamicOverlaySuccess);
  assert(state.Applied == 0u && encoder_bytes[0] == 0u &&
         pipeline_bytes[0x10000] == 0u && pipeline_bytes[0x4000] == 0u &&
         pipeline_bytes[0x8000] == 0u &&
         descriptor_bytes[0x8000] == 0u && scissor_bytes[0] == 0u &&
         depth_bytes[0] == 0u && pipeline_bytes[0x20000] == 0u &&
         pipeline_bytes[0x2000] == 0x5au);

  initialize_image(&image);
  initialize_view(&view, &header, references, &draw);
  references[8].Bytes = 0x181u;
  assert(AdmissionDynamicOverlayPlan(&image, &view, &plan) ==
         AdmissionDynamicOverlayRange);

  initialize_image(&image);
  initialize_view(&view, &header, references, &draw);
  initialize_job(&job, storage, &view);
  assert(AdmissionDynamicOverlayPlan(&image, &view, &plan) ==
         AdmissionDynamicOverlaySuccess);
  pipeline_bytes[0x10000] = 1u;
  AdmissionDynamicOverlayStateInitialize(&state);
  assert(AdmissionDynamicOverlayApply(&image, &plan, &job, storage,
                                      sizeof(storage), 256u, &state) ==
         AdmissionDynamicOverlayOccupied);
  assert(state.Applied == 0u && encoder_bytes[0] == 0u);
  return 0;
}
