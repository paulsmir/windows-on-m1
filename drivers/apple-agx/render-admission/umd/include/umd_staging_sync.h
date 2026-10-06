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

static inline unsigned long long AdmissionUmdStagingHash(
    const void *Data, unsigned long long Bytes) {
  const unsigned char *p = (const unsigned char *)Data;
  unsigned long long h = 0x9e3779b97f4a7c15ULL ^ Bytes;
  unsigned long long i = 0;
  for (; i + 8u <= Bytes; i += 8u) {
    unsigned long long w;
    memcpy(&w, p + i, sizeof(w));
    h = (h ^ w) * 0x100000001b3ULL;
    h ^= h >> 29;
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

#endif
