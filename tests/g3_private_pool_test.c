#include "apple_agx_g3_private_pool.h"
#include <assert.h>
#include <string.h>
int main(void) {
  APPLE_AGX_G3_PRIVATE_POOL p={0}, before;
  APPLE_AGX_G3_PRIVATE_EXTENT a={0}, b={0}, c={0};
  /* A full process quota includes data, table extents and padding. */
  assert(AppleAgxG3PrivateAllocate(&p, 1, 8u<<20, &a));
  assert(a.Bytes==(8u<<20) && a.Generation && a.Offset==0);
  before=p;
  assert(!AppleAgxG3PrivateAllocate(&p, 1, 1, &c));
  assert(memcmp(&p,&before,sizeof(p))==0);
  assert(AppleAgxG3PrivateAllocate(&p, 2, 8u<<20, &b));
  assert(b.Offset==8u<<20 && b.Generation!=a.Generation);
  /* Fill the remaining pool with further full quotas. */
  APPLE_AGX_G3_PRIVATE_EXTENT fill[APPLE_AGX_G3_PRIVATE_UNITS/128u];
  unsigned fills=0;
  for(unsigned owner=10;owner<8u+APPLE_AGX_G3_PRIVATE_UNITS/128u;++owner)
    assert(AppleAgxG3PrivateAllocate(&p, owner, 8u<<20, &fill[fills++]));
  /* A caller-supplied extent may not walk beyond the final pool block. */
  c=b; c.Bytes+=APPLE_AGX_G3_PRIVATE_UNIT;
  before=p; assert(!AppleAgxG3PrivateFree(&p, 2, &c));
  assert(memcmp(&p,&before,sizeof(p))==0);
  assert(!AppleAgxG3PrivateAllocate(&p, 3, 0x10000, &c));
  assert(!AppleAgxG3PrivateFree(&p, 2, &a));
  c=a; c.Generation++; assert(!AppleAgxG3PrivateFree(&p, 1, &c));
  assert(AppleAgxG3PrivateFree(&p, 1, &a));
  assert(!AppleAgxG3PrivateFree(&p, 1, &a));
  assert(AppleAgxG3PrivateAllocate(&p, 3, 1, &c));
  assert(c.Offset==a.Offset && c.Bytes==0x10000 && c.Generation!=a.Generation);
  before=p; assert(!AppleAgxG3PrivateFree(&p, 1, &a));
  assert(memcmp(&p,&before,sizeof(p))==0);
  assert(AppleAgxG3PrivateFree(&p, 3, &c));
  assert(AppleAgxG3PrivateFree(&p, 2, &b));
  for(unsigned i=0;i<fills;++i) assert(AppleAgxG3PrivateFree(&p, 10+i, &fill[i]));
  for(unsigned n=1;n<0x30000;n+=113) {
    assert(AppleAgxG3PrivateAllocate(&p, 4, n, &c));
    assert(c.Bytes>=n && c.Bytes-n<0x10000 && !(c.Bytes&0xffff));
    assert(AppleAgxG3PrivateFree(&p, 4, &c));
  }
  before=p;
  assert(!AppleAgxG3PrivateAllocate(&p, 0, 0x10000, &c));
  assert(!AppleAgxG3PrivateAllocate(&p, 1, 0, &c));
  assert(!AppleAgxG3PrivateAllocate(&p, 1, ~0u, &c));
  assert(memcmp(&p,&before,sizeof(p))==0);
  p.NextGeneration=~0ULL;
  assert(!AppleAgxG3PrivateAllocate(&p, 1, 0x10000, &c));
  return 0;
}
