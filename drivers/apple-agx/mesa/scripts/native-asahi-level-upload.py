#!/usr/bin/env python3
"""rc103: whole-level texture uploads through one compute copy
(mesa/winsys/agx_win32_asahi_level_upload.h, APPLE_AGX_D3D12_LEVEL_UPLOAD).

Appends three functions to agx_blit.c, next to agx_resource_copy_region whose
buffer path they follow (agx_get_compute_batch, agx_batch_init_state,
agx_batch_reads/writes, libagx_copy_uint4), plus ail_tile as
agx_transfer_unmap uses it for a CPU-mapped GPU-tiled level. Projected after
the batch lifecycle (fallible compute-batch getter, ctx->any_faults). No
existing function changes.
"""
import hashlib

PATH = 'src/gallium/drivers/asahi/agx_blit.c'

INCLUDE = '#include "agx_win32_asahi_batch.h"\n'

FUNCTIONS = r'''
/* rc103 (agx_win32_asahi_level_upload.h): whole-level uploads of GPU-tiled,
 * uncompressed colour levels; the level bytes, CPU tiling and one compute
 * copy into the level. */
uint64_t
AgxWin32AsahiLevelUploadBytes(const struct pipe_resource *texture,
                              unsigned level, unsigned layer,
                              unsigned width_px, unsigned height_px)
{
   const struct agx_resource *rsrc = (const struct agx_resource *)texture;
   if (!texture || (texture->target != PIPE_TEXTURE_2D &&
                    texture->target != PIPE_TEXTURE_2D_ARRAY) ||
       texture->nr_samples > 1 || !rsrc->bo || rsrc->separate_stencil ||
       (rsrc->bo->flags & (AGX_BO_SHARED | AGX_BO_SHAREABLE)) ||
       util_format_is_depth_or_stencil(texture->format))
      return 0;
   const struct ail_layout *layout = &rsrc->layout;
   if (layout->compressed || layout->tiling == AIL_TILING_LINEAR ||
       level >= layout->levels || level > texture->last_level ||
       layer >= layout->depth_px || layer >= texture->array_size ||
       !ail_is_level_twiddled_uncompressed(layout, level) ||
       width_px != u_minify(layout->width_px, level) ||
       height_px != u_minify(layout->height_px, level))
      return 0;
   const unsigned blocksize_B = ail_get_blocksize_B(layout);
   if (blocksize_B != 1 && blocksize_B != 2 && blocksize_B != 4 &&
       blocksize_B != 8 && blocksize_B != 16)
      return 0;
   const uint64_t bytes = ail_get_level_size_B(layout, level);
   const uint64_t offset = ail_get_layer_level_B(layout, layer, level);
   if (!bytes || (bytes & 15) || (offset & 15) || offset > layout->size_B ||
       bytes > layout->size_B - offset)
      return 0;
   return bytes;
}

int
AgxWin32AsahiTileLevel(const struct pipe_resource *texture, unsigned level,
                       void *dst, uint64_t dst_bytes, const void *linear,
                       unsigned linear_pitch_B)
{
   const struct agx_resource *rsrc = (const struct agx_resource *)texture;
   if (!texture || !dst || !linear || level >= rsrc->layout.levels)
      return 0;
   const unsigned width_px = u_minify(rsrc->layout.width_px, level);
   const unsigned height_px = u_minify(rsrc->layout.height_px, level);
   const uint64_t bytes =
      AgxWin32AsahiLevelUploadBytes(texture, level, 0, width_px, height_px);
   const unsigned blocksize_B = bytes ? ail_get_blocksize_B(&rsrc->layout) : 0;
   if (!bytes || dst_bytes != bytes || !linear_pitch_B ||
       linear_pitch_B % blocksize_B ||
       linear_pitch_B < util_format_get_nblocksx(texture->format, width_px) *
                           blocksize_B)
      return 0;
   memset(dst, 0, (size_t)bytes);
   ail_tile(dst, (void *)(uintptr_t)linear, &rsrc->layout, level,
            linear_pitch_B, 0, 0, width_px, height_px);
   return 1;
}

int
AgxWin32AsahiCopyToLevel(struct pipe_context *pctx,
                         struct pipe_resource *texture, unsigned level,
                         unsigned layer, struct pipe_resource *src,
                         uint64_t src_offset, uint64_t bytes)
{
   if (!pctx || !texture || !src || src->target != PIPE_BUFFER)
      return 0;
   struct agx_context *ctx = agx_context(pctx);
   struct agx_resource *dst = agx_resource(texture);
   struct agx_resource *from = agx_resource(src);
   if (ctx->any_faults || !from->bo || !from->bo->va || !dst->bo ||
       !dst->bo->va || level >= dst->layout.levels ||
       AgxWin32AsahiLevelUploadBytes(texture, level, layer,
                                     u_minify(dst->layout.width_px, level),
                                     u_minify(dst->layout.height_px, level)) != bytes ||
       !bytes || bytes / 16 > UINT32_MAX || (src_offset & 15) ||
       src_offset > src->width0 || bytes > src->width0 - src_offset)
      return 0;
   const uint64_t dst_addr = dst->bo->va->addr +
      ail_get_layer_level_B(&dst->layout, layer, level);
   const uint64_t src_addr = from->bo->va->addr + src_offset;
   if ((dst_addr & 15) || (src_addr & 15))
      return 0;
   struct agx_batch *batch = agx_get_compute_batch(ctx);
   if (!batch)
      return 0;
   agx_batch_init_state(batch);
   agx_batch_reads(batch, from);
   agx_batch_writes(batch, dst, level);
   libagx_copy_uint4(batch, agx_1d((uint32_t)(bytes / 16)), AGX_BARRIER_ALL,
                     dst_addr, src_addr);
   return 1;
}
'''


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous level upload anchor: ' + old[:100])
    return text.replace(old, new, 1)


def project_blit(s):
    if 'AgxWin32AsahiLevelUploadBytes' in s:
        raise RuntimeError('Level upload already projected')
    s = once(s, INCLUDE, INCLUDE + '#include "agx_win32_asahi_level_upload.h"\n')
    return s.rstrip('\n') + '\n' + FUNCTIONS


FILES = {PATH: project_blit}


def project_sources(out, project, overlays):
    for path, transform in FILES.items():
        text = transform((out / path).read_text())
        (out / path).write_text(text)
        overlays.setdefault(path, {})['after_level_upload'] = (
            hashlib.sha256(text.encode()).hexdigest())
