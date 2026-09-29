#ifndef APPLE_AGX_G3_PRIVATE_POOL_H
#define APPLE_AGX_G3_PRIVATE_POOL_H

/* Caller serializes all operations. This is allocation accounting only:
 * Free is legal only after GPU unlink/TLB/revoke and zeroing have completed.
 * Quarantined extents deliberately remain allocated. No physical addresses
 * or user pointers are accepted here. */
#define APPLE_AGX_G3_PRIVATE_UNIT 0x10000u
/* Global physical backing is independent of each process reservation. */
#define APPLE_AGX_G3_PRIVATE_UNITS 1024u
#define APPLE_AGX_G3_PROCESS_UNITS 128u
#define APPLE_AGX_G3_PRIVATE_VA_BYTES 0x02000000ULL
#define APPLE_AGX_G3_PRIVATE_VA_UNITS 512u

typedef struct {
  unsigned long long Owner, Generation;
  unsigned First, Count, VaFirst;
} APPLE_AGX_G3_PRIVATE_BLOCK;
typedef struct {
  APPLE_AGX_G3_PRIVATE_BLOCK Blocks[APPLE_AGX_G3_PRIVATE_UNITS];
  unsigned long long NextGeneration;
} APPLE_AGX_G3_PRIVATE_POOL;
typedef struct {
  unsigned long long Generation;
  unsigned Offset, Bytes, VaOffset;
} APPLE_AGX_G3_PRIVATE_EXTENT;

typedef struct {
  unsigned GlobalUnits, OwnerUnits, LargestFreeUnits;
} APPLE_AGX_G3_PRIVATE_POOL_STATS;

/* Observational only; caller holds the same serialization as allocation. */
static inline void AppleAgxG3PrivatePoolStats(
    const APPLE_AGX_G3_PRIVATE_POOL *p, unsigned long long owner,
    APPLE_AGX_G3_PRIVATE_POOL_STATS *out) {
  unsigned run=0;
  out->GlobalUnits=out->OwnerUnits=out->LargestFreeUnits=0;
  if (!p) return;
  for (unsigned i=0;i<APPLE_AGX_G3_PRIVATE_UNITS;++i) {
    if (p->Blocks[i].Owner) {
      ++out->GlobalUnits;
      if (owner && p->Blocks[i].Owner==owner) ++out->OwnerUnits;
      run=0;
    } else {
      ++run;
      if (run>out->LargestFreeUnits) out->LargestFreeUnits=run;
    }
  }
}

static inline int AppleAgxG3PrivateAllocate(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, unsigned bytes, APPLE_AGX_G3_PRIVATE_EXTENT *out) {
  unsigned count, used=0, run=0, first=0, va_run=0, va_first=0;
  unsigned char va_used[APPLE_AGX_G3_PRIVATE_VA_UNITS]={0};
  if (!p || !out || !owner || !bytes || bytes > (8u<<20) ||
      p->NextGeneration == ~0ULL) return 0;
  count=(bytes+APPLE_AGX_G3_PRIVATE_UNIT-1u)/APPLE_AGX_G3_PRIVATE_UNIT;
  for (unsigned i=0; i<APPLE_AGX_G3_PRIVATE_UNITS; ++i)
    if (p->Blocks[i].Owner==owner) {
      const APPLE_AGX_G3_PRIVATE_BLOCK *b=&p->Blocks[i];
      unsigned delta, va_index;
      if(i<b->First || i-b->First>=b->Count) return 0;
      delta=i-b->First;
      if(b->VaFirst>=APPLE_AGX_G3_PRIVATE_VA_UNITS ||
          delta>=APPLE_AGX_G3_PRIVATE_VA_UNITS-b->VaFirst) return 0;
      va_index=b->VaFirst+delta;
      if(va_index>=APPLE_AGX_G3_PRIVATE_VA_UNITS) return 0;
      if(va_used[va_index]) return 0;
      va_used[va_index]=1;
      ++used;
    }
  if(used>APPLE_AGX_G3_PROCESS_UNITS) return 0;
  if (count > APPLE_AGX_G3_PROCESS_UNITS-used) return 0;
  for (unsigned i=0; i<APPLE_AGX_G3_PRIVATE_UNITS; ++i) {
    if (p->Blocks[i].Owner) run=0;
    else if (++run==count) { first=i+1-count; break; }
  }
  if (run!=count) return 0;
  for(unsigned i=0;i<APPLE_AGX_G3_PRIVATE_VA_UNITS;++i) {
    if(va_used[i]) va_run=0;
    else if(++va_run==count) {va_first=i+1-count;break;}
  }
  if(va_run!=count) return 0;
  ++p->NextGeneration;
  for (unsigned i=first; i<first+count; ++i) {
    p->Blocks[i].Owner=owner;
    p->Blocks[i].Generation=p->NextGeneration;
    p->Blocks[i].First=first;
    p->Blocks[i].Count=count;
    p->Blocks[i].VaFirst=va_first;
  }
  out->Generation=p->NextGeneration;
  out->Offset=first*APPLE_AGX_G3_PRIVATE_UNIT;
  out->Bytes=count*APPLE_AGX_G3_PRIVATE_UNIT;
  out->VaOffset=va_first*APPLE_AGX_G3_PRIVATE_UNIT;
  return 1;
}

static inline int AppleAgxG3PrivateFree(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, const APPLE_AGX_G3_PRIVATE_EXTENT *e) {
  unsigned first, count;
  if (!p || !e || !owner || !e->Generation || !e->Bytes ||
      ((e->Offset|e->Bytes|e->VaOffset)&(APPLE_AGX_G3_PRIVATE_UNIT-1)) ||
      e->Offset >= APPLE_AGX_G3_PRIVATE_UNITS*APPLE_AGX_G3_PRIVATE_UNIT ||
      e->Bytes > APPLE_AGX_G3_PRIVATE_UNITS*APPLE_AGX_G3_PRIVATE_UNIT-e->Offset ||
      e->VaOffset>=APPLE_AGX_G3_PRIVATE_VA_BYTES ||
      e->Bytes>APPLE_AGX_G3_PRIVATE_VA_BYTES-e->VaOffset) return 0;
  first=e->Offset/APPLE_AGX_G3_PRIVATE_UNIT;
  count=e->Bytes/APPLE_AGX_G3_PRIVATE_UNIT;
  if (first >= APPLE_AGX_G3_PRIVATE_UNITS ||
      count > APPLE_AGX_G3_PRIVATE_UNITS-first) return 0;
  for (unsigned i=first; i<APPLE_AGX_G3_PRIVATE_UNITS && i<first+count; ++i)
    if (p->Blocks[i].Owner!=owner || p->Blocks[i].Generation!=e->Generation ||
        p->Blocks[i].First!=first || p->Blocks[i].Count!=count ||
        p->Blocks[i].VaFirst!=e->VaOffset/APPLE_AGX_G3_PRIVATE_UNIT) return 0;
  for (unsigned i=first; i<APPLE_AGX_G3_PRIVATE_UNITS && i<first+count; ++i)
    p->Blocks[i]=(APPLE_AGX_G3_PRIVATE_BLOCK){0};
  return 1;
}
#endif
