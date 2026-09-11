#include "apple_agx_dma_shadow.h"

#include <assert.h>
#include <string.h>

static void test_append_preserves_exact_dma_interval(void) {
  unsigned char storage[128];
  const unsigned char first[] = {0x10, 0x20, 0x30, 0x40};
  const unsigned char second[] = {0xaa, 0xbb, 0xcc};
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;

  memset(storage, 0xa5, sizeof(storage));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x20u, first, sizeof(first)));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x80u, second, sizeof(second)));
  assert(AppleAgxDmaShadowValidate(storage, shadow.BytesUsed));
  assert(AppleAgxDmaShadowFind(storage, shadow.BytesUsed, 0x20u,
                              sizeof(first), &view));
  assert(view.DmaOffset == 0x20u);
  assert(view.DmaBytes == sizeof(first));
  assert(memcmp(view.Bytes, first, sizeof(first)) == 0);
  assert(AppleAgxDmaShadowFind(storage, shadow.BytesUsed, 0x80u,
                              sizeof(second), &view));
  assert(memcmp(view.Bytes, second, sizeof(second)) == 0);
}

static void test_append_rejects_overflow_overlap_and_truncation(void) {
  unsigned char storage[80];
  unsigned char bytes[32];
  APPLE_AGX_DMA_SHADOW shadow;

  memset(bytes, 0x5a, sizeof(bytes));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(!AppleAgxDmaShadowAppend(&shadow, 0xfffffff0u, bytes,
                                  sizeof(bytes)));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x100u, bytes, 16u));
  assert(!AppleAgxDmaShadowAppend(&shadow, 0x108u, bytes, 16u));
  assert(!AppleAgxDmaShadowAppend(&shadow, 0x0f8u, bytes, 16u));
  assert(!AppleAgxDmaShadowAppend(&shadow, 0x200u, bytes, sizeof(bytes)));
  assert(!AppleAgxDmaShadowValidate(storage, shadow.BytesUsed - 1u));
}

static void test_patch_updates_exactly_one_containing_record(void) {
  unsigned char storage[160];
  unsigned char first[24];
  unsigned char second[24];
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  APPLE_AGX_U64 address = 0x15000012340ULL;
  APPLE_AGX_U64 observed = 0ULL;

  memset(first, 0x11, sizeof(first));
  memset(second, 0x22, sizeof(second));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x40u, first, sizeof(first)));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x100u, second, sizeof(second)));
  assert(AppleAgxDmaShadowPatchU64(storage, shadow.BytesUsed, 0x48u,
                                  address));
  assert(AppleAgxDmaShadowFind(storage, shadow.BytesUsed, 0x40u,
                              sizeof(first), &view));
  memcpy(&observed, view.Bytes + 8u, sizeof(observed));
  assert(observed == address);
  assert(AppleAgxDmaShadowFind(storage, shadow.BytesUsed, 0x100u,
                              sizeof(second), &view));
  assert(memcmp(view.Bytes, second, sizeof(second)) == 0);
  assert(!AppleAgxDmaShadowPatchU64(storage, shadow.BytesUsed, 0x3fu,
                                   address));
  assert(!AppleAgxDmaShadowPatchU64(storage, shadow.BytesUsed, 0x54u,
                                   address));
  assert(!AppleAgxDmaShadowPatchU64(storage, shadow.BytesUsed, 0x90u,
                                   address));
}

static void test_reopen_uses_only_persisted_valid_bytes(void) {
  unsigned char storage[128];
  unsigned char bytes[16];
  APPLE_AGX_DMA_SHADOW writer;
  APPLE_AGX_DMA_SHADOW reader;
  APPLE_AGX_DMA_SHADOW_VIEW view;

  memset(bytes, 0x3c, sizeof(bytes));
  AppleAgxDmaShadowInitialize(&writer, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&writer, 0x200u, bytes, sizeof(bytes)));
  memset(storage + writer.BytesUsed, 0xff,
         sizeof(storage) - writer.BytesUsed);
  assert(AppleAgxDmaShadowOpen(&reader, storage, sizeof(storage)));
  assert(reader.BytesUsed == writer.BytesUsed);
  assert(AppleAgxDmaShadowFind(reader.Storage, reader.BytesUsed, 0x200u,
                              sizeof(bytes), &view));
  assert(memcmp(view.Bytes, bytes, sizeof(bytes)) == 0);
  ((APPLE_AGX_DMA_SHADOW_HEADER *)storage)->BytesUsed = sizeof(storage) + 1u;
  assert(!AppleAgxDmaShadowOpen(&reader, storage, sizeof(storage)));
}

static void test_copy_submission_requires_exact_record_boundaries(void) {
  unsigned char storage[192];
  unsigned char first[16];
  unsigned char second[24];
  unsigned char output[40];
  unsigned int copied = 99u;
  APPLE_AGX_DMA_SHADOW shadow;

  memset(first, 0x31, sizeof(first));
  memset(second, 0x72, sizeof(second));
  memset(output, 0xee, sizeof(output));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x40u, first, sizeof(first)));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x50u, second, sizeof(second)));
  assert(AppleAgxDmaShadowCopySubmission(
      storage, shadow.BytesUsed, 0x40u, 0x68u, output, sizeof(output),
      &copied));
  assert(copied == sizeof(output));
  assert(memcmp(output, first, sizeof(first)) == 0);
  assert(memcmp(output + sizeof(first), second, sizeof(second)) == 0);

  memset(output, 0xee, sizeof(output));
  copied = 99u;
  assert(!AppleAgxDmaShadowCopySubmission(
      storage, shadow.BytesUsed, 0x41u, 0x68u, output, sizeof(output),
      &copied));
  assert(copied == 0u && output[0] == 0xee);
  assert(!AppleAgxDmaShadowCopySubmission(
      storage, shadow.BytesUsed, 0x40u, 0x67u, output, sizeof(output),
      &copied));
  assert(!AppleAgxDmaShadowCopySubmission(
      storage, shadow.BytesUsed, 0x40u, 0x68u, output,
      sizeof(output) - 1u, &copied));
}

static void test_corrupt_or_ambiguous_shadow_is_rejected(void) {
  unsigned char storage[160];
  unsigned char bytes[24];
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_RECORD *record;

  memset(bytes, 0, sizeof(bytes));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x40u, bytes, sizeof(bytes)));
  record = (APPLE_AGX_DMA_SHADOW_RECORD *)(
      storage + sizeof(APPLE_AGX_DMA_SHADOW_HEADER));
  record->Magic ^= 1u;
  assert(!AppleAgxDmaShadowValidate(storage, shadow.BytesUsed));
  assert(!AppleAgxDmaShadowPatchU64(storage, shadow.BytesUsed, 0x48u,
                                   0x1234ULL));
}

static void test_only_an_all_zero_block_is_virgin(void) {
  unsigned char storage[128];

  memset(storage, 0, sizeof(storage));
  assert(AppleAgxDmaShadowIsVirgin(storage, sizeof(storage)));
  storage[sizeof(storage) - 1u] = 1u;
  assert(!AppleAgxDmaShadowIsVirgin(storage, sizeof(storage)));
  assert(!AppleAgxDmaShadowIsVirgin(NULL, sizeof(storage)));
  assert(!AppleAgxDmaShadowIsVirgin(storage, 0u));
}

static void test_extent_and_exact_coverage_use_whole_dma_offsets(void) {
  unsigned char storage[192];
  unsigned char bytes[16];
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_U32 extent = 0u;

  memset(bytes, 0x6c, sizeof(bytes));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x20u, bytes, sizeof(bytes)));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x30u, bytes, sizeof(bytes)));
  assert(AppleAgxDmaShadowExtent(storage, shadow.BytesUsed, &extent));
  assert(extent == 0x40u);
  assert(AppleAgxDmaShadowCoversSubmission(storage, shadow.BytesUsed,
                                           0x20u, 0x40u));
  assert(!AppleAgxDmaShadowCoversSubmission(storage, shadow.BytesUsed,
                                            0x21u, 0x40u));
  assert(!AppleAgxDmaShadowCoversSubmission(storage, shadow.BytesUsed,
                                            0x20u, 0x3fu));
  assert(!AppleAgxDmaShadowCoversSubmission(storage, shadow.BytesUsed,
                                            0x10u, 0x40u));
}

static void test_seal_is_irreversible_and_preserves_owned_bytes(void) {
  unsigned char storage[192];
  unsigned char bytes[24];
  unsigned char before[24];
  unsigned char output[24];
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW reopened;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  APPLE_AGX_U32 copied = 0u;

  memset(bytes, 0x4d, sizeof(bytes));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x80u, bytes, sizeof(bytes)));
  assert(AppleAgxDmaShadowFind(storage, shadow.BytesUsed, 0x80u,
                              sizeof(bytes), &view));
  memcpy(before, view.Bytes, sizeof(before));
  assert(!AppleAgxDmaShadowIsSealed(storage, shadow.BytesUsed));
  assert(AppleAgxDmaShadowSeal(&shadow, 73u));
  assert(AppleAgxDmaShadowIsSealed(storage, shadow.BytesUsed));
  assert(AppleAgxDmaShadowIsSealedForFence(storage, shadow.BytesUsed, 73u));
  assert(!AppleAgxDmaShadowIsSealedForFence(storage, shadow.BytesUsed, 74u));
  assert(!AppleAgxDmaShadowSeal(&shadow, 73u));
  assert(!AppleAgxDmaShadowAppend(&shadow, 0x100u, bytes, sizeof(bytes)));
  assert(!AppleAgxDmaShadowPatchU64(storage, shadow.BytesUsed, 0x88u,
                                   0x1122334455667788ULL));
  assert(AppleAgxDmaShadowFind(storage, shadow.BytesUsed, 0x80u,
                              sizeof(bytes), &view));
  assert(memcmp(view.Bytes, before, sizeof(before)) == 0);
  assert(AppleAgxDmaShadowOpen(&reopened, storage, sizeof(storage)));
  assert(AppleAgxDmaShadowIsSealed(reopened.Storage, reopened.BytesUsed));
  assert(AppleAgxDmaShadowCopySubmission(
      reopened.Storage, reopened.BytesUsed, 0x80u, 0x98u, output,
      sizeof(output), &copied));
  assert(copied == sizeof(output));
  assert(memcmp(output, before, sizeof(output)) == 0);
}

static void test_sealed_patch_receipt_matches_exact_value_and_fence(void) {
  unsigned char storage[160];
  unsigned char bytes[24];
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_U64 address = 0x15000123450ULL;

  memset(bytes, 0, sizeof(bytes));
  AppleAgxDmaShadowInitialize(&shadow, storage, sizeof(storage));
  assert(AppleAgxDmaShadowAppend(&shadow, 0x40u, bytes, sizeof(bytes)));
  assert(AppleAgxDmaShadowPatchU64(storage, shadow.BytesUsed, 0x48u,
                                  address));
  assert(AppleAgxDmaShadowMatchesWritableU64(
      storage, shadow.BytesUsed, 0x48u, address));
  assert(!AppleAgxDmaShadowMatchesWritableU64(
      storage, shadow.BytesUsed, 0x48u, address + 1ULL));
  assert(AppleAgxDmaShadowSeal(&shadow, 91u));
  assert(!AppleAgxDmaShadowMatchesWritableU64(
      storage, shadow.BytesUsed, 0x48u, address));
  assert(AppleAgxDmaShadowMatchesU64(storage, shadow.BytesUsed, 0x48u,
                                    address));
  assert(!AppleAgxDmaShadowMatchesU64(storage, shadow.BytesUsed, 0x48u,
                                     address + 1ULL));
  assert(!AppleAgxDmaShadowMatchesU64(storage, shadow.BytesUsed, 0x3fu,
                                     address));
}

static void test_recycled_dma_storage_starts_new_request(void) {
  unsigned char storage[4][512] = {{0}};
  unsigned char command[168];
  APPLE_AGX_DMA_SHADOW shadow;
  APPLE_AGX_DMA_SHADOW_VIEW view;
  unsigned int request;
  for (request = 0u; request < 16u; ++request) {
    unsigned char *block = storage[request % 4u];
    if (request >= 4u)
      assert(!AppleAgxDmaShadowIsVirgin(block, sizeof(storage[0])));
    memset(command, (int)(request + 1u), sizeof(command));
    /* Dxgkrnl supplies the block for a new request after previous completion.
       Zero-on-creation does not imply zero-on-reuse. */
    AppleAgxDmaShadowInitialize(&shadow, block, sizeof(storage[0]));
    assert(AppleAgxDmaShadowAppend(&shadow, 0u, command, sizeof(command)));
    assert(AppleAgxDmaShadowSeal(&shadow, 256u + request));
    assert(AppleAgxDmaShadowIsSealedForFence(block, shadow.BytesUsed,
                                           256u + request));
    assert(!AppleAgxDmaShadowIsSealedForFence(block, shadow.BytesUsed,
                                            255u + request));
    assert(AppleAgxDmaShadowFind(block, shadow.BytesUsed, 0u,
                               sizeof(command), &view));
    assert(memcmp(view.Bytes, command, sizeof(command)) == 0);
  }
}

int main(void) {
  test_recycled_dma_storage_starts_new_request();
  test_append_preserves_exact_dma_interval();
  test_append_rejects_overflow_overlap_and_truncation();
  test_patch_updates_exactly_one_containing_record();
  test_reopen_uses_only_persisted_valid_bytes();
  test_copy_submission_requires_exact_record_boundaries();
  test_corrupt_or_ambiguous_shadow_is_rejected();
  test_only_an_all_zero_block_is_virgin();
  test_extent_and_exact_coverage_use_whole_dma_offsets();
  test_seal_is_irreversible_and_preserves_owned_bytes();
  test_sealed_patch_receipt_matches_exact_value_and_fence();
  return 0;
}
