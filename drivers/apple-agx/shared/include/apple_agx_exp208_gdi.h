#ifndef APPLE_AGX_EXP208_GDI_H
#define APPLE_AGX_EXP208_GDI_H

#include "apple_agx_exp208_relocation.h"
#include "apple_agx_gdi.h"
#include "apple_agx_render_template.h"

#define APPLE_AGX_EXP208_GDI_OUTPUT_OBJECT 40u
#define APPLE_AGX_EXP208_GDI_OUTPUT_BYTES 0x4000u
#define APPLE_AGX_EXP208_GDI_WIDTH 16u
#define APPLE_AGX_EXP208_GDI_HEIGHT 16u
#define APPLE_AGX_EXP208_GDI_PITCH 64u
#define APPLE_AGX_EXP208_GDI_COLOR 0xff112233u

typedef struct _APPLE_AGX_EXP208_GDI_BINDING {
  APPLE_AGX_U32 OutputObject;
  APPLE_AGX_U64 DestinationGpuVa;
  APPLE_AGX_U64 DestinationPhysical;
  APPLE_AGX_U32 DestinationBytes;
} APPLE_AGX_EXP208_GDI_BINDING;

APPLE_AGX_BOOL AppleAgxExp208BindGdiColorFill(
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount,
    void *DestinationCpuAddress,
    APPLE_AGX_U64 DestinationGpuVa,
    APPLE_AGX_U64 DestinationPhysical,
    APPLE_AGX_U32 DestinationCapacity,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    const APPLE_AGX_EXP208_RELOCATION *Relocations,
    APPLE_AGX_U32 RelocationCount,
    APPLE_AGX_EXP208_GDI_BINDING *Binding);

#endif /* APPLE_AGX_EXP208_GDI_H */
