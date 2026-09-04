#include "render_paging.h"

int AdmissionPagingFenceCanSubmit(unsigned int LastSubmitted,
                                  unsigned int Candidate,
                                  unsigned int Resubmission) {
  unsigned int distance;
  if (Candidate == 0u || Resubmission > 1u)
    return 0;
  if (Resubmission != 0u)
    return LastSubmitted != 0u && Candidate == LastSubmitted;
  if (LastSubmitted == 0u)
    return 1;
  distance = Candidate - LastSubmitted;
  return distance != 0u && distance < 0x80000000u;
}

int AdmissionPagingRecordsValid(const ADMISSION_PAGING_RECORD *Records,
                                unsigned int RecordCount,
                                unsigned int MaximumRecords,
                                unsigned int DmaBytes) {
  unsigned int index;
  if (Records == (const ADMISSION_PAGING_RECORD *)0 || RecordCount == 0u ||
      MaximumRecords == 0u || RecordCount > MaximumRecords ||
      RecordCount > 0xffffffffu / sizeof(ADMISSION_PAGING_MARKER) ||
      DmaBytes != RecordCount * sizeof(ADMISSION_PAGING_MARKER))
    return 0;
  for (index = 0u; index < RecordCount; ++index) {
    if (Records[index].Header.Magic != ADMISSION_PAGING_MAGIC ||
        Records[index].Header.Version != ADMISSION_PAGING_VERSION ||
        Records[index].Header.RecordBytes != sizeof(Records[index]) ||
        Records[index].Header.Reserved != 0u ||
        Records[index].Plan.Kind < AppleAgxPhysicalPagingUpload ||
        Records[index].Plan.Kind > AppleAgxPhysicalPagingDiscard)
      return 0;
  }
  return 1;
}
