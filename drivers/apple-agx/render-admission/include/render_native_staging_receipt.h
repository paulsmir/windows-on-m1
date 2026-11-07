#ifndef APPLE_AGX_RENDER_NATIVE_STAGING_RECEIPT_H
#define APPLE_AGX_RENDER_NATIVE_STAGING_RECEIPT_H
/* Plain C types like render_omitted_leaves.h: no user-mode CRT header may
 * reach the KMD build (EXP1923). v1 little-endian REG_BINARY
 * Wom1G3NativePteRefusal, written by the paging receipt worker.
 *
 * RC60 (EXP1981): one native-staging UpdatePageTable page that the KMD did
 * not publish as a native leaf because it is not the authenticated ring page
 * at its allocation offset. The first refusal is kept field by field; the
 * totals count every later one. VidMm's logical mapping is still applied and
 * the process is not poisoned; only the affected 16 KiB group stays omitted.
 * No pointer in this record is dereferenced by the worker or a decoder. */
#define ADMISSION_NATIVE_PTE_REFUSAL_VERSION 1u
/* Reason: why the page cannot be published from the native ring. */
#define ADMISSION_NATIVE_PTE_OWNER 1u   /* group page owned by another hAllocation */
#define ADMISSION_NATIVE_PTE_SEGMENT 2u /* valid PTE outside system segment 0 */
#define ADMISSION_NATIVE_PTE_RANGE 3u   /* allocation offset outside the binding */
#define ADMISSION_NATIVE_PTE_RING 4u    /* PFN is a ring page at another offset */
#define ADMISSION_NATIVE_PTE_FOREIGN 5u /* PFN is not a page of the ring */
#define ADMISSION_NATIVE_PTE_NONE (~0ULL)
typedef struct {
  unsigned int Version, Bytes, Reason, UpdateFlags;
  /* Input geometry of the first refused update (DXGK units). InputIndex is
   * ~0u for a retained neighbour page that this update did not supply. */
  unsigned int StartIndex, NumPageTableEntries, PageIndex, InputIndex;
  unsigned int Segment, OsProcessId, PageTableLevel, Reserved;
  unsigned long long Qpc, ProcessId, Allocation, PageOwner;
  unsigned long long FirstPteVirtualAddress, AllocationOffsetInBytes;
  unsigned long long PageVa, PageAllocationOffset, PageIpa;
  unsigned long long InputPteFlags, InputPageAddress;
  /* Ring identity: ExpectedIpa is the authenticated page at
   * BindingOffset + PageAllocationOffset, RingOffset the ring offset whose
   * page equals PageIpa; both ADMISSION_NATIVE_PTE_NONE when absent. */
  unsigned long long ExpectedIpa, BindingOffset, BindingBytes, RingOffset;
  unsigned long long RefusedGroups, RefusedUpdates;
} ADMISSION_NATIVE_PTE_REFUSAL;
#endif
