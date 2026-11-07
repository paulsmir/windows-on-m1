#ifndef ADMISSION_UMD_CPU_ACCESS_H
#define ADMISSION_UMD_CPU_ACCESS_H

/* Receipt-only lifetime facts. Caller holds ScreenBufferLock. Never use these
 * counters as permission to map, trim, submit or publish shadow backing. */
typedef struct _ADMISSION_UMD_CPU_ACCESS {
  unsigned Maps, Locks, ActiveLocks, OwnerPointers, HeapMaps, HeapDepth;
  unsigned DestructionReported;
} ADMISSION_UMD_CPU_ACCESS;

enum ADMISSION_UMD_CPU_EVENT {
  AdmissionCpuMap, AdmissionCpuLock, AdmissionCpuUnlock,
  AdmissionCpuOwnerPointer, AdmissionCpuHeapMap, AdmissionCpuHeapUnmap
};

static inline void AdmissionUmdCpuIncrement(unsigned *Value) {
  if (*Value != ~0u) ++*Value; /* saturation must never look like no access */
}
static inline void AdmissionUmdCpuAccessEvent(ADMISSION_UMD_CPU_ACCESS *Facts,
                                             unsigned Event) {
  switch (Event) {
  case AdmissionCpuMap: AdmissionUmdCpuIncrement(&Facts->Maps); break;
  case AdmissionCpuLock:
    AdmissionUmdCpuIncrement(&Facts->Locks);
    AdmissionUmdCpuIncrement(&Facts->ActiveLocks);
    break;
  case AdmissionCpuUnlock:
    if (Facts->ActiveLocks && Facts->ActiveLocks != ~0u) --Facts->ActiveLocks;
    break;
  case AdmissionCpuOwnerPointer: AdmissionUmdCpuIncrement(&Facts->OwnerPointers); break;
  case AdmissionCpuHeapMap:
    AdmissionUmdCpuIncrement(&Facts->HeapMaps);
    AdmissionUmdCpuIncrement(&Facts->HeapDepth);
    break;
  case AdmissionCpuHeapUnmap:
    if (Facts->HeapDepth && Facts->HeapDepth != ~0u) --Facts->HeapDepth;
    break;
  }
}
#endif
