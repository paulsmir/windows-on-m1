#include "render_dynamic_dma.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REF_COUNT 11u
#define RELOC_COUNT 7u

typedef struct _DRAW_COMMAND {
  APPLE_AGX_WIN32_COMMAND_HEADER Header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE References[REF_COUNT];
  APPLE_AGX_WIN32_DRAW_PAYLOAD Draw;
  APPLE_AGX_WIN32_RELOCATION Relocations[RELOC_COUNT];
} DRAW_COMMAND;

typedef struct _FILE_OBJECT {
  unsigned char *Bytes;
  unsigned int Size;
  unsigned long long Token;
} FILE_OBJECT;

typedef struct _FIXTURE_CONTEXT {
  FILE_OBJECT Objects[REF_COUNT];
  ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan;
} FIXTURE_CONTEXT;

static unsigned int align4(unsigned int value) {
  return (value + 3u) & ~3u;
}

static FILE_OBJECT load_file(const char *path, unsigned long long token) {
  FILE_OBJECT object = {0};
  FILE *file = fopen(path, "rb");
  long size;
  assert(file != NULL);
  assert(fseek(file, 0, SEEK_END) == 0);
  size = ftell(file);
  assert(size > 0 && size <= 0x10000);
  assert(fseek(file, 0, SEEK_SET) == 0);
  object.Size = align4((unsigned int)size);
  object.Bytes = (unsigned char *)calloc(1u, object.Size);
  assert(object.Bytes != NULL);
  assert(fread(object.Bytes, 1u, (size_t)size, file) == (size_t)size);
  assert(fclose(file) == 0);
  object.Token = token;
  return object;
}

static int read_object(void *opaque, APPLE_AGX_U64 token,
                       APPLE_AGX_U32 referenceIndex,
                       APPLE_AGX_U32 role, APPLE_AGX_U64 offset,
                       APPLE_AGX_U32 bytes, void *destination) {
  FIXTURE_CONTEXT *context = (FIXTURE_CONTEXT *)opaque;
  FILE_OBJECT *object;
  (void)role;
  if (context == NULL || referenceIndex >= REF_COUNT || destination == NULL)
    return 0;
  object = &context->Objects[referenceIndex];
  if (object->Bytes == NULL || object->Token != token ||
      offset > object->Size || bytes > object->Size - offset)
    return 0;
  memcpy(destination, object->Bytes + (size_t)offset, bytes);
  return 1;
}

static int resolve_object(void *opaque, APPLE_AGX_U64 token,
                          APPLE_AGX_U32 classId,
                          APPLE_AGX_U32 referenceIndex,
                          APPLE_AGX_U32 role, APPLE_AGX_U64 offset,
                          APPLE_AGX_U32 bytes,
                          APPLE_AGX_U64 *gpuVirtualAddress) {
  FIXTURE_CONTEXT *context = (FIXTURE_CONTEXT *)opaque;
  (void)token;
  (void)classId;
  (void)role;
  return context != NULL &&
         AdmissionDynamicOverlayResolve(context->Plan, referenceIndex,
                                        offset, bytes, gpuVirtualAddress) ==
             AdmissionDynamicOverlaySuccess;
}

static void reference(DRAW_COMMAND *command, unsigned index, unsigned role,
                      unsigned access, unsigned allocation,
                      unsigned long long offset, unsigned long long bytes) {
  command->References[index].AllocationIndex = allocation;
  command->References[index].Role = role;
  command->References[index].Access = access;
  command->References[index].Offset = offset;
  command->References[index].Bytes = bytes;
}

static unsigned long long read_le(const unsigned char *bytes, unsigned count) {
  unsigned long long value = 0ULL;
  for (unsigned index = 0u; index < count; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

int main(int argc, char **argv) {
  DRAW_COMMAND command;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  ADMISSION_WIN32_ALLOCATION_FACT facts[REF_COUNT];
  FIXTURE_CONTEXT fixture;
  ADMISSION_BACKEND_IMAGE image;
  ADMISSION_DYNAMIC_OVERLAY_PLAN plan;
  ADMISSION_DYNAMIC_OVERLAY_PLAN restoredPlan;
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS bindings;
  ADMISSION_DYNAMIC_OVERLAY_STATE state;
  APPLE_AGX_DYNAMIC_JOB job;
  ADMISSION_DYNAMIC_DMA_VIEW dmaView;
  unsigned char materialized[1024];
  unsigned char dma[ADMISSION_DYNAMIC_DMA_MAX_BYTES];
  unsigned char *arena;
  unsigned char store0[0x2020];
  unsigned int dmaBytes = 0u;
  const APPLE_AGX_DYNAMIC_JOB_OBJECT *encoderObject = NULL;
  const APPLE_AGX_DYNAMIC_JOB_OBJECT *pipelineObject = NULL;
  assert(argc == 8);
  memset(&command, 0, sizeof(command));
  memset(&facts, 0, sizeof(facts));
  memset(&fixture, 0, sizeof(fixture));
  memset(&image, 0, sizeof(image));
  assert(sizeof(command) == 808u);

  fixture.Objects[2] = load_file(argv[1], 3u);
  fixture.Objects[3] = load_file(argv[2], 4u);
  fixture.Objects[4] = load_file(argv[4], 5u);
  fixture.Objects[6] = load_file(argv[6], 7u);
  fixture.Objects[7] = load_file(argv[7], 8u);
  fixture.Objects[8] = load_file(argv[5], 9u);
  fixture.Objects[9] = fixture.Objects[2];
  fixture.Objects[10] = load_file(argv[3], 11u);
  fixture.Objects[5].Bytes = (unsigned char *)calloc(1u, 4u);
  assert(fixture.Objects[5].Bytes != NULL);
  fixture.Objects[5].Size = 4u;
  fixture.Objects[5].Token = 6u;

  command.Header.Magic = APPLE_AGX_WIN32_COMMAND_MAGIC;
  command.Header.Version = APPLE_AGX_WIN32_COMMAND_VERSION;
  command.Header.HeaderBytes = sizeof(command.Header);
  command.Header.TotalBytes = sizeof(command);
  command.Header.Opcode = AppleAgxWin32OpcodeDraw;
  command.Header.Generation = 7u;
  command.Header.ReferenceCount = REF_COUNT;
  command.Header.ReferencesOffset = sizeof(command.Header);
  command.Header.PayloadOffset =
      sizeof(command.Header) + sizeof(command.References);
  command.Header.PayloadBytes = sizeof(command.Draw) + sizeof(command.Relocations);
  reference(&command, 0u, AppleAgxWin32RoleRenderTarget,
            AppleAgxWin32AccessWrite, 0u, 0u, 0xfa0000ULL);
  reference(&command, 1u, AppleAgxWin32RoleVertex,
            AppleAgxWin32AccessRead, 1u, 0u, 4u);
  reference(&command, 2u, AppleAgxWin32RoleShader,
            AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
            2u, 0u, fixture.Objects[2].Size);
  reference(&command, 3u, AppleAgxWin32RoleShader,
            AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
            3u, 0u, fixture.Objects[3].Size);
  reference(&command, 4u, AppleAgxWin32RoleUscPipeline,
            AppleAgxWin32AccessRead, 4u, 0u, fixture.Objects[4].Size);
  reference(&command, 5u, AppleAgxWin32RoleDescriptor,
            AppleAgxWin32AccessRead, 5u, 0u, 4u);
  reference(&command, 6u, AppleAgxWin32RoleScissor,
            AppleAgxWin32AccessRead, 6u, 0u, fixture.Objects[6].Size);
  reference(&command, 7u, AppleAgxWin32RoleDepthBias,
            AppleAgxWin32AccessRead, 7u, 0u, fixture.Objects[7].Size);
  reference(&command, 8u, AppleAgxWin32RoleEncoder,
            AppleAgxWin32AccessRead, 8u, 0u, fixture.Objects[8].Size);
  reference(&command, 9u, AppleAgxWin32RoleShaderRodata,
            AppleAgxWin32AccessRead, 2u, 0u, 8u);
  reference(&command, 10u, AppleAgxWin32RoleShaderRodata,
            AppleAgxWin32AccessRead, 10u, 0u, 12u);
  command.Draw.StructBytes = sizeof(command.Draw);
  command.Draw.Format = AppleAgxWin32FormatBgra8Unorm;
  command.Draw.SurfaceWidth = 2560u;
  command.Draw.SurfaceHeight = 1600u;
  command.Draw.SurfacePitch = 10240u;
  command.Draw.Topology = AppleAgxWin32TopologyTriangleList;
  command.Draw.VertexCount = 3u;
  command.Draw.InstanceCount = 1u;
  command.Draw.DestinationReference = 0u;
  command.Draw.VertexReference = 1u;
  command.Draw.IndexReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Draw.ConstantReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Draw.TextureReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Draw.VertexShaderReference = 2u;
  command.Draw.FragmentShaderReference = 3u;
  command.Draw.VertexRodataReference = 9u;
  command.Draw.FragmentRodataReference = 10u;
  command.Draw.UscPipelineReference = 4u;
  command.Draw.DescriptorReference = 5u;
  command.Draw.ScissorReference = 6u;
  command.Draw.DepthBiasReference = 7u;
  command.Draw.EncoderReference = 8u;
  command.Draw.RelocationsOffset = sizeof(command.Draw);
  command.Draw.RelocationCount = RELOC_COUNT;
#define RELOC(i, kind, width, dst, target, dstoff, targetoff)                \
  command.Relocations[i] = (APPLE_AGX_WIN32_RELOCATION){                     \
      kind, width, 0u, dst, target, dstoff, targetoff, 0ULL}
  RELOC(0, AppleAgxWin32RelocationUscBufferAddress40, 8u, 4u, 9u, 4u, 0u);
  RELOC(1, AppleAgxWin32RelocationUscShaderOffset32, 6u, 4u, 2u, 12u, 128u);
  RELOC(2, AppleAgxWin32RelocationUscBufferAddress40, 8u, 4u, 10u, 64u, 0u);
  RELOC(3, AppleAgxWin32RelocationUscShaderOffset32, 6u, 4u, 3u, 76u, 0u);
  RELOC(4, AppleAgxWin32RelocationVdmPipelineOffset32, 4u, 8u, 4u, 8u, 0u);
  RELOC(5, AppleAgxWin32RelocationPppStateAddress40, 8u, 8u, 8u, 24u, 128u);
  RELOC(6, AppleAgxWin32RelocationVdmPipelineOffset32, 4u, 8u, 4u, 220u, 64u);
#undef RELOC
  command.Header.ContentHash = AppleAgxWin32CommandHash(&command, sizeof(command));
  assert(AppleAgxWin32CommandValidate(&command, sizeof(command), 7u,
                                      REF_COUNT, &view) ==
         AppleAgxWin32AbiSuccess);

  arena = (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  assert(arena != NULL);
  assert(AppleAgxRenderTemplateMaterialize(arena,
                                           AppleAgxRenderTemplateBytes(),
                                           &image.Roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjects(
      arena, AppleAgxRenderTemplateBytes(), 0x9d3000000ULL, image.Objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  image.Ready = APPLE_AGX_TRUE;
  memcpy(store0, image.Objects[73u].Data, sizeof(store0));
  assert(AdmissionDynamicOverlayPlan(&image, &view, &plan) ==
         AdmissionDynamicOverlaySuccess);
  fixture.Plan = &plan;
  for (unsigned index = 0u; index < REF_COUNT; ++index) {
    facts[index].AllocationToken = index == 9u ? 3u : index + 1u;
    facts[index].Bytes = command.References[index].Offset +
                         command.References[index].Bytes;
    facts[index].Generation = 7u;
    facts[index].ClassId = index == 0u ? 0u :
        ((index == 2u || index == 3u || index == 9u || index == 10u)
             ? AgxWin32BufferClassShader
             : (index >= 4u ? AgxWin32BufferClassEncoder
                            : AgxWin32BufferClassGeneral));
  }
  assert(AppleAgxDynamicJobMaterialize(
      &view, facts, REF_COUNT, 0x1100000000ULL, read_object,
      resolve_object, &fixture, materialized, sizeof(materialized), &job) ==
      AppleAgxDynamicJobSuccess);
  assert(job.ObjectCount == 9u && job.RelocationCount == 7u);
  assert(AdmissionDynamicOverlayBindingsFromView(&view, &bindings) ==
         AdmissionDynamicOverlaySuccess);
  assert(AdmissionDynamicDmaBuild(
      7u, command.Header.ContentHash, 0x1501000000ULL, 0u, 0xff101820u,
      &bindings, &job, materialized, job.StorageBytes, dma, sizeof(dma),
      &dmaBytes) == AdmissionDynamicDmaSuccess);
  assert(dmaBytes <= sizeof(dma));
  assert(AdmissionDynamicDmaOpen(dma, dmaBytes, &dmaView) ==
         AdmissionDynamicDmaSuccess);
  assert(AdmissionDynamicOverlayPlanFromJob(
      &image, dmaView.Bindings, dmaView.Job, &restoredPlan) ==
      AdmissionDynamicOverlaySuccess);
  AdmissionDynamicOverlayStateInitialize(&state);
  assert(AdmissionDynamicOverlayApply(
      &image, &restoredPlan, dmaView.Job, dmaView.Storage,
      dmaView.StorageBytes, 256u, &state) == AdmissionDynamicOverlaySuccess);
  for (unsigned index = 0u; index < job.ObjectCount; ++index) {
    if (job.Objects[index].ReferenceIndex == 8u)
      encoderObject = &job.Objects[index];
    if (job.Objects[index].ReferenceIndex == 4u)
      pipelineObject = &job.Objects[index];
  }
  assert(encoderObject != NULL && pipelineObject != NULL);
  assert(memcmp(image.Objects[71u].Data,
                dmaView.Storage + encoderObject->StorageOffset,
                encoderObject->Bytes) == 0);
  assert(memcmp(image.Objects[73u].Data + 0x10000u,
                dmaView.Storage + pipelineObject->StorageOffset,
                pipelineObject->Bytes) == 0);
  assert((read_le(image.Objects[71u].Data + 8u, 4u) & ~0x3fULL) ==
         0x30000ULL);
  assert((read_le(image.Objects[71u].Data + 220u, 4u) & ~0x3fULL) ==
         0x30040ULL);
  assert((read_le(image.Objects[71u].Data + 24u, 4u) & 0xffULL) ==
         (image.Objects[71u].GpuVa >> 32u));
  assert(read_le(image.Objects[71u].Data + 28u, 4u) ==
         (unsigned int)(image.Objects[71u].GpuVa + 128u));
  assert(memcmp(store0, image.Objects[73u].Data, sizeof(store0)) == 0);
  assert(AdmissionDynamicOverlayRelease(
      &image, &restoredPlan, dmaView.Job, dmaView.Storage,
      dmaView.StorageBytes, 256u, &state) == AdmissionDynamicOverlaySuccess);
  assert(memcmp(store0, image.Objects[73u].Data, sizeof(store0)) == 0);

  free(arena);
  free(fixture.Objects[10].Bytes);
  free(fixture.Objects[8].Bytes);
  free(fixture.Objects[7].Bytes);
  free(fixture.Objects[6].Bytes);
  free(fixture.Objects[5].Bytes);
  free(fixture.Objects[4].Bytes);
  free(fixture.Objects[3].Bytes);
  free(fixture.Objects[2].Bytes);
  return 0;
}
