#include "direct_flip_contract.h"

static int AdmissionUmdDirectFlipResourceValid(
    const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *Resource,
    unsigned int ExpectedFormat) {
  const ADMISSION_ALLOCATION_DESCRIPTION *allocation;
  if (Resource == (const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *)0 ||
      Resource->Magic != ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC ||
      Resource->Version != ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION ||
      Resource->SegmentId != 2u || Resource->Linear != 1u ||
      Resource->Displayable != 1u || Resource->Reserved != 0u ||
      ExpectedFormat == 0u)
    return 0;
  allocation = &Resource->Allocation;
  return AdmissionAllocationDescriptionValid(allocation) &&
                 allocation->Format == ExpectedFormat &&
                 allocation->Width == 2560u &&
                 allocation->Height == 1600u &&
                 allocation->Pitch == 10240u &&
                 allocation->BytesPerPixel == 4u &&
                 allocation->Size == 0xfa0000ULL &&
                 allocation->CpuVisible == 0u
             ? 1
             : 0;
}

int AdmissionUmdDirectFlipCompatible(
    const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *Current,
    const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *Candidate,
    unsigned int Flags, unsigned int ExpectedFormat) {
  if (Flags != 0u ||
      !AdmissionUmdDirectFlipResourceValid(Current, ExpectedFormat) ||
      !AdmissionUmdDirectFlipResourceValid(Candidate, ExpectedFormat))
    return 0;
  return Current->Allocation.Magic == Candidate->Allocation.Magic &&
                 Current->Allocation.Version == Candidate->Allocation.Version &&
                 Current->Allocation.Type == Candidate->Allocation.Type &&
                 Current->Allocation.Format == Candidate->Allocation.Format &&
                 Current->Allocation.Width == Candidate->Allocation.Width &&
                 Current->Allocation.Height == Candidate->Allocation.Height &&
                 Current->Allocation.Pitch == Candidate->Allocation.Pitch &&
                 Current->Allocation.BytesPerPixel ==
                     Candidate->Allocation.BytesPerPixel &&
                 Current->Allocation.Size == Candidate->Allocation.Size &&
                 Current->Allocation.CpuVisible ==
                     Candidate->Allocation.CpuVisible &&
                 Current->SegmentId == Candidate->SegmentId &&
                 Current->Linear == Candidate->Linear &&
                 Current->Displayable == Candidate->Displayable
             ? 1
             : 0;
}
