#include "render_hvc.h"

#define ADMISSION_HVC_NULL ((void *)0)

static void AdmissionHvcZeroRequest(
    struct hv_guest_ipa_pa_request *Request) {
  unsigned char *bytes = (unsigned char *)Request;
  unsigned int index;
  for (index = 0u; index < (unsigned int)sizeof(*Request); ++index)
    bytes[index] = 0u;
}

static void AdmissionHvcZeroPages(unsigned long long *Pages,
                                  unsigned int PageCount) {
  unsigned int index;
  if (Pages == ADMISSION_HVC_NULL)
    return;
  for (index = 0u; index < PageCount; ++index)
    Pages[index] = 0ULL;
}

int AdmissionHvcRequestFitsLeaf(unsigned long long RequestIpa) {
  unsigned long long offset =
      RequestIpa & (HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE - 1ULL);
  return RequestIpa != 0ULL &&
         (RequestIpa & (sizeof(hv_guest_ipa_pa_u64) - 1ULL)) == 0ULL &&
         offset <= HV_GUEST_IPA_PA_STAGE2_LEAF_SIZE -
                       sizeof(struct hv_guest_ipa_pa_request);
}

int AdmissionHvcTranslatePages(
    const ADMISSION_HVC_IO *Io, struct hv_guest_ipa_pa_request *Request,
    unsigned long long RequestIpa, const unsigned long long *IpaPages,
    unsigned int PageCount, unsigned long long *PhysicalPages) {
  unsigned int translated = 0u;
  unsigned int index;

  if (PhysicalPages != ADMISSION_HVC_NULL)
    AdmissionHvcZeroPages(PhysicalPages, PageCount);
  if (Io == ADMISSION_HVC_NULL || Io->Invoke == ADMISSION_HVC_NULL ||
      Request == ADMISSION_HVC_NULL || IpaPages == ADMISSION_HVC_NULL ||
      PhysicalPages == ADMISSION_HVC_NULL || PageCount == 0u ||
      !AdmissionHvcRequestFitsLeaf(RequestIpa))
    return 0;
  for (index = 0u; index < PageCount; ++index) {
    if (IpaPages[index] == 0ULL ||
        (IpaPages[index] & (HV_GUEST_IPA_PA_PAGE_SIZE - 1ULL)) != 0ULL)
      return 0;
  }

  while (translated < PageCount) {
    unsigned int batch = PageCount - translated;
    unsigned int hvcStatus;
    if (batch > HV_GUEST_IPA_PA_MAX_PAGES)
      batch = HV_GUEST_IPA_PA_MAX_PAGES;
    AdmissionHvcZeroRequest(Request);
    Request->version = HV_GUEST_IPA_PA_VERSION;
    Request->operation = HV_GUEST_IPA_PA_TRANSLATE;
    Request->count = batch;
    for (index = 0u; index < batch; ++index)
      Request->ipa[index] = IpaPages[translated + index];
    hvcStatus = Io->Invoke(Io->Context, HV_GUEST_IPA_PA_HVC_IMMEDIATE,
                           RequestIpa, Request);
    if (hvcStatus != HV_GUEST_IPA_PA_STATUS_SUCCESS ||
        Request->status != HV_GUEST_IPA_PA_STATUS_SUCCESS)
      goto Fail;
    for (index = 0u; index < batch; ++index) {
      unsigned long long physical = Request->pa[index];
      if (physical == 0ULL ||
          (physical & (HV_GUEST_IPA_PA_PAGE_SIZE - 1ULL)) != 0ULL ||
          physical >= ADMISSION_HVC_PHYSICAL_LIMIT)
        goto Fail;
      PhysicalPages[translated + index] = physical;
    }
    translated += batch;
  }
  AdmissionHvcZeroRequest(Request);
  return 1;

Fail:
  AdmissionHvcZeroRequest(Request);
  AdmissionHvcZeroPages(PhysicalPages, PageCount);
  return 0;
}
