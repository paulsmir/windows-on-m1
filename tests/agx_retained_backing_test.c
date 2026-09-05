#include "hv_agx_retained_backing.h"
#include <assert.h>
#include <string.h>
int main(void) {
  struct hv_contract_snapshot s;
  memset(&s,0,sizeof(s)); s.boot.ram_base=0x850000000ULL; s.boot.ram_size=0x10000000;
  s.region_count=3;
  s.regions[0]=(struct hv_contract_region){HV_CONTRACT_REGION_GUEST_RAM,0,0x850000000ULL,0x10000000};
  s.regions[1]=(struct hv_contract_region){HV_CONTRACT_REGION_FRAMEBUFFER,0,0x85f000000ULL,0x4000};
  s.regions[2]=(struct hv_contract_region){HV_CONTRACT_REGION_FIRMWARE,0,0x851000000ULL,0x4000};
  assert(hv_agx_retained_backing_allowed(&s,0x858000000ULL,0x4000,0x860000000ULL,0x40000000));
  assert(!hv_agx_retained_backing_allowed(&s,0x805000000ULL,0x4000,0,0));
  assert(!hv_agx_retained_backing_allowed(&s,0x9fff78000ULL,0x4000,0,0));
  assert(!hv_agx_retained_backing_allowed(&s,0x85f000000ULL,0x4000,0,0));
  assert(!hv_agx_retained_backing_allowed(&s,0x851000000ULL,0x4000,0,0));
  assert(!hv_agx_retained_backing_allowed(&s,0x858000000ULL,0x4000,0x858000000ULL,0x400000));
  assert(!hv_agx_retained_backing_allowed(&s,0x858000001ULL,0x4000,0,0));
  s.regions[2].base=~0ULL-0x1000;
  assert(!hv_agx_retained_backing_allowed(&s,0x858000000ULL,0x4000,0,0));
  return 0;
}
