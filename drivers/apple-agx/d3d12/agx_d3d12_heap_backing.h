#ifndef AGX_D3D12_HEAP_BACKING_H
#define AGX_D3D12_HEAP_BACKING_H

/* rc87 (EXP2017 GravityMark FHD: about 3.2 GiB of native BOs against a
 * LOCAL budget of about 2.9 GiB, residency thrash in the first frame): four
 * texture-only heaps (DDI heap flags 0x2 and 0x20, 400 MiB) each had a
 * GPU-local BO of their full size, while every texture placed in them got a
 * BO of its own (C36, agx_d3d12_ddi.cpp CreateHeapAndResource). Nothing
 * reads such a heap's memory except a tile mapping of a reserved resource,
 * so its backing waits for the first one (EnsureHeapBacking).
 *
 * Contract (d3d12umddi.h D3D12DDI_HEAP_FLAGS): the DDI flags are allow
 * flags. NON_RT_DS_TEXTURES 0x2, BUFFERS 0x4, RT_DS_TEXTURES 0x20. Under
 * resource heap tier 1 (this device) the runtime places only the resource
 * category a heap allows, so a heap without BUFFERS never hosts a placed
 * buffer. Flags 0 (no category named) and CPU-visible heaps (a CPU page
 * property other than NOT_AVAILABLE 0: Map and placed buffers) keep their
 * backing. Plain C: also host-tested. */
#define AGX_D3D12_HEAP_FLAG_BUFFERS 0x4u
#define AGX_D3D12_HEAP_FLAG_TEXTURES 0x22u
#define AGX_D3D12_CPU_PAGE_NOT_AVAILABLE 0u

static inline int AgxD3d12HeapDefersBacking(unsigned flags, unsigned cpu_page_property) {
  return (flags & AGX_D3D12_HEAP_FLAG_TEXTURES) != 0u && (flags & AGX_D3D12_HEAP_FLAG_BUFFERS) == 0u &&
         cpu_page_property == AGX_D3D12_CPU_PAGE_NOT_AVAILABLE;
}

#endif
