#include "apple_agx_wddm_feature_contract.h"

#define APPLE_AGX_WDDM_FEATURE_NULL ((void *)0)

APPLE_AGX_WDDM_FEATURE_CONTRACT_RESULT AppleAgxWddmFeatureContractEvaluate(
    const APPLE_AGX_WDDM_FEATURE_INPUT *Input,
    APPLE_AGX_WDDM_FEATURE_OUTPUT *Output) {
  APPLE_AGX_WDDM_FEATURE_OUTPUT candidate;
  unsigned int unknownReadyBits;

  if (Input == APPLE_AGX_WDDM_FEATURE_NULL ||
      Output == APPLE_AGX_WDDM_FEATURE_NULL ||
      Input->Version != APPLE_AGX_WDDM_FEATURE_CONTRACT_VERSION ||
      Input->Size != sizeof(*Input) || Input->WddmMajor != 3u ||
      Input->WddmMinor != 0u || Input->NodeCount != 1u ||
      Input->Reserved != 0u)
    return AppleAgxWddmFeatureContractInvalid;

  unknownReadyBits = Input->ReadyMask & ~APPLE_AGX_WDDM_REQUIRED_READY_MASK;
  if (unknownReadyBits != 0u)
    return AppleAgxWddmFeatureContractInvalid;

  candidate.Ready = 0u;
  candidate.RequiredReadyMask = APPLE_AGX_WDDM_REQUIRED_READY_MASK;
  candidate.MissingReadyMask =
      APPLE_AGX_WDDM_REQUIRED_READY_MASK & ~Input->ReadyMask;
  candidate.PublishCapsMask = 0u;
  if (candidate.MissingReadyMask == 0u) {
    candidate.Ready = 1u;
    candidate.PublishCapsMask = APPLE_AGX_WDDM_MANDATORY_CAPS_MASK;
  }
  *Output = candidate;
  return candidate.Ready != 0u ? AppleAgxWddmFeatureContractReady
                               : AppleAgxWddmFeatureContractIncomplete;
}
