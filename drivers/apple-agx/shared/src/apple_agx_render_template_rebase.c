#include "apple_agx_render_template_rebase.h"

#define REBASE_NULL ((void *)0)
#define REBASE_PHYSICAL_LIMIT (1ULL << 40u)

static APPLE_AGX_BOOL AppleAgxRenderTemplateDefaultRoots(
    const APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots) {
  return Roots != REBASE_NULL &&
                 Roots->Ta[0] == 0x1503880000ULL &&
                 Roots->Ta[1] == 0x1503898000ULL &&
                 Roots->D3[0] == 0x1503870000ULL &&
                 Roots->D3[1] == 0x1503890000ULL
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxRenderTemplateBuildRelocationObjectsRebased(
    void *Arena, APPLE_AGX_U32 ArenaCapacity,
    APPLE_AGX_U64 ArenaPhysicalAddress,
    APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_U64 MappedGpuBase,
    APPLE_AGX_U64 MappedGpuBytes,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCapacity,
    APPLE_AGX_RENDER_TEMPLATE_ROOTS *Roots) {
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  APPLE_AGX_U64 mapped_end;
  APPLE_AGX_U64 arena_end;
  APPLE_AGX_U32 index;

  if (Arena == REBASE_NULL || Objects == REBASE_NULL ||
      Roots == REBASE_NULL ||
      ArenaCapacity < AppleAgxRenderTemplateBytes() ||
      ObjectCapacity < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      MappedGpuBytes == 0ULL ||
      (MappedGpuBase & 0x3fffULL) != 0ULL ||
      (ArenaGpuAddress & (APPLE_AGX_RENDER_TEMPLATE_ALIGNMENT - 1u)) != 0ULL ||
      (ArenaPhysicalAddress & 0x3fffULL) != 0ULL ||
      MappedGpuBase > ~0ULL - MappedGpuBytes ||
      ArenaGpuAddress > ~0ULL - AppleAgxRenderTemplateBytes() ||
      ArenaPhysicalAddress >
          REBASE_PHYSICAL_LIMIT - AppleAgxRenderTemplateBytes() ||
      !AppleAgxRenderTemplateDefaultRoots(Roots))
    return APPLE_AGX_FALSE;
  mapped_end = MappedGpuBase + MappedGpuBytes;
  arena_end = ArenaGpuAddress + AppleAgxRenderTemplateBytes();
  if (ArenaGpuAddress < MappedGpuBase || arena_end > mapped_end)
    return APPLE_AGX_FALSE;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  if (layouts == REBASE_NULL ||
      !AppleAgxRenderTemplateBuildRelocationObjects(
          Arena, ArenaCapacity, ArenaPhysicalAddress, Objects,
          ObjectCapacity))
    return APPLE_AGX_FALSE;
  for (index = 0u; index < APPLE_AGX_RENDER_TEMPLATE_OBJECT_COUNT;
       ++index) {
    APPLE_AGX_U64 offset =
        layouts[index].PackedGpuVa - APPLE_AGX_RENDER_TEMPLATE_GPU_BASE;
    Objects[index].GpuVa = ArenaGpuAddress + offset;
  }
  Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX].GpuVa =
      ArenaGpuAddress;
  Roots->Ta[0] = ArenaGpuAddress + 0x80000ULL;
  Roots->Ta[1] = ArenaGpuAddress + 0x98000ULL;
  Roots->D3[0] = ArenaGpuAddress + 0x70000ULL;
  Roots->D3[1] = ArenaGpuAddress + 0x90000ULL;
  return APPLE_AGX_TRUE;
}
