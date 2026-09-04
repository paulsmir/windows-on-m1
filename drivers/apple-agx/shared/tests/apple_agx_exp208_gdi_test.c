#include "apple_agx_exp208_gdi.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static APPLE_AGX_U32 make_exact_clear(unsigned char *dma,
                                      APPLE_AGX_U64 gpu_va) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_U32 written = 0u;

  memset(&description, 0, sizeof(description));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.Destination =
      (APPLE_AGX_GDI_RECT){0u, 0u, 16u, 16u};
  description.Command.DestinationAllocationIndex = 0u;
  description.Command.DestinationGpuAddress = gpu_va;
  description.Command.DestinationPitch = 64u;
  description.Command.Color = 0xff112233u;
  description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  assert(AppleAgxGdiEncodeDmaCommand(
      &description, dma, 256u, &written));
  return written;
}

static void init_objects(APPLE_AGX_EXP208_RELOCATION_OBJECT *objects,
                         unsigned char *internal_output) {
  memset(objects, 0,
         sizeof(*objects) * APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT);
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].GpuVa = 0x1503ae0000ULL;
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].PhysicalAddress =
      0x9d1000000ULL;
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Size =
      APPLE_AGX_EXP208_GDI_OUTPUT_BYTES;
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data = internal_output;
}

static void test_exact_clear_binds_hardware_proven_output_object(void) {
  unsigned char dma[256];
  unsigned char internal_output[APPLE_AGX_EXP208_GDI_OUTPUT_BYTES];
  unsigned char destination[0x10000];
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_RELOCATION relocation = {
      18u, 148u, APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT, 0u,
      AppleAgxExp208RelocationGpuVa, AppleAgxExp208RelocationExactU64, 0ULL};
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_U32 bytes = make_exact_clear(dma, 0x1500010000ULL);

  init_objects(objects, internal_output);
  memset(&binding, 0, sizeof(binding));
  assert(AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
  assert(binding.OutputObject == APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT);
  assert(binding.DestinationGpuVa == 0x1500010000ULL);
  assert(binding.DestinationPhysical == 0x9d2000000ULL);
  assert(binding.DestinationBytes == APPLE_AGX_EXP208_GDI_OUTPUT_BYTES);
  assert(objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].GpuVa ==
         binding.DestinationGpuVa);
  assert(objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].PhysicalAddress ==
         binding.DestinationPhysical);
  assert(objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Size ==
         APPLE_AGX_EXP208_GDI_OUTPUT_BYTES);
  assert(objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data == destination);
}

static void test_wrong_workload_or_physical_edge_is_rejected_atomically(void) {
  unsigned char dma[256];
  unsigned char internal_output[APPLE_AGX_EXP208_GDI_OUTPUT_BYTES];
  unsigned char destination[0x10000];
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_RELOCATION_OBJECT before;
  APPLE_AGX_EXP208_RELOCATION relocation = {
      18u, 148u, APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT, 0u,
      AppleAgxExp208RelocationGpuVa, AppleAgxExp208RelocationExactU64, 0ULL};
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_GDI_DMA_COMMAND *command = (APPLE_AGX_GDI_DMA_COMMAND *)dma;
  APPLE_AGX_U32 bytes = make_exact_clear(dma, 0x1500010000ULL);

  init_objects(objects, internal_output);
  before = objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT];
  command->Color ^= 1u;
  assert(!AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
  assert(memcmp(&before,
                &objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT],
                sizeof(before)) == 0);
  command->Color ^= 1u;
  relocation.AddressSpace = AppleAgxExp208RelocationPhysical;
  assert(!AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
  assert(memcmp(&before,
                &objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT],
                sizeof(before)) == 0);
}

static void test_generated_exp208_graph_has_one_bindable_output_edge(void) {
  unsigned char dma[256];
  unsigned char destination[0x10000];
  unsigned char *arena =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_U32 bytes = make_exact_clear(dma, 0x1500010000ULL);

  assert(arena != NULL);
  assert(AppleAgxRenderTemplateMaterialize(
      arena, AppleAgxRenderTemplateBytes(), &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjects(
      arena, AppleAgxRenderTemplateBytes(), 0x9d3000000ULL, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  assert(AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),
      AppleAgxRenderTemplateRelocationCount(), &binding));
  assert(binding.OutputObject == 40u);
  free(arena);
}

int main(void) {
  test_exact_clear_binds_hardware_proven_output_object();
  test_wrong_workload_or_physical_edge_is_rejected_atomically();
  test_generated_exp208_graph_has_one_bindable_output_edge();
  return 0;
}
