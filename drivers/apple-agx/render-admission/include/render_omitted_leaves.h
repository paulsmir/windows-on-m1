#ifndef ADMISSION_RENDER_OMITTED_LEAVES_H
#define ADMISSION_RENDER_OMITTED_LEAVES_H
#include <stdint.h>
/* v1 little-endian REG_BINARY. History, not a current mapping inventory.
 * Bitmap bit n names GroupVa + n*16KiB; intersect it with the update span.
 * A later repair/unmap can supersede an event. No pointers are dereferenced
 * by the deferred worker. All fields are copied under the G3 state lock. */
#define ADMISSION_OMITTED_RING 8u
#define ADMISSION_OMITTED_BITMAP_BYTES 256u
#define ADMISSION_OMITTED_METADATA 1u
#define ADMISSION_OMITTED_BASE_VALID 2u
typedef struct {
  uint64_t Sequence, Qpc, ProcessId, OsProcessId, Allocation;
  uint64_t AllocationVa, AllocationBytes, UpdateVa, UpdateBytes;
  uint64_t GroupVa, FirstOmittedVa, LastOmittedVa;
  uint32_t Segment, OmittedGroups, GroupCount, Flags;
  uint8_t Bitmap[ADMISSION_OMITTED_BITMAP_BYTES];
} ADMISSION_OMITTED_EVENT;
typedef struct {
  uint32_t Version, Bytes, Next, Count;
  uint64_t TotalEvents, TotalGroups;
  ADMISSION_OMITTED_EVENT Events[ADMISSION_OMITTED_RING];
} ADMISSION_OMITTED_LEAVES;
#endif
