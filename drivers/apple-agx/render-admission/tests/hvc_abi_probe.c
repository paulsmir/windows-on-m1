#include <intrin.h>
#include "arm64_hypercalls.h"
#if defined(USE_EXPLICIT_HVC_ABI)
#define ARM_CALL(value) AdmissionHvcArmConsumed(value)
#define IPA_CALL(value) AdmissionHvcGuestIpaPa(value)
#else
#pragma intrinsic(__hvc)
#define ARM_CALL(value) __hvc(0x4d32, value)
#define IPA_CALL(value) __hvc(0x4d31, value)
#endif
unsigned long long CallArm(unsigned long long payload) { return ARM_CALL(payload); }
unsigned long long CallTranslate(unsigned long long payload) { return IPA_CALL(payload); }
unsigned int CompareArm(unsigned long long payload) { return ARM_CALL(payload) == 1; }
