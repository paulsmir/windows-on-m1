#include "apple_agx_gpuva_b1_graph.h"
#define B1_PAGE 0x4000ULL
#define B1_BACKEND_BYTES 0x800000ULL
#define B1_USER_BASE 0x1500000000ULL
#define B1_USER_TOP 0x1600000000ULL
#define B1_KERNEL_BASE 0xffffffa000000000ULL

static APPLE_AGX_BOOL append(APPLE_AGX_GPUVA_B1_PAGE *pages,
    APPLE_AGX_U32 capacity, APPLE_AGX_U32 *count,
    APPLE_AGX_U64 va, APPLE_AGX_U64 ipa,
    APPLE_AGX_U32 shared, APPLE_AGX_U32 object)
{
  if (*count >= capacity || (va & (B1_PAGE-1)) || (ipa & (B1_PAGE-1)) ||
      va > ~0ULL-B1_PAGE || ipa > ~0ULL-B1_PAGE)
    return APPLE_AGX_FALSE;
  pages[*count].GpuVa=va;
  pages[*count].GuestIpa=ipa;
  pages[*count].Shared=shared;
  pages[*count].ObjectIndex=object;
  ++*count;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxGpuvaB1Graph(
    APPLE_AGX_U64 backendIpa, APPLE_AGX_U64 outputIpa,
    APPLE_AGX_GPUVA_B1_PAGE *pages, APPLE_AGX_U32 capacity,
    APPLE_AGX_U32 *count)
{
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layout;
  APPLE_AGX_U32 i,n=0;
  if (count) *count=0;
  if (!pages || !count || capacity < APPLE_AGX_GPUVA_B1_GRAPH_PAGES ||
      !backendIpa || !outputIpa || (backendIpa & (B1_PAGE-1)) ||
      (outputIpa & (B1_PAGE-1)) || backendIpa > ~0ULL-B1_BACKEND_BYTES ||
      (outputIpa >= backendIpa &&
       outputIpa < backendIpa+B1_BACKEND_BYTES))
    return APPLE_AGX_FALSE;
  layout=AppleAgxRenderTemplateObjectLayouts();
  if (!layout || AppleAgxRenderTemplateObjectCount()!=75u ||
      AppleAgxRenderTemplateBytes()!=6094848u ||
      layout[40].OriginalGpuVa!=0x15001d0000ULL ||
      layout[73].OriginalGpuVa!=0x1100020000ULL ||
      layout[74].OriginalGpuVa!=0x1100010000ULL)
    return APPLE_AGX_FALSE;
  /* Firmware objects 0..35 are relocated to retained context-0 kernel half. */
  for (i=0;i<36u;i++)
    if (layout[i].OriginalIndex!=i ||
        layout[i].OriginalGpuVa<B1_KERNEL_BASE)
      return APPLE_AGX_FALSE;
  for (i=36u;i<75u;i++) {
    APPLE_AGX_U64 offset;
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *o=&layout[i];
    if (o->OriginalIndex!=i || o->OriginalGpuVa>=B1_KERNEL_BASE ||
        o->Size==0u || (o->ArenaOffset & (B1_PAGE-1)) ||
        (o->PackedGpuVa & (B1_PAGE-1)) ||
        o->ArenaOffset>B1_BACKEND_BYTES ||
        (APPLE_AGX_U64)o->Size>B1_BACKEND_BYTES-o->ArenaOffset ||
        o->PackedGpuVa<B1_USER_BASE || o->PackedGpuVa>=B1_USER_TOP)
      return APPLE_AGX_FALSE;
    if (i==40u) continue; /* per-process output is mapped below */
    for (offset=0;offset<o->Size;offset+=B1_PAGE)
      if (!append(pages,capacity,&n,o->PackedGpuVa+offset,
                  backendIpa+o->ArenaOffset+offset,1u,i))
        return APPLE_AGX_FALSE;
  }
  /* Two lower-VA shader aliases reference already granted packed pages. */
  for (i=73u;i<=74u;i++) {
    APPLE_AGX_U64 offset;
    const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *o=&layout[i];
    for (offset=0;offset<o->Size;offset+=B1_PAGE)
      if (!append(pages,capacity,&n,o->OriginalGpuVa+offset,
                  backendIpa+o->ArenaOffset+offset,1u,i))
        return APPLE_AGX_FALSE;
  }
  if (!append(pages,capacity,&n,layout[40].OriginalGpuVa,outputIpa,0u,40u) ||
      n!=APPLE_AGX_GPUVA_B1_GRAPH_PAGES)
    return APPLE_AGX_FALSE;
  *count=n;
  return APPLE_AGX_TRUE;
}
