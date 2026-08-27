#include "apple_agx_uat_publication.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_PUBLICATION {
  unsigned char Region[J313_AGX_G2_GPU_SIZE];
  unsigned int MapCount;
  unsigned int BarrierCount;
  unsigned int UnmapCount;
  unsigned char FailMap;
  unsigned char FailUnmap;
} FAKE_PUBLICATION;

static unsigned long long read_u64(const unsigned char *bytes) {
  unsigned long long value = 0ULL;
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    value |= (unsigned long long)bytes[index] << (index * 8u);
  return value;
}

static void write_u64(unsigned char *bytes, unsigned long long value) {
  unsigned int index;
  for (index = 0u; index < 8u; ++index)
    bytes[index] = (unsigned char)(value >> (index * 8u));
}

static unsigned char map_region(void *context, unsigned long long address,
                                unsigned int length,
                                volatile unsigned char **mapped) {
  FAKE_PUBLICATION *fake = (FAKE_PUBLICATION *)context;
  ++fake->MapCount;
  if (fake->FailMap != 0u || address != J313_AGX_G2_GPU_BASE ||
      length != J313_AGX_G2_GPU_SIZE)
    return 0u;
  *mapped = fake->Region;
  return 1u;
}

static void barrier(void *context) {
  FAKE_PUBLICATION *fake = (FAKE_PUBLICATION *)context;
  ++fake->BarrierCount;
}

static unsigned char unmap_region(void *context,
                                  volatile unsigned char *mapped) {
  FAKE_PUBLICATION *fake = (FAKE_PUBLICATION *)context;
  ++fake->UnmapCount;
  return fake->FailUnmap == 0u && mapped == fake->Region;
}

static APPLE_AGX_UAT_PUBLICATION_IO publication_io(FAKE_PUBLICATION *fake) {
  APPLE_AGX_UAT_PUBLICATION_IO io;
  io.Context = fake;
  io.Map = map_region;
  io.Barrier = barrier;
  io.Unmap = unmap_region;
  return io;
}

static APPLE_AGX_CONFIG_SNAPSHOT snapshot(void) {
  APPLE_AGX_CONFIG_SNAPSHOT value;
  memset(&value, 0, sizeof(value));
  value.GpuRegionBase = J313_AGX_G2_GPU_BASE;
  return value;
}

static void test_publish_and_restore_exact_context_zero_pair(void) {
  FAKE_PUBLICATION fake;
  APPLE_AGX_UAT_PUBLICATION_IO io;
  APPLE_AGX_UAT_PUBLICATION_STATE state;
  APPLE_AGX_CONFIG_SNAPSHOT config = snapshot();
  APPLE_AGX_UAT_TTBR_PAIR pair = {0x10004001ULL, 0x10008001ULL};

  memset(&fake, 0, sizeof(fake));
  memset(&state, 0, sizeof(state));
  write_u64(fake.Region, 0xaaaaaaaaaaaaaaaaULL);
  write_u64(fake.Region + 8u, 0xbbbbbbbbbbbbbbbbULL);
  io = publication_io(&fake);
  assert(AppleAgxUatPublishJ313(&config, &pair, &io, &state) ==
         AppleAgxUatPublicationResultOk);
  assert(state.Active == 1u && fake.MapCount == 1u);
  assert(fake.BarrierCount == 2u && fake.UnmapCount == 0u);
  assert(read_u64(fake.Region) == pair.Ttbr0);
  assert(read_u64(fake.Region + 8u) == pair.Ttbr1);
  assert(AppleAgxUatUnpublishJ313(&io, &state) ==
         AppleAgxUatPublicationResultOk);
  assert(read_u64(fake.Region) == 0xaaaaaaaaaaaaaaaaULL);
  assert(read_u64(fake.Region + 8u) == 0xbbbbbbbbbbbbbbbbULL);
  assert(fake.BarrierCount == 4u && fake.UnmapCount == 1u);
  assert(state.Active == 0u);
}

static void test_fail_closed_and_retryable_unmap(void) {
  FAKE_PUBLICATION fake;
  APPLE_AGX_UAT_PUBLICATION_IO io;
  APPLE_AGX_UAT_PUBLICATION_STATE state;
  APPLE_AGX_CONFIG_SNAPSHOT config = snapshot();
  APPLE_AGX_UAT_TTBR_PAIR pair = {0x10004001ULL, 0x10008001ULL};

  memset(&fake, 0, sizeof(fake));
  memset(&state, 0, sizeof(state));
  io = publication_io(&fake);
  config.GpuRegionBase += 0x4000ULL;
  assert(AppleAgxUatPublishJ313(&config, &pair, &io, &state) ==
         AppleAgxUatPublicationResultInvalidArgument);
  assert(fake.MapCount == 0u);
  config = snapshot();
  pair.Ttbr1 = 0ULL;
  assert(AppleAgxUatPublishJ313(&config, &pair, &io, &state) ==
         AppleAgxUatPublicationResultInvalidArgument);
  assert(fake.MapCount == 0u);
  pair.Ttbr1 = 0x10008001ULL;
  assert(AppleAgxUatPublishJ313(&config, &pair, &io, &state) ==
         AppleAgxUatPublicationResultOk);
  fake.FailUnmap = 1u;
  assert(AppleAgxUatUnpublishJ313(&io, &state) ==
         AppleAgxUatPublicationResultUnmapFailed);
  assert(state.Active == 1u);
  fake.FailUnmap = 0u;
  assert(AppleAgxUatUnpublishJ313(&io, &state) ==
         AppleAgxUatPublicationResultOk);
  assert(state.Active == 0u);
}

int main(void) {
  test_publish_and_restore_exact_context_zero_pair();
  test_fail_closed_and_retryable_unmap();
  return 0;
}
