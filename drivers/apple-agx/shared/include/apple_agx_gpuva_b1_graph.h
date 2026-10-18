#ifndef APPLE_AGX_GPUVA_B1_GRAPH_H
#define APPLE_AGX_GPUVA_B1_GRAPH_H
#include "apple_agx_render_template.h"
#define APPLE_AGX_GPUVA_B1_GRAPH_PAGES 302u
typedef struct _APPLE_AGX_GPUVA_B1_PAGE {
  APPLE_AGX_U64 GpuVa;
  APPLE_AGX_U64 GuestIpa;
  APPLE_AGX_U32 Shared;
  APPLE_AGX_U32 ObjectIndex;
} APPLE_AGX_GPUVA_B1_PAGE;
APPLE_AGX_BOOL AppleAgxGpuvaB1Graph(
    APPLE_AGX_U64 BackendGuestIpa, APPLE_AGX_U64 OutputGuestIpa,
    APPLE_AGX_GPUVA_B1_PAGE *Pages, APPLE_AGX_U32 Capacity,
    APPLE_AGX_U32 *Count);
#endif
