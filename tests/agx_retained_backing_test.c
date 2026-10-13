#include "hv_agx_retained_backing.h"
#include <assert.h>
#include <string.h>
int main(void) {
  struct hv_contract_snapshot s;
  memset(&s,0,sizeof(s)); s.boot.ram_base=0x850000000ULL; s.boot.ram_size=0x60000000;
  s.region_count=3;
  s.regions[0]=(struct hv_contract_region){HV_CONTRACT_REGION_GUEST_RAM,0,0x850000000ULL,0x60000000};
  s.regions[1]=(struct hv_contract_region){HV_CONTRACT_REGION_FRAMEBUFFER,0,0x85f000000ULL,0x4000};
  s.regions[2]=(struct hv_contract_region){HV_CONTRACT_REGION_FIRMWARE,0,0x851000000ULL,0x4000};
  assert(hv_agx_retained_backing_allowed(&s,0x858000000ULL,0x4000));
  assert(!hv_agx_retained_backing_allowed(&s,0x805000000ULL,0x4000));
  assert(!hv_agx_retained_backing_allowed(&s,0x9fff78000ULL,0x4000));
  assert(!hv_agx_retained_backing_allowed(&s,0x85f000000ULL,0x4000));
  /* EXP1251: the boot inputs the launcher hands the guest (firmware image,
   * its heap, ADT and boot-args copies) are guest RAM once the guest runs
   * (Windows allocates them); they no longer deny GPU backing. The boot
   * framebuffer, device DART tables and a published ramdisk still do. */
  assert(hv_agx_retained_backing_allowed(&s,0x851000000ULL,0x4000));
  s.regions[2].kind=HV_CONTRACT_REGION_HEAP;
  assert(hv_agx_retained_backing_allowed(&s,0x851000000ULL,0x4000));
  s.regions[2].kind=HV_CONTRACT_REGION_ADT;
  assert(hv_agx_retained_backing_allowed(&s,0x851000000ULL,0x4000));
  s.regions[2].kind=HV_CONTRACT_REGION_BOOT_ARGS;
  assert(hv_agx_retained_backing_allowed(&s,0x851000000ULL,0x4000));
  s.regions[2].kind=HV_CONTRACT_REGION_DART_TABLES;
  assert(!hv_agx_retained_backing_allowed(&s,0x851000000ULL,0x4000));
  s.regions[2].kind=HV_CONTRACT_REGION_FIRMWARE;
  assert(!hv_agx_retained_backing_allowed(&s,0x858000001ULL,0x4000));
  /* M1: the ramdisk window is ordinary guest RAM unless the launcher
   * published a ramdisk region; then GPU DMA must not reach the boot medium. */
  assert(hv_agx_retained_backing_allowed(&s,0x860000000ULL,0x4000));
  assert(hv_agx_retained_backing_allowed(&s,0x89fffc000ULL,0x4000));
  s.regions[3]=(struct hv_contract_region){HV_CONTRACT_REGION_RAMDISK,0,0x860000000ULL,0x40000000};
  s.region_count=4;
  assert(!hv_agx_retained_backing_allowed(&s,0x860000000ULL,0x4000));
  assert(!hv_agx_retained_backing_allowed(&s,0x89fffc000ULL,0x4000));
  assert(hv_agx_retained_backing_allowed(&s,0x85fffc000ULL - 0x4000ULL,0x4000));
  assert(hv_agx_retained_backing_allowed(&s,0x8a0000000ULL,0x4000));
  /* A region kind this m1n1 does not know denies every page. */
  s.regions[3].kind=HV_CONTRACT_REGION_RAMDISK+1;
  assert(!hv_agx_retained_backing_allowed(&s,0x858000000ULL,0x4000));
  s.regions[3].kind=HV_CONTRACT_REGION_RAMDISK;
  s.regions[2].base=~0ULL-0x1000;
  assert(!hv_agx_retained_backing_allowed(&s,0x858000000ULL,0x4000));
  return 0;
}
