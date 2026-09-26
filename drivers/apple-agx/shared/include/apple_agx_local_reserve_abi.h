#ifndef APPLE_AGX_LOCAL_RESERVE_ABI_H
#define APPLE_AGX_LOCAL_RESERVE_ABI_H

/* Read-only m1n1 broker subwindow. The base of the broker itself comes from
 * the J313 G2 generated contract; this ABI never assigns a slab address. */
#define APPLE_AGX_LOCAL_RESERVE_OFFSET 0xD00ULL
#define APPLE_AGX_LOCAL_RESERVE_WINDOW_BYTES 0x28ULL
#define APPLE_AGX_LOCAL_RESERVE_MAGIC 0x4C584741U
#define APPLE_AGX_LOCAL_RESERVE_VERSION 1U
#define APPLE_AGX_LOCAL_RESERVE_BYTES 0x04000000ULL
#define APPLE_AGX_LOCAL_RESERVE_PHYSICAL_LIMIT (1ULL << 40)

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

static inline unsigned char AppleAgxLocalReserveMatchesResource(
    const APPLE_AGX_LOCAL_RESERVE_RECEIPT *Receipt,
    unsigned long long ResourceIpa, unsigned long long ResourceBytes) {
  return Receipt != 0 && Receipt->Magic == APPLE_AGX_LOCAL_RESERVE_MAGIC &&
         Receipt->Version == APPLE_AGX_LOCAL_RESERVE_VERSION &&
         Receipt->Valid == 1U &&
         Receipt->Bytes == APPLE_AGX_LOCAL_RESERVE_BYTES &&
         ResourceBytes == APPLE_AGX_LOCAL_RESERVE_BYTES &&
         Receipt->GuestIpa == ResourceIpa &&
         (ResourceIpa & (APPLE_AGX_LOCAL_RESERVE_BYTES - 1ULL)) == 0ULL &&
         (Receipt->HostPa & (APPLE_AGX_LOCAL_RESERVE_BYTES - 1ULL)) == 0ULL &&
         ResourceIpa != 0ULL && Receipt->HostPa != 0ULL &&
         Receipt->HostPa < APPLE_AGX_LOCAL_RESERVE_PHYSICAL_LIMIT &&
         APPLE_AGX_LOCAL_RESERVE_BYTES <=
             APPLE_AGX_LOCAL_RESERVE_PHYSICAL_LIMIT - Receipt->HostPa;
}

#endif
