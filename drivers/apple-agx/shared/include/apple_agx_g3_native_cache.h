#ifndef APPLE_AGX_G3_NATIVE_CACHE_H
#define APPLE_AGX_G3_NATIVE_CACHE_H

/* KMD-owned proof hints, serialized by the G3 state lock. Keys are identities,
 * never dereferenced pointers. Bounded four-way replacement can cause a miss,
 * never a false hit. No allocation or page walk on the lookup path. */
#define APPLE_AGX_G3_NATIVE_CACHE_BUCKETS 1024u
#define APPLE_AGX_G3_NATIVE_CACHE_WAYS 4u
typedef struct _APPLE_AGX_G3_NATIVE_PROOF {
  unsigned long long Allocation, Va, Bytes, Token;
  unsigned int Access;
} APPLE_AGX_G3_NATIVE_PROOF;
typedef struct _APPLE_AGX_G3_NATIVE_CACHE {
  unsigned long long Clock, InvalidBefore;
  APPLE_AGX_G3_NATIVE_PROOF Entries[APPLE_AGX_G3_NATIVE_CACHE_BUCKETS]
                                     [APPLE_AGX_G3_NATIVE_CACHE_WAYS];
} APPLE_AGX_G3_NATIVE_CACHE;

static inline unsigned int AppleAgxG3NativeCacheBucket(unsigned long long key) {
  key ^= key >> 17;
  key ^= key >> 31;
  return (unsigned int)(key >> 4) & (APPLE_AGX_G3_NATIVE_CACHE_BUCKETS - 1u);
}

static inline void AppleAgxG3NativeCacheInvalidateAll(
    APPLE_AGX_G3_NATIVE_CACHE *cache) {
  cache->InvalidBefore = cache->Clock;
}

static inline void AppleAgxG3NativeCacheInvalidate(
    APPLE_AGX_G3_NATIVE_CACHE *cache, unsigned long long allocation) {
  unsigned int i, bucket = AppleAgxG3NativeCacheBucket(allocation);
  for (i = 0u; i < APPLE_AGX_G3_NATIVE_CACHE_WAYS; ++i)
    if (cache->Entries[bucket][i].Allocation == allocation)
      cache->Entries[bucket][i].Token = 0ULL;
}

static inline int AppleAgxG3NativeCacheHit(
    const APPLE_AGX_G3_NATIVE_CACHE *cache, unsigned long long allocation,
    unsigned long long va, unsigned long long bytes, unsigned int access,
    unsigned long long token) {
  unsigned int i, bucket = AppleAgxG3NativeCacheBucket(allocation);
  if (!token || token <= cache->InvalidBefore) return 0;
  for (i = 0u; i < APPLE_AGX_G3_NATIVE_CACHE_WAYS; ++i) {
    const APPLE_AGX_G3_NATIVE_PROOF *p = &cache->Entries[bucket][i];
    if (p->Token == token && p->Allocation == allocation && p->Va == va &&
        p->Bytes == bytes && p->Access == access) return 1;
  }
  return 0;
}

static inline unsigned long long AppleAgxG3NativeCacheStore(
    APPLE_AGX_G3_NATIVE_CACHE *cache, unsigned long long allocation,
    unsigned long long va, unsigned long long bytes, unsigned int access) {
  unsigned int i, bucket = AppleAgxG3NativeCacheBucket(allocation);
  APPLE_AGX_G3_NATIVE_PROOF *p = 0;
  /* Exhaustion disables new hints. Never wrap into a previously issued token. */
  if (cache->Clock == ~0ULL) return 0ULL;
  for (i = 0u; i < APPLE_AGX_G3_NATIVE_CACHE_WAYS; ++i)
    if (cache->Entries[bucket][i].Allocation == allocation) {
      p = &cache->Entries[bucket][i]; break;
    }
  for (i = 0u; !p && i < APPLE_AGX_G3_NATIVE_CACHE_WAYS; ++i)
    if (cache->Entries[bucket][i].Token <= cache->InvalidBefore)
      p = &cache->Entries[bucket][i];
  if (!p) p = &cache->Entries[bucket][cache->Clock % APPLE_AGX_G3_NATIVE_CACHE_WAYS];
  p->Allocation = allocation; p->Va = va; p->Bytes = bytes; p->Access = access;
  p->Token = ++cache->Clock;
  return p->Token;
}
#endif
