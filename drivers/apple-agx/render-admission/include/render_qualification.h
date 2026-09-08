#ifndef APPLE_AGX_RENDER_QUALIFICATION_H
#define APPLE_AGX_RENDER_QUALIFICATION_H

#define ADMISSION_PRESENT_QUERY_MAGIC 0x51504741u /* AGPQ */
#define ADMISSION_PRESENT_QUERY_VERSION 1u
#define ADMISSION_PRESENT_QUERY_CAPACITY 2u

typedef struct _ADMISSION_PRESENT_QUERY {
  unsigned int Magic, Version, Index, PresentCount;
  unsigned int Fence, Status, Valid, Reserved;
  unsigned int ExpectedColor, PixelsExpected, PixelsVerified, Format;
  unsigned int Captured, Exported, Durable, Reserved2;
  unsigned long long Sequence, ActiveOffset, PhysicalAddress, ContentHash;
} ADMISSION_PRESENT_QUERY;

#endif
