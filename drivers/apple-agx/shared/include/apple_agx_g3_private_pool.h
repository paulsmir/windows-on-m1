#ifndef APPLE_AGX_G3_PRIVATE_POOL_H
#define APPLE_AGX_G3_PRIVATE_POOL_H

/* Caller serializes all operations. This is allocation accounting only:
 * Free is legal only after GPU unlink/TLB/revoke and zeroing have completed.
 * Quarantined extents deliberately remain allocated. No physical addresses
 * or user pointers are accepted here. */
#define APPLE_AGX_G3_PRIVATE_UNIT 0x10000u
#define APPLE_AGX_G3_PRIVATE_UNITS 256u
#define APPLE_AGX_G3_PROCESS_UNITS 128u
#define APPLE_AGX_G3_PRIVATE_VA_BYTES 0x02000000ULL

typedef struct {
  unsigned long long Owner, Generation;
  unsigned First, Count;
} APPLE_AGX_G3_PRIVATE_BLOCK;
typedef struct {
  APPLE_AGX_G3_PRIVATE_BLOCK Blocks[APPLE_AGX_G3_PRIVATE_UNITS];
  unsigned long long NextGeneration;
} APPLE_AGX_G3_PRIVATE_POOL;
typedef struct {
  unsigned long long Generation;
  unsigned Offset, Bytes;
} APPLE_AGX_G3_PRIVATE_EXTENT;

static inline int AppleAgxG3PrivateAllocate(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, unsigned bytes, APPLE_AGX_G3_PRIVATE_EXTENT *out) {
  unsigned count, used=0, run=0, first=0;
  if (!p || !out || !owner || !bytes || bytes > (8u<<20) ||
      p->NextGeneration == ~0ULL) return 0;
  count=(bytes+APPLE_AGX_G3_PRIVATE_UNIT-1u)/APPLE_AGX_G3_PRIVATE_UNIT;
  for (unsigned i=0; i<APPLE_AGX_G3_PRIVATE_UNITS; ++i)
    if (p->Blocks[i].Owner==owner) ++used;
  if (count > APPLE_AGX_G3_PROCESS_UNITS-used) return 0;
  for (unsigned i=0; i<APPLE_AGX_G3_PRIVATE_UNITS; ++i) {
    if (p->Blocks[i].Owner) run=0;
    else if (++run==count) { first=i+1-count; break; }
  }
  if (run!=count) return 0;
  ++p->NextGeneration;
  for (unsigned i=first; i<first+count; ++i) {
    p->Blocks[i].Owner=owner;
    p->Blocks[i].Generation=p->NextGeneration;
    p->Blocks[i].First=first;
    p->Blocks[i].Count=count;
  }
  out->Generation=p->NextGeneration;
  out->Offset=first*APPLE_AGX_G3_PRIVATE_UNIT;
  out->Bytes=count*APPLE_AGX_G3_PRIVATE_UNIT;
  return 1;
}

static inline int AppleAgxG3PrivateFree(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, const APPLE_AGX_G3_PRIVATE_EXTENT *e) {
  unsigned first, count;
  if (!p || !e || !owner || !e->Generation || !e->Bytes ||
      ((e->Offset|e->Bytes)&(APPLE_AGX_G3_PRIVATE_UNIT-1)) ||
      e->Offset >= (16u<<20) || e->Bytes > (16u<<20)-e->Offset) return 0;
  first=e->Offset/APPLE_AGX_G3_PRIVATE_UNIT;
  count=e->Bytes/APPLE_AGX_G3_PRIVATE_UNIT;
  if (first >= APPLE_AGX_G3_PRIVATE_UNITS ||
      count > APPLE_AGX_G3_PRIVATE_UNITS-first) return 0;
  for (unsigned i=first; i<first+count; ++i)
    if (p->Blocks[i].Owner!=owner || p->Blocks[i].Generation!=e->Generation ||
        p->Blocks[i].First!=first || p->Blocks[i].Count!=count) return 0;
  for (unsigned i=first; i<first+count; ++i)
    p->Blocks[i]=(APPLE_AGX_G3_PRIVATE_BLOCK){0};
  return 1;
}
#endif
