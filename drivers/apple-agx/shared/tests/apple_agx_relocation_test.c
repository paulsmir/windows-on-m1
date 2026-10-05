#include "apple_agx_relocation.h"

#include <assert.h>
#include <string.h>

static APPLE_AGX_U32 get_u32(const unsigned char *source) {
  return (APPLE_AGX_U32)source[0] |
         ((APPLE_AGX_U32)source[1] << 8) |
         ((APPLE_AGX_U32)source[2] << 16) |
         ((APPLE_AGX_U32)source[3] << 24);
}

static APPLE_AGX_U64 get_u64(const unsigned char *source) {
  return (APPLE_AGX_U64)get_u32(source) |
         ((APPLE_AGX_U64)get_u32(source + 4) << 32);
}

static void test_all_proven_g13_encodings(void) {
  unsigned char ta[24], list[8], block[0x10000], flagged[16];
  APPLE_AGX_RELOCATION_OBJECT objects[4] = {
      {0x200000ULL, 0x900000ULL, sizeof(ta), ta},
      {0x208000ULL, 0x904000ULL, sizeof(list), list},
      {0x210000ULL, 0x908000ULL, sizeof(block), block},
      {0x230000ULL, 0x90c000ULL, sizeof(flagged), flagged},
  };
  APPLE_AGX_RELOCATION relocations[4] = {
      {0u, 0u, 1u, 0u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationExactU64, 0ULL},
      {0u, 8u, 3u, 0u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationTaFlaggedGpuVa, 0x0004000000000000ULL},
      {1u, 0u, 2u, 0u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationGpuVaPage32kU32, 0ULL},
      {1u, 4u, 2u, 0x8000u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationGpuVaPage32kU32, 0ULL},
  };
  memset(ta, 0, sizeof(ta));
  memset(list, 0, sizeof(list));
  memset(block, 0, sizeof(block));
  memset(flagged, 0, sizeof(flagged));
  assert(AppleAgxApplyRelocations(objects, 4u, relocations, 4u));
  assert(get_u64(ta) == 0x208000ULL);
  assert(get_u64(ta + 8) == (0x230000ULL | 0x0004000000000000ULL));
  assert(get_u32(list) == 0x210000ULL / 0x8000ULL);
  assert(get_u32(list + 4) ==
         (0x210000ULL + 0x8000ULL) / 0x8000ULL);
}

static void test_batch_is_fail_closed_and_atomic(void) {
  unsigned char source[16];
  unsigned char target[16];
  APPLE_AGX_RELOCATION_OBJECT objects[2] = {
      {0x200000ULL, 0x900000ULL, sizeof(source), source},
      {0x210001ULL, 0x904000ULL, sizeof(target), target},
  };
  APPLE_AGX_RELOCATION relocations[2] = {
      {0u, 0u, 1u, 0u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationExactU64, 0ULL},
      {0u, 8u, 1u, 0u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationGpuVaPage32kU32, 0ULL},
  };
  memset(source, 0xa5, sizeof(source));
  memset(target, 0, sizeof(target));
  assert(!AppleAgxApplyRelocations(objects, 2u, relocations, 2u));
  assert(source[0] == 0xa5 && source[15] == 0xa5);
}

static void test_unproven_encoding_bits_are_rejected_atomically(void) {
  unsigned char source[16];
  unsigned char target[16];
  APPLE_AGX_RELOCATION_OBJECT objects[2] = {
      {0x200000ULL, 0x900000ULL, sizeof(source), source},
      {0x210000ULL, 0x904000ULL, sizeof(target), target},
  };
  APPLE_AGX_RELOCATION relocations[2] = {
      {0u, 0u, 1u, 0u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationExactU64, 0ULL},
      {0u, 8u, 1u, 0u, AppleAgxRelocationGpuVa,
       AppleAgxRelocationTaFlaggedGpuVa, 0x1000000000000000ULL},
  };
  memset(source, 0x5a, sizeof(source));
  memset(target, 0, sizeof(target));
  assert(!AppleAgxApplyRelocations(objects, 2u, relocations, 2u));
  assert(source[0] == 0x5a && source[15] == 0x5a);

  relocations[1].Encoding = AppleAgxRelocationExactU64;
  assert(!AppleAgxApplyRelocations(objects, 2u, relocations, 2u));
  assert(source[0] == 0x5a && source[15] == 0x5a);
}

int main(void) {
  test_all_proven_g13_encodings();
  test_batch_is_fail_closed_and_atomic();
  test_unproven_encoding_bits_are_rejected_atomically();
  return 0;
}
