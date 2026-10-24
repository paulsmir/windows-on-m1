#ifndef ADMISSION_RENDER_OMITTED_LEAVES_H
#define ADMISSION_RENDER_OMITTED_LEAVES_H
/* Plain C types like render_gpu_fault.h: <stdint.h> pulls the user-mode
 * vcruntime.h into the KMD build (EXP1923 C4083/C2220 under /WX). */
/* v1 little-endian REG_BINARY. History, not a current mapping inventory.
 * Bitmap bit n names GroupVa + n*16KiB; intersect it with the update span.
 * A later repair/unmap can supersede an event. No pointers are dereferenced
 * by the deferred worker. All fields are copied under the G3 state lock. */
#define ADMISSION_OMITTED_RING 8u
#define ADMISSION_OMITTED_BITMAP_BYTES 256u
#define ADMISSION_OMITTED_METADATA 1u
#define ADMISSION_OMITTED_BASE_VALID 2u
typedef struct {
  unsigned long long Sequence, Qpc, ProcessId, OsProcessId, Allocation;
  unsigned long long AllocationVa, AllocationBytes, UpdateVa, UpdateBytes;
  unsigned long long GroupVa, FirstOmittedVa, LastOmittedVa;
  unsigned int Segment, OmittedGroups, GroupCount, Flags;
  unsigned char Bitmap[ADMISSION_OMITTED_BITMAP_BYTES];
} ADMISSION_OMITTED_EVENT;
typedef struct {
  unsigned int Version, Bytes, Next, Count;
  unsigned long long TotalEvents, TotalGroups;
  ADMISSION_OMITTED_EVENT Events[ADMISSION_OMITTED_RING];
} ADMISSION_OMITTED_LEAVES;
#endif
