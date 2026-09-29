#ifndef APPLE_AGX_G3_PRIVATE_STORAGE_H
#define APPLE_AGX_G3_PRIVATE_STORAGE_H
#include "apple_agx_g3_private_pool.h"
#include "apple_agx_g4_submit.h"
#include <string.h>

typedef struct {
  unsigned long long Owner, Generation, VaBase;
  APPLE_AGX_G3_PRIVATE_EXTENT Extents[3];
} APPLE_AGX_G3_PRIVATE_MANAGER;
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
  unsigned required[9], managers=0, scratch=0;
  int fresh;
  if (diagnostic) memset(diagnostic,0,sizeof(*diagnostic));
  if (!pool || !owner || !cpu || !manager || !scene || scene->Generation ||
      va < (1ULL<<25) || va >= (1ULL<<39) || (va&0x01ffffffULL) ||
      !AppleAgxG4ProcessRequiredBytes(render,required) ||
      render->Layers!=1 || render->Samples!=1 || required[2]!=(32u*0x20000u))
  {
    AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,1u,~0u);
    return 0;
  }
  fresh=manager->Generation==0;
  if (!fresh && (manager->Owner!=owner || manager->VaBase!=va)) {
    AppleAgxG3PrivatePrepareFailure(diagnostic,pool,owner,2u,~0u);
    return 0;
  }
  m=*manager;
  for (unsigned i=0;i<9;++i) {
    APPLE_AGX_G3_PRIVATE_EXTENT *e;
    if (i<3) {
      e=&m.Extents[i];
      if (fresh) {
        if (!AppleAgxG3PrivateAllocate(pool,owner,required[i],e)) {
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
    s.Ranges[i]=(APPLE_AGX_G4_PROCESS_RANGE){va+e->Offset,e->Bytes,0};
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

static inline int AppleAgxG3PrivatePrepare(APPLE_AGX_G3_PRIVATE_POOL *pool,
    unsigned long long owner, unsigned char *cpu, unsigned long long va,
    const APPLE_AGX_G4_NATIVE_RENDER *render,
    APPLE_AGX_G3_PRIVATE_MANAGER *manager, APPLE_AGX_G3_PRIVATE_SCENE *scene) {
  return AppleAgxG3PrivatePrepareObserved(pool,owner,cpu,va,render,manager,
      scene,NULL);
}
#endif
