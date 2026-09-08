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

static APPLE_AGX_U32 make_fullscreen_clear(unsigned char *dma,
                                           APPLE_AGX_U64 gpu_va) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_U32 written = 0u;

  memset(&description, 0, sizeof(description));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.Destination =
      (APPLE_AGX_GDI_RECT){0u, 0u, 2560u, 1600u};
  description.Command.DestinationAllocationIndex = 0u;
  description.Command.DestinationGpuAddress = gpu_va;
  description.Command.DestinationPitch = 10240u;
  description.Command.Color = 0xff112233u;
  description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  assert(AppleAgxGdiEncodeDmaCommand(
      &description, dma, 256u, &written));
  return written;
}

static APPLE_AGX_U32 make_fullscreen_color_clear(
    unsigned char *dma, APPLE_AGX_U64 gpu_va, APPLE_AGX_U32 color) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_U32 written = 0u;
  memset(&description, 0, sizeof(description));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.Destination = (APPLE_AGX_GDI_RECT){
      0u, 0u, APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH,
      APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT};
  description.Command.DestinationGpuAddress = gpu_va;
  description.Command.DestinationPitch = APPLE_AGX_EXP208_FRAMEBUFFER_PITCH;
  description.Command.Color = color;
  description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  assert(AppleAgxGdiEncodeDmaCommand(&description, dma, 256u, &written));
  return written;
}

static APPLE_AGX_U32 make_bottom_band_clear(unsigned char *dma,
                                            APPLE_AGX_U64 gpu_va,
                                            APPLE_AGX_U32 color) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_U32 written = 0u;
  memset(&description, 0, sizeof(description));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.Destination = (APPLE_AGX_GDI_RECT){
      0u, APPLE_AGX_EXP208_FRAMEBUFFER_BAND_TOP,
      APPLE_AGX_EXP208_FRAMEBUFFER_WIDTH,
      APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT};
  description.Command.DestinationGpuAddress = gpu_va;
  description.Command.DestinationPitch = APPLE_AGX_EXP208_FRAMEBUFFER_PITCH;
  description.Command.Color = color;
  description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  assert(AppleAgxGdiEncodeDmaCommand(&description, dma, 256u, &written));
  return written;
}

static void write_u64(unsigned char *bytes, APPLE_AGX_U64 value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    bytes[index] = (unsigned char)(value >> (index * 8u));
}

static void write_u32(unsigned char *bytes, APPLE_AGX_U32 value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 4u; ++index)
    bytes[index] = (unsigned char)(value >> (index * 8u));
}

static APPLE_AGX_U64 read_u64(const unsigned char *bytes) {
  APPLE_AGX_U64 value = 0ULL;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    value |= (APPLE_AGX_U64)bytes[index] << (index * 8u);
  return value;
}

static APPLE_AGX_U32 read_u32(const unsigned char *bytes) {
  return (APPLE_AGX_U32)bytes[0] |
         ((APPLE_AGX_U32)bytes[1] << 8u) |
         ((APPLE_AGX_U32)bytes[2] << 16u) |
         ((APPLE_AGX_U32)bytes[3] << 24u);
}

static APPLE_AGX_U32 read_u16(const unsigned char *bytes) {
  return (APPLE_AGX_U32)bytes[0] |
         ((APPLE_AGX_U32)bytes[1] << 8u);
}

static APPLE_AGX_U64 read_usc_buffer(const unsigned char *bytes) {
  return ((read_u64(bytes) >> 27u) & 0xfffffffffULL) << 3u;
}

static void init_objects(APPLE_AGX_EXP208_RELOCATION_OBJECT *objects,
                         unsigned char *internal_output,
                         unsigned char *store_descriptors,
                         unsigned char *store_pipeline) {
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
  objects[APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT].Size = 0x40000u;
  objects[APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OBJECT].Data =
      store_descriptors;
  write_u64(store_descriptors + APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET,
            0x100000015001d000ULL);
  objects[73u].GpuVa = 0x1503d88000ULL;
  objects[73u].PhysicalAddress = 0x9d3400000ULL;
  objects[73u].Size = 0x40000u;
  objects[73u].Data = store_pipeline;
  write_u64(store_pipeline, 0x1500000000400c1dULL);
  write_u64(store_pipeline + 0x1000u, 0x15000020001000ddULL);
  write_u64(store_pipeline + 0x1008u, 0x150000100010009dULL);
  write_u64(store_pipeline + 0x2000u, 0x15000030001000ddULL);
  write_u64(store_pipeline + 0x2008u, 0x150000400040041dULL);
}

static void test_exact_clear_binds_hardware_proven_output_object(void) {
  unsigned char dma[256];
  unsigned char internal_output[APPLE_AGX_EXP208_GDI_OUTPUT_BYTES];
  unsigned char store_descriptors[0x40000];
  unsigned char store_pipeline[0x40000];
  unsigned char destination[0x10000];
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_RELOCATION relocation = {
      18u, 148u, APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT, 0u,
      AppleAgxExp208RelocationGpuVa, AppleAgxExp208RelocationExactU64, 0ULL};
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_U32 bytes = make_exact_clear(dma, 0x1500010000ULL);

  memset(store_descriptors, 0, sizeof(store_descriptors));
  memset(store_pipeline, 0, sizeof(store_pipeline));
  init_objects(objects, internal_output, store_descriptors, store_pipeline);
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
  assert(read_usc_buffer(store_pipeline) == 0x1503920000ULL);
  assert(read_u64(store_pipeline + 0x1000u) ==
         0x15000020001000ddULL);
  assert(read_u64(store_pipeline + 0x1008u) ==
         0x150000100010009dULL);
  assert(read_usc_buffer(store_pipeline + 0x2000u) == 0x1503923000ULL);
  assert(read_usc_buffer(store_pipeline + 0x2008u) == 0x1503924000ULL);
  assert(AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT].Data == internal_output);
  assert(read_u64(store_descriptors +
                  APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET) ==
         0x100000015001d000ULL);
  assert(read_u64(store_pipeline) == 0x1500000000400c1dULL);
  assert(read_u64(store_pipeline + 0x2000u) == 0x15000030001000ddULL);
  assert(read_u64(store_pipeline + 0x2008u) == 0x150000400040041dULL);
}

static void test_wrong_workload_or_physical_edge_is_rejected_atomically(void) {
  unsigned char dma[256];
  unsigned char internal_output[APPLE_AGX_EXP208_GDI_OUTPUT_BYTES];
  unsigned char store_descriptors[0x40000];
  unsigned char store_pipeline[0x40000];
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
  memset(store_pipeline, 0, sizeof(store_pipeline));
  init_objects(objects, internal_output, store_descriptors, store_pipeline);
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
  write_u64(store_descriptors + APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET,
            0x100000015001c000ULL);
  relocation.AddressSpace = AppleAgxExp208RelocationGpuVa;
  assert(!AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
  relocation.AddressSpace = AppleAgxExp208RelocationPhysical;
  assert(!AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
  assert(memcmp(&before,
                &objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT],
                sizeof(before)) == 0);

  relocation.AddressSpace = AppleAgxExp208RelocationGpuVa;
  write_u64(store_descriptors + APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET,
            0x100000015001d000ULL);
  write_u64(store_pipeline + 0x2008u, 0x150000500040041dULL);
  assert(!AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
  assert(read_u64(store_descriptors +
                  APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET) ==
         0x100000015001d000ULL);
  assert(read_u64(store_pipeline + 0x2000u) == 0x15000030001000ddULL);
  assert(read_u64(store_pipeline + 0x2008u) == 0x150000500040041dULL);
  assert(memcmp(&before,
                &objects[APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT],
                sizeof(before)) == 0);

  write_u64(store_pipeline + 0x2008u, 0x150000400040041dULL);
  write_u64(store_pipeline, 0x1500001000400c1dULL);
  assert(!AppleAgxExp208BindGdiColorFill(
      dma, bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      sizeof(destination), objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &relocation, 1u,
      &binding));
  assert(read_u64(store_descriptors +
                  APPLE_AGX_EXP208_GDI_STORE_DESCRIPTOR_OFFSET) ==
         0x100000015001d000ULL);
  assert(read_u64(store_pipeline) == 0x1500001000400c1dULL);
  assert(read_u64(store_pipeline + 0x2000u) == 0x15000030001000ddULL);
  assert(read_u64(store_pipeline + 0x2008u) == 0x150000400040041dULL);
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
  assert(read_usc_buffer(objects[73].Data) == objects[36].GpuVa);
  assert(read_u64(objects[73].Data + 0x1000u) ==
         0x15000020001000ddULL);
  assert(read_usc_buffer(objects[73].Data + 0x2000u) ==
         objects[36].GpuVa + 0x3000u);
  assert(read_usc_buffer(objects[73].Data + 0x2008u) ==
         objects[36].GpuVa + 0x4000u);
  assert(AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(read_u64(objects[73].Data) == 0x1500000000400c1dULL);
  assert(read_u64(objects[73].Data + 0x2000u) ==
         0x15000030001000ddULL);
  assert(read_u64(objects[73].Data + 0x2008u) ==
         0x150000400040041dULL);
  free(arena);
}

static void test_fullscreen_clear_binds_exact_g13_geometry_and_rolls_back(void) {
  const APPLE_AGX_U32 backend_bytes = 0x800000u;
  const APPLE_AGX_U32 surface_bytes = 0xfa0000u;
  unsigned char dma[256];
  unsigned char *arena = (unsigned char *)malloc(backend_bytes);
  unsigned char *before =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  unsigned char *destination = (unsigned char *)malloc(surface_bytes);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_U32 bytes =
      make_fullscreen_clear(dma, 0x1500010000ULL);
  APPLE_AGX_U64 pbe0;
  APPLE_AGX_U64 pbe1;
  APPLE_AGX_U32 index;
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;

  assert(arena != NULL && before != NULL && destination != NULL);
  memset(arena, 0xa5, backend_bytes);
  assert(AppleAgxRenderTemplateMaterialize(arena, backend_bytes, &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjects(
      arena, backend_bytes, 0x9d3000000ULL, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  layouts = AppleAgxRenderTemplateObjectLayouts();
  assert(objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].Data == arena);
  assert(objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].GpuVa ==
         0x1503800000ULL);
  assert(objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX]
             .PhysicalAddress == 0x9d3000000ULL);
  for (index = 0u; index < AppleAgxRenderTemplateObjectCount(); ++index) {
    assert(objects[index].Data == arena + layouts[index].ArenaOffset);
    assert(objects[index].GpuVa ==
           0x1503800000ULL + layouts[index].ArenaOffset);
    assert(objects[index].PhysicalAddress ==
           0x9d3000000ULL + layouts[index].ArenaOffset);
    assert(objects[index].Size == layouts[index].Size);
  }
  assert(read_u32(objects[18u].Data + 0x68u) == 0x3dddb3d9u);
  assert(read_u32(objects[18u].Data + 0x3d8u) == 0x3dddb3d9u);
  assert(read_u32(objects[19u].Data + 0x3d8u) == 0x10100cu);
  assert(read_u64(objects[36u].Data + 0x3000u) ==
         0x000003c00fc60a22ULL);
  assert((read_u64(objects[36u].Data + 0x3008u) & 0xfffffffffULL) ==
         (0x15001d0000ULL >> 4u));
  memcpy(before, arena, AppleAgxRenderTemplateBytes());
  memset(&binding, 0, sizeof(binding));

  assert(AppleAgxExp208BindGdiFramebufferColorFill(
      dma, bytes, arena, 0x1503800000ULL, 0x9d3000000ULL,
      backend_bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      surface_bytes, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),
      AppleAgxRenderTemplateRelocationCount(), &binding));
  assert(binding.DestinationBytes == surface_bytes);
  assert(objects[40u].Data == destination &&
         objects[40u].Size == surface_bytes);
  assert(objects[64u].Data == arena + 0x5d0000u &&
         objects[64u].GpuVa == 0x1503dd0000ULL &&
         objects[64u].PhysicalAddress == 0x9d35d0000ULL &&
         objects[64u].Size == 0x50000u);
  assert(objects[65u].Data == arena + 0x620000u &&
         objects[65u].GpuVa == 0x1503e20000ULL &&
         objects[65u].PhysicalAddress == 0x9d3620000ULL &&
         objects[65u].Size == 0x6400u);
  assert(objects[67u].Data == arena + 0x628000u &&
         objects[67u].GpuVa == 0x1503e28000ULL &&
         objects[67u].PhysicalAddress == 0x9d3628000ULL &&
         objects[67u].Size == 0x32000u);

  assert(read_u16(objects[18u].Data + 0x54u) == 16u);
  assert(read_u16(objects[18u].Data + 0x56u) == 20u);
  assert(read_u64(objects[18u].Data + 0x78u) == 4000u);
  assert(read_u32(objects[18u].Data + 0xb8u) == 2560u);
  assert(read_u32(objects[18u].Data + 0xbcu) == 1600u);
  assert(read_u64(objects[18u].Data + 0xc8u) == 0x31f89ffULL);
  assert(read_u64(objects[18u].Data + 0x170u) == 0x190000000ULL);
  assert(read_u16(objects[18u].Data + 0x3e8u) == 16u);
  assert(read_u16(objects[18u].Data + 0x3eau) == 20u);
  assert(read_u32(objects[18u].Data + 0x3f0u) == 0x3104fu);
  assert(read_u32(objects[18u].Data + 0x6e0u) == 2560u);
  assert(read_u32(objects[18u].Data + 0x6e4u) == 1600u);
  assert(read_u64(objects[18u].Data + 0x768u) == 0x31f89ffULL);

  assert(read_u32(objects[19u].Data + 0x3c4u) == 400u);
  assert(read_u16(objects[19u].Data + 0x3d0u) == 2559u);
  assert(read_u16(objects[19u].Data + 0x3d2u) == 1599u);
  assert(read_u32(objects[19u].Data + 0x3d4u) == 0x3104fu);
  assert(read_u32(objects[19u].Data + 0x3d8u) == 0x50503cu);
  assert(read_u32(objects[19u].Data + 0x3dcu) == 0x404030u);
  assert(read_u32(objects[19u].Data + 0x3e0u) == 320u);
  assert(read_u32(objects[19u].Data + 0x3e4u) == 640u);
  assert(read_u64(objects[19u].Data + 0x46cu) == 0x50000u);

  pbe0 = read_u64(objects[36u].Data + 0x3000u);
  pbe1 = read_u64(objects[36u].Data + 0x3008u);
  assert((pbe0 & 0xfu) == 2u);
  assert(((pbe0 >> 4u) & 3u) == 0u);
  assert(((pbe0 >> 24u) & 0x3fffu) + 1u == 2560u);
  assert(((pbe0 >> 38u) & 0x3fffu) + 1u == 1600u);
  assert((pbe1 & 0xfffffffffULL) == (0x1500010000ULL >> 4u));
  assert(((pbe1 >> 40u) & 0x1fffffu) == 10236u);
  assert(pbe0 == binding.Framebuffer.BoundPbe0);
  assert((pbe1 & ~0xfffffffffULL) ==
         (binding.Framebuffer.BoundPbe1 & ~0xfffffffffULL));
  assert(binding.Framebuffer.ArenaCapacity == backend_bytes);
  assert(objects[64u].Data == arena + 0x5d0000u &&
         objects[64u].Size == 0x50000u);
  assert(objects[65u].Data == arena + 0x620000u &&
         objects[65u].Size == 0x6400u);
  assert(objects[67u].Data == arena + 0x628000u &&
         objects[67u].Size == 0x32000u);

  write_u32(objects[18u].Data + 0x3f0u, 0x3104eu);
  assert(!AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(objects[40u].Data == destination);
  assert(read_u64(objects[36u].Data + 0x3008u) == pbe1);
  write_u32(objects[18u].Data + 0x3f0u, 0x3104fu);
  assert(AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(memcmp(before, arena, AppleAgxRenderTemplateBytes()) == 0);
  for (index = 0x5d0000u; index < 0x65a000u; ++index)
    assert(arena[index] == 0u);
  free(destination);
  free(before);
  free(arena);
}

static void test_fullscreen_rejects_short_backend_without_mutation(void) {
  unsigned char dma[256];
  unsigned char *arena =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  unsigned char *before =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  unsigned char *destination = (unsigned char *)malloc(0xfa0000u);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_RELOCATION_OBJECT objects_before[
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_U32 bytes =
      make_fullscreen_clear(dma, 0x1500010000ULL);

  assert(arena != NULL && before != NULL && destination != NULL);
  assert(AppleAgxRenderTemplateMaterialize(
      arena, AppleAgxRenderTemplateBytes(), &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjects(
      arena, AppleAgxRenderTemplateBytes(), 0x9d3000000ULL, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  memcpy(before, arena, AppleAgxRenderTemplateBytes());
  memcpy(objects_before, objects, sizeof(objects));
  assert(!AppleAgxExp208BindGdiFramebufferColorFill(
      dma, bytes, arena, 0x1503800000ULL, 0x9d3000000ULL,
      AppleAgxRenderTemplateBytes(), destination,
      0x1500010000ULL, 0x9d2000000ULL, 0xfa0000u, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),
      AppleAgxRenderTemplateRelocationCount(), &binding));
  assert(memcmp(before, arena, AppleAgxRenderTemplateBytes()) == 0);
  assert(memcmp(objects_before, objects, sizeof(objects)) == 0);
  free(destination);
  free(before);
  free(arena);
}

static void test_bottom_band_uses_aligned_subregion_and_distinct_fp16_color(void) {
  const APPLE_AGX_U32 backend_bytes = 0x800000u;
  unsigned char dma[256];
  unsigned char *arena = (unsigned char *)malloc(backend_bytes);
  unsigned char *before =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  unsigned char *destination =
      (unsigned char *)malloc(APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_GDI_BINDING binding;
  const APPLE_AGX_U32 color = 0xff0000ffu;
  APPLE_AGX_U32 bytes = make_bottom_band_clear(
      dma, 0x1500010000ULL, color);
  APPLE_AGX_U64 pbe0;
  APPLE_AGX_U64 pbe1;

  assert(arena != NULL && before != NULL && destination != NULL);
  memset(arena, 0xa5, backend_bytes);
  assert(AppleAgxRenderTemplateMaterialize(arena, backend_bytes, &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjects(
      arena, backend_bytes, 0x9d3000000ULL, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  memcpy(before, arena, AppleAgxRenderTemplateBytes());
  assert(AppleAgxExp208BindGdiFramebufferColorFill(
      dma, bytes, arena, 0x1503800000ULL, 0x9d3000000ULL,
      backend_bytes, destination, 0x1500010000ULL, 0x9d2000000ULL,
      APPLE_AGX_EXP208_FRAMEBUFFER_BYTES, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),
      AppleAgxRenderTemplateRelocationCount(), &binding));
  assert(binding.DestinationGpuVa ==
         0x1500010000ULL + APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET);
  assert(binding.DestinationPhysical ==
         0x9d2000000ULL + APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET);
  assert(binding.DestinationBytes == APPLE_AGX_EXP208_FRAMEBUFFER_BAND_BYTES);
  assert(objects[40u].Data ==
         destination + APPLE_AGX_EXP208_FRAMEBUFFER_BAND_OFFSET);
  assert(binding.Framebuffer.RenderHeight == 800u);
  assert(binding.Framebuffer.ClearColor ==
         color);
  assert(read_u64(objects[36u].Data) == 0x3c003c0000000000ULL);
  assert(read_u16(objects[18u].Data + 0x54u) == 8u);
  assert(read_u16(objects[18u].Data + 0x56u) == 20u);
  assert(read_u32(objects[18u].Data + 0x68u) == 0x3a315caeu);
  assert(read_u32(objects[18u].Data + 0x6cu) == 0x3b0de3beu);
  assert(read_u64(objects[18u].Data + 0x78u) == 2000u);
  assert(read_u32(objects[18u].Data + 0xb8u) == 2560u);
  assert(read_u32(objects[18u].Data + 0xbcu) == 800u);
  assert(read_u64(objects[18u].Data + 0xc8u) == 0x18f89ffULL);
  assert(read_u64(objects[18u].Data + 0x170u) == 0xc8000000ULL);
  assert(read_u32(objects[18u].Data + 0x3d8u) == 0x3a315caeu);
  assert(read_u32(objects[18u].Data + 0x3dcu) == 0x3b0de3beu);
  assert(read_u16(objects[18u].Data + 0x3e8u) == 8u);
  assert(read_u16(objects[18u].Data + 0x3eau) == 20u);
  assert(read_u32(objects[18u].Data + 0x3f0u) == 0x1804fu);
  assert(read_u32(objects[18u].Data + 0x6e0u) == 2560u);
  assert(read_u32(objects[18u].Data + 0x6e4u) == 800u);
  assert(read_u64(objects[18u].Data + 0x768u) == 0x18f89ffULL);
  assert(read_u32(objects[19u].Data + 0x3c4u) == 200u);
  assert(read_u16(objects[19u].Data + 0x3d0u) == 2559u);
  assert(read_u16(objects[19u].Data + 0x3d2u) == 799u);
  assert(read_u32(objects[19u].Data + 0x3d4u) == 0x1804fu);
  assert(read_u32(objects[19u].Data + 0x3d8u) == 0x50503cu);
  assert(read_u32(objects[19u].Data + 0x3dcu) == 0x202018u);
  assert(read_u32(objects[19u].Data + 0x3e0u) == 160u);
  assert(read_u32(objects[19u].Data + 0x3e4u) == 320u);
  assert(read_u64(objects[19u].Data + 0x46cu) == 0x28000u);
  pbe0 = read_u64(objects[36u].Data + 0x3000u);
  pbe1 = read_u64(objects[36u].Data + 0x3008u);
  assert(((pbe0 >> 38u) & 0x3fffu) + 1u == 800u);
  assert((pbe1 & 0xfffffffffULL) ==
         ((0x1500010000ULL + 0x7d0000ULL) >> 4u));
  assert(((pbe1 >> 40u) & 0x1fffffu) == 10236u);
  assert(pbe0 == binding.Framebuffer.BoundPbe0);
  assert((pbe1 & ~0xfffffffffULL) ==
         (binding.Framebuffer.BoundPbe1 & ~0xfffffffffULL));
  assert(read_u64(objects[36u].Data) ==
         binding.Framebuffer.BoundClearColor);
  assert(read_u64(objects[36u].Data + 0x3010u) == 0u);
  assert(binding.Framebuffer.Active == APPLE_AGX_TRUE);
  assert(binding.Framebuffer.ArenaCpuAddress == arena);
  assert(binding.Framebuffer.ArenaGpuAddress == 0x1503800000ULL);
  assert(binding.Framebuffer.ArenaPhysicalAddress == 0x9d3000000ULL);
  assert(binding.Framebuffer.ArenaCapacity == backend_bytes);
  assert(objects[64u].Data == arena + 0x5d0000u &&
         objects[64u].GpuVa == 0x1503dd0000ULL &&
         objects[64u].PhysicalAddress == 0x9d35d0000ULL &&
         objects[64u].Size == 0x50000u);
  assert(objects[65u].Data == arena + 0x620000u &&
         objects[65u].GpuVa == 0x1503e20000ULL &&
         objects[65u].PhysicalAddress == 0x9d3620000ULL &&
         objects[65u].Size == 0x6400u);
  assert(objects[67u].Data == arena + 0x628000u &&
         objects[67u].GpuVa == 0x1503e28000ULL &&
         objects[67u].PhysicalAddress == 0x9d3628000ULL &&
         objects[67u].Size == 0x32000u);
  assert(AppleAgxExp208FramebufferCanUnbind(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      &binding.Framebuffer));
  assert(AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(memcmp(before, arena, AppleAgxRenderTemplateBytes()) == 0);
  free(destination);
  free(before);
  free(arena);
}

static void test_two_fullscreen_ping_pong_colors_bind_after_exact_release(void) {
  const APPLE_AGX_U32 backend_bytes =
      APPLE_AGX_EXP208_FRAMEBUFFER_BACKEND_BYTES;
  unsigned char dma[256];
  unsigned char *arena = (unsigned char *)malloc(backend_bytes);
  unsigned char *before =
      (unsigned char *)malloc(AppleAgxRenderTemplateBytes());
  unsigned char *destination0 =
      (unsigned char *)malloc(APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  unsigned char *destination1 =
      (unsigned char *)malloc(APPLE_AGX_EXP208_FRAMEBUFFER_BYTES);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  APPLE_AGX_EXP208_GDI_BINDING binding;
  APPLE_AGX_U32 bytes;

  assert(arena != NULL && before != NULL &&
         destination0 != NULL && destination1 != NULL);
  assert(AppleAgxRenderTemplateMaterialize(arena, backend_bytes, &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjects(
      arena, backend_bytes, 0x9d3000000ULL, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT));
  memcpy(before, arena, AppleAgxRenderTemplateBytes());

  bytes = make_fullscreen_color_clear(
      dma, 0x1500010000ULL, APPLE_AGX_EXP208_FRAMEBUFFER_BASE_COLOR);
  assert(AppleAgxExp208BindGdiFramebufferColorFill(
      dma, bytes, arena, 0x1503800000ULL, 0x9d3000000ULL,
      backend_bytes, destination0, 0x1500010000ULL, 0x9d2000000ULL,
      APPLE_AGX_EXP208_FRAMEBUFFER_BYTES, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),
      AppleAgxRenderTemplateRelocationCount(), &binding));
  assert(AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(memcmp(before, arena, AppleAgxRenderTemplateBytes()) == 0);

  bytes = make_fullscreen_color_clear(
      dma, 0x1500fb0000ULL, 0xff00ff00u);
  assert(AppleAgxExp208BindGdiFramebufferColorFill(
      dma, bytes, arena, 0x1503800000ULL, 0x9d3000000ULL,
      backend_bytes, destination1, 0x1500fb0000ULL, 0x9d2fa0000ULL,
      APPLE_AGX_EXP208_FRAMEBUFFER_BYTES, objects,
      APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
      AppleAgxRenderTemplateRelocations(),
      AppleAgxRenderTemplateRelocationCount(), &binding));
  assert(binding.Framebuffer.RenderHeight ==
         APPLE_AGX_EXP208_FRAMEBUFFER_HEIGHT);
  assert(binding.Framebuffer.ClearColor ==
         0xff00ff00u);
  assert(read_u64(objects[36u].Data) == 0x3c0000003c000000ULL);
  assert(binding.DestinationGpuVa == 0x1500fb0000ULL);
  assert(binding.DestinationPhysical == 0x9d2fa0000ULL);
  assert(AppleAgxExp208UnbindGdiColorFill(
      objects, APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT, &binding));
  assert(memcmp(before, arena, AppleAgxRenderTemplateBytes()) == 0);
  free(destination1);
  free(destination0);
  free(before);
  free(arena);
}

int main(void) {
  assert(AppleAgxExp208PackClearColor(0x00000000u) == 0x0000000000000000ULL);
  assert(AppleAgxExp208PackClearColor(0xffffffffu) == 0x3c003c003c003c00ULL);
  assert(AppleAgxExp208PackClearColor(0xff112233u) == 0x3c00326630442c44ULL);
  assert(AppleAgxExp208PackClearColor(0xffcc8844u) == 0x3c00344438443a66ULL);
  test_exact_clear_binds_hardware_proven_output_object();
  test_wrong_workload_or_physical_edge_is_rejected_atomically();
  test_generated_exp208_graph_has_one_bindable_output_edge();
  test_fullscreen_clear_binds_exact_g13_geometry_and_rolls_back();
  test_fullscreen_rejects_short_backend_without_mutation();
  test_bottom_band_uses_aligned_subregion_and_distinct_fp16_color();
  test_two_fullscreen_ping_pong_colors_bind_after_exact_release();
  return 0;
}
