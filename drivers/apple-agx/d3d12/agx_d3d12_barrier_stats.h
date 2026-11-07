#ifndef AGX_D3D12_BARRIER_STATS_H
#define AGX_D3D12_BARRIER_STATS_H

/* rc105 receipt-only: what ResourceBarrier asks for. Every D3D12 barrier is
 * still a no-op on this driver (agx_d3d12_ddi.cpp ResourceBarrier): ordering
 * comes from Asahi's batch hazard tracking and, since rc105, from the
 * submission order check (mesa/winsys/agx_win32_submit_order.h). The
 * receipt measure-d3d12-barriers {AGX_D3D12_BARRIER_*} per Execute shows
 * which writer->reader transitions an application relies on (GravityMark's
 * shadow map, G-buffer and TAA history) without a log line per barrier.
 *
 * Contract (d3d12umddi.h): D3D12DDI_RESOURCE_BARRIER_TYPE TRANSITION 0,
 * ALIASING 1, UAV 2, 0022_RANGED 3; flags BEGIN_ONLY 0x1 (the first half of
 * a split barrier: counted, never classified as a hand-off), END_ONLY 0x2;
 * D3D12DDI_RESOURCE_STATES RENDER_TARGET 0x4, UNORDERED_ACCESS 0x8,
 * DEPTH_WRITE 0x10, DEPTH_READ 0x20, NON_PIXEL_SHADER_RESOURCE 0x40,
 * PIXEL_SHADER_RESOURCE 0x80, INDIRECT_ARGUMENT 0x200, COPY_SOURCE 0x800.
 * Plain C: also host-tested. */
#define AGX_D3D12_BARRIER_TRANSITIONS 0u   /* every transition barrier */
#define AGX_D3D12_BARRIER_ALIASING 1u      /* aliasing (type 1, or ranged with the ALIASING flag) */
#define AGX_D3D12_BARRIER_UAV 2u           /* UAV barriers */
#define AGX_D3D12_BARRIER_RANGED 3u        /* 0022 ranged barriers */
#define AGX_D3D12_BARRIER_RT_TO_READ 4u    /* RENDER_TARGET -> a read state */
#define AGX_D3D12_BARRIER_DEPTH_TO_READ 5u /* DEPTH_WRITE -> a read state (shadow maps) */
#define AGX_D3D12_BARRIER_UAV_TO_READ 6u   /* UNORDERED_ACCESS -> a read state */
#define AGX_D3D12_BARRIER_SPLIT_BEGIN 7u   /* BEGIN_ONLY halves */
#define AGX_D3D12_BARRIER_COUNTERS 8u

#define AGX_D3D12_STATE_RENDER_TARGET 0x4u
#define AGX_D3D12_STATE_UNORDERED_ACCESS 0x8u
#define AGX_D3D12_STATE_DEPTH_WRITE 0x10u
/* DEPTH_READ, NON_PIXEL/PIXEL_SHADER_RESOURCE, INDIRECT_ARGUMENT, COPY_SOURCE */
#define AGX_D3D12_STATE_READS 0xae0u

static inline void AgxD3d12BarrierCount(unsigned counters[AGX_D3D12_BARRIER_COUNTERS], unsigned type,
                                        unsigned flags, unsigned before, unsigned after) {
  if (type == 1u || (type == 3u && (flags & 0x8u))) {
    ++counters[AGX_D3D12_BARRIER_ALIASING];
    if (type == 3u) ++counters[AGX_D3D12_BARRIER_RANGED];
    return;
  }
  if (type == 2u) {
    ++counters[AGX_D3D12_BARRIER_UAV];
    return;
  }
  if (type == 3u) {
    ++counters[AGX_D3D12_BARRIER_RANGED];
    return;
  }
  if (type != 0u) return;
  ++counters[AGX_D3D12_BARRIER_TRANSITIONS];
  if (flags & 0x1u) {
    ++counters[AGX_D3D12_BARRIER_SPLIT_BEGIN];
    return;
  }
  if (!(after & AGX_D3D12_STATE_READS)) return;
  if (before & AGX_D3D12_STATE_RENDER_TARGET) ++counters[AGX_D3D12_BARRIER_RT_TO_READ];
  if (before & AGX_D3D12_STATE_DEPTH_WRITE) ++counters[AGX_D3D12_BARRIER_DEPTH_TO_READ];
  if (before & AGX_D3D12_STATE_UNORDERED_ACCESS) ++counters[AGX_D3D12_BARRIER_UAV_TO_READ];
}

#endif
