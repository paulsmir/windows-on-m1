#include "apple_agx_wddm_feature_contract.h"

#include <assert.h>
#include <string.h>

static APPLE_AGX_WDDM_FEATURE_INPUT all_ready(void) {
  APPLE_AGX_WDDM_FEATURE_INPUT input;
  memset(&input, 0, sizeof(input));
  input.Version = APPLE_AGX_WDDM_FEATURE_CONTRACT_VERSION;
  input.Size = sizeof(input);
  input.WddmMajor = 3u;
  input.WddmMinor = 0u;
  input.NodeCount = 1u;
  input.ReadyMask = APPLE_AGX_WDDM_REQUIRED_READY_MASK;
  return input;
}

static void test_complete_contract_publishes_atomic_caps(void) {
  APPLE_AGX_WDDM_FEATURE_INPUT input = all_ready();
  APPLE_AGX_WDDM_FEATURE_OUTPUT output;

  memset(&output, 0xa5, sizeof(output));
  assert(AppleAgxWddmFeatureContractEvaluate(&input, &output) ==
         AppleAgxWddmFeatureContractReady);
  assert(output.Ready == 1u);
  assert(output.RequiredReadyMask == APPLE_AGX_WDDM_REQUIRED_READY_MASK);
  assert(output.MissingReadyMask == 0u);
  assert(output.PublishCapsMask == APPLE_AGX_WDDM_MANDATORY_CAPS_MASK);
}

static void test_each_missing_prerequisite_suppresses_all_caps(void) {
  unsigned int bit;

  for (bit = 0u; bit < 32u; ++bit) {
    APPLE_AGX_WDDM_FEATURE_INPUT input;
    APPLE_AGX_WDDM_FEATURE_OUTPUT output;
    unsigned int prerequisite = 1u << bit;

    if ((APPLE_AGX_WDDM_REQUIRED_READY_MASK & prerequisite) == 0u)
      continue;
    input = all_ready();
    input.ReadyMask &= ~prerequisite;
    memset(&output, 0xa5, sizeof(output));
    assert(AppleAgxWddmFeatureContractEvaluate(&input, &output) ==
           AppleAgxWddmFeatureContractIncomplete);
    assert(output.Ready == 0u);
    assert(output.RequiredReadyMask == APPLE_AGX_WDDM_REQUIRED_READY_MASK);
    assert(output.MissingReadyMask == prerequisite);
    assert(output.PublishCapsMask == 0u);
  }
}

static void test_multiple_missing_prerequisites_are_reported_together(void) {
  APPLE_AGX_WDDM_FEATURE_INPUT input = all_ready();
  APPLE_AGX_WDDM_FEATURE_OUTPUT output;
  unsigned int missing = APPLE_AGX_WDDM_READY_MEMORY_PAGING |
                         APPLE_AGX_WDDM_READY_D589_SCANOUT |
                         APPLE_AGX_WDDM_READY_UMD_DIRECT_FLIP;

  input.ReadyMask &= ~missing;
  assert(AppleAgxWddmFeatureContractEvaluate(&input, &output) ==
         AppleAgxWddmFeatureContractIncomplete);
  assert(output.MissingReadyMask == missing);
  assert(output.PublishCapsMask == 0u);
}

static void assert_invalid_without_output_mutation(
    const APPLE_AGX_WDDM_FEATURE_INPUT *input) {
  APPLE_AGX_WDDM_FEATURE_OUTPUT output;
  APPLE_AGX_WDDM_FEATURE_OUTPUT before;
  memset(&output, 0x5a, sizeof(output));
  before = output;
  assert(AppleAgxWddmFeatureContractEvaluate(input, &output) ==
         AppleAgxWddmFeatureContractInvalid);
  assert(memcmp(&output, &before, sizeof(output)) == 0);
}

static void test_invalid_identity_and_reserved_bits_fail_atomically(void) {
  APPLE_AGX_WDDM_FEATURE_INPUT input = all_ready();

  assert_invalid_without_output_mutation(NULL);
  assert(AppleAgxWddmFeatureContractEvaluate(&input, NULL) ==
         AppleAgxWddmFeatureContractInvalid);

  input = all_ready();
  input.Version++;
  assert_invalid_without_output_mutation(&input);
  input = all_ready();
  input.Size--;
  assert_invalid_without_output_mutation(&input);
  input = all_ready();
  input.WddmMajor = 2u;
  assert_invalid_without_output_mutation(&input);
  input = all_ready();
  input.WddmMinor = 1u;
  assert_invalid_without_output_mutation(&input);
  input = all_ready();
  input.NodeCount = 0u;
  assert_invalid_without_output_mutation(&input);
  input = all_ready();
  input.NodeCount = 2u;
  assert_invalid_without_output_mutation(&input);
  input = all_ready();
  input.ReadyMask |= 0x80000000u;
  assert_invalid_without_output_mutation(&input);
  input = all_ready();
  input.Reserved = 1u;
  assert_invalid_without_output_mutation(&input);
}

int main(void) {
  test_complete_contract_publishes_atomic_caps();
  test_each_missing_prerequisite_suppresses_all_caps();
  test_multiple_missing_prerequisites_are_reported_together();
  test_invalid_identity_and_reserved_bits_fail_atomically();
  return 0;
}
