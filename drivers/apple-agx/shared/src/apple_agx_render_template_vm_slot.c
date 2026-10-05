#include "apple_agx_render_template_vm_slot.h"

/* G13/V13_5 template object and field offsets, checked against m1n1's
 * WorkCommandInitBM/3D/TA and Start/Finalize3D/TA Construct layouts. */
typedef struct _APPLE_AGX_VM_SLOT_FIELD {
  APPLE_AGX_U32 Object;
  APPLE_AGX_U32 Offset;
} APPLE_AGX_VM_SLOT_FIELD;
static const APPLE_AGX_VM_SLOT_FIELD VmSlotFields[] = {
    {16u, 0x004u}, /* WorkCommandInitBM.context_id */
    {18u, 0x00cu}, /* WorkCommand3D.context_id */
    {19u, 0x00cu}, /* WorkCommandTA.context_id */
    {15u, 0x04cu}, /* Start3DCmd.context_id */
    {15u, 0x284u}, /* Finalize3DCmd.context_id */
    {17u, 0x034u}, /* StartTACmd.context_id */
    {17u, 0x22cu}, /* FinalizeTACmd.context_id */
};

APPLE_AGX_BOOL AppleAgxRenderTemplateSelectVmSlot(
    void *Arena, APPLE_AGX_U32 ArenaBytes, APPLE_AGX_U32 Slot)
{
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layout;
  APPLE_AGX_U32 index;
  unsigned char *bytes = Arena;
  if (bytes == 0 || Slot == 0u || Slot >= 63u ||
      ArenaBytes < AppleAgxRenderTemplateBytes())
    return APPLE_AGX_FALSE;
  layout = AppleAgxRenderTemplateObjectLayouts();
  if (layout == 0) return APPLE_AGX_FALSE;
  /* Validate all seven fields before changing any one of them. */
  for (index = 0u; index < sizeof(VmSlotFields)/sizeof(VmSlotFields[0]); ++index) {
    const APPLE_AGX_VM_SLOT_FIELD *field = &VmSlotFields[index];
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *object = &layout[field->Object];
    APPLE_AGX_U32 offset = object->ArenaOffset + field->Offset;
    if (object->OriginalIndex != field->Object ||
        field->Offset > object->Size || object->Size - field->Offset < 4u ||
        object->ArenaOffset > ArenaBytes || ArenaBytes - object->ArenaOffset < object->Size ||
        bytes[offset] != 63u || bytes[offset+1u] != 0u ||
        bytes[offset+2u] != 0u || bytes[offset+3u] != 0u)
      return APPLE_AGX_FALSE;
  }
  for (index = 0u; index < sizeof(VmSlotFields)/sizeof(VmSlotFields[0]); ++index) {
    const APPLE_AGX_VM_SLOT_FIELD *field = &VmSlotFields[index];
    APPLE_AGX_U32 offset = layout[field->Object].ArenaOffset + field->Offset;
    bytes[offset] = (unsigned char)Slot;
  }
  return APPLE_AGX_TRUE;
}
