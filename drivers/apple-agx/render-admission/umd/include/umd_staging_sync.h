#ifndef UMD_STAGING_SYNC_H
#define UMD_STAGING_SYNC_H

#include <string.h>

/* A staged slot keeps its GPU-local canonical allocation equal to the CPU
 * staging copy after every successful upload or download. Re-uploading an
 * unchanged staging copy is pure overhead (EXP974: every DWM submit re-sent
 * ~2.9 MiB in 45 copy escapes). The content hash recorded at the last
 * synchronization decides whether the next upload is needed. */
typedef struct _ADMISSION_UMD_STAGING_SYNC {
  unsigned long long Hash;
  unsigned long long Bytes;
  int Valid;
} ADMISSION_UMD_STAGING_SYNC;

static inline unsigned long long AdmissionUmdStagingMix(
    unsigned long long h, unsigned long long w) {
  h = (h ^ w) * 0x100000001b3ULL;
  return h ^ (h >> 29);
}

/* Four independent lanes per 32 bytes so the multiply chain does not bound
 * throughput (EXP1055: DWM hashed 29.8 GB of staging at 2.5 GB/s in 170 s);
 * the lanes are folded in order, so words moved between lanes still change
 * the result. */
static inline unsigned long long AdmissionUmdStagingHash(
    const void *Data, unsigned long long Bytes) {
  const unsigned char *p = (const unsigned char *)Data;
  unsigned long long a = 0x9e3779b97f4a7c15ULL ^ Bytes;
  unsigned long long b = a + 0x632be59bd9b4e019ULL;
  unsigned long long c = a ^ 0x85ebca6b2fd5c3a1ULL;
  unsigned long long d = a - 0x27d4eb2f165667c5ULL;
  unsigned long long h, i = 0;
  for (; i + 32u <= Bytes; i += 32u) {
    unsigned long long w[4];
    memcpy(w, p + i, sizeof(w));
    a = AdmissionUmdStagingMix(a, w[0]);
    b = AdmissionUmdStagingMix(b, w[1]);
    c = AdmissionUmdStagingMix(c, w[2]);
    d = AdmissionUmdStagingMix(d, w[3]);
  }
  h = AdmissionUmdStagingMix(AdmissionUmdStagingMix(
      AdmissionUmdStagingMix(a, b), c), d);
  for (; i + 8u <= Bytes; i += 8u) {
    unsigned long long w;
    memcpy(&w, p + i, sizeof(w));
    h = AdmissionUmdStagingMix(h, w);
  }
  for (; i < Bytes; ++i)
    h = (h ^ p[i]) * 0x100000001b3ULL;
  return h ^ (h >> 32);
}

static inline int AdmissionUmdStagingUploadNeeded(
    const ADMISSION_UMD_STAGING_SYNC *Sync, unsigned long long Hash,
    unsigned long long Bytes) {
  return Sync == 0 || !Sync->Valid || Sync->Bytes != Bytes || Sync->Hash != Hash;
}

static inline void AdmissionUmdStagingRecord(
    ADMISSION_UMD_STAGING_SYNC *Sync, unsigned long long Hash,
    unsigned long long Bytes) {
  if (Sync == 0) return;
  Sync->Hash = Hash;
  Sync->Bytes = Bytes;
  Sync->Valid = 1;
}

static inline void AdmissionUmdStagingInvalidate(ADMISSION_UMD_STAGING_SYNC *Sync) {
  if (Sync != 0) Sync->Valid = 0;
}

/* EXP999: per-chunk hashes of the canonical copy as last synchronized. A
 * changed staging chunk is uploaded; an unchanged one is skipped (EXP997:
 * reused 512 KiB encoder BOs were re-sent whole for a few KiB of commands). */
#define ADMISSION_UMD_STAGING_CHUNKS 64u
typedef struct _ADMISSION_UMD_STAGING_CHUNK_SET {
  unsigned long long Hash[ADMISSION_UMD_STAGING_CHUNKS];
  int Valid;
} ADMISSION_UMD_STAGING_CHUNK_SET;

static inline int AdmissionUmdStagingChunked(unsigned long long Bytes,
                                             unsigned long long Chunk) {
  return Chunk != 0u && Bytes != 0u &&
         Bytes <= Chunk * (unsigned long long)ADMISSION_UMD_STAGING_CHUNKS;
}

static inline int AdmissionUmdStagingChunkCurrent(
    const ADMISSION_UMD_STAGING_CHUNK_SET *Set, unsigned Index,
    unsigned long long Hash) {
  return Set != 0 && Set->Valid && Index < ADMISSION_UMD_STAGING_CHUNKS &&
         Set->Hash[Index] == Hash;
}

static inline void AdmissionUmdStagingChunkStore(
    ADMISSION_UMD_STAGING_CHUNK_SET *Set, unsigned Index,
    unsigned long long Hash) {
  if (Set != 0 && Index < ADMISSION_UMD_STAGING_CHUNKS) Set->Hash[Index] = Hash;
}

static inline void AdmissionUmdStagingChunksValidate(
    ADMISSION_UMD_STAGING_CHUNK_SET *Set) {
  if (Set != 0) Set->Valid = 1;
}

static inline void AdmissionUmdStagingChunksInvalidate(
    ADMISSION_UMD_STAGING_CHUNK_SET *Set) {
  if (Set != 0) Set->Valid = 0;
}

#endif
