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
static unsigned char output_bytes[0x4000];
static unsigned char store_work_bytes[0x800];
static unsigned char store_microsequence_bytes[0x200];
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
  references[4].Bytes = 0x54u;
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
  unsigned storage_offset = 0u;
  for (unsigned index = 0u; index < job->ObjectCount; ++index) {
    unsigned reference = refs[index];
    unsigned bytes = (unsigned)view->References[reference].Bytes;
    job->Objects[index].ReferenceIndex = reference;
    job->Objects[index].Role = view->References[reference].Role;
    job->Objects[index].StorageOffset = storage_offset;
    job->Objects[index].Bytes = bytes;
    memset(storage + job->Objects[index].StorageOffset,
           0x20 + (int)reference, bytes);
    storage_offset += bytes;
  }
  job->StorageBytes = storage_offset;
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

static void write_u32(unsigned char *bytes, unsigned value) {
  for (unsigned index = 0u; index < 4u; ++index)
    bytes[index] = (unsigned char)(value >> (index * 8u));
}

int main(void) {
  const ADMISSION_DYNAMIC_OVERLAY_ALIAS *aliases;
  APPLE_AGX_U32 alias_count = 0u;
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
  ADMISSION_DYNAMIC_GRAPH_RECEIPT graph;
  ADMISSION_DYNAMIC_STORE_RECEIPT store;
  APPLE_AGX_EXP208_RELOCATION_OBJECT active_capture[76];
  unsigned char capture_work[0x100];
  APPLE_AGX_U64 address = 0ULL;

  initialize_image(&image);
  aliases = AdmissionDynamicOverlayShaderAliases(&alias_count);
  assert(aliases != NULL &&
         alias_count == ADMISSION_DYNAMIC_OVERLAY_SHADER_ALIAS_COUNT);
  assert(aliases[0].ObjectIndex == 73u &&
         aliases[0].ObjectOffset == 0x10000u &&
         aliases[0].Bytes == 0x4000u &&
         aliases[0].Reserved == 0u &&
         aliases[0].GpuVirtualAddress == 0x1100064000ULL);
  assert(aliases[1].ObjectIndex == 73u &&
         aliases[1].ObjectOffset == 0x14000u &&
         aliases[1].Bytes == 0x4000u &&
         aliases[1].Reserved == 0u &&
         aliases[1].GpuVirtualAddress == 0x110006c000ULL);
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
  assert(find_entry(&plan, 4u)->GpuVirtualAddress == 0x1100020000ULL);
  assert(find_entry(&plan, 2u)->ObjectIndex == 73u);
  assert(find_entry(&plan, 2u)->ObjectOffset == 0x10000u);
  assert(find_entry(&plan, 2u)->GpuVirtualAddress == 0x1100064000ULL);
  assert(find_entry(&plan, 3u)->ObjectIndex == 73u);
  assert(find_entry(&plan, 3u)->ObjectOffset == 0x14000u);
  assert(find_entry(&plan, 3u)->GpuVirtualAddress == 0x110006c000ULL);
  assert(find_entry(&plan, 9u)->GpuVirtualAddress == 0x1100013000ULL);
  assert(find_entry(&plan, 10u)->GpuVirtualAddress == 0x1100013400ULL);
  assert(AdmissionDynamicOverlayResolve(&plan, 4u, 0x20u, 1u, &address) ==
         AdmissionDynamicOverlaySuccess);
  assert(address == 0x1100020020ULL);
  assert(AdmissionDynamicOverlayResolve(&plan, 4u, 0x3fu, 2u, &address) ==
         AdmissionDynamicOverlayRange);
  assert(AdmissionDynamicOverlayResolve(&plan, 4u, 0x40u, 1u, &address) ==
         AdmissionDynamicOverlaySuccess);
  assert(address == 0x1100021000ULL);

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
  assert(pipeline_bytes[0] == 0x24u);
  assert(pipeline_bytes[0x1000] == 0x24u);
  assert(pipeline_bytes[0x10000] == 0x22u);
  assert(pipeline_bytes[0x14000] == 0x23u);
  assert(shader_bytes[0x3000] == 0x29u);
  assert(shader_bytes[0x3400] == 0x2au);
  assert(descriptor_bytes[0x8000] == 0x25u);
  assert(scissor_bytes[0] == 0x26u);
  assert(depth_bytes[0] == 0x27u);
  assert(pipeline_bytes[0x2000] == 0x5au);
  memcpy(active_capture, image.Objects, sizeof(active_capture));
  memset(capture_work, 0, sizeof(capture_work));
  active_capture[19u].Data = capture_work;
  active_capture[19u].Size = sizeof(capture_work);
  write_u64(capture_work + 0xd0u, image.Objects[71u].GpuVa);
  assert(AdmissionDynamicOverlayCaptureGraph(
             &image, &plan, &state, active_capture, 76u, 256u,
             &graph) == AdmissionDynamicOverlaySuccess);
  assert(graph.Valid == 1u && graph.Fence == 256u &&
         graph.ActiveEncoderAddress == image.Objects[71u].GpuVa &&
         graph.VertexPipelineAddress == 0x1100020000ULL &&
         graph.FragmentPipelineAddress == 0x1100021000ULL &&
         graph.VertexShaderAddress == 0x1100064000ULL &&
         graph.FragmentShaderAddress == 0x110006c000ULL &&
         graph.EncoderFnv1a != 0ULL &&
         graph.VertexPipelineFnv1a != 0ULL &&
         graph.FragmentPipelineFnv1a != 0ULL &&
         graph.VertexShaderFnv1a != 0ULL &&
         graph.FragmentShaderFnv1a != 0ULL);
  memset(output_bytes, 0, sizeof(output_bytes));
  memset(store_work_bytes, 0, sizeof(store_work_bytes));
  memset(store_microsequence_bytes, 0,
         sizeof(store_microsequence_bytes));
  image.BoundFence = 256u;
  image.Binding.DestinationGpuVa = 0x1500fa0000ULL;
  image.Binding.DestinationPhysical = 0x9bcfd0000ULL;
  image.Binding.DestinationBytes = sizeof(output_bytes);
  image.Objects[40u].Data = output_bytes;
  image.Objects[40u].GpuVa = image.Binding.DestinationGpuVa;
  image.Objects[40u].PhysicalAddress = image.Binding.DestinationPhysical;
  image.Objects[40u].Size = sizeof(output_bytes);
  image.Objects[36u].PhysicalAddress = 0x9d1200000ULL;
  image.Objects[73u].PhysicalAddress = 0x9d5a80000ULL;
  image.Objects[74u].PhysicalAddress = 0x9d5e80000ULL;
  memset(pipeline_bytes + 0x2000u, 0, 0x3000u);
  write_u64(pipeline_bytes + 0x2000u, 0x1503920000400c1dULL);
  write_u64(pipeline_bytes + 0x4000u, 0x15039230001000ddULL);
  write_u64(pipeline_bytes + 0x4008u, 0x150392400040041dULL);
  pipeline_bytes[0x4010u] = 0x4du;
  pipeline_bytes[0x4011u] = 0xbdu;
  pipeline_bytes[0x4012u] = 0x10u;
  pipeline_bytes[0x4013u] = 0x20u;
  pipeline_bytes[0x4014u] = 0x0du;
  pipeline_bytes[0x4015u] = 0x0cu;
  pipeline_bytes[0x4016u] = 0x00u;
  pipeline_bytes[0x4017u] = 0x04u;
  pipeline_bytes[0x4018u] = 0x01u;
  pipeline_bytes[0x4019u] = 0x00u;
  for (unsigned index = 0u; index < 256u; ++index)
    shader_bytes[0x400u + index] = (unsigned char)index;
  write_u64(descriptor_bytes + 0x3000u, 0x000003c00fc60a22ULL);
  write_u64(descriptor_bytes + 0x3008u, 0x10000001500fa000ULL);
  write_u64(descriptor_bytes + 0x3010u, 0ULL);
  write_u64(descriptor_bytes + 0x4000u, 0xffffffff00000000ULL);
  write_u64(store_work_bytes + 0x90u, 0x22004ULL);
  write_u64(store_work_bytes + 0x170u, 0x14000000ULL);
  write_u32(store_work_bytes + 0x3ccu, 0x24004u);
  write_u64(store_work_bytes + 0x618u, 0x23004ULL);
  write_u64(store_work_bytes + 0x648u, 0x23004ULL);
  write_u32(store_work_bytes + 0x714u, 0x24004u);
  write_u32(store_work_bytes + 0x734u, 0x24004u);
  write_u64(store_microsequence_bytes + 148u,
            image.Binding.DestinationGpuVa);
  memcpy(active_capture, image.Objects, sizeof(active_capture));
  active_capture[18u].Data = store_work_bytes;
  active_capture[18u].Size = sizeof(store_work_bytes);
  active_capture[15u].Data = store_microsequence_bytes;
  active_capture[15u].Size = sizeof(store_microsequence_bytes);
  assert(AdmissionDynamicOverlayCaptureStoreGraph(
             &image, &state, active_capture, 76u, 256u,
             &store) == AdmissionDynamicOverlaySuccess);
  assert(store.Version == ADMISSION_DYNAMIC_STORE_RECEIPT_VERSION &&
         sizeof(store) == ADMISSION_DYNAMIC_STORE_RECEIPT_BYTES &&
         store.Bytes == sizeof(store) && store.Valid == 1u &&
         store.Fence == 256u && store.Generation == 7u);
  assert(store.DestinationGpuVa == 0x1500fa0000ULL &&
         store.DestinationPhysical == 0x9bcfd0000ULL &&
         store.DestinationBytes == 0x4000u &&
         store.AttachmentGpuVa == 0x1500fa0000ULL);
  assert(store.PipelineBaseRaw == 0x14000000ULL &&
         store.LoadPipeline == 0x22004ULL &&
         store.StorePipeline == 0x24004u &&
         store.ReloadPipeline0 == 0x23004ULL &&
         store.ReloadPipeline1 == 0x23004ULL &&
         store.PartialStorePipeline0 == 0x24004u &&
         store.PartialStorePipeline1 == 0x24004u);
  assert(store.ClearUniformWord == 0x1503920000400c1dULL &&
         store.StoreTextureWord == 0x15039230001000ddULL &&
         store.StoreUniformWord == 0x150392400040041dULL &&
         store.StoreShaderGpuVa == 0x1100010400ULL);
  assert(store.RenderTargetGpuVa == image.Objects[36u].GpuVa + 0x3000u &&
         store.RenderTargetQword0 == 0x000003c00fc60a22ULL &&
         store.RenderTargetQword1 == 0x10000001500fa000ULL &&
         store.RenderTargetQword2 == 0ULL &&
         store.CompanionGpuVa == image.Objects[36u].GpuVa + 0x4000u &&
         store.CompanionQword0 == 0xffffffff00000000ULL);
  assert(store.ClearPageFnv1a != 0ULL &&
         store.ReloadPageFnv1a != 0ULL &&
         store.StorePageFnv1a != 0ULL &&
         store.StoreShaderFnv1a != 0ULL &&
         store.RenderTargetFnv1a != 0ULL &&
         store.CompanionFnv1a != 0ULL);
  {
    ADMISSION_DYNAMIC_STORE_RECEIPT before = store;
    assert(AdmissionDynamicOverlayCaptureStoreGraph(
               &image, &state, active_capture, 76u, 257u,
               &store) == AdmissionDynamicOverlayState);
    assert(memcmp(&store, &before, sizeof(store)) == 0);
  }
  pipeline_bytes[0x2000u] = 0x5au;
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
         pipeline_bytes[0] == 0u && pipeline_bytes[0x1000] == 0u &&
         pipeline_bytes[0x10000] == 0u &&
         pipeline_bytes[0x14000] == 0u &&
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
  pipeline_bytes[0] = 1u;
  AdmissionDynamicOverlayStateInitialize(&state);
  assert(AdmissionDynamicOverlayApply(&image, &plan, &job, storage,
                                      sizeof(storage), 256u, &state) ==
         AdmissionDynamicOverlayOccupied);
  assert(state.Applied == 0u && encoder_bytes[0] == 0u);
  return 0;
}
