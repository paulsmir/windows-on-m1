#ifndef APPLE_AGX_RENDER_PAGING_H
#define APPLE_AGX_RENDER_PAGING_H

#include "apple_agx_physical_paging.h"

#define ADMISSION_PAGING_MAGIC 0x504d4152u /* "RAMP" */
#define ADMISSION_PAGING_VERSION 1u
/* ADMISSION_MEMORY_LOCAL_SEGMENT.  Its CPU view is linear, so a virtual
 * fill/transfer record may span physically contiguous pages there (up to
 * ADMISSION_PAGING_LOCAL_RUN_MAX bytes); any other segment is one 4 KiB
 * page per record. */
#define ADMISSION_PAGING_LOCAL_SEGMENT 2u
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

typedef struct _ADMISSION_PAGING_RECORD {
  ADMISSION_PAGING_MARKER Header;
  APPLE_AGX_PHYSICAL_PAGING_PLAN Plan;
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
} ADMISSION_PAGING_RECORD;

int AdmissionPagingFenceCanSubmit(unsigned int LastSubmitted,
                                  unsigned int Candidate,
                                  unsigned int Resubmission);
/* Nonzero when every byte of the record lies in the local segment. */
int AdmissionPagingLocalRun(const ADMISSION_PAGING_RECORD *Record);
int AdmissionPagingRecordsValid(const ADMISSION_PAGING_RECORD *Records,
                                unsigned int RecordCount,
                                unsigned int MaximumRecords,
                                unsigned int DmaBytes);

#endif /* APPLE_AGX_RENDER_PAGING_H */
