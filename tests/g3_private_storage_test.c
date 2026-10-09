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
  /* A 66-unit manager plus 27-unit scenes (2560x1600, 16x16 utiles). */
  const unsigned fit=(APPLE_AGX_G3_PROCESS_UNITS-66u)/27u;
  APPLE_AGX_G4_NATIVE_RENDER r={0};
  r.WidthPx=2560;r.HeightPx=1600;r.Layers=1;r.Samples=1;
  r.UtileWidthPx=r.UtileHeightPx=16;
  memset(cpu,0xa5,pool_bytes);
  assert(AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[0]));
  for(unsigned i=0;i<9;++i) {
    const APPLE_AGX_G3_PRIVATE_EXTENT *e=i<3 ? &manager.Extents[i] : &scenes[0].Extents[i-3];
    for(unsigned j=0;j<e->Bytes;++j) {
      if(i==0 && j<512) continue;
      if(i==1 && j<256 && (j%8)<4) continue;
      assert(cpu[e->Offset+j]==0);
    }
  }
  /* The heap range is the fixed 32-block window; 2560x1600 backs all of it. */
  assert(scenes[0].Ranges[2].Va==(1ULL<<36)+APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET);
  assert(scenes[0].Ranges[2].Bytes==32u*0x20000u);
  assert(manager.Blocks==32u && manager.Extents[2].Bytes==32u*0x20000u);
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
  assert(fit>=2u && fit<8u);
  for(unsigned i=2;i<fit;++i)
    assert(AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[i]));
  APPLE_AGX_G3_PRIVATE_POOL before=pool;
  assert(!AppleAgxG3PrivatePrepare(&pool,1,cpu,1ULL<<36,&r,&manager,&scenes[fit]));
  for(unsigned i=0;i<APPLE_AGX_G3_PRIVATE_UNITS;++i) assert(!memcmp(&before.Blocks[i],&pool.Blocks[i],sizeof(pool.Blocks[i])));
  assert(!scenes[fit].Generation);
  /* The existing quota refusal must be observed before its partial allocations
   * are rolled back; observing it must not change the restored pool. */
  {
    APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC diagnostic={0};
    APPLE_AGX_G3_PRIVATE_POOL_STATS restored;
    assert(!AppleAgxG3PrivatePrepareObserved(&pool,1,cpu,1ULL<<36,&r,
        &manager,&scenes[fit],&diagnostic));
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
    /* Full-screen renders back the whole 32-block heap of every manager. */
    small.WidthPx=2560;small.HeightPx=1600;small.Layers=small.Samples=1;
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
  /* Dynamic TVB heap (Asahi buffer.rs ensure_blocks/new_scene): the 32-block
   * window sits at the top of the process VA; a manager backs only its first
   * render's min_tvb_blocks and grows by whole blocks, never shrinking. */
  {
    APPLE_AGX_G3_PRIVATE_POOL q={0};
    APPLE_AGX_G3_PRIVATE_MANAGER m={0};
    APPLE_AGX_G3_PRIVATE_SCENE s1={0}, s2={0}, s3={0};
    APPLE_AGX_G3_PRIVATE_EXTENT *grown=(APPLE_AGX_G3_PRIVATE_EXTENT *)1;
    APPLE_AGX_G3_PRIVATE_POOL_STATS st;
    APPLE_AGX_G4_NATIVE_RENDER small={0}, full={0};
    const unsigned long long va=1ULL<<36;
    small.WidthPx=1280;small.HeightPx=800;small.Layers=small.Samples=1;
    small.UtileWidthPx=small.UtileHeightPx=32;
    full=small;full.WidthPx=2560;full.HeightPx=1600;
    assert(AppleAgxG4MinTvbBlocks(1280,800)==8u && AppleAgxG4MinTvbBlocks(2560,1600)==32u);
    assert(AppleAgxG4MinTvbBlocks(1707,1067)==16u && AppleAgxG4MinTvbBlocks(0,5)==0u);
    memset(cpu,0xa5,pool_bytes);
    assert(AppleAgxG3PrivatePrepare(&q,1,cpu,va,&small,&m,&s1));
    assert(m.Blocks==8u && m.GrownCount==0u);
    assert(m.Extents[2].Bytes==8u*0x20000u &&
        m.Extents[2].VaOffset==APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET);
    assert(s1.Ranges[2].Va==va+APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET &&
        s1.Ranges[2].Bytes==32u*0x20000u);
    for(unsigned j=0;j<m.Extents[2].Bytes;++j) assert(cpu[m.Extents[2].Offset+j]==0);
    {
      unsigned base=(unsigned)((va+APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET)>>15);
      unsigned *pages=(unsigned *)(cpu+m.Extents[0].Offset);
      unsigned *blocks=(unsigned *)(cpu+m.Extents[1].Offset);
      for(unsigned i=0;i<128;++i) assert(pages[i]==base+i);
      for(unsigned i=0;i<32;++i) assert(blocks[2*i]==base+4*i);
    }
    AppleAgxG3PrivatePoolStats(&q,1,&st);
    assert(st.OwnerUnits==2u+16u+8u);
    /* A larger render on this manager is refused until the heap grows. */
    assert(!AppleAgxG3PrivatePrepare(&q,1,cpu,va,&full,&m,&s2) && !s2.Generation);
    assert(AppleAgxG3PrivateGrowHeap(&q,1,cpu,&m,32u,&grown));
    assert(grown==&m.Grown[0] && m.Blocks==32u && m.GrownCount==1u);
    assert(grown->VaOffset==APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET+8u*0x20000u &&
        grown->Bytes==24u*0x20000u);
    for(unsigned j=0;j<grown->Bytes;++j) assert(cpu[grown->Offset+j]==0);
    assert(AppleAgxG3PrivateManagerExtentCount(&m)==4u &&
        AppleAgxG3PrivateManagerExtent(&m,3u)==grown &&
        AppleAgxG3PrivateManagerExtent(&m,2u)==&m.Extents[2] &&
        !AppleAgxG3PrivateManagerExtent(&m,4u));
    assert(AppleAgxG3PrivatePrepare(&q,1,cpu,va,&full,&m,&s2));
    assert(s2.Ranges[2].Va==s1.Ranges[2].Va && s2.Ranges[2].Bytes==s1.Ranges[2].Bytes);
    /* Never shrinks; a smaller need is a no-op without an extent. */
    assert(AppleAgxG3PrivateGrowHeap(&q,1,cpu,&m,8u,&grown) && !grown && m.Blocks==32u);
    assert(!AppleAgxG3PrivateGrowHeap(&q,1,cpu,&m,33u,&grown) && m.Blocks==32u);
    assert(!AppleAgxG3PrivateGrowHeap(&q,2,cpu,&m,32u,&grown));
    (void)s3;
  }
  /* A refused growth leaves pool and manager unchanged. */
  {
    APPLE_AGX_G3_PRIVATE_POOL q={0}, old;
    APPLE_AGX_G3_PRIVATE_MANAGER m={0}, mold;
    APPLE_AGX_G3_PRIVATE_SCENE s={0};
    APPLE_AGX_G3_PRIVATE_EXTENT other, *grown;
    APPLE_AGX_G4_NATIVE_RENDER small={0};
    small.WidthPx=1280;small.HeightPx=800;small.Layers=small.Samples=1;
    small.UtileWidthPx=small.UtileHeightPx=32;
    assert(AppleAgxG3PrivatePrepare(&q,1,cpu,1ULL<<36,&small,&m,&s));
    {
      unsigned owner=2;
      while(AppleAgxG3PrivateAllocate(&q,owner,8u<<20,&other)) ++owner;
      while(AppleAgxG3PrivateAllocate(&q,owner,0x10000u,&other)) {}
    }
    old=q;mold=m;
    assert(!AppleAgxG3PrivateGrowHeap(&q,1,cpu,&m,32u,&grown) && !grown);
    assert(!memcmp(&old,&q,sizeof(q)) && !memcmp(&mold,&m,sizeof(m)));
  }
  /* Fixed-VA allocation: inside the window, no overlap; general allocations
   * stay below the window. */
  {
    APPLE_AGX_G3_PRIVATE_POOL q={0};
    APPLE_AGX_G3_PRIVATE_EXTENT e, f;
    assert(!AppleAgxG3PrivateAllocateAt(&q,5,0x20000u,APPLE_AGX_G3_PRIVATE_VA_BYTES-0x10000u,&e));
    assert(!AppleAgxG3PrivateAllocateAt(&q,5,0x20000u,APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET+0x8000u,&e));
    assert(AppleAgxG3PrivateAllocateAt(&q,5,0x20000u,APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET,&e));
    assert(e.VaOffset==APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET);
    assert(!AppleAgxG3PrivateAllocateAt(&q,5,0x10000u,APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET+0x10000u,&f));
    for(unsigned i=0;i<126u;++i) {
      assert(AppleAgxG3PrivateAllocate(&q,5,0x10000u,&f));
      assert(f.VaOffset<APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET);
    }
  }
  free(cpu);
  return 0;
}
