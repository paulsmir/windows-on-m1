#ifndef APPLE_AGX_RENDER_TEMPLATE_REBASE_H
#define APPLE_AGX_RENDER_TEMPLATE_REBASE_H

#include "apple_agx_render_template.h"

APPLE_AGX_BOOL AppleAgxRenderTemplateBuildRelocationObjectsRebased(
    void *Arena, APPLE_AGX_U32 ArenaCapacity,
    APPLE_AGX_U64 ArenaPhysicalAddress,
    APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_U64 MappedGpuBase,
    APPLE_AGX_U64 MappedGpuBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCapacity,
    APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots);

#endif /* APPLE_AGX_RENDER_TEMPLATE_REBASE_H */
