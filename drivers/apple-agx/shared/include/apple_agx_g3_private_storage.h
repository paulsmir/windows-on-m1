#ifndef APPLE_AGX_G3_PRIVATE_STORAGE_H
#define APPLE_AGX_G3_PRIVATE_STORAGE_H
#include "apple_agx_g3_private_pool.h"
#include "apple_agx_g4_submit.h"
#include <string.h>

/* TVB heap blocks are 128 KiB (Asahi buffer.rs BLOCK_SIZE); the process
 * window holds 32, matching AppleAgxG4ProcessRequiredBytes. */
#define APPLE_AGX_G3_PRIVATE_HEAP_BLOCK 0x20000u
#define APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS 32u
#define APPLE_AGX_G3_PRIVATE_HEAP_GROWS 8u
typedef struct {
  unsigned long long Owner, Generation, VaBase;
  /* Page list, block list, and the heap's first backed blocks. */
  APPLE_AGX_G3_PRIVATE_EXTENT Extents[3];
  /* Asahi buffer.rs ensure_blocks: later blocks, VA-contiguous after
   * Extents[2]; Blocks of the 32-block window are backed, never fewer. */
  APPLE_AGX_G3_PRIVATE_EXTENT Grown[APPLE_AGX_G3_PRIVATE_HEAP_GROWS];
  unsigned GrownCount, Blocks;
} APPLE_AGX_G3_PRIVATE_MANAGER;

static inline unsigned AppleAgxG3PrivateManagerExtentCount(
    const APPLE_AGX_G3_PRIVATE_MANAGER *m) {
  return m && m->Generation ? 3u+m->GrownCount : 0u;
}
static inline APPLE_AGX_G3_PRIVATE_EXTENT *AppleAgxG3PrivateManagerExtent(
    APPLE_AGX_G3_PRIVATE_MANAGER *m, unsigned i) {
  if (!m || i>=AppleAgxG3PrivateManagerExtentCount(m)) return 0;
  return i<3u ? &m->Extents[i] : &m->Grown[i-3u];
}
typedef struct {
  unsigned long long Generation;
  APPLE_AGX_G3_PRIVATE_EXTENT Extents[6];
  APPLE_AGX_G4_PROCESS_RANGE Ranges[9];
} APPLE_AGX_G3_PRIVATE_SCENE;

/* CPU-only construction. Caller owns the Normal/WC pool mapping and mutex.
 * No grant, leaf, GPU lease or public token exists until this succeeds and
 * the caller orders stores before graph publication. Existing manager lists
 * may be firmware-owned and must never be rewritten by a new scene. */
typedef struct {
  unsigned Predicate, FailedRange;
  APPLE_AGX_G3_PRIVATE_POOL_STATS Stats;
} APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC;

static inline void AppleAgxG3PrivatePrepareFailure(
    APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC *diagnostic,
    const APPLE_AGX_G3_PRIVATE_POOL *pool, unsigned long long owner,
    unsigned predicate, unsigned range) {
  if (!diagnostic) return;
  diagnostic->Predicate=predicate;
  diagnostic->FailedRange=range;
  AppleAgxG3PrivatePoolStats(pool,owner,&diagnostic->Stats);
}

static inline int AppleAgxG3PrivatePrepareObserved(APPLE_AGX_G3_PRIVATE_POOL *pool,
    unsigned long long owner, unsigned char *cpu, unsigned long long va,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    APPLE_AGX_G3_PRIVATE_MANAGER *manager, APPLE_AGX_G3_PRIVATE_SCENE *scene,
    APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC *diagnostic) {
  APPLE_AGX_G3_PRIVATE_MANAGER m={0};
  APPLE_AGX_G3_PRIVATE_SCENE s={0};
  unsigned required[9], managers=0, scratch=0, need;
  int fresh;
  if (diagnostic) memset(diagnostic,0,sizeof(*diagnostic));
  if (!pool || !owner || !cpu || !manager || !scene || scene->Generation ||
      va < (1ULL<<25) || va >= (1ULL<<39) || (va&0x01ffffffULL) ||
      !AppleAgxG4ProcessRequiredBytes(render,required) ||
      render->Layers!=1 || render->Samples!=1 ||
      required[2]!=APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK)
  {
    AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,1u,~0u);
    return 0;
  }
  need=AppleAgxG4MinTvbBlocks(render->WidthPx,render->HeightPx);
  fresh=manager->Generation==0;
  if (!fresh && (manager->Owner!=owner || manager->VaBase!=va)) {
    AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,2u,~0u);
    return 0;
  }
  /* Asahi queue/render.rs: ensure_blocks(min_tvb_blocks) precedes the
   * scene; an existing manager is grown by the caller while idle. */
  if (!need || need>APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS ||
      (!fresh && need>manager->Blocks)) {
    AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,6u,2u);
    return 0;
  }
  m=*manager;
  for (unsigned i=0;i<9;++i) {
    APPLE_AGX_G3_PRIVATE_EXTENT *e;
    if (i<3) {
      e=&m.Extents[i];
      if (fresh) {
        if (i<2 ? !AppleAgxG3PrivateAllocate(pool,owner,required[i],e) :
            !AppleAgxG3PrivateAllocateAt(pool,owner,
                need*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK,
                APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET,e)) {
          AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,3u,i);
          goto Fail;
        }
        ++managers;
      }
    } else {
      e=&s.Extents[i-3];
      if (!AppleAgxG3PrivateAllocate(pool,owner,required[i],e)) {
        AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,3u,i);
        goto Fail;
      }
      ++scratch;
    }
    if (fresh || i>=3) memset(cpu+e->Offset,0,e->Bytes);
    s.Ranges[i]=i==2 ? (APPLE_AGX_G4_PROCESS_RANGE){
        va+APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET,required[2],0} :
        (APPLE_AGX_G4_PROCESS_RANGE){va+e->VaOffset,e->Bytes,0};
  }
  if (fresh) {
    unsigned long long number=s.Ranges[2].Va>>15;
    unsigned blocks=required[2]/0x20000u;
    unsigned *page_list=(unsigned *)(cpu+m.Extents[0].Offset);
    unsigned *block_list=(unsigned *)(cpu+m.Extents[1].Offset);
    if (number+4ULL*blocks>0xffffffffULL) {
      AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,4u,2u);
      goto Fail;
    }
    for (unsigned i=0;i<blocks;++i) {
      block_list[2*i]=(unsigned)(number+4ULL*i);
      for (unsigned j=0;j<4;++j) page_list[4*i+j]=(unsigned)(number+4ULL*i+j);
    }
    m.Owner=owner; m.VaBase=va; m.Generation=m.Extents[0].Generation;
    m.Blocks=need;
  }
  s.Generation=s.Extents[0].Generation;
  *manager=m; *scene=s;
  return 1;
Fail:
  while (scratch) {
    APPLE_AGX_G3_PRIVATE_EXTENT *e=&s.Extents[--scratch];
    memset(cpu+e->Offset,0,e->Bytes);
    (void)AppleAgxG3PrivateFree(pool,owner,e);
  }
  while (managers) {
    APPLE_AGX_G3_PRIVATE_EXTENT *e=&m.Extents[--managers];
    memset(cpu+e->Offset,0,e->Bytes);
    (void)AppleAgxG3PrivateFree(pool,owner,e);
  }
  return 0;
}

/* Asahi buffer.rs ensure_blocks: back heap blocks [Blocks, blocks) at their
 * window VA. CPU-only: the caller maps *grown (NULL when nothing was needed)
 * before a render names the new blocks, and grows only an idle manager.
 * A failure leaves pool and manager unchanged. */
static inline int AppleAgxG3PrivateGrowHeap(APPLE_AGX_G3_PRIVATE_POOL *pool,
    unsigned long long owner, unsigned char *cpu,
    APPLE_AGX_G3_PRIVATE_MANAGER *m, unsigned blocks,
    APPLE_AGX_G3_PRIVATE_EXTENT **grown) {
  APPLE_AGX_G3_PRIVATE_EXTENT e;
  if (grown) *grown=0;
  if (!pool || !cpu || !m || !grown || !owner || !m->Generation ||
      m->Owner!=owner || !m->Blocks ||
      blocks>APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS) return 0;
  if (blocks<=m->Blocks) return 1;
  if (m->GrownCount>=APPLE_AGX_G3_PRIVATE_HEAP_GROWS ||
      !AppleAgxG3PrivateAllocateAt(pool,owner,
          (blocks-m->Blocks)*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK,
          APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET+
              m->Blocks*APPLE_AGX_G3_PRIVATE_HEAP_BLOCK,&e)) return 0;
  memset(cpu+e.Offset,0,e.Bytes);
  m->Grown[m->GrownCount]=e;
  *grown=&m->Grown[m->GrownCount++];
  m->Blocks=blocks;
  return 1;
}

static inline int AppleAgxG3PrivatePrepare(APPLE_AGX_G3_PRIVATE_POOL *pool,
    unsigned long long owner, unsigned char *cpu, unsigned long long va,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    APPLE_AGX_G3_PRIVATE_MANAGER *manager, APPLE_AGX_G3_PRIVATE_SCENE *scene) {
  return AppleAgxG3PrivatePrepareObserved(pool,owner,cpu,va,render,manager,
      scene,NULL);
}
#endif
