#ifndef APPLE_AGX_RENDER_ALLOCATION_H
#define APPLE_AGX_RENDER_ALLOCATION_H

#define ADMISSION_ALLOCATION_MAGIC 0x414d4152u /* "RAMA" */
#define ADMISSION_ALLOCATION_VERSION 1u
#define ADMISSION_ALLOCATION_OBJECT_MAGIC 0x4f4d4152u /* "RAMO" */
#define ADMISSION_ALLOCATION_ALIGNMENT 0x10000ULL

typedef struct _ADMISSION_ALLOCATION_DESCRIPTION {
  unsigned int Magic;
  unsigned int Version;
  unsigned int Type;
  unsigned int Format;
  unsigned int Width;
  unsigned int Height;
  unsigned int Pitch;
  unsigned int BytesPerPixel;
  unsigned long long Size;
  unsigned int CpuVisible;
  unsigned int Reserved;
} ADMISSION_ALLOCATION_DESCRIPTION;

typedef struct _ADMISSION_ALLOCATION_OBJECT {
  unsigned int Magic;
  unsigned int OpenCount;
  ADMISSION_ALLOCATION_DESCRIPTION Description;
} ADMISSION_ALLOCATION_OBJECT;

int AdmissionAllocationDescribe(unsigned int Width, unsigned int Height,
                                unsigned int BytesPerPixel,
                                unsigned int Type, unsigned int Format,
                                unsigned int CpuVisible,
                                ADMISSION_ALLOCATION_DESCRIPTION *Description);
int AdmissionAllocationDescriptionValid(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description);
int AdmissionAllocationAlign64K(unsigned long long Size,
                                unsigned long long *AlignedSize);
int AdmissionAllocationCreate(
    const ADMISSION_ALLOCATION_DESCRIPTION *Description,
    ADMISSION_ALLOCATION_OBJECT *Allocation);
int AdmissionAllocationOpen(ADMISSION_ALLOCATION_OBJECT *Allocation);
int AdmissionAllocationClose(ADMISSION_ALLOCATION_OBJECT *Allocation);
int AdmissionAllocationDestroy(ADMISSION_ALLOCATION_OBJECT *Allocation);

#endif /* APPLE_AGX_RENDER_ALLOCATION_H */
