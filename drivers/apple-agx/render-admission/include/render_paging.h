#ifndef APPLE_AGX_RENDER_PAGING_H
#define APPLE_AGX_RENDER_PAGING_H

#include "apple_agx_physical_paging.h"

#define ADMISSION_PAGING_MAGIC 0x504d4152u /* "RAMP" */
#define ADMISSION_PAGING_VERSION 1u

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
} ADMISSION_PAGING_RECORD;

int AdmissionPagingFenceCanSubmit(unsigned int LastSubmitted,
                                  unsigned int Candidate,
                                  unsigned int Resubmission);
int AdmissionPagingRecordsValid(const ADMISSION_PAGING_RECORD *Records,
                                unsigned int RecordCount,
                                unsigned int MaximumRecords,
                                unsigned int DmaBytes);

#endif /* APPLE_AGX_RENDER_PAGING_H */
