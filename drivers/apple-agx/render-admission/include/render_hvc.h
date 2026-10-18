#ifndef APPLE_AGX_RENDER_HVC_H
#define APPLE_AGX_RENDER_HVC_H

#include "hv_guest_ipa_pa_abi.h"

#define ADMISSION_HVC_PHYSICAL_LIMIT (1ULL << 40u)

typedef struct _ADMISSION_HVC_IO {
  void *Context;
  unsigned int (*Invoke)(void *Context, unsigned int Immediate,
                         unsigned long long RequestIpa,
                         struct hv_guest_ipa_pa_request *Request);
} ADMISSION_HVC_IO;

int AdmissionHvcRequestFitsLeaf(unsigned long long RequestIpa);
int AdmissionHvcTranslatePages(
    const ADMISSION_HVC_IO *Io, struct hv_guest_ipa_pa_request *Request,
    unsigned long long RequestIpa, const unsigned long long *IpaPages,
    unsigned int PageCount, unsigned long long *PhysicalPages);

#endif /* APPLE_AGX_RENDER_HVC_H */
