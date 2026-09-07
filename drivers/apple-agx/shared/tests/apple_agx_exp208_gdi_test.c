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

static void write_u64(unsigned char *bytes, APPLE_AGX_U64 value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    bytes[index] = (unsigned char)(value >> (index * 8u));
}

static APPLE_AGX_U64 read_u64(const unsigned char *bytes) {
  APPLE_AGX_U64 value = 0ULL;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    value |= (APPLE_AGX_U64)bytes[index] << (index * 8u);
  return value;
}

static void init_objects(APPLE_AGX_EXP208_RELOCATION_OBJECT *objects,
                         unsigned char *internal_output,
                         unsigned char *store_descriptors) {
  memset(objects, 0,
         sizeof(*objects) * APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT);
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].GpuVa = 0x1503ae0000ULL;
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].PhysicalAddress =
      0x9d1000000ULL;
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Size =
      APPLE_AGX_EXP208_GDI_OUTPUT_BYTES;
  objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data = internal_output;
  objects[APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT].GpuVa =
      0x1503920000ULL;
  objects[APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT].PhysicalAddress =
      0x9d3000000ULL;
  objects[APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT].Size = 0x4000u;
  objects[APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT].Data =
      store_descriptors;
  write_u64(store_descriptors + APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET,
            0x100000015001d000ULL);
}

static void test_exact_clear_binds_hardware_proven_output_object(void) {
  unsigned char dma[256];
  unsigned char internal_output[APPLE_AGX_EXP208_GDI_OUTPUT_BYTES];
  unsigned char store_descriptors[0x4000];
  unsigned char destination[0x10000];
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_RELOCATION relocation = {
      18u, 148u, APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT, 0u,
      AppleAgxExp208RelocationGpuVa, AppleAgxExp208RelocationExactU64, 0ULL};
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_U32 bytes = make_exact_clear(dma, 0x1500010000ULL);

  memset(store_descriptors, 0, sizeof(store_descriptors));
  init_objects(objects, internal_output, store_descriptors);
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
  assert((read_u64(store_descriptors +
                   APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET) &
          APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_ADDRESS_MASK) ==
         (0x1500010000ULL >> 4u));
  assert(AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data == internal_output);
  assert(read_u64(store_descriptors +
                  APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET) ==
         0x100000015001d000ULL);
}

static void test_wrong_workload_or_physical_edge_is_rejected_atomically(void) {
  unsigned char dma[256];
  unsigned char internal_output[APPLE_AGX_EXP208_GDI_OUTPUT_BYTES];
  unsigned char store_descriptors[0x4000];
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

  memset(store_descriptors, 0, sizeof(store_descriptors));
  init_objects(objects, internal_output, store_descriptors);
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
  write_u64(store_descriptors + APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET,
            0x100000015001c000ULL);
  relocation.AddressSpace = AppleAgxExp208RelocationGpuVa;
  assert(!AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
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
  assert(binding.StoreDescriptorObject == 36u);
  assert(binding.StoreDescriptorOffset == 0x3008u);
  assert((read_u64(objects[36].Data + 0x3008u) &
          APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_ADDRESS_MASK) ==
         (0x1500010000ULL >> 4u));
  free(arena);
}

int main(void) {
  test_exact_clear_binds_hardware_proven_output_object();
  test_wrong_workload_or_physical_edge_is_rejected_atomically();
  test_generated_exp208_graph_has_one_bindable_output_edge();
  return 0;
}
