#ifndef APPLE_AGX_G3_PRIVATE_POOL_H
#define APPLE_AGX_G3_PRIVATE_POOL_H

/* Caller serializes all operations. This is allocation accounting only:
 * Free is legal only after GPU unlink/TLB/revoke and zeroing have completed.
 * Quarantined extents deliberately remain allocated. No physical addresses
 * or user pointers are accepted here. */
#define APPLE_AGX_G3_PRIVATE_UNIT 0x10000u
/* Global physical backing is independent of each process reservation. */
#define APPLE_AGX_G3_PRIVATE_UNITS 1024u
/* EXP1116: 192 units (12 MiB). Two submissions in flight (EXP1093) left
 * DWM's 128-unit budget (66-unit full-screen manager plus 7-11 units per
 * scene) evicting a cached scene on nearly every miss; EXP1094 raised it
 * while every process still held a 4 MiB heap and exhausted the pool.
 * EXP1115's on-demand heaps leave ~500 of 1024 units free.
 * EXP1139/EXP1140: with two submissions per context in flight, queued
 * scenes are not cacheable and DWM's 192 units forced 6734 pressure
 * evictions (one in flight: 262); scene misses rose 0.45 % -> 9.7 % (29 %
 * in a GDI-window drag), and each miss maps tables only after the
 * process's queue drains. 246 units (~15.4 MiB) adds 54 units: still two
 * 8 MiB extents, the same 18-unit remainder after a 66-unit manager and
 * 27-unit full-screen scenes as 192 (quota refusals stay partial), and
 * ~300 pool units left with today's load. */
#define APPLE_AGX_G3_PROCESS_UNITS 246u
#define APPLE_AGX_G3_PRIVATE_VA_BYTES 0x02000000ULL
#define APPLE_AGX_G3_PRIVATE_VA_UNITS 512u
/* The top 4 MiB of each process's private VA are the 32-block TVB heap
 * window (Asahi buffer.rs), backed on demand at fixed VA; general
 * allocations stay below it. */
#define APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET 0x01c00000u
#define APPLE_AGX_G3_PRIVATE_GENERAL_VA_UNITS \
  (APPLE_AGX_G3_PRIVATE_HEAP_VA_OFFSET / APPLE_AGX_G3_PRIVATE_UNIT)

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

/* va_unit ~0u: first fit below the heap window; otherwise exactly there. */
static inline int AppleAgxG3PrivateAllocateVa(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, unsigned bytes, unsigned va_unit,
    APPLE_AGX_G3_PRIVATE_EXTENT *out) {
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
  if (va_unit!=~0u) {
    if (va_unit>=APPLE_AGX_G3_PRIVATE_VA_UNITS ||
        count>APPLE_AGX_G3_PRIVATE_VA_UNITS-va_unit) return 0;
    for (unsigned i=va_unit;i<va_unit+count;++i) if (va_used[i]) return 0;
    va_first=va_unit;
  } else {
    for(unsigned i=0;i<APPLE_AGX_G3_PRIVATE_GENERAL_VA_UNITS;++i) {
      if(va_used[i]) va_run=0;
      else if(++va_run==count) {va_first=i+1-count;break;}
    }
    if(va_run!=count) return 0;
  }
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

static inline int AppleAgxG3PrivateAllocate(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, unsigned bytes, APPLE_AGX_G3_PRIVATE_EXTENT *out) {
  return AppleAgxG3PrivateAllocateVa(p,owner,bytes,~0u,out);
}

/* Fixed process VA offset (unit aligned), e.g. TVB heap blocks. */
static inline int AppleAgxG3PrivateAllocateAt(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, unsigned bytes, unsigned va_offset,
    APPLE_AGX_G3_PRIVATE_EXTENT *out) {
  if (va_offset & (APPLE_AGX_G3_PRIVATE_UNIT-1u)) return 0;
  return AppleAgxG3PrivateAllocateVa(p,owner,bytes,
      va_offset/APPLE_AGX_G3_PRIVATE_UNIT,out);
}

/* The blocks of `e` as allocated to `owner` with e's generation. */
static inline int AppleAgxG3PrivateOwned(const APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, const APPLE_AGX_G3_PRIVATE_EXTENT *e,
    unsigned *first_out, unsigned *count_out) {
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
  *first_out=first; *count_out=count;
  return 1;
}

/* EXP1086: a released extent the same owner keeps (still mapped, its bytes
 * zeroed by the caller) takes a fresh generation, so leases naming the old
 * generation can no longer match it. */
static inline int AppleAgxG3PrivateRenew(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, APPLE_AGX_G3_PRIVATE_EXTENT *e) {
  unsigned first, count;
  if (!AppleAgxG3PrivateOwned(p,owner,e,&first,&count) ||
      p->NextGeneration == ~0ULL) return 0;
  ++p->NextGeneration;
  for (unsigned i=first; i<first+count; ++i)
    p->Blocks[i].Generation=p->NextGeneration;
  e->Generation=p->NextGeneration;
  return 1;
}

static inline int AppleAgxG3PrivateFree(APPLE_AGX_G3_PRIVATE_POOL *p,
    unsigned long long owner, const APPLE_AGX_G3_PRIVATE_EXTENT *e) {
  unsigned first, count;
  if (!AppleAgxG3PrivateOwned(p,owner,e,&first,&count)) return 0;
  for (unsigned i=first; i<first+count; ++i)
    p->Blocks[i]=(APPLE_AGX_G3_PRIVATE_BLOCK){0};
  return 1;
}
#endif
