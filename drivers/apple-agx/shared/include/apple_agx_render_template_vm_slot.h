#ifndef APPLE_AGX_RENDER_TEMPLATE_VM_SLOT_H
#define APPLE_AGX_RENDER_TEMPLATE_VM_SLOT_H
#include "apple_agx_render_template.h"

/* Diagnostic B1 only: patch exact captured context fields before publication.
 * This does not authorize a GPUVA mapping or change firmware context 0. */
APPLE_AGX_BOOL AppleAgxRenderTemplateSelectVmSlot(
    void *Arena, APPLE_AGX_U32 ArenaBytes, APPLE_AGX_U32 Slot);
/* V1: rewrite the seven context fields of an already selected (or
 * pristine, 63) arena to Slot (1..62). All seven must hold one value first;
 * otherwise nothing is written. *Previous (may be NULL) gets that value. */
APPLE_AGX_BOOL AppleAgxRenderTemplateRetargetVmSlot(
    void *Arena, APPLE_AGX_U32 ArenaBytes, APPLE_AGX_U32 Slot,
    APPLE_AGX_U32 *Previous);
#endif
