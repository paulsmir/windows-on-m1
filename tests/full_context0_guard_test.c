#include "apple_agx_uat_table.h"
#include "apple_agx_uat_publication.h"
#include <assert.h>
#include <string.h>
static unsigned allocations,maps;
static unsigned long long entries[2][2048];
static unsigned char region[0x4000];
static unsigned char allocate(void *ctx,APPLE_AGX_UAT_PAGE *p) {
  (void)ctx; assert(allocations<2); p->Entries=entries[allocations];
  p->PhysicalAddress=0x10004000ULL+allocations++*0x4000; return 1;
}
static void release(void *ctx,const APPLE_AGX_UAT_PAGE *p){(void)ctx;(void)p;}
static unsigned char map(void *ctx,unsigned long long pa,unsigned bytes,volatile unsigned char **v) {
  (void)ctx; assert(pa==J313_AGX_G2_GPU_BASE && bytes==0x4000);++maps;*v=region;return 1;
}
static void barrier(void *ctx){(void)ctx;}
static unsigned char unmap(void *ctx,volatile unsigned char *v){(void)ctx;return v==region;}
int main(void) {
  APPLE_AGX_UAT_ROOTS roots={0}; APPLE_AGX_UAT_PAGE pages[2];
  APPLE_AGX_UAT_MAPPING mappings[1];
  APPLE_AGX_UAT_INVENTORY inv={pages,2,0,mappings,1,0};
  APPLE_AGX_UAT_ALLOCATOR allocator={0,allocate,release};
  APPLE_AGX_CONFIG_SNAPSHOT snapshot={0}; APPLE_AGX_UAT_TTBR_PAIR pair;
  APPLE_AGX_UAT_PUBLICATION_IO io={0,map,barrier,unmap};
  APPLE_AGX_UAT_PUBLICATION_STATE state={0};
  assert(AppleAgxUatCreateAddressSpace(0,&allocator,&inv,&roots)==AppleAgxUatResultUnsupportedContext);
  assert(allocations==0 && !roots.Ttbr0PhysicalAddress && !roots.Ttbr1PhysicalAddress);
  assert(AppleAgxUatCreateAddressSpace(63,&allocator,&inv,&roots)==0 && allocations==2);
  assert(AppleAgxUatMap(0,&roots,0xffffffa000000000ULL,0x20000000,0x4000,
      AppleAgxUatFirmwareSharedReadWrite,&allocator,&inv)==AppleAgxUatResultUnsupportedContext);
  assert(allocations==2 && inv.MappingCount==0);
  snapshot.GpuRegionBase=J313_AGX_G2_GPU_BASE;
  assert(AppleAgxUatEncodeTtbrPair(0,&roots,&pair)==0);
  assert(AppleAgxUatPublishJ313(&snapshot,&pair,&io,&state)==AppleAgxUatPublicationResultInvalidArgument);
  assert(maps==0 && !state.Active);
  assert(AppleAgxUatEncodeTtbrPair(63,&roots,&pair)==0);
  assert(AppleAgxUatPublishJ313Context(&snapshot,63,&pair,&io,&state)==0);
  assert(maps==1 && state.Active && region[0]==0 && region[63*16]==1);
  assert(AppleAgxUatUnpublishJ313(&io,&state)==0);
  AppleAgxUatDestroy(&allocator,&inv);
  return 0;
}
