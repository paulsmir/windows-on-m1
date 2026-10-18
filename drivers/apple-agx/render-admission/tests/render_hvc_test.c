#include "render_hvc.h"

#include <assert.h>
#include <string.h>

typedef struct _FAKE_HVC {
  unsigned int Calls;
  unsigned int FailCall;
  unsigned int PayloadFailureCall;
  unsigned int BadPhysicalCall;
} FAKE_HVC;

static unsigned int invoke(void *opaque, unsigned int immediate,
                           unsigned long long requestIpa,
                           struct hv_guest_ipa_pa_request *request) {
  FAKE_HVC *fake = (FAKE_HVC *)opaque;
  unsigned int index;
  assert(immediate == HV_GUEST_IPA_PA_HVC_IMMEDIATE);
  assert((requestIpa & 7ULL) == 0ULL);
  assert(request->version == HV_GUEST_IPA_PA_VERSION);
  assert(request->operation == HV_GUEST_IPA_PA_TRANSLATE);
  assert(request->count > 0u && request->count <= HV_GUEST_IPA_PA_MAX_PAGES);
  ++fake->Calls;
  if (fake->Calls == fake->FailCall)
    return HV_GUEST_IPA_PA_STATUS_UNMAPPED;
  if (fake->Calls == fake->PayloadFailureCall) {
    request->status = HV_GUEST_IPA_PA_STATUS_NOT_RAM;
    return HV_GUEST_IPA_PA_STATUS_SUCCESS;
  }
  request->status = HV_GUEST_IPA_PA_STATUS_SUCCESS;
  for (index = 0u; index < request->count; ++index)
    request->pa[index] = request->ipa[index] + 0x800000000ULL;
  if (fake->Calls == fake->BadPhysicalCall)
    request->pa[0] = 3ULL;
  return HV_GUEST_IPA_PA_STATUS_SUCCESS;
}

static void test_bounded_batch_translation(void) {
  struct hv_guest_ipa_pa_request request;
  ADMISSION_HVC_IO io;
  FAKE_HVC fake;
  unsigned long long ipa[130];
  unsigned long long pa[130];
  unsigned int index;

  memset(&request, 0, sizeof(request));
  memset(&fake, 0, sizeof(fake));
  io.Context = &fake;
  io.Invoke = invoke;
  for (index = 0u; index < 130u; ++index)
    ipa[index] = 0x100000ULL + (unsigned long long)index * 0x1000ULL;
  assert(AdmissionHvcRequestFitsLeaf(0x204000ULL));
  assert(AdmissionHvcTranslatePages(&io, &request, 0x204000ULL, ipa, 130u,
                                    pa));
  assert(fake.Calls == 3u);
  for (index = 0u; index < 130u; ++index)
    assert(pa[index] == ipa[index] + 0x800000000ULL);
  for (index = 0u; index < HV_GUEST_IPA_PA_MAX_PAGES; ++index) {
    assert(request.ipa[index] == 0ULL);
    assert(request.pa[index] == 0ULL);
  }
}

static void assert_failure_zeros_output(unsigned int failCall,
                                        unsigned int payloadFailureCall,
                                        unsigned int badPhysicalCall) {
  struct hv_guest_ipa_pa_request request;
  ADMISSION_HVC_IO io;
  FAKE_HVC fake;
  unsigned long long ipa[65];
  unsigned long long pa[65];
  unsigned int index;

  memset(&request, 0, sizeof(request));
  memset(&fake, 0, sizeof(fake));
  memset(pa, 0x5a, sizeof(pa));
  fake.FailCall = failCall;
  fake.PayloadFailureCall = payloadFailureCall;
  fake.BadPhysicalCall = badPhysicalCall;
  io.Context = &fake;
  io.Invoke = invoke;
  for (index = 0u; index < 65u; ++index)
    ipa[index] = 0x400000ULL + (unsigned long long)index * 0x1000ULL;
  assert(!AdmissionHvcTranslatePages(&io, &request, 0x800000ULL, ipa, 65u,
                                     pa));
  for (index = 0u; index < 65u; ++index)
    assert(pa[index] == 0ULL);
}

static void test_failure_is_atomic(void) {
  assert_failure_zeros_output(2u, 0u, 0u);
  assert_failure_zeros_output(0u, 2u, 0u);
  assert_failure_zeros_output(0u, 0u, 2u);
}

static void test_request_and_input_validation(void) {
  struct hv_guest_ipa_pa_request request;
  ADMISSION_HVC_IO io;
  FAKE_HVC fake;
  unsigned long long ipa = 0x100000ULL;
  unsigned long long pa = 0ULL;

  memset(&fake, 0, sizeof(fake));
  io.Context = &fake;
  io.Invoke = invoke;
  assert(!AdmissionHvcRequestFitsLeaf(0x203ff8ULL));
  assert(!AdmissionHvcRequestFitsLeaf(0x204001ULL));
  assert(!AdmissionHvcTranslatePages(&io, &request, 0x203ff8ULL, &ipa, 1u,
                                     &pa));
  ipa++;
  assert(!AdmissionHvcTranslatePages(&io, &request, 0x204000ULL, &ipa, 1u,
                                     &pa));
}

int main(void) {
  test_bounded_batch_translation();
  test_failure_is_atomic();
  test_request_and_input_validation();
  return 0;
}
