#include "apple_agx_g3_private_storage.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
  const unsigned pool_bytes=APPLE_AGX_G3_PRIVATE_UNITS*APPLE_AGX_G3_PRIVATE_UNIT;
  unsigned char *cpu=malloc(pool_bytes);
  APPLE_AGX_G3_PRIVATE_POOL pool={0};
  APPLE_AGX_G3_PRIVATE_MANAGER manager={0};
  APPLE_AGX_G3_PRIVATE_SCENE scenes[8]={{0}};
  unsigned full=2;
  APPLE_AGX_G4_NATIVE_RENDER r={0};
  r.WidthPx=2560;r.HeightPx=1600;r.Layers=1;r.Samples=1;
  r.UtileWidthPx=r.UtileHeightPx=16;
  memset(cpu,0xa5,pool_bytes);
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
  /* Further scenes until the process quota refuses one (EXP1094: 12 MiB). */
  while(full<7 && AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[full])) ++full;
  assert(full<7);
  APPLE_AGX_G3_PRIVATE_POOL before=pool;
  assert(!AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[full]));
  for(unsigned i=0;i<APPLE_AGX_G3_PRIVATE_UNITS;++i) assert(!memcmp(&before.Blocks[i],&pool.Blocks[i],sizeof(pool.Blocks[i])));
  assert(!scenes[full].Generation);
  /* The existing quota refusal must be observed before its partial allocations
   * are rolled back; observing it must not change the restored pool. */
  {
    APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC diagnostic={0};
    APPLE_AGX_G3_PRIVATE_POOL_STATS restored;
    assert(!AppleAgxG3PrivatePrepareObserved(&pool,1,cpu,1ULL<<36,&r,
        &manager,&scenes[full],&diagnostic));
    assert(diagnostic.Predicate==3u && diagnostic.FailedRange<9u);
    AppleAgxG3PrivatePoolStats(&pool,1,&restored);
    assert(diagnostic.Stats.OwnerUnits>restored.OwnerUnits);
    assert(diagnostic.Stats.GlobalUnits>restored.GlobalUnits);
    for(unsigned i=0;i<APPLE_AGX_G3_PRIVATE_UNITS;++i)
      assert(!memcmp(&before.Blocks[i],&pool.Blocks[i],sizeof(pool.Blocks[i])));
  }
  /* Failed construction of a new manager rolls back every extent. */
  for(unsigned free_units=1;free_units<93;++free_units) {
    APPLE_AGX_G3_PRIVATE_POOL q={0};
    APPLE_AGX_G3_PRIVATE_MANAGER m={0}; APPLE_AGX_G3_PRIVATE_SCENE s={0};
    APPLE_AGX_G3_PRIVATE_EXTENT used[2];
    /* Other owners take everything except free_units units. */
    for(unsigned owner=4;owner<2+APPLE_AGX_G3_PRIVATE_UNITS/128u;++owner) {
      APPLE_AGX_G3_PRIVATE_EXTENT other;
      assert(AppleAgxG3PrivateAllocate(&q,owner,8u<<20,&other));
    }
    assert(AppleAgxG3PrivateAllocate(&q,2,8u<<20,&used[0]));
    assert(AppleAgxG3PrivateAllocate(&q,3,(128-free_units)*0x10000,&used[1]));
    APPLE_AGX_G3_PRIVATE_POOL old=q;
    assert(!AppleAgxG3PrivatePrepare(&q,1,cpu,1ULL<<36,&r,&m,&s));
    assert(!m.Generation && !s.Generation);
    for(unsigned i=0;i<APPLE_AGX_G3_PRIVATE_UNITS;++i) assert(!memcmp(&old.Blocks[i],&q.Blocks[i],sizeof(q.Blocks[i])));
  }
  /* R164 (EXP884): the shared pool must host the buffer managers of the
   * desktop's concurrent D3D processes (DWM, Explorer, shell hosts). A
   * 4 MiB manager per process left only three processes renderable. */
  {
    APPLE_AGX_G3_PRIVATE_POOL q={0};
    APPLE_AGX_G4_NATIVE_RENDER small={0};
    small.WidthPx=16;small.HeightPx=16;small.Layers=1;small.Samples=1;
    small.UtileWidthPx=small.UtileHeightPx=32;
    memset(cpu,0xa5,pool_bytes);
    for(unsigned long long owner=1;owner<=7;++owner) {
      APPLE_AGX_G3_PRIVATE_MANAGER m={0}; APPLE_AGX_G3_PRIVATE_SCENE sc={0};
      assert(AppleAgxG3PrivatePrepare(&q,owner,cpu,owner<<36,&small,&m,&sc));
    }
  }
  /* EXP887: the eighth concurrent manager is refused by the global pool,
   * although its own quota is empty. Global backing must grow without using
   * its physical offset as an offset beyond a process's 32 MiB reservation. */
  {
    APPLE_AGX_G3_PRIVATE_POOL q={0};
    APPLE_AGX_G3_PRIVATE_MANAGER managers[9]={{0}};
    APPLE_AGX_G3_PRIVATE_SCENE sc[9]={{0}};
    APPLE_AGX_G4_NATIVE_RENDER small={0};
    const unsigned long long base=APPLE_AGX_G3_PRIVATE_VA_BYTES;
    unsigned high_backing=0;
    small.WidthPx=small.HeightPx=16;small.Layers=small.Samples=1;
    small.UtileWidthPx=small.UtileHeightPx=32;
    for(unsigned owner=1;owner<=9;++owner) {
      assert(AppleAgxG3PrivatePrepare(&q,owner,cpu,base,&small,
          &managers[owner-1],&sc[owner-1]));
      for(unsigned i=0;i<9;++i) {
        APPLE_AGX_G4_PROCESS_RANGE range=sc[owner-1].Ranges[i];
        assert(range.Va>=base && range.Va+range.Bytes<=base+APPLE_AGX_G3_PRIVATE_VA_BYTES);
        const APPLE_AGX_G3_PRIVATE_EXTENT *e=i<3 ?
            &managers[owner-1].Extents[i] : &sc[owner-1].Extents[i-3];
        if(e->Offset>=32u<<20) high_backing=1;
        for(unsigned prior=1;prior<owner;++prior)
          for(unsigned j=0;j<9;++j) {
            const APPLE_AGX_G3_PRIVATE_EXTENT *other=j<3 ?
                &managers[prior-1].Extents[j] : &sc[prior-1].Extents[j-3];
            assert(e->Offset+e->Bytes<=other->Offset || other->Offset+other->Bytes<=e->Offset);
          }
      }
    }
    assert(high_backing);
  }
  free(cpu);
  return 0;
}
