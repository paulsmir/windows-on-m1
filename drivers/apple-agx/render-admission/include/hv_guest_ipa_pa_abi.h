#ifndef APPLE_AGX_HV_GUEST_IPA_PA_ABI_H
#define APPLE_AGX_HV_GUEST_IPA_PA_ABI_H

typedef unsigned int hv_guest_ipa_pa_u32;
typedef unsigned long long hv_guest_ipa_pa_u64;

#define HV_GUEST_IPA_PA_HVC_IMMEDIATE 0x4d31u
#define HV_GUEST_IPA_PA_VERSION 1u
#define HV_GUEST_IPA_PA_TRANSLATE 1u
#define HV_GUEST_IPA_PA_MAX_PAGES 64u
#define HV_GUEST_IPA_PA_PAGE_SIZE 0x1000ull
#define HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE 0x4000ull

enum hv_guest_ipa_pa_status {
  HV_GUEST_IPA_PA_STATUS_EMPTY = 0,
  HV_GUEST_IPA_PA_STATUS_SUCCESS = 1,
  HV_GUEST_IPA_PA_STATUS_INVALID_REQUEST = 2,
  HV_GUEST_IPA_PA_STATUS_UNMAPPED = 3,
  HV_GUEST_IPA_PA_STATUS_NOT_RAM = 4,
};

struct hv_guest_ipa_pa_request {
  hv_guest_ipa_pa_u32 version;
  hv_guest_ipa_pa_u32 operation;
  hv_guest_ipa_pa_u32 count;
  hv_guest_ipa_pa_u32 status;
  hv_guest_ipa_pa_u64 ipa[HV_GUEST_IPA_PA_MAX_PAGES];
  hv_guest_ipa_pa_u64 pa[HV_GUEST_IPA_PA_MAX_PAGES];
};

#endif /* APPLE_AGX_HV_GUEST_IPA_PA_ABI_H */
