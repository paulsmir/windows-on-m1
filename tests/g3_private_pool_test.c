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
  assert(a.VaOffset==0 && b.VaOffset==0); /* Independent process namespaces. */
  c=b;c.VaOffset=0x10000;before=p;
  assert(!AppleAgxG3PrivateFree(&p,2,&c) && !memcmp(&p,&before,sizeof(p)));
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
  /* EXP1086: renewal gives a kept extent a fresh generation in place; the
   * old generation no longer frees it, a foreign owner cannot renew it. */
  {
    APPLE_AGX_G3_PRIVATE_EXTENT kept={0}, old;
    assert(AppleAgxG3PrivateAllocate(&p, 5, 0x30000, &kept));
    old=kept; before=p;
    assert(!AppleAgxG3PrivateRenew(&p, 6, &kept) && !memcmp(&p,&before,sizeof(p)));
    c=kept; c.Generation++;
    assert(!AppleAgxG3PrivateRenew(&p, 5, &c) && !memcmp(&p,&before,sizeof(p)));
    assert(AppleAgxG3PrivateRenew(&p, 5, &kept));
    assert(kept.Generation>old.Generation && kept.Offset==old.Offset &&
           kept.Bytes==old.Bytes && kept.VaOffset==old.VaOffset);
    for(unsigned i=old.Offset/0x10000u;i<(old.Offset+old.Bytes)/0x10000u;++i)
      assert(p.Blocks[i].Owner==5 && p.Blocks[i].Generation==kept.Generation);
    assert(!AppleAgxG3PrivateFree(&p, 5, &old));
    assert(AppleAgxG3PrivateFree(&p, 5, &kept));
    unsigned long long next=p.NextGeneration;
    assert(AppleAgxG3PrivateAllocate(&p, 5, 0x10000, &kept));
    p.NextGeneration=~0ULL; before=p;
    assert(!AppleAgxG3PrivateRenew(&p, 5, &kept) && !memcmp(&p,&before,sizeof(p)));
    p.NextGeneration=next+1;
    assert(AppleAgxG3PrivateFree(&p, 5, &kept));
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
