#ifndef APPLE_AGX_RETAINED_ROOT_ABI_H
#define APPLE_AGX_RETAINED_ROOT_ABI_H
#define AGX_RR_ABI_VERSION 4u
#define AGX_RR_OFFSET 0x600u
#define AGX_RR_WINDOW 0x100u
#define AGX_RR_DOORBELL 0x40u
#define AGX_RR_RESPONSE_OFFSET 0x80u
#define AGX_RR_PREPARE 1u
#define AGX_RR_ACTIVATE 2u
#define AGX_RR_MAP 3u
#define AGX_RR_UNMAP 4u
#define AGX_RR_QUERY 5u
#define AGX_RR_CLOSE 6u
#define AGX_RR_VERIFY_ABSENT 7u
#define AGX_RR_QUERY_ARENA 8u
#define AGX_RR_ARENA_VERSION 1u
#define AGX_RR_ARENA_SHARED 1u
#define AGX_RR_ARENA_TIMESTAMP 2u
#define AGX_RR_SHARED_ARENA_VA 0xffffffa041000000ULL
#define AGX_RR_SHARED_ARENA_BYTES 0x01000000ULL
#define AGX_RR_TIMESTAMP_ARENA_VA 0xffffffa071100000ULL
#define AGX_RR_TIMESTAMP_ARENA_BYTES 0x01000000ULL
#define AGX_RR_STATUS_REQUEST 0x100u
#define AGX_RR_FLAG_PREPARED 1u
#define AGX_RR_FLAG_ACTIVE 2u
#define AGX_RR_FLAG_PREFIX_UNCHANGED 4u
/* Preserve the existing Windows context63 contract, not arbitrary contexts. */
static inline unsigned char AgxRrGpuRegionWritable(unsigned long long offset,
                                                  unsigned long long bytes) {
  return bytes && bytes <= 16 && offset >= 63u * 16u &&
         offset <= 64u * 16u - bytes;
}
typedef struct _AGX_RR_REQUEST {
  unsigned int Version, Bytes, Command, Reserved;
  unsigned long long Sequence, Epoch, Va, Ipa, Length, Handle;
} AGX_RR_REQUEST;
typedef struct _AGX_RR_ARENA_DESCRIPTOR {
  unsigned int Version, Class;
  unsigned long long Va, Bytes;
} AGX_RR_ARENA_DESCRIPTOR;
typedef struct _AGX_RR_RESPONSE {
  unsigned long long Receipt;
  unsigned int Status, Flags;
  unsigned long long Epoch, Root, Ttbr0, SystemVa, SystemBytes, Handle, Pa, Count;
  unsigned int ArenaVersion, ArenaClass;
  unsigned long long ArenaVa, ArenaBytes;
} AGX_RR_RESPONSE;
#endif
