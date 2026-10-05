#include "agx_win32_construction_address.h"
#include <assert.h>
int main(void){AGX_WIN32_CONSTRUCTION_SPACE s; APPLE_AGX_U64 a=0,b=0,r=0;assert(AgxWin32ConstructionInitialize(&s,0x1000000000ULL,7)==AgxWin32ConstructionSuccess);assert(AgxWin32ConstructionReserve(&s,1,11,1,&a)==AgxWin32ConstructionSuccess&&a==0x1000000000ULL);assert(AgxWin32ConstructionReserve(&s,2,12,0x4001,&b)==AgxWin32ConstructionSuccess&&b==0x1000004000ULL);assert(AgxWin32ConstructionResolve(&s,1,11,0,1,&r)==AgxWin32ConstructionSuccess&&r==a);assert(AgxWin32ConstructionResolve(&s,1,12,0,1,&r)==AgxWin32ConstructionStale);assert(AgxWin32ConstructionResolve(&s,2,12,0x4001,1,&r)==AgxWin32ConstructionRange);assert(AgxWin32ConstructionRelease(&s,1,11)==AgxWin32ConstructionSuccess);assert(AgxWin32ConstructionResolve(&s,1,11,0,1,&r)==AgxWin32ConstructionStale);/* Live shader variants plus user resources exceeded the old 64 entries.
 * Capacity must agree with the existing Windows registry and fail atomically. */
assert(AgxWin32ConstructionInitialize(&s,0x1000000000ULL,7)==AgxWin32ConstructionSuccess);
for(unsigned i=0;i<AGX_WIN32_CONSTRUCTION_MAX_OBJECTS;++i)
  assert(AgxWin32ConstructionReserve(&s,i+1,i+1,1,&a)==AgxWin32ConstructionSuccess);
b=s.Next;
assert(AgxWin32ConstructionReserve(&s,999,999,1,&a)==AgxWin32ConstructionCapacity && !a && s.Next==b);
assert(AgxWin32ConstructionRelease(&s,1,1)==AgxWin32ConstructionSuccess);
assert(AgxWin32ConstructionReserve(&s,999,999,1,&a)==AgxWin32ConstructionSuccess && a==b);
assert(AgxWin32ConstructionResolve(&s,1,1,0,1,&r)==AgxWin32ConstructionStale);
return 0;}
