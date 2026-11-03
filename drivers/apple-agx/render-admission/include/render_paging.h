#ifndef APPLE_AGX_RENDER_PAGING_H
#define APPLE_AGX_RENDER_PAGING_H

#include "apple_agx_physical_paging.h"

#define ADMISSION_PAGING_MAGIC 0x504d4152u /* "RAMP" */
#define ADMISSION_PAGING_VERSION 1u
/* Resolved system/local/pool physical runs may span up to 4 MiB.
 * Aperture offsets must be resolved before coalescing. */
#define ADMISSION_PAGING_LOCAL_SEGMENT 2u
/* M2-f2: the KMD-populated pool is a second memory segment. */
#define ADMISSION_PAGING_POOL_SEGMENT 3u
#define ADMISSION_PAGING_MEMORY_SEGMENT(segment) \
  ((segment) == ADMISSION_PAGING_LOCAL_SEGMENT || \
   (segment) == ADMISSION_PAGING_POOL_SEGMENT)
#define ADMISSION_PAGING_LOCAL_RUN_MAX 0x400000u

enum {
  AdmissionPagingPhysical = 0u,
  AdmissionPagingVirtualFill = 1u,
  AdmissionPagingVirtualTransfer = 2u,
  AdmissionPagingMonitoredFence = 3u
};

typedef struct _ADMISSION_PAGING_MARKER {
  unsigned int Magic;
  unsigned int Version;
  unsigned int RecordBytes;
  unsigned int Reserved;
} ADMISSION_PAGING_MARKER;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201) /* nameless union, as in WDK headers */
#endif
typedef struct _ADMISSION_PAGING_RECORD {
  ADMISSION_PAGING_MARKER Header;
  /* Same-size union: virtual records do not execute the physical plan.
   * Retain input addresses for diagnostics without changing packet capacity. */
  union {
    APPLE_AGX_PHYSICAL_PAGING_PLAN Plan;
    struct { unsigned long long SourceVa, DestinationVa; } Virtual;
  };
  void *SystemMdl;
  unsigned int Kind;
  unsigned int SourceSegment;
  unsigned int DestinationSegment;
  unsigned int PatternOffset;
  unsigned long long SourceIpa;
  unsigned long long DestinationIpa;
  unsigned int Bytes;
  unsigned int FillPattern;
  unsigned long long FenceValue;
  /* M2-f3: encode order of virtual records (physical records keep 0). */
  unsigned long long Seq;
} ADMISSION_PAGING_RECORD;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

int AdmissionPagingFenceCanSubmit(unsigned int LastSubmitted,
                                  unsigned int Candidate,
                                  unsigned int Resubmission);
/* Nonzero for endpoints that can map a verified contiguous physical run. */
int AdmissionPagingContiguousRun(const ADMISSION_PAGING_RECORD *Record);
int AdmissionPagingRecordsValid(const ADMISSION_PAGING_RECORD *Records,
                                unsigned int RecordCount,
                                unsigned int MaximumRecords,
                                unsigned int DmaBytes);

#endif /* APPLE_AGX_RENDER_PAGING_H */
