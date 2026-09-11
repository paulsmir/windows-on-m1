#include "apple_agx_mapping.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct _FAKE_MAPPING {
  unsigned char *Base;
  unsigned long long LastPhysical;
  unsigned int LastLength;
  unsigned int MapCount;
  unsigned int UnmapCount;
  unsigned char MapSucceeds;
  unsigned char ReturnNull;
  unsigned char UnmapSucceeds;
} FAKE_MAPPING;

static unsigned char map_memory(void *context,
                                unsigned long long physical_address,
                                unsigned int length,
                                unsigned char **virtual_address) {
  FAKE_MAPPING *fake = (FAKE_MAPPING *)context;
  ++fake->MapCount;
  fake->LastPhysical = physical_address;
  fake->LastLength = length;
  if (fake->MapSucceeds == 0u) return 0u;
  *virtual_address = fake->ReturnNull != 0u ? 0 : fake->Base;
  return 1u;
}

static unsigned char unmap_memory(void *context,
                                  unsigned char *virtual_address) {
  FAKE_MAPPING *fake = (FAKE_MAPPING *)context;
  ++fake->UnmapCount;
  assert(virtual_address == fake->Base);
  return fake->UnmapSucceeds;
}

static void init_fixture(FAKE_MAPPING *fake, APPLE_AGX_MAPPING_IO *io,
                         APPLE_AGX_MAPPING_STATE *state) {
  memset(fake, 0, sizeof(*fake));
  memset(state, 0, sizeof(*state));
  fake->Base = (unsigned char *)malloc(J313_AGX_G2_SGX_MMIO_SIZE);
  assert(fake->Base != 0);
  fake->MapSucceeds = 1u;
  fake->UnmapSucceeds = 1u;
  io->Context = fake;
  io->Map = map_memory;
  io->Unmap = unmap_memory;
}

static void test_map_once_subview_and_stop(void) {
  FAKE_MAPPING fake;
  APPLE_AGX_MAPPING_IO io;
  APPLE_AGX_MAPPING_STATE state;
  init_fixture(&fake, &io, &state);

  assert(AppleAgxMappingStart(&io, &state) == AppleAgxUatResultOk);
  assert(fake.MapCount == 1u);
  assert(fake.LastPhysical == 0x204000000ULL);
  assert(fake.LastLength == 0x04000000u);
  assert(state.SgxBase == fake.Base);
  assert(state.AscBase == fake.Base + 0x02400000u);
  assert(state.SgxPhysicalAddress == 0x204000000ULL);
  assert(state.SgxLength == 0x04000000u);
  assert(state.Active == 1u);
  assert(AppleAgxMappingStart(&io, &state) ==
         AppleAgxUatResultAlreadyMapped);
  assert(fake.MapCount == 1u);

  assert(AppleAgxMappingStop(&io, &state) == AppleAgxUatResultOk);
  assert(fake.UnmapCount == 1u);
  assert(state.SgxBase == 0 && state.AscBase == 0);
  assert(state.SgxPhysicalAddress == 0ULL && state.SgxLength == 0u);
  assert(state.Active == 0u);
  assert(AppleAgxMappingStop(&io, &state) == AppleAgxUatResultOk);
  assert(fake.UnmapCount == 1u);
  free(fake.Base);
}

static void test_failures_and_retry(void) {
  FAKE_MAPPING fake;
  APPLE_AGX_MAPPING_IO io;
  APPLE_AGX_MAPPING_STATE state;
  init_fixture(&fake, &io, &state);

  fake.MapSucceeds = 0u;
  assert(AppleAgxMappingStart(&io, &state) ==
         AppleAgxUatResultAllocationFailed);
  assert(state.Active == 0u && state.SgxBase == 0);
  fake.MapSucceeds = 1u;
  fake.ReturnNull = 1u;
  assert(AppleAgxMappingStart(&io, &state) ==
         AppleAgxUatResultAllocationFailed);
  assert(state.Active == 0u && state.SgxBase == 0);
  fake.ReturnNull = 0u;
  assert(AppleAgxMappingStart(&io, &state) == AppleAgxUatResultOk);

  fake.UnmapSucceeds = 0u;
  assert(AppleAgxMappingStop(&io, &state) ==
         AppleAgxUatResultAllocationFailed);
  assert(state.Active == 1u && state.SgxBase == fake.Base);
  fake.UnmapSucceeds = 1u;
  assert(AppleAgxMappingStop(&io, &state) == AppleAgxUatResultOk);
  assert(state.Active == 0u && state.SgxBase == 0);
  free(fake.Base);
}

static void test_invalid_callbacks_and_containment(void) {
  FAKE_MAPPING fake;
  APPLE_AGX_MAPPING_IO io;
  APPLE_AGX_MAPPING_STATE state;
  init_fixture(&fake, &io, &state);

  assert(AppleAgxMappingStart(0, &state) ==
         AppleAgxUatResultInvalidArgument);
  io.Map = 0;
  assert(AppleAgxMappingStart(&io, &state) ==
         AppleAgxUatResultInvalidArgument);
  io.Map = map_memory;
  io.Unmap = 0;
  assert(AppleAgxMappingStart(&io, &state) ==
         AppleAgxUatResultInvalidArgument);
  io.Unmap = unmap_memory;

  assert(AppleAgxMappingStartWithRangesForTest(
             &io, &state, 0x204000000ULL, 0x04000000u,
             0x203ffc000ULL, 0x0006c000u) ==
         AppleAgxUatResultOutOfRange);
  assert(AppleAgxMappingStartWithRangesForTest(
             &io, &state, 0x204000000ULL, 0x04000000u,
             0x207ffc000ULL, 0x0006c000u) ==
         AppleAgxUatResultOutOfRange);
  assert(fake.MapCount == 0u);
  free(fake.Base);
}

int main(void) {
  test_map_once_subview_and_stop();
  test_failures_and_retry();
  test_invalid_callbacks_and_containment();
  return 0;
}
