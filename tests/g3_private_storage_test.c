#include "apple_agx_g3_private_storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  unsigned char *cpu=malloc(16u<<20);
  APPLE_AGX_G3_PRIVATE_POOL pool={0};
  APPLE_AGX_G3_PRIVATE_MANAGER manager={0};
  APPLE_AGX_G3_PRIVATE_SCENE scenes[3]={{0}};
  APPLE_AGX_G4_NATIVE_RENDER r={0};
  r.WidthPx=2560;r.HeightPx=1600;r.Layers=1;r.Samples=1;
  r.UtileWidthPx=r.UtileHeightPx=16;
  memset(cpu,0xa5,16u<<20);
  assert(AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[0]));
  for(unsigned i=0;i<9;++i) {
    APPLE_AGX_G4_PROCESS_RANGE range=scenes[0].Ranges[i];
    unsigned offset=(unsigned)(range.Va-(1ULL<<36));
    for(unsigned j=0;j<range.Bytes;++j) {
      if(i==0 && j<512) continue;
      if(i==1 && j<256 && (j%8)<4) continue;
      assert(cpu[offset+j]==0);
    }
  }
  unsigned base=(unsigned)(scenes[0].Ranges[2].Va>>15);
  unsigned *pages=(unsigned *)(cpu+manager.Extents[0].Offset);
  unsigned *blocks=(unsigned *)(cpu+manager.Extents[1].Offset);
  for(unsigned i=0;i<128;++i) assert(pages[i]==base+i);
  for(unsigned i=0;i<32;++i) assert(blocks[2*i]==base+4*i && blocks[2*i+1]==0);
  /* Firmware-owned manager contents survive a second scene prepare. */
  pages[0]=0x1234;
  assert(AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[1]));
  assert(pages[0]==0x1234);
  for(unsigned i=3;i<9;++i)
    assert(scenes[0].Ranges[i].Va!=scenes[1].Ranges[i].Va);
  APPLE_AGX_G3_PRIVATE_POOL before=pool;
  assert(!AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[2]));
  for(unsigned i=0;i<256;++i) assert(!memcmp(&before.Blocks[i],&pool.Blocks[i],sizeof(pool.Blocks[i])));
  assert(!scenes[2].Generation);
  /* Failed construction of a new manager rolls back every extent. */
  for(unsigned free_units=1;free_units<93;++free_units) {
    APPLE_AGX_G3_PRIVATE_POOL q={0};
    APPLE_AGX_G3_PRIVATE_MANAGER m={0}; APPLE_AGX_G3_PRIVATE_SCENE s={0};
    APPLE_AGX_G3_PRIVATE_EXTENT used[2];
    assert(AppleAgxG3PrivateAllocate(&q,2,8u<<20,&used[0]));
    assert(AppleAgxG3PrivateAllocate(&q,3,(128-free_units)*0x10000,&used[1]));
    APPLE_AGX_G3_PRIVATE_POOL old=q;
    assert(!AppleAgxG3PrivatePrepare(&q,1,cpu,1ULL<<36,&r,&m,&s));
    assert(!m.Generation && !s.Generation);
    for(unsigned i=0;i<256;++i) assert(!memcmp(&old.Blocks[i],&q.Blocks[i],sizeof(q.Blocks[i])));
  }
  free(cpu);
  return 0;
}
