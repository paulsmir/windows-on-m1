#ifndef APPLE_AGX_LOCAL_RESERVE_ABI_H
#define APPLE_AGX_LOCAL_RESERVE_ABI_H

/* Read-only m1n1 broker subwindow. The base of the broker itself comes from
 * the J313 G2 generated contract; this ABI never assigns a slab address. */
#define APPLE_AGX_LOCAL_RESERVE_OFFSET 0xD00ULL
#define APPLE_AGX_LOCAL_RESERVE_WINDOW_BYTES 0x28ULL
#define APPLE_AGX_LOCAL_RESERVE_MAGIC 0x4C584741U
/* M2-f2c (version 3): the producer (m1n1) chooses the reserve size and the
 * receipt carries it. Its layout is fixed: segment 2 (VidMm local memory)
 * from offset 0, then the 64 MiB private span, then the 8 MiB backend arena
 * at the end. Consumers accept a 64 KiB-aligned size in [MIN, MAX]. */
#define APPLE_AGX_LOCAL_RESERVE_VERSION 3U
#define APPLE_AGX_LOCAL_RESERVE_ALIGNMENT 0x10000ULL
#define APPLE_AGX_LOCAL_RESERVE_PHYSICAL_LIMIT (1ULL << 40)
#define APPLE_AGX_LOCAL_RESERVE_PRIVATE_BYTES 0x04000000ULL
#define APPLE_AGX_LOCAL_RESERVE_BACKEND_BYTES 0x00800000ULL
/* Segment 2 holds scanout primaries (16 MiB each at 2560x1600) and the G3
 * page tables; 64 MiB is the validity floor, not a sizing policy. */
#define APPLE_AGX_LOCAL_RESERVE_MIN_SEGMENT_BYTES 0x04000000ULL
#define APPLE_AGX_LOCAL_RESERVE_MIN_BYTES                                      \
  (APPLE_AGX_LOCAL_RESERVE_MIN_SEGMENT_BYTES +                                 \
   APPLE_AGX_LOCAL_RESERVE_PRIVATE_BYTES + APPLE_AGX_LOCAL_RESERVE_BACKEND_BYTES)
/* The broker's local grants and the KMD's UAT page budget cover 1 GiB. */
#define APPLE_AGX_LOCAL_RESERVE_MAX_BYTES 0x40000000ULL

#define APPLE_AGX_LOCAL_REG_MAGIC 0x00ULL
#define APPLE_AGX_LOCAL_REG_VERSION 0x04ULL
#define APPLE_AGX_LOCAL_REG_VALID 0x08ULL
#define APPLE_AGX_LOCAL_REG_GUEST_IPA 0x10ULL
#define APPLE_AGX_LOCAL_REG_HOST_PA 0x18ULL
#define APPLE_AGX_LOCAL_REG_BYTES 0x20ULL

typedef struct _APPLE_AGX_LOCAL_RESERVE_RECEIPT {
  unsigned int Magic;
  unsigned int Version;
  unsigned int Valid;
  unsigned long long GuestIpa;
  unsigned long long HostPa;
  unsigned long long Bytes;
} APPLE_AGX_LOCAL_RESERVE_RECEIPT;

static inline unsigned char AppleAgxLocalReserveBytesValid(
    unsigned long long Bytes) {
  return Bytes >= APPLE_AGX_LOCAL_RESERVE_MIN_BYTES &&
         Bytes <= APPLE_AGX_LOCAL_RESERVE_MAX_BYTES &&
         (Bytes & (APPLE_AGX_LOCAL_RESERVE_ALIGNMENT - 1ULL)) == 0ULL;
}

/* Segment 2 bytes and the backend arena's offset for a valid size. */
static inline unsigned long long AppleAgxLocalReserveSegmentBytes(
    unsigned long long Bytes) {
  return Bytes - APPLE_AGX_LOCAL_RESERVE_PRIVATE_BYTES -
         APPLE_AGX_LOCAL_RESERVE_BACKEND_BYTES;
}

static inline unsigned long long AppleAgxLocalReserveBackendOffset(
    unsigned long long Bytes) {
  return Bytes - APPLE_AGX_LOCAL_RESERVE_BACKEND_BYTES;
}

static inline unsigned char AppleAgxLocalReserveMatchesResource(
    const APPLE_AGX_LOCAL_RESERVE_RECEIPT *Receipt,
    unsigned long long ResourceIpa, unsigned long long ResourceBytes) {
  return Receipt != 0 && Receipt->Magic == APPLE_AGX_LOCAL_RESERVE_MAGIC &&
         Receipt->Version == APPLE_AGX_LOCAL_RESERVE_VERSION &&
         Receipt->Valid == 1U &&
         AppleAgxLocalReserveBytesValid(Receipt->Bytes) &&
         ResourceBytes == Receipt->Bytes &&
         Receipt->GuestIpa == ResourceIpa && Receipt->HostPa == ResourceIpa &&
         (ResourceIpa & (APPLE_AGX_LOCAL_RESERVE_ALIGNMENT - 1ULL)) == 0ULL &&
         (Receipt->HostPa & (APPLE_AGX_LOCAL_RESERVE_ALIGNMENT - 1ULL)) == 0ULL &&
         ResourceIpa != 0ULL && Receipt->HostPa != 0ULL &&
         Receipt->HostPa < APPLE_AGX_LOCAL_RESERVE_PHYSICAL_LIMIT &&
         Receipt->Bytes <=
             APPLE_AGX_LOCAL_RESERVE_PHYSICAL_LIMIT - Receipt->HostPa;
}

#endif
