/* SPDX-License-Identifier: MIT
 * rc103: whole-level texture uploads without a render pass per level.
 * Defined in the projected agx_blit.c (mesa/scripts/native-asahi-level-upload.py);
 * used by the D3D12 owner (agx_d3d12_windows.cpp level_upload).
 *
 * EXP2037a (GravityMark D3D12 FHD 100k): the first scored frames upload ~113
 * BC textures (1024x1024 x 11 levels, 512x512 x 10 levels) into GPU-only
 * (UNMAPPABLE) textures. Each CopyTextureRegion went through the owner's
 * linear staging texture and a u_blitter render-pass blit (compute blits are
 * off on Windows), and Asahi flushes the earlier blit's batch before every
 * blit into the same texture (agx_blit "Flush writer due to: Blit",
 * agx_batch_writes_internal "Multiple writers"): one render submission per
 * mip level, 1262 submissions in frame 2->3 (1.9 s).
 *
 * Instead, the D3D12 owner tiles the level on the CPU with Asahi's own
 * ail_tile (as agx_transfer_unmap does for a mapped GPU-tiled level), stores
 * it in a CPU-visible buffer, and records one libagx_copy_uint4 compute copy
 * (as agx_resource_copy_region does for buffers) of the level's bytes. All
 * such copies of a replay share the context's compute batch: Asahi tracks
 * one writer per BO and the compute batch stays that writer, so consecutive
 * levels of one texture no longer flush each other.
 *
 * Plain C, no CRT headers beyond stdint, valid C and C++ (x86 /TP).
 */
#ifndef AGX_WIN32_ASAHI_LEVEL_UPLOAD_H
#define AGX_WIN32_ASAHI_LEVEL_UPLOAD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct pipe_context;
struct pipe_resource;

/* The bytes of one layer's level `level` (ail_get_level_size_B) when a
 * whole-level upload of width_px x height_px may take this path, else 0:
 * a 2D or 2D-array, single-sampled, colour (not depth/stencil) texture
 * whose layout is GPU-tiled and not lossless-compressed, block size
 * 1/2/4/8/16 bytes, level and layer in range, and width/height equal to
 * the level's extent (a sub-rectangle keeps the blit path: the copy writes
 * the whole level, including its padding). Does not touch the BO. */
uint64_t AgxWin32AsahiLevelUploadBytes(const struct pipe_resource *texture,
   unsigned level, unsigned layer, unsigned width_px, unsigned height_px);

/* Zero dst (the level's padding has no texels) and ail_tile the level's
 * linear block rows (linear_pitch_B apart) into it, in the level's GPU
 * layout relative to the level start. dst_bytes must be the value
 * AgxWin32AsahiLevelUploadBytes returned for the whole level. Returns 1,
 * or 0 (dst untouched) when the arguments do not describe that level. */
int AgxWin32AsahiTileLevel(const struct pipe_resource *texture, unsigned level,
   void *dst, uint64_t dst_bytes, const void *linear, unsigned linear_pitch_B);

/* Record on the context's compute batch a copy of `bytes` bytes at
 * src_offset of the buffer src into layer `layer`, level `level` of
 * texture (agx_batch_reads src, agx_batch_writes texture/level, so Asahi's
 * hazard tracking orders it against every other batch). bytes must be the
 * level size. Returns 1 when recorded, 0 when nothing was recorded
 * (ineligible, misaligned, a faulted context or no compute batch). */
int AgxWin32AsahiCopyToLevel(struct pipe_context *pctx,
   struct pipe_resource *texture, unsigned level, unsigned layer,
   struct pipe_resource *src, uint64_t src_offset, uint64_t bytes);
#ifdef __cplusplus
}
#endif
#endif
