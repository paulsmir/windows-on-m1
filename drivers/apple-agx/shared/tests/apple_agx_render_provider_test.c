#include "apple_agx_render_provider.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct _FAKE_PUBLICATION {
  unsigned char Region[J313_AGX_G2_GPU_SIZE];
  unsigned int MapCalls;
  unsigned int UnmapCalls;
  unsigned int BarrierCalls;
} FAKE_PUBLICATION;

static unsigned char map_region(void *context, unsigned long long address,
                                unsigned int length,
                                volatile unsigned char **virtual_address) {
  FAKE_PUBLICATION *fake = (FAKE_PUBLICATION *)context;
  if (address != J313_AGX_G2_GPU_BASE ||
      length != (unsigned int)J313_AGX_G2_GPU_SIZE)
    return 0u;
  ++fake->MapCalls;
  *virtual_address = fake->Region;
  return 1u;
}

static void barrier(void *context) {
  ++((FAKE_PUBLICATION *)context)->BarrierCalls;
}

static unsigned char unmap_region(void *context,
                                  volatile unsigned char *virtual_address) {
  FAKE_PUBLICATION *fake = (FAKE_PUBLICATION *)context;
  if (virtual_address != fake->Region)
    return 0u;
  ++fake->UnmapCalls;
  return 1u;
}

static unsigned long long get_u64(const unsigned char *source) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)source[index] << (index * 8u);
  return value;
}

static void put_u64(unsigned char *destination, unsigned long long value) {
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    destination[index] = (unsigned char)(value >> (index * 8u));
}

static void configure_provider(APPLE_AGX_RENDER_PROVIDER *provider,
                               APPLE_AGX_MEMORY_OBJECT *pool,
                               unsigned char *pool_bytes,
                               APPLE_AGX_INITDATA_MEMORY_GRAPH *initdata,
                               APPLE_AGX_CONFIG_SNAPSHOT *snapshot,
                               APPLE_AGX_UAT_ROOTS *render_roots,
                               FAKE_PUBLICATION *publication) {
  APPLE_AGX_RENDER_PROVIDER_CONFIG config;
  APPLE_AGX_UAT_PUBLICATION_IO publication_io;

  memset(provider, 0, sizeof(*provider));
  memset(pool, 0, sizeof(*pool));
  memset(initdata, 0, sizeof(*initdata));
  memset(snapshot, 0, sizeof(*snapshot));
  memset(render_roots, 0, sizeof(*render_roots));
  memset(publication, 0, sizeof(*publication));
  memset(&config, 0, sizeof(config));
  memset(&publication_io, 0, sizeof(publication_io));

  pool->CpuAddress = pool_bytes;
  pool->DeviceAddress = 0x900000000ULL;
  pool->Length = APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES;
  pool->GpuVirtualAddress = APPLE_AGX_BACKEND_BRIDGE_GDI_GPU_BASE;
  pool->Context = APPLE_AGX_RENDER_PROVIDER_CONTEXT;
  pool->State = AppleAgxMemoryGpuMapped;
  initdata->Initialized = 1u;
  initdata->Built = 1u;
  initdata->InitdataVirtualAddress = J313_AGX_G2_KERNEL_VA_BASE;
  initdata->InitdataDeviceAddress = 0x880000000ULL;
  initdata->TtbrPair.Ttbr0 = 0x800001ULL;
  initdata->TtbrPair.Ttbr1 = 0x804001ULL;
  snapshot->GpuRegionBase = J313_AGX_G2_GPU_BASE;
  render_roots->Ttbr0PhysicalAddress = 0x840000ULL;
  render_roots->Ttbr1PhysicalAddress = 0x844000ULL;
  publication_io.Context = publication;
  publication_io.Map = map_region;
  publication_io.Barrier = barrier;
  publication_io.Unmap = unmap_region;

  config.PreparedPool = pool;
  config.PreparedRoots.Ta[0] = APPLE_AGX_EXP208_TA_WORK_ROOT;
  config.PreparedRoots.Ta[1] = APPLE_AGX_EXP208_TA_SECONDARY_ROOT;
  config.PreparedRoots.D3[0] = APPLE_AGX_EXP208_D3_WORK_ROOT;
  config.PreparedRoots.D3[1] = APPLE_AGX_EXP208_D3_SECONDARY_ROOT;
  config.Initdata = initdata;
  config.Snapshot = snapshot;
  config.RenderRoots = render_roots;
  config.PublicationIo = &publication_io;
  assert(AppleAgxRenderProviderInitialize(provider, &config));
}

static void test_publish_and_unpublish_exact_context(void) {
  APPLE_AGX_RENDER_PROVIDER provider;
  APPLE_AGX_BACKEND_IO io;
  APPLE_AGX_MEMORY_OBJECT pool;
  APPLE_AGX_INITDATA_MEMORY_GRAPH initdata;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot;
  APPLE_AGX_UAT_ROOTS render_roots;
  FAKE_PUBLICATION publication;
  unsigned char *pool_bytes = (unsigned char *)malloc(
      APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES);
  unsigned int offset = APPLE_AGX_RENDER_PROVIDER_CONTEXT * 16u;

  assert(pool_bytes != NULL);
  memset(pool_bytes, 0x5a, APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES);
  configure_provider(&provider, &pool, pool_bytes, &initdata, &snapshot,
                     &render_roots, &publication);
  memset(&io, 0, sizeof(io));
  assert(AppleAgxRenderProviderInstallIo(&provider, &io));
  assert(io.Context == &provider);
  assert(io.Image.Relocate == AppleAgxRenderProviderRelocate);
  assert(io.RenderContext.Publish == AppleAgxRenderProviderPublish);
  assert(io.RenderContext.Unpublish == AppleAgxRenderProviderUnpublish);
  put_u64(publication.Region + offset, 0x1111111111111111ULL);
  put_u64(publication.Region + offset + 8u, 0x2222222222222222ULL);

  assert(AppleAgxRenderProviderPublish(&provider));
  assert(provider.ContextPublication.Active == 1u);
  assert(get_u64(publication.Region + offset) ==
         (63ULL << 48 | 0x840001ULL));
  assert(get_u64(publication.Region + offset + 8u) ==
         (63ULL << 48 | 0x844001ULL));
  assert(publication.MapCalls == 1u && publication.UnmapCalls == 0u);
  assert(AppleAgxRenderProviderUnpublish(&provider));
  assert(get_u64(publication.Region + offset) == 0x1111111111111111ULL);
  assert(get_u64(publication.Region + offset + 8u) ==
         0x2222222222222222ULL);
  assert(publication.UnmapCalls == 1u);
  assert(pool_bytes[0] == 0x5a);
  assert(pool_bytes[APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET] == 0x5a);
  free(pool_bytes);
}

static void make_submission(unsigned char *bytes, unsigned int *byte_count) {
  APPLE_AGX_GDI_COMMAND_DESCRIPTION description;
  APPLE_AGX_GDI_RECT sub_rect = {0u, 0u, 16u, 16u};

  memset(&description, 0, sizeof(description));
  description.Command.Opcode = AppleAgxGdiColorFill;
  description.Command.SubRectCount = 1u;
  description.Command.Destination = sub_rect;
  description.Command.DestinationGpuAddress = 0x20000ULL;
  description.Command.Rop = AppleAgxGdiColorFillPatCopy;
  description.SubRects = &sub_rect;
  assert(AppleAgxGdiEncodeDmaCommand(
      &description, bytes,
      sizeof(APPLE_AGX_GDI_DMA_COMMAND) + sizeof(APPLE_AGX_GDI_RECT),
      byte_count));
}

static void test_stage_rejects_unimplemented_gdi_semantics_without_mutation(void) {
  APPLE_AGX_RENDER_PROVIDER provider;
  APPLE_AGX_RENDER_PROVIDER provider_before;
  APPLE_AGX_MEMORY_OBJECT pool;
  APPLE_AGX_INITDATA_MEMORY_GRAPH initdata;
  APPLE_AGX_CONFIG_SNAPSHOT snapshot;
  APPLE_AGX_UAT_ROOTS render_roots;
  FAKE_PUBLICATION publication;
  APPLE_AGX_EXP208_RELOCATION_OBJECT objects[
      APPLE_AGX_EXP208_TA_WORK_OBJECT + 1u];
  APPLE_AGX_EXP208_RELOCATION relocation;
  APPLE_AGX_RENDER_PROVIDER_JOB_CONFIG job_config;
  unsigned char submission_bytes[sizeof(APPLE_AGX_GDI_DMA_COMMAND) +
                                 sizeof(APPLE_AGX_GDI_RECT)];
  unsigned char target[16];
  unsigned char *pool_bytes = (unsigned char *)malloc(
      APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES);
  unsigned int submission_byte_count;
  unsigned char *arena;

  assert(pool_bytes != NULL);
  memset(pool_bytes, 0xa5, APPLE_AGX_BACKEND_BRIDGE_GDI_POOL_BYTES);
  memset(target, 0, sizeof(target));
  configure_provider(&provider, &pool, pool_bytes, &initdata, &snapshot,
                     &render_roots, &publication);
  assert(AppleAgxRenderProviderPublish(&provider));
  make_submission(submission_bytes, &submission_byte_count);
  arena = pool_bytes + APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET;
  memset(arena, 0, 8u);

  memset(objects, 0, sizeof(objects));
  objects[0].GpuVa = APPLE_AGX_EXP208_ARENA_GPU_BASE;
  objects[0].PhysicalAddress =
      pool.DeviceAddress + APPLE_AGX_RENDER_PROVIDER_ARENA_OFFSET;
  objects[0].Size = APPLE_AGX_EXP208_ARENA_BYTES;
  objects[0].Data = arena;
  objects[1].GpuVa = 0x1600000000ULL;
  objects[1].PhysicalAddress = 0x980000000ULL;
  objects[1].Size = sizeof(target);
  objects[1].Data = target;
  objects[APPLE_AGX_EXP208_D3_BARRIER_OBJECT].GpuVa =
      APPLE_AGX_EXP208_D3_WORK_ROOT;
  objects[APPLE_AGX_EXP208_TA_INITBM_OBJECT].GpuVa =
      APPLE_AGX_EXP208_TA_WORK_ROOT;
  objects[APPLE_AGX_EXP208_D3_WORK_OBJECT].GpuVa =
      APPLE_AGX_EXP208_D3_SECONDARY_ROOT;
  objects[APPLE_AGX_EXP208_TA_WORK_OBJECT].GpuVa =
      APPLE_AGX_EXP208_TA_SECONDARY_ROOT;
  relocation.SourceObject = 0u;
  relocation.SourceOffset = 0u;
  relocation.TargetObject = 1u;
  relocation.TargetOffset = 0u;
  relocation.AddressSpace = AppleAgxExp208RelocationGpuVa;
  relocation.Encoding = AppleAgxExp208RelocationExactU64;
  relocation.EncodingBits = 0ULL;

  memset(&job_config, 0, sizeof(job_config));
  job_config.ContextIdentity = 0x63ULL;
  job_config.Fence = 9u;
  job_config.Parameters.ArenaGpuAddress = APPLE_AGX_EXP208_ARENA_GPU_BASE;
  job_config.Parameters.ArenaBytes = APPLE_AGX_EXP208_ARENA_BYTES;
  job_config.Parameters.TaEvent = 7u;
  job_config.Parameters.D3Event = 8u;
  job_config.Parameters.TaExpectedStamp = 0x7a000100u;
  job_config.Parameters.D3ExpectedStamp = 0x3d000100u;
  job_config.Parameters.TaExpectedDonePointer = 2u;
  job_config.Parameters.D3ExpectedDonePointer = 2u;
  job_config.Objects = objects;
  job_config.ObjectCount = APPLE_AGX_EXP208_TA_WORK_OBJECT + 1u;
  job_config.ArenaObject = 0u;
  job_config.Relocations = &relocation;
  job_config.RelocationCount = 1u;
  provider_before = provider;
  assert(AppleAgxExp208SupportedGdiPrimitiveMask() == 0u);
  assert(!AppleAgxRenderProviderStageJob(
      &provider, &job_config, submission_bytes, submission_byte_count));
  assert(memcmp(&provider, &provider_before, sizeof(provider)) == 0);
  assert(get_u64(arena) == 0ULL);
  assert(AppleAgxRenderProviderUnpublish(&provider));
  free(pool_bytes);
}

int main(void) {
  test_publish_and_unpublish_exact_context();
  test_stage_rejects_unimplemented_gdi_semantics_without_mutation();
  return 0;
}
