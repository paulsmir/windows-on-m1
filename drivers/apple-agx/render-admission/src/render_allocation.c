#include "render_allocation.h"

#define ADMISSION_ALLOCATION_NULL ((void *)0)
#define ADMISSION_ALLOCATION_U32_MAX 0xffffffffu
#define ADMISSION_ALLOCATION_U64_MAX (~0ULL)

int AdmissionAllocationDescribe(unsigned int Width, unsigned int Height,
                                unsigned int BytesPerPixel,
                                unsigned int Type, unsigned int Format,
                                unsigned int CpuVisible,
                                ADMISSION_ALLOCATION_DESCRIPTION *Description) {
  ADMISSION_ALLOCATION_DESCRIPTION candidate;
  unsigned long long rowBytes;
  unsigned long long pitch;

  if (Description == ADMISSION_ALLOCATION_NULL || Width == 0u ||
      Height == 0u || BytesPerPixel == 0u || BytesPerPixel > 4u ||
      Type == 0u || Format == 0u || CpuVisible > 1u)
    return 0;
  rowBytes = (unsigned long long)Width * BytesPerPixel;
  if (rowBytes > ADMISSION_ALLOCATION_U32_MAX ||
      rowBytes > ADMISSION_ALLOCATION_U64_MAX - 15ULL)
    return 0;
  pitch = (rowBytes + 15ULL) & ~15ULL;
  if (pitch > ADMISSION_ALLOCATION_U32_MAX ||
      (unsigned long long)Height > ADMISSION_ALLOCATION_U64_MAX / pitch)
    return 0;
  candidate.Magic = ADMISSION_ALLOCATION_MAGIC;
  candidate.Version = ADMISSION_ALLOCATION_VERSION;
  candidate.Type = Type;
  candidate.Format = Format;
  candidate.Width = Width;
  candidate.Height = Height;
  candidate.Pitch = (unsigned int)pitch;
  candidate.BytesPerPixel = BytesPerPixel;
  candidate.Size = pitch * Height;
  candidate.CpuVisible = CpuVisible;
  candidate.Reserved = 0u;
  *Description = candidate;
  return 1;
}

int AdmissionAllocationDescriptionValid(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description) {
  ADMISSION_ALLOCATION_DESCRIPTION expected;
  if (Description == ADMISSION_ALLOCATION_NULL ||
      Description->Magic != ADMISSION_ALLOCATION_MAGIC ||
      Description->Version != ADMISSION_ALLOCATION_VERSION ||
      Description->Reserved != 0u ||
      !AdmissionAllocationDescribe(
          Description->Width, Description->Height,
          Description->BytesPerPixel, Description->Type,
          Description->Format, Description->CpuVisible, &expected))
    return 0;
  return expected.Pitch == Description->Pitch &&
         expected.Size == Description->Size;
}

int AdmissionAllocationContainsView(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description,
    unsigned int Width, unsigned int Height, unsigned int Pitch,
    unsigned long long ReferencedBytes) {
  unsigned long long rowBytes;
  unsigned long long viewBytes;
  if (!AdmissionAllocationDescriptionValid(Description) || Width == 0u ||
      Height == 0u || Width > Description->Width ||
      Height > Description->Height || Pitch != Description->Pitch ||
      ReferencedBytes == 0ULL || ReferencedBytes > Description->Size)
    return 0;
  rowBytes = (unsigned long long)Width * Description->BytesPerPixel;
  if (rowBytes > Pitch || (unsigned long long)Height >
                            ADMISSION_ALLOCATION_U64_MAX / Pitch)
    return 0;
  viewBytes = (unsigned long long)Pitch * Height;
  return viewBytes <= ReferencedBytes;
}

int AdmissionAllocationAlign64K(unsigned long long Size,
                                unsigned long long *AlignedSize) {
  unsigned long long mask = ADMISSION_ALLOCATION_ALIGNMENT - 1ULL;
  if (AlignedSize == ADMISSION_ALLOCATION_NULL || Size == 0ULL ||
      Size > ADMISSION_ALLOCATION_U64_MAX - mask)
    return 0;
  *AlignedSize = (Size + mask) & ~mask;
  return *AlignedSize != 0ULL;
}

int AdmissionAllocationCreate(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description,
    ADMISSION_ALLOCATION_OBJECT *Allocation) {
  ADMISSION_ALLOCATION_OBJECT candidate;
  if (!AdmissionAllocationDescriptionValid(Description) ||
      Allocation == ADMISSION_ALLOCATION_NULL)
    return 0;
  candidate.Magic = ADMISSION_ALLOCATION_OBJECT_MAGIC;
  candidate.OpenCount = 0u;
  candidate.Description = *Description;
  *Allocation = candidate;
  return 1;
}

int AdmissionAllocationOpen(ADMISSION_ALLOCATION_OBJECT *Allocation) {
  if (Allocation == ADMISSION_ALLOCATION_NULL ||
      Allocation->Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC ||
      Allocation->OpenCount == ADMISSION_ALLOCATION_U32_MAX)
    return 0;
  ++Allocation->OpenCount;
  return 1;
}

int AdmissionAllocationClose(ADMISSION_ALLOCATION_OBJECT *Allocation) {
  if (Allocation == ADMISSION_ALLOCATION_NULL ||
      Allocation->Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC ||
      Allocation->OpenCount == 0u)
    return 0;
  --Allocation->OpenCount;
  return 1;
}

int AdmissionAllocationDestroy(ADMISSION_ALLOCATION_OBJECT *Allocation) {
  if (Allocation == ADMISSION_ALLOCATION_NULL ||
      Allocation->Magic != ADMISSION_ALLOCATION_OBJECT_MAGIC ||
      Allocation->OpenCount != 0u)
    return 0;
  Allocation->Magic = 0u;
  return 1;
}
