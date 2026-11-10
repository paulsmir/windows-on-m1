#include "gallium/drivers/asahi/agx_state.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_inlines.h"
#include "util/u_helpers.h" /* rc76b: util_clamp_color, as agx_pipe.c */
#include "util/format/u_format.h"
#include "drm-uapi/drm_fourcc.h"
#include "agx_win32_asahi_scene.h"
#include "agx_win32_asahi_batch.h"
#include "agx_win32_asahi_bo.h"
#ifdef APPLE_AGX_GPUVA_WINSYS
#include "agx_win32_sparse.h"
#endif
#include "agx_win32_sampler_border.h" /* RC108: after agx_pack.h */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* OV2: AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE of the device the
 * screen was created for (read by the D3D12 owner and the batch submit). */
volatile unsigned AgxWin32Ov2Feature;
/* OV5: AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT of that device */
volatile unsigned AgxWin32Ov5Feature;
/* C2a: AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE of that device */
volatile unsigned AgxWin32PrivateOnlyFeature;

struct pipe_screen *AgxWin32AsahiScreenCreate(AGX_WIN32_ASAHI_BACKEND *,AGX_WIN32_SCREEN *,
    const AGX_WIN32_ASAHI_OWNER_OPS *,void *,const AGX_WIN32_ASAHI_BATCH_OPS *,
    const struct drm_asahi_params_global *);

static int context_idle(struct pipe_context *ctx) {
  struct agx_context *native=ctx?agx_context(ctx):NULL;
  if(!native) return 0;
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i)
    if(native->batches.slots[i].windows_batch || BITSET_TEST(native->batches.active,i) ||
       BITSET_TEST(native->batches.submitted,i)) return 0;
  return 1;
}
static void tracked_context_destroy(struct pipe_context *ctx) {
  AGX_WIN32_ASAHI_BACKEND *backend=ctx && ctx->screen?
      agx_device(ctx->screen)->windows_private:NULL;
  if(!backend || !backend->ContextDestroy || !backend->ContextCount || !context_idle(ctx)) return;
  void (*destroy)(struct pipe_context *)=backend->ContextDestroy;
  ctx->destroy=destroy;
  destroy(ctx);
  --backend->ContextCount;
}
static struct pipe_context *tracked_context_create(struct pipe_screen *screen,void *owner,unsigned flags) {
  AGX_WIN32_ASAHI_BACKEND *backend=screen?agx_device(screen)->windows_private:NULL;
  if(!backend || backend->Closing || !backend->ContextCreate || backend->ContextCount==UINT32_MAX) return NULL;
  struct pipe_context *ctx=backend->ContextCreate(screen,owner,flags);
  if(!ctx || !ctx->destroy) return NULL;
  if(backend->ContextDestroy && backend->ContextDestroy!=ctx->destroy) {
    ctx->destroy(ctx);return NULL;
  }
  backend->ContextDestroy=ctx->destroy;
  ctx->destroy=tracked_context_destroy;
  ++backend->ContextCount;
  return ctx;
}

struct pipe_screen *AgxWin32AsahiScreenCreateForWindows(AGX_WIN32_ASAHI_BACKEND *backend,
    AGX_WIN32_SCREEN *windows,const AGX_WIN32_ASAHI_OWNER_OPS *owners,void *owner,
    const AGX_WIN32_ASAHI_BATCH_OPS *batches) {
  if(!backend || !windows || !windows->Active || !windows->Generation ||
      !AgxWin32DeviceInfoValid(&windows->Info) || !owners || !owner || !batches) return NULL;
  struct drm_asahi_params_global params={0};
  params.gpu_generation=windows->Info.GpuGeneration;
  params.gpu_variant='G';
  /* KMD lifecycle.c exposes the compiled G13G/16-KiB contract, not measured
   * topology. Pinned Asahi hw/t8103.rs (77cb8f24c238...) defines G13G, one die,
   * max_num_clusters=1. This adapter implements that one-cluster model only.
   * Core masks, revision and timestamp frequency are not invented. */
  params.num_clusters_total=1;
  params.num_dies=1;
  /* EXP1317: the cores the SGX reports (KMD, Asahi regs.rs derivation).
   * Mesa's scratch block list and max_compute_units follow them; without
   * them a fragment shader using scratch walked an empty list
   * (dwmcore.dll_003d8800 removed the device on both shader paths). */
  params.num_cores_per_cluster=windows->Info.CoresPerCluster;
  params.core_masks[0]=windows->Info.CoreMask;
  /* RC82: Asahi reports DRM_ASAHI_FEATURE_SOFT_FAULTS when the kernel runs
   * the firmware with fault_control 0xb (file.rs:333-335). The KMD reports
   * it only with the device value Wom1SoftFaults = 1 and the published
   * RegionC word 0xb (AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS); otherwise no
   * optional feature is enabled. The projected agx_has_soft_fault stays
   * false; the feature reaches the compiler as agx_win32_soft_faults
   * (agx_win32_soft_fault.h: shader memory loads may speculate). */
  params.features=(windows->Info.Features & AGX_WIN32_DEVICE_FEATURE_SOFT_FAULTS) ?
      DRM_ASAHI_FEATURE_SOFT_FAULTS : 0;
  /* OV2: the KMD admits independent compute envelopes (VdmBarrier NONE) */
  AgxWin32Ov2Feature=(windows->Info.Features & AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE) ? 1u : 0u;
  /* C2a: the KMD admits private-only compute envelopes */
  AgxWin32PrivateOnlyFeature=(windows->Info.Features & AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE) ? 1u : 0u;
  /* OV5: the KMD admits TA-independent renders (render CdmBarrier NONE) */
  AgxWin32Ov5Feature=(windows->Info.Features & AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT) ? 1u : 0u;
  if(windows->Info.PageBytes!=AIL_PAGESIZE) return NULL;
  struct pipe_screen *screen=AgxWin32AsahiScreenCreate(backend,windows,owners,owner,batches,&params);
  if(screen) {
    backend->ContextCreate=screen->context_create;
    backend->ContextDestroy=NULL;
    backend->ContextCount=0;
    backend->Closing=0;
    screen->context_create=tracked_context_create;
  }
  return screen;
}

struct pipe_screen *AgxWin32AsahiScreenRecover(AGX_WIN32_ASAHI_BACKEND *backend) {
  struct agx_screen *screen=backend && backend->Native ?
      (struct agx_screen *)((unsigned char *)backend->Native-
          offsetof(struct agx_screen,dev)) : NULL;
  return screen ? &screen->pscreen : NULL;
}

struct pipe_context *AgxWin32AsahiContextCreate(struct pipe_screen *screen,void *owner) {
  return screen && screen->context_create ? screen->context_create(screen,owner,0) : NULL;
}
struct pipe_resource *AgxWin32AsahiImportLinearColor32(
    struct pipe_screen *screen,const AGX_WIN32_SCREEN_BUFFER *buffer,
    APPLE_AGX_U32 width,APPLE_AGX_U32 height,APPLE_AGX_U32 pitch,
    APPLE_AGX_U64 bytes,AGX_WIN32_ASAHI_LINEAR_FORMAT format) {
  AGX_WIN32_ASAHI_BACKEND *backend=screen?
      agx_device(screen)->windows_private:NULL;
  if(!screen || !buffer || !width || !height ||
     pitch<(APPLE_AGX_U64)width*4ULL || (pitch&15u) ||
     bytes<(APPLE_AGX_U64)pitch*height ||
     !backend || buffer->Transport.Bytes!=bytes ||
     buffer->Transport.Generation!=backend->Buffers.Generation)
    return NULL;
  enum pipe_format pipe_format;
  switch(format) {
  case AgxWin32AsahiLinearFormatBgra8Unorm:
    pipe_format=PIPE_FORMAT_B8G8R8A8_UNORM;break;
  case AgxWin32AsahiLinearFormatRgba8Unorm:
    pipe_format=PIPE_FORMAT_R8G8B8A8_UNORM;break;
  default: return NULL;
  }
  struct pipe_resource info={0};
  info.target=PIPE_TEXTURE_2D;info.format=pipe_format;
  info.width0=width;info.height0=height;info.depth0=1;info.array_size=1;
  info.nr_samples=1;info.nr_storage_samples=1;
  info.bind=PIPE_BIND_RENDER_TARGET|PIPE_BIND_SAMPLER_VIEW|PIPE_BIND_SHARED;
  info.usage=PIPE_USAGE_DEFAULT;
  struct agx_resource *resource=calloc(1,sizeof(*resource));
  if(!resource) return NULL;
  resource->base=info;resource->base.screen=screen;
  resource->modifier=DRM_FORMAT_MOD_LINEAR;
  resource->layout=(struct ail_layout){
      .tiling=AIL_TILING_LINEAR,.format=pipe_format,
      .width_px=width,.height_px=height,.depth_px=1,.sample_count_sa=1,
      .levels=1,.renderable=true,.linear_stride_B=pitch};
  pipe_reference_init(&resource->base.reference,1);
  ail_make_miptree(&resource->layout);
  if(resource->layout.size_B>bytes ||
     ail_get_linear_stride_B(&resource->layout,0)!=pitch ||
     resource->layout.level_offsets_B[0]!=0) {
    free(resource);return NULL;
  }
  struct agx_bo *imported=AgxWin32AsahiImportBo(backend,buffer,"Display primary");
  if(!imported) {free(resource);return NULL;}
  resource->bo=imported;
  if(backend->Failed) {
    agx_bo_unreference(agx_device(screen),imported);free(resource);return NULL;
  }
  return &resource->base;
}
/* EXP1330: Asahi has no pipe->clear_render_target (and no scissored
 * clears), so a rectangle of a render-target view is cleared by the
 * driver's blitter with the context's state saved around it, unaffected by
 * any render condition (D3D11.1 ClearView). */
int AgxWin32AsahiClearRenderTargetRect(
    struct pipe_context *pctx,struct pipe_surface *surface,
    const union pipe_color_union *color,unsigned x,unsigned y,
    unsigned width,unsigned height) {
  if(!pctx || !surface || !surface->texture || !color || !width || !height)
    return 0;
  struct agx_context *ctx=agx_context(pctx);
  if(!ctx->blitter) return 0;
  agx_blitter_save(ctx,ctx->blitter,ASAHI_CLEAR|ASAHI_SAVE_FRAMEBUFFER);
  util_blitter_clear_render_target(ctx->blitter,surface,color,x,y,width,height);
  return 1;
}
void AgxWin32AsahiResourceRelease(struct pipe_resource **resource) {
  if(resource) pipe_resource_reference(resource,NULL);
}
int AgxWin32AsahiResourceIdentity(
    struct pipe_resource *resource,AGX_WIN32_RELOC_ALLOCATION *identity) {
  if(identity) memset(identity,0,sizeof(*identity));
  if(!resource || !resource->screen || !identity) return 0;
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(resource->screen)->windows_private;
  struct agx_resource *native=agx_resource(resource);
  return backend && native->bo &&
      AgxWin32AsahiIdentity(backend,native->bo,identity);
}
struct pipe_resource *AgxWin32AsahiCreateUncompressedDepthStencil(
    struct pipe_screen *screen,const struct pipe_resource *templ) {
  if(!screen||!templ||!screen->resource_create_with_modifiers||
     (templ->format!=PIPE_FORMAT_Z24_UNORM_S8_UINT &&
      templ->format!=PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) return NULL;
  const uint64_t modifier=DRM_FORMAT_MOD_APPLE_GPU_TILED;
  struct pipe_resource depth_info=*templ,stencil_info=*templ;
  depth_info.format=PIPE_FORMAT_Z32_FLOAT;
  stencil_info.format=PIPE_FORMAT_S8_UINT;
  struct pipe_resource *depth=screen->resource_create_with_modifiers(
      screen,&depth_info,&modifier,1);
  if(!depth) return NULL;
  struct pipe_resource *stencil=screen->resource_create_with_modifiers(
      screen,&stencil_info,&modifier,1);
  if(!stencil) { screen->resource_destroy(screen,depth); return NULL; }
  struct agx_resource *native_depth=agx_resource(depth);
  native_depth->base.format=templ->format;
  native_depth->separate_stencil=agx_resource(stencil);
  return depth;
}
int AgxWin32AsahiContextDestroy(struct pipe_context *ctx) {
  if(!ctx || !ctx->destroy) return 0;
  if(!context_idle(ctx)) return 0;
  ctx->destroy(ctx);
  return 1;
}
int AgxWin32AsahiContextRetire(struct pipe_context *ctx,APPLE_AGX_U32 timeout) {
  if(!ctx) return 0;
  struct agx_context *native=agx_context(ctx);
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *batch=&native->batches.slots[i];
    if(batch->windows_batch) {
      if(!AgxWin32AsahiBatchPoll(batch,timeout)) return 0;
      agx_sync_batch(native,batch);
    }
    if(batch->windows_batch || BITSET_TEST(native->batches.active,i) ||
       BITSET_TEST(native->batches.submitted,i)) return 0;
  }
  return 1;
}
void *AgxWin32AsahiBufferCurrentMap(struct pipe_context *ctx,
    struct pipe_resource *resource) {
  if(!ctx || !resource || resource->screen!=ctx->screen ||
     resource->target!=PIPE_BUFFER) return NULL;
  struct agx_resource *rsrc=agx_resource(resource);
  return rsrc->bo ? rsrc->bo->_map : NULL;
}
/* D3D12 heaps are zero-initialised and valid from creation: a later CPU
 * write map of the range must synchronise with the GPU users. */
int AgxWin32AsahiBufferMarkValid(struct pipe_context *ctx,
    struct pipe_resource *resource) {
  if(!ctx || !resource || resource->screen!=ctx->screen ||
     resource->target!=PIPE_BUFFER || resource->width0>UINT32_MAX) return 0;
  struct agx_resource *rsrc=agx_resource(resource);
  if(!rsrc->bo) {
    fprintf(stderr,"NATIVE_BUFFER_WRITE_MAP: no-bo\n");
    return 0;
  }
  util_range_add(resource,&rsrc->valid_buffer_range,0,(unsigned)resource->width0);
  BITSET_SET(rsrc->data_valid,0);
  return 1;
}
void *AgxWin32AsahiBufferWriteMap(struct pipe_context *ctx,
    struct pipe_resource *resource) {
  if(!AgxWin32AsahiBufferMarkValid(ctx,resource)) return NULL;
  struct agx_resource *rsrc=agx_resource(resource);
  void *map=agx_bo_map(rsrc->bo);
  if(!map) {
    AGX_WIN32_ASAHI_BACKEND *backend=agx_device(ctx->screen)->windows_private;
    fprintf(stderr,"NATIVE_BUFFER_WRITE_MAP: map-failed backend=%u ref=%d bytes=%zu\n",
        backend?(unsigned)backend->Failed:~0u,rsrc->bo->refcnt,rsrc->bo->size);
    return NULL;
  }
  return map;
}
int AgxWin32AsahiBufferCopy(struct pipe_context *ctx,
    struct pipe_resource *dst,APPLE_AGX_U64 dst_offset,
    struct pipe_resource *src,APPLE_AGX_U64 src_offset,APPLE_AGX_U64 bytes) {
  if(!ctx || !dst || !src || dst==src || !bytes ||
     dst->screen!=ctx->screen || src->screen!=ctx->screen ||
     dst->target!=PIPE_BUFFER || src->target!=PIPE_BUFFER ||
     dst_offset>dst->width0 || bytes>dst->width0-dst_offset ||
     src_offset>src->width0 || bytes>src->width0-src_offset) return 0;
  struct agx_resource *d=agx_resource(dst),*s=agx_resource(src);
  if(!d->bo || !s->bo || agx_context(ctx)->any_faults) return 0;
  /* EXP1312: a CPU copy syncs like a CPU transfer (Asahi
   * agx_prepare_for_map): the source's GPU writer, and every batch using
   * the destination. CopyStructureCount's compute copy had written the
   * count on the GPU while this memcpy read the stale CPU contents. */
  agx_sync_writer(agx_context(ctx),s,"Windows buffer copy source");
  if(agx_context(ctx)->any_faults) return 0;
  agx_sync_readers(agx_context(ctx),d,"Windows buffer copy destination");
  if(agx_context(ctx)->any_faults) return 0;
  unsigned char *dm=(unsigned char *)agx_bo_map(d->bo);
  unsigned char *sm=(unsigned char *)agx_bo_map(s->bo);
  if(!dm || !sm) return 0;
  memcpy(dm+dst_offset,sm+src_offset,(size_t)bytes);
  util_range_add(dst,&d->valid_buffer_range,(unsigned)dst_offset,
                 (unsigned)(dst_offset+bytes));
  BITSET_SET(d->data_valid,0);
  return 1;
}
/* D3D12 C4b: the GPU virtual address of a buffer resource (Asahi's own
 * mapping, agx_map_gpu), which D3D12 reports as the resource's address and
 * records in copies; 0 for anything but a buffer with storage. */
#ifdef APPLE_AGX_GPUVA_WINSYS
/* C53 B1: a buffer resource whose BO is a virtual (reserved) BO of Bytes:
 * created as a one-tile buffer, then given the virtual BO. Tiles map the
 * device scratch BO until AgxWin32AsahiMapReservedTiles maps backing
 * buffer tiles (64 KiB, from the backing resource's start). */
struct pipe_resource *AgxWin32AsahiCreateReservedBuffer(struct pipe_screen *screen,
                                                        APPLE_AGX_U64 bytes) {
  struct pipe_resource templ;
  if(!screen || !bytes || bytes>UINT32_MAX) return NULL;
  memset(&templ,0,sizeof(templ));
  templ.target=PIPE_BUFFER;
  templ.format=PIPE_FORMAT_R8_UNORM;
  templ.width0=0x10000u;
  templ.height0=templ.depth0=templ.array_size=1;
  templ.usage=PIPE_USAGE_DEFAULT;
  templ.bind=PIPE_BIND_VERTEX_BUFFER|PIPE_BIND_INDEX_BUFFER|PIPE_BIND_CONSTANT_BUFFER|
             PIPE_BIND_SHADER_BUFFER|PIPE_BIND_SAMPLER_VIEW;
  struct pipe_resource *resource=screen->resource_create(screen,&templ);
  if(!resource) return NULL;
  struct agx_device *device=agx_device(screen);
  struct agx_bo *virt=AgxWin32AsahiCreateVirtualBo(device,(size_t)bytes,"Reserved buffer");
  if(!virt) { AgxWin32AsahiResourceRelease(&resource); return NULL; }
  /* C53 Tier 2: reads through the read-only shadow return 0 for unmapped
   * tiles (the shadow VA is what texel-buffer views read) */
  if(!AgxWin32AsahiAttachReadShadow(virt)) {
    agx_bo_unreference(device,virt);
    AgxWin32AsahiResourceRelease(&resource);
    return NULL;
  }
  struct agx_resource *rsrc=agx_resource(resource);
  agx_bo_unreference(device,rsrc->bo);
  rsrc->bo=virt;
  resource->width0=(unsigned)bytes;
  rsrc->layout.size_B=bytes;
  return resource;
}
int AgxWin32AsahiMapReservedTiles(struct pipe_resource *reserved,unsigned first,unsigned count,
                                  struct pipe_resource *backing,unsigned backing_tile) {
  if(!reserved || reserved->target!=PIPE_BUFFER ||
     (backing && (backing->target!=PIPE_BUFFER || backing->screen!=reserved->screen))) return 0;
  return AgxWin32AsahiMapVirtualTiles(agx_resource(reserved)->bo,first,count,
                                      backing?agx_resource(backing)->bo:NULL,backing_tile);
}
/* C53 T2D (D3D12-C53-tiled-textures.md): a reserved Texture2D (array),
 * created by Asahi with the plain GPU-tiled modifier (no compression) while
 * the winsys makes its image BO a virtual BO, then given its Sparse Block
 * table. Refused where agx_win32_sparse cannot tile the layout. */
struct pipe_resource *AgxWin32AsahiCreateReservedTexture(struct pipe_screen *screen,
                                                         const struct pipe_resource *templ) {
  static const uint64_t tiled=DRM_FORMAT_MOD_APPLE_GPU_TILED;
  unsigned standard,packed,tiles_x[AIL_MAX_MIP_LEVELS],tiles_y[AIL_MAX_MIP_LEVELS];
  if(!screen || !templ || !screen->resource_create_with_modifiers ||
     (templ->target!=PIPE_TEXTURE_2D && templ->target!=PIPE_TEXTURE_2D_ARRAY) ||
     templ->nr_samples>1 || templ->depth0!=1) return NULL;
  struct agx_device *device=agx_device(screen);
  if(!AgxWin32AsahiArmVirtual(device)) return NULL;
  struct pipe_resource *resource=screen->resource_create_with_modifiers(screen,templ,&tiled,1);
  AgxWin32AsahiDisarmVirtual(device);
  if(!resource) return NULL;
  struct agx_resource *rsrc=agx_resource(resource);
  if(!AgxWin32AsahiIsVirtual(rsrc->bo) ||
     !AgxWin32SparseTiling(&rsrc->layout,&standard,tiles_x,tiles_y,&packed) ||
     rsrc->layout.sparse_table_size_B>SIZE_MAX ||
     !AgxWin32AsahiAttachSparseTable(rsrc->bo,(size_t)rsrc->layout.sparse_table_size_B)) {
    AgxWin32AsahiResourceRelease(&resource);
    return NULL;
  }
  return resource;
}
int AgxWin32AsahiReservedTextureTiling(struct pipe_resource *reserved,unsigned *standard_mips,
                                       unsigned *tiles_x,unsigned *tiles_y,unsigned *packed_tiles) {
  if(!reserved || reserved->target==PIPE_BUFFER ||
     !AgxWin32AsahiSparseTableVa(agx_resource(reserved)->bo)) return 0;
  return AgxWin32SparseTiling(&agx_resource(reserved)->layout,standard_mips,tiles_x,tiles_y,
                              packed_tiles);
}
int AgxWin32AsahiMapReservedTextureTile(struct pipe_resource *reserved,unsigned layer,int packed,
                                        unsigned mip,unsigned x,unsigned y,
                                        struct pipe_resource *heap,unsigned heap_tile) {
  AGX_WIN32_SPARSE_PAGES pages;
  if(!reserved || reserved->target==PIPE_BUFFER ||
     !AgxWin32AsahiSparseTableVa(agx_resource(reserved)->bo) ||
     (heap && (heap->target!=PIPE_BUFFER || heap->screen!=reserved->screen))) return 0;
  const struct ail_layout *layout=&agx_resource(reserved)->layout;
  if(!(packed ? AgxWin32SparsePackedTile(layout,layer,x,&pages) :
                AgxWin32SparseStandardTile(layout,layer,mip,x,y,&pages))) return 0;
  return AgxWin32AsahiMapVirtualTilePages(agx_resource(reserved)->bo,&pages,
                                          heap?agx_resource(heap)->bo:NULL,heap_tile);
}
#endif

APPLE_AGX_U64 AgxWin32AsahiBufferGpuAddress(struct pipe_resource *resource) {
  if(!resource || resource->target!=PIPE_BUFFER) return 0;
  struct agx_resource *native=agx_resource(resource);
  return native->bo ? agx_map_gpu(native) : 0;
}
/* rc110 receipt-only: {tiling (ail_tiling: 0 linear, 1 GPU tiled), bit 4 the
 * layout is compressed (ail_layout.compressed), bit 5 the modifier is the
 * compressed one, bits 8.. the layout size in KiB} of a created resource, so
 * a log shows whether a render target took Asahi's default lossless
 * compression (agx_select_best_modifier) without a hardware probe. */
APPLE_AGX_U32 AgxWin32AsahiResourceLayoutFacts(struct pipe_resource *resource) {
  if(!resource) return 0;
  const struct agx_resource *native=agx_resource(resource);
  const APPLE_AGX_U64 kib=native->layout.size_B>>10;
  return ((APPLE_AGX_U32)native->layout.tiling & 0xfu) |
         (native->layout.compressed ? 0x10u : 0u) |
         (ail_is_drm_modifier_compressed(native->modifier) ? 0x20u : 0u) |
         (APPLE_AGX_U32)((kib>0xffffffull ? 0xffffffull : kib)<<8);
}
int AgxWin32AsahiResourceBusy(struct pipe_context *ctx,struct pipe_resource *resource) {
  if(!ctx || !resource || resource->screen!=ctx->screen) return 1;
  struct agx_context *native=agx_context(ctx);
  struct agx_resource *rsrc=agx_resource(resource);
  if(!rsrc->bo) return 1;
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    int active=BITSET_TEST(native->batches.active,i);
    int submitted=BITSET_TEST(native->batches.submitted,i);
    struct agx_batch *batch=&native->batches.slots[i];
    if((!active && !submitted) || !agx_batch_uses_bo(batch,rsrc->bo)) continue;
    /* Do not flush a pending batch merely to answer a busy query. */
    if(active || !AgxWin32AsahiBatchPoll(batch,0)) return 1;
    /* Poll proved completion; the existing sync cleanup cannot wait again
     * because the capsule is already retired. Preserve all other batches. */
    agx_sync_batch(native,batch);
    if(BITSET_TEST(native->batches.submitted,i)) return 1;
  }
  return 0;
}
/* D3D12 C47: the descriptor-heap form of a texture view: Asahi's packed
 * texture descriptor (AGX_TEXTURE_LENGTH bytes, agx_win32_pack_sampler_view
 * in the native state) and the resource's layout key, which changes when
 * Asahi replaces the BO or changes its tiling/compression (a heap
 * descriptor is re-packed then). */
void agx_win32_pack_sampler_view(void *out, struct pipe_sampler_view *view);
APPLE_AGX_U64 AgxWin32AsahiResourceLayoutKey(struct pipe_resource *resource) {
  if(!resource) return 0u;
  struct agx_resource *rsrc=agx_resource(resource);
  if(!rsrc->bo || !rsrc->bo->va) return 0u;
  return rsrc->bo->va->addr ^ ((APPLE_AGX_U64)rsrc->layout.tiling<<56) ^
         ((APPLE_AGX_U64)rsrc->layout.compressed<<62);
}
int AgxWin32AsahiTextureDescriptor(struct pipe_context *ctx, struct pipe_resource *resource,
    const struct pipe_sampler_view *templ, void *out, APPLE_AGX_U64 *key) {
  if(!ctx || !resource || !templ || !out || !key || resource->screen!=ctx->screen) return 0;
  struct pipe_sampler_view *view=ctx->create_sampler_view(ctx,resource,templ);
  if(!view) return 0;
  agx_win32_pack_sampler_view(out,view);
  *key=AgxWin32AsahiResourceLayoutKey(resource);
  pipe_sampler_view_reference(&view,NULL);
  return 1;
}
void AgxWin32AsahiNullTextureDescriptor(void *out) {
  if(out) agx_set_null_texture((struct agx_texture_packed *)out);
}
unsigned AgxWin32AsahiTextureDescriptorBytes(void) { return AGX_TEXTURE_LENGTH; }
/* D3D12 C47: an image's texture and PBE descriptors for a descriptor heap.
 * Binding it (spare compute image slot) applies Asahi's legalisation --
 * decompressing a written image -- before the descriptors are packed. */
void agx_win32_pack_image_view(void *texture, void *pbe, const struct pipe_image_view *view);
int AgxWin32AsahiImageDescriptors(struct pipe_context *ctx, const struct pipe_image_view *view,
    void *texture, void *pbe, APPLE_AGX_U64 *key) {
  if(!ctx || !view || !view->resource || !texture || !pbe || !key || view->resource->screen!=ctx->screen) return 0;
  if(agx_context(ctx)->any_faults) return 0;
  const unsigned slot=PIPE_MAX_SHADER_IMAGES-1u;
  ctx->set_shader_images(ctx,MESA_SHADER_COMPUTE,slot,1,0,view);
  if(agx_context(ctx)->any_faults) {
    ctx->set_shader_images(ctx,MESA_SHADER_COMPUTE,slot,0,1,NULL);
    return 0;
  }
  agx_win32_pack_image_view(texture,pbe,view);
  ctx->set_shader_images(ctx,MESA_SHADER_COMPUTE,slot,0,1,NULL);
  *key=AgxWin32AsahiResourceLayoutKey(view->resource);
  return 1;
}
unsigned AgxWin32AsahiPbeDescriptorBytes(void) { return AGX_PBE_LENGTH; }
/* D3D12 C47: a sampler CSO's heap form: the AGX sampler without custom
 * border (the form Asahi's sampler heap takes, agx_uniforms.c), its LOD
 * bias as fp16 and whether it needs a custom border colour */
unsigned AgxWin32AsahiSamplerDescriptorBytes(void) { return AGX_SAMPLER_LENGTH; }
int AgxWin32AsahiSamplerDescriptor(void *cso, void *out, APPLE_AGX_U16 *bias, int *custom_border) {
  struct agx_sampler_state *so=cso;
  if(!so || !out || !bias || !custom_border) return 0;
  memcpy(out,&so->desc_without_custom_border,AGX_SAMPLER_LENGTH);
  *bias=so->lod_bias_as_fp16;
  *custom_border=so->uses_custom_border;
  return 1;
}
/* D3D12 RC108: a sampler CSO's built-in border colour
 * (agx_win32_sampler_border.h), in the form slots bind and the form the
 * sampler heap takes; a custom border keeps Asahi's own packing */
void AgxWin32AsahiSamplerSetBorder(void *cso, unsigned colour) {
  struct agx_sampler_state *so=cso;
  if(!so || so->uses_custom_border || colour>AGX_WIN32_BORDER_OPAQUE_WHITE) return;
  AgxWin32SamplerSetBorder(&so->desc,colour);
  AgxWin32SamplerSetBorder(&so->desc_without_custom_border,colour);
}
void AgxWin32AsahiSetDrawBindings(struct pipe_context *pctx,
    int (*bindings)(void *), void *data) {
  struct agx_context *ctx=agx_context(pctx);
  ctx->windows_draw_bindings=bindings;
  ctx->windows_draw_bindings_data=data;
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(bindings) {
    ctx->windows_indirect_path=0;
    ctx->windows_indirect_reason=0;
    ctx->windows_indirect_encode_us=0;
  }
#endif
}
void AgxWin32AsahiSetIndirectIndexBuffer(struct pipe_context *pctx,
    struct pipe_resource *resource, unsigned offset) {
  struct agx_context *ctx=agx_context(pctx);
  ctx->windows_indirect_index_resource=resource;
  ctx->windows_indirect_index_offset=resource ? offset : 0;
}
void AgxWin32AsahiIndirectMetrics(struct pipe_context *pctx, unsigned out[3]) {
#ifdef APPLE_AGX_GPUVA_WINSYS
  struct agx_context *ctx=agx_context(pctx);
  out[0]=ctx->windows_indirect_path;
  out[1]=ctx->windows_indirect_encode_us;
  out[2]=ctx->windows_indirect_reason;
#else
  (void)pctx;
  out[0]=2;out[1]=out[2]=0;
#endif
}
/* rc76: whether pipe->clear(Buffers) on the bound framebuffer clears every
 * buffer fast (load-op clear): agx_clear (agx_pipe.c) clears fast exactly
 * the buffers its batch -- agx_get_batch, taken here as agx_clear takes it
 * -- has neither drawn nor loaded, and draws a u_blitter quad for the rest. */
int AgxWin32AsahiClearIsFast(struct pipe_context *pctx, unsigned buffers) {
  struct agx_context *ctx=pctx?agx_context(pctx):NULL;
  if(!ctx || ctx->any_faults) return 0;
  struct agx_batch *batch=agx_get_batch(ctx);
  return !ctx->any_faults && batch && !(buffers & (batch->draw | batch->load));
}
/* rc76b: right after a fast pipe->clear of the bound framebuffer, colour
 * buffer Rt's own clear colour: agx_clear uploads one clamped colour per
 * fast-cleared buffer into the batch pool (uploaded_clear_color), which only
 * the background program reads when the batch is flushed, so replacing it
 * before the batch draws Rt clears Rt with Color. A buffer the clear did not
 * set (a render condition skipped it) is left alone. 0 on a fault. */
int AgxWin32AsahiSetFastClearColor(struct pipe_context *pctx, unsigned rt,
    const union pipe_color_union *color) {
  struct agx_context *ctx=pctx?agx_context(pctx):NULL;
  if(!ctx || ctx->any_faults || !color || rt>=PIPE_MAX_COLOR_BUFS) return 0;
  struct agx_batch *batch=agx_get_batch(ctx);
  if(ctx->any_faults || !batch) return 0;
  const unsigned bit=PIPE_CLEAR_COLOR0<<rt;
  if(!(batch->clear & bit)) return 1;
  if((batch->draw & bit) || rt>=batch->key.nr_cbufs || !batch->key.cbufs[rt].texture) return 0;
  if(!AgxWin32AsahiBatchEnter(batch)) { ctx->any_faults=true; return 0; }
  union pipe_color_union clamped=util_clamp_color(batch->key.cbufs[rt].format,color);
  batch->uploaded_clear_color[rt]=agx_pool_upload_aligned(&batch->pool,clamped.f,sizeof(clamped.f),16);
  if(!AgxWin32AsahiBatchLeave(batch)) { ctx->any_faults=true; return 0; }
  return batch->uploaded_clear_color[rt]!=0;
}
/* EXP1927: a residency failure can occur while acquiring a batch (or
 * flushing a hazard in a prior heap chunk). Never initialize its encoder or
 * grow its sampler heap after that failure. Kind 4 names this scene bridge. */
static struct agx_batch *heap_batch(struct agx_context *ctx, int compute) {
  if(ctx->any_faults) return NULL;
  struct agx_batch *batch=compute ? agx_get_compute_batch(ctx) : agx_get_batch(ctx);
  if(ctx->any_faults) return NULL;
  if(!batch || !(compute ? batch->cdm.bo && batch->cdm.current :
                           batch->vdm.bo && batch->vdm.current)) {
    AgxWin32AsahiMarkContextFault(ctx,(4u<<16)|__LINE__);
    return NULL;
  }
  return batch;
}
/* agx_set_sampler_uniforms can append each bound native sampler later in
 * the draw/dispatch. Count all stages conservatively, including helper work;
 * these entries do not deduplicate against the explicit heap union. */
static unsigned heap_sampler_headroom(const struct agx_context *ctx) {
  unsigned count = 0;
  for (unsigned stage = 0; stage < MESA_SHADER_STAGES; ++stage) {
    if (!ctx->stage[stage].shader || !ctx->stage[stage].shader->uses_bindless_samplers)
      continue;
    for (unsigned mask = ctx->stage[stage].valid_samplers; mask; mask &= mask - 1u)
      ++count;
  }
  return count;
}
/* D3D12 C47: heap samplers enter the sampler heap of the batch the next draw
 * (dispatch) uses, deduplicated; handles are 28 + heap index (agx_uniforms.c).
 * A batch whose heap cannot take them all is flushed first. */
int AgxWin32AsahiBatchSamplerHandles(struct pipe_context *pctx, int compute, const void *packed,
    unsigned stride, unsigned count, APPLE_AGX_U16 *handles) {
  if(!pctx || !count) return 1;
  if(!packed || !handles || count>AGX_SAMPLER_HEAP_SIZE) return 0;
  struct agx_context *ctx=agx_context(pctx);
  struct agx_device *dev=agx_device(pctx->screen);
  struct agx_batch *batch=heap_batch(ctx,compute);
  if(!batch) return 0;
  unsigned reserve=heap_sampler_headroom(ctx);
  if(reserve>AGX_SAMPLER_HEAP_SIZE-count) return 0;
  if(batch->sampler_heap.count>AGX_SAMPLER_HEAP_SIZE-count-reserve) {
    agx_flush_batch_for_reason(ctx,batch,"D3D12 heap samplers");
    batch=heap_batch(ctx,compute);
    if(!batch) return 0;
  }
  for(unsigned i=0;i<count;++i) {
    const struct agx_sampler_packed *sampler=(const struct agx_sampler_packed *)((const char *)packed+(size_t)i*stride);
    const struct agx_sampler_packed *heap=batch->sampler_heap.bo ? agx_bo_map(batch->sampler_heap.bo) : NULL;
    if(batch->sampler_heap.bo && !heap) {
      AgxWin32AsahiMarkContextFault(ctx,(4u<<16)|__LINE__);
      return 0;
    }
    unsigned index=batch->sampler_heap.count;
    for(unsigned j=0;heap && j<batch->sampler_heap.count;++j)
      if(!memcmp(&heap[j],sampler,sizeof(*sampler))) { index=j; break; }
    if(index==batch->sampler_heap.count)
      index=agx_sampler_heap_add(dev,&batch->sampler_heap,(struct agx_sampler_packed *)sampler);
    if(index>=AGX_SAMPLER_HEAP_SIZE) {
      AgxWin32AsahiMarkContextFault(ctx,(4u<<16)|__LINE__);
      return 0;
    }
    handles[i]=(APPLE_AGX_U16)(28u+index);
  }
  return 1;
}
void AgxWin32AsahiNullPbeDescriptor(void *out) {
  if(out) agx_set_null_pbe((struct agx_pbe_packed *)out);
}
/* D3D12 C47: resources a descriptor heap makes reachable become reads of
 * the batch the next draw (or dispatch) uses, for residency and hazards */
void AgxWin32AsahiBatchReadResources(struct pipe_context *pctx, struct pipe_resource *const *resources,
    unsigned count, int compute) {
  if(!pctx || !resources || !count) return;
  struct agx_context *ctx=agx_context(pctx);
  struct agx_batch *batch=heap_batch(ctx,compute);
  if(!batch) return;
  /* EXP1855: Asahi tracks a batch's resources only after its state is
   * initialised (agx_batch_writes_internal asserts batch->initialized; the
   * draw/launch paths call agx_batch_init_state first). It is idempotent. */
  agx_batch_init_state(batch);
  for(unsigned i=0;i<count && !ctx->any_faults;++i)
    if(resources[i] && resources[i]->screen==pctx->screen) agx_batch_reads(batch,agx_resource(resources[i]));
}
/* D3D12 C47: resources a descriptor heap lets the draw (dispatch) write --
 * invisible to Asahi's binding-based writer tracking -- are written by its
 * batch, so later readers and writers flush it first */
void AgxWin32AsahiBatchWriteResources(struct pipe_context *pctx, struct pipe_resource *const *resources,
    unsigned count, int compute) {
  if(!pctx || !resources || !count) return;
  struct agx_context *ctx=agx_context(pctx);
  struct agx_batch *batch=heap_batch(ctx,compute);
  if(!batch) return;
  agx_batch_init_state(batch); /* as AgxWin32AsahiBatchReadResources */
  for(unsigned i=0;i<count && !ctx->any_faults;++i) {
    if(!resources[i] || resources[i]->screen!=pctx->screen) continue;
    struct agx_resource *rsrc=agx_resource(resources[i]);
    if(resources[i]->target==PIPE_BUFFER)
      agx_batch_writes_range(batch,rsrc,0,resources[i]->width0);
    else
      for(unsigned level=0;level<=resources[i]->last_level && !ctx->any_faults;++level) agx_batch_writes(batch,rsrc,level);
    batch->incoherent_writes=true;
  }
}
/* D3D12 C46 receipt-only: the completion fence of the context's newest
 * accepted submission; fences increase with every submission. */
APPLE_AGX_U64 AgxWin32AsahiContextNewestFence(struct pipe_context *ctx) {
  return ctx ? AgxWin32AsahiNewestFence(agx_context(ctx)) : 0u;
}
/* rc101 receipt-only: the lean CDM barrier mode and word, and the barriers
 * left lean and full since the last call (agx_win32_cdm_barrier.h). */
void AgxWin32AsahiContextCdmBarriers(struct pipe_context *ctx, APPLE_AGX_U32 out[4]) {
  memset(out,0,4*sizeof(*out));
#ifdef APPLE_AGX_GPUVA_WINSYS
  if(!ctx) return;
  struct agx_context *n=agx_context(ctx);
  out[0]=n->windows_cdm_barriers[2];
  out[1]=n->windows_cdm_barriers[3];
  out[2]=n->windows_cdm_barriers[0];
  out[3]=n->windows_cdm_barriers[1];
  n->windows_cdm_barriers[0]=n->windows_cdm_barriers[1]=0;
#else
  (void)ctx;
#endif
}
void AgxWin32AsahiContextRetireDiagnostic(struct pipe_context *ctx,
    APPLE_AGX_U32 state[12]) {
  memset(state,0,12*sizeof(*state));
  if(!ctx) return;
  struct agx_context *n=agx_context(ctx);
  AGX_WIN32_ASAHI_BACKEND *d=agx_device(ctx->screen)->windows_private;
#if defined(APPLE_AGX_GPUVA_WINSYS)
  for(unsigned i=0;i<AGX_MAX_BATCHES && i<4u;++i) {
    APPLE_AGX_U32 slot[2];
    AgxWin32AsahiBatchDiagnostic(&n->batches.slots[i],slot);
    state[2u*i]=slot[0]|((APPLE_AGX_U32)BITSET_TEST(n->batches.active,i)<<8)|
        ((APPLE_AGX_U32)BITSET_TEST(n->batches.submitted,i)<<9);
    state[2u*i+1u]=slot[1];
  }
  if(d) {
    state[10]=(APPLE_AGX_U32)(d->Gpuva.Held!=NULL)|((APPLE_AGX_U32)(d->Gpuva.Terminal!=0)<<1)|
        (d->Gpuva.LastFailure<<8);
    state[11]=(APPLE_AGX_U32)d->Gpuva.RenderFence;
  }
#endif
  state[8]=n->any_faults;state[9]=d?(APPLE_AGX_U32)d->Failed:0u;
}
void AgxWin32AsahiContextDiagnostic(struct pipe_context *ctx,
    APPLE_AGX_U32 state[16],APPLE_AGX_U32 bindings[16]) {
  memset(state,0,16*sizeof(*state));memset(bindings,0,16*sizeof(*bindings));
  if(!ctx) return;
  struct agx_context *n=agx_context(ctx);
  AGX_WIN32_ASAHI_BACKEND *d=agx_device(ctx->screen)->windows_private;
  struct agx_batch *b=n->batch;
#if defined(APPLE_AGX_GPUVA_WINSYS)
  /* EXP842: in GPUVA builds windows_batch is an AGX_WIN32_GPUVA_BATCH; reading
   * it as the capture batch overran the object and crashed Explorer. */
  AGX_WIN32_ASAHI_BATCH *c=NULL;
#else
  AGX_WIN32_ASAHI_BATCH *c=b?b->windows_batch:NULL;
#endif
  state[0]=n->any_faults;state[1]=d?d->Failed:0;state[2]=b!=NULL;
  state[3]=b?b->draws:0;state[4]=b?b->initialized:0;
  state[5]=c?c->Capture.Capture.State:0;state[6]=c?c->Root.Scope.Failed:0;
  state[7]=c?c->Entered:0;state[8]=c?c->Submitted:0;state[9]=c?c->Rejected:0;
  state[10]=c?c->Capture.Capture.ReferenceCount:0;
  state[11]=c?c->Capture.Capture.RelocationCount:0;
  state[12]=d?d->LiveBos:0;state[13]=n->framebuffer.nr_cbufs;
  state[14]=n->framebuffer.cbufs[0].format;state[15]=b?b->sampler_heap.count:0;
  bindings[0]=n->rast!=NULL;bindings[1]=n->rast?n->rast->depth_bias:0;
  bindings[2]=n->attributes!=NULL;
  bindings[3]=n->stage[MESA_SHADER_VERTEX].shader!=NULL;
  bindings[4]=n->stage[MESA_SHADER_FRAGMENT].shader!=NULL;
  bindings[5]=(n->stage[MESA_SHADER_GEOMETRY].shader?1u:0u)|
      (n->stage[MESA_SHADER_TESS_CTRL].shader?2u:0u)|
      (n->stage[MESA_SHADER_TESS_EVAL].shader?4u:0u)|
      (n->cond_query?8u:0u)|(n->occlusion_query?16u:0u)|(n->time_elapsed?32u:0u);
  bindings[6]=n->stage[MESA_SHADER_VERTEX].texture_count;
  bindings[7]=n->stage[MESA_SHADER_FRAGMENT].texture_count;
  bindings[8]=n->stage[MESA_SHADER_VERTEX].sampler_count;
  bindings[9]=n->stage[MESA_SHADER_FRAGMENT].sampler_count;
  bindings[10]=n->stage[MESA_SHADER_VERTEX].cb_mask;
  bindings[11]=n->stage[MESA_SHADER_FRAGMENT].cb_mask;
  bindings[12]=n->stage[MESA_SHADER_GEOMETRY].cb_mask;
  for(unsigned i=0;i<MESA_SHADER_STAGES;++i) {
    bindings[13]|=n->stage[i].image_mask;
    bindings[14]|=n->stage[i].ssbo_mask;
    if(n->stage[i].custom_borders) bindings[15]|=1u<<i;
  }
}
int AgxWin32AsahiContextDrawReceipt(struct pipe_context *ctx) {
  if(!ctx) return 0;
#ifdef APPLE_AGX_GPUVA_WINSYS
  /* EXP1035: a GPUVA batch submits Asahi's own VDM stream, which carries any
   * number of draws. The one-draw split (EXP755) is a limit of the legacy
   * capture capsule; under GPUVA it turned every draw into a synchronous
   * flush/retire (EXP1024: ~11 submit+wait per DWM frame). */
  return 0;
#else
  struct agx_context *native=agx_context(ctx);
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(ctx->screen)->windows_private;
  struct agx_batch *batch=native->batch;
  return batch && batch->draws==1 && batch->windows_batch && backend &&
      !backend->Failed && !native->any_faults;
#endif
}
int AgxWin32AsahiSetStreamOutputTargetOffsetForTest(
  struct pipe_stream_output_target *base,APPLE_AGX_U32 value) {
  struct agx_streamout_target *target=base?agx_so_target(base):NULL;
  struct agx_resource *offset=target&&target->offset?agx_resource(target->offset):NULL;
  void *map=offset&&offset->bo?agx_bo_map(offset->bo):NULL;
  if(!target||!offset||!offset->bo||!map)
    return 0;
  memcpy(map,&value,sizeof(value));
  util_range_add(&offset->base,&offset->valid_buffer_range,0,sizeof(value));
  return 1;
}
struct pipe_resource *AgxWin32AsahiStreamOutputOffset(struct pipe_stream_output_target *base) {
  return base?agx_so_target(base)->offset:NULL;
}
int AgxWin32AsahiContextFaulted(struct pipe_context *ctx) {
  return !ctx || agx_context(ctx)->any_faults;
}
/* OV4: submit the open compute batch now (a split at a barrier) */
int AgxWin32AsahiFlushComputeBatch(struct pipe_context *ctx) {
  struct agx_context *native=ctx ? agx_context(ctx) : NULL;
  struct agx_batch *batch=native ? native->batch : NULL;
  if(!batch || native->any_faults || !agx_batch_is_compute(batch) || !agx_batch_is_active(batch)) return 0;
  agx_flush_batch_for_reason(native,batch,"OV4 compute split");
  return !native->any_faults;
}
int AgxWin32AsahiContextFlushForPresent(struct pipe_context *ctx) {
  if(!ctx) return 0;
  struct agx_context *native=agx_context(ctx);
  struct agx_batch *batch=native->batch;
  int drawn=0;
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *slot=&native->batches.slots[i];
    if(slot->windows_batch && (slot->draws || slot->clear)) { drawn=1; break; }
  }
  if(drawn) {
    ctx->flush(ctx,NULL,0);
    return !native->any_faults;
  }
  if(!batch) return !native->any_faults;
  if(!AgxWin32AsahiBatchAbort(batch)) return 0;
  agx_batch_reset(native,batch);
  return !native->any_faults && native->batch==NULL;
}
int AgxWin32AsahiScreenDestroy(struct pipe_screen *screen) {
  if(!screen || !screen->destroy) return 0;
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(screen)->windows_private;
  if(!backend || backend->Native!=agx_device(screen) || backend->ContextCount) return 0;
  screen->destroy(screen);
  if(backend->Native) return 0;
  backend->ContextCreate=NULL;backend->ContextDestroy=NULL;backend->Closing=0;
  return 1;
}

static AGX_WIN32_ASAHI_BACKEND *scene_backend(const AGX_WIN32_ASAHI_SCENE *s) {
  return s && s->Screen && s->Context && s->Context->screen==s->Screen ?
      agx_device(s->Screen)->windows_private : NULL;
}
/* One source of truth for real native shader/clear inputs and CPU pixel
 * expectations. The canonical pack is never injected into a GPU command. */
static const float scene_background[4]={0.05f,0.05f,0.05f,1.0f};
static const float scene_foreground[4]={0.9f,0.2f,0.1f,1.0f};
static APPLE_AGX_U32 scene_bgra8(const float color[4]) {
  unsigned char pixel[4];
  util_format_pack_rgba(PIPE_FORMAT_B8G8R8A8_UNORM,pixel,color,1);
  return (APPLE_AGX_U32)pixel[0] | ((APPLE_AGX_U32)pixel[1]<<8) |
      ((APPLE_AGX_U32)pixel[2]<<16) | ((APPLE_AGX_U32)pixel[3]<<24);
}
static nir_shader *scene_shader(mesa_shader_stage stage) {
  nir_builder b=nir_builder_init_simple_shader(stage,&agx_nir_options,"Windows native scene");
  nir_variable *out=nir_variable_create(b.shader,nir_var_shader_out,glsl_vec4_type(),"out");
  out->data.location=stage==MESA_SHADER_VERTEX?VARYING_SLOT_POS:FRAG_RESULT_DATA0;
  out->data.driver_location=0;
  if(stage==MESA_SHADER_VERTEX) {
    nir_variable *in=nir_variable_create(b.shader,nir_var_shader_in,glsl_vec4_type(),"position");
    in->data.location=VERT_ATTRIB_GENERIC0;in->data.driver_location=0;
    nir_store_var(&b,out,nir_load_var(&b,in),0xf);
  } else nir_store_var(&b,out,nir_imm_vec4(&b,scene_foreground[0],scene_foreground[1],scene_foreground[2],scene_foreground[3]),0xf);
  nir_shader_gather_info(b.shader,nir_shader_get_entrypoint(b.shader));
  return b.shader;
}
int AgxWin32AsahiSceneInit(AGX_WIN32_ASAHI_SCENE *s,struct pipe_screen *screen,struct pipe_context *ctx) {
  if(!s || s->Phase!=AgxAsahiSceneEmpty || !screen || !ctx || ctx->screen!=screen) return 0;
  struct agx_context *native=agx_context(ctx);
  AGX_WIN32_ASAHI_BACKEND *backend=agx_device(screen)->windows_private;
  if(!backend || backend->Native!=agx_device(screen) || backend->Failed || backend->Closing || !backend->BatchOps ||
     backend->ActiveCapture || backend->ActiveEmission || native->any_faults) return 0;
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i)
    if(native->batches.slots[i].windows_batch || BITSET_TEST(native->batches.active,i) ||
       BITSET_TEST(native->batches.submitted,i)) return 0;
  s->Screen=screen;s->Context=ctx;s->Phase=AgxAsahiScenePreparing;
  struct pipe_resource rt_info={0},vb_info={0};
  rt_info.target=PIPE_TEXTURE_2D;rt_info.format=PIPE_FORMAT_B8G8R8A8_UNORM;
  rt_info.width0=16;rt_info.height0=16;rt_info.depth0=1;rt_info.array_size=1;
  rt_info.bind=PIPE_BIND_RENDER_TARGET;rt_info.usage=PIPE_USAGE_DEFAULT;
  vb_info.target=PIPE_BUFFER;vb_info.format=PIPE_FORMAT_R8_UNORM;
  vb_info.width0=48;vb_info.height0=vb_info.depth0=vb_info.array_size=1;
  vb_info.bind=PIPE_BIND_VERTEX_BUFFER;vb_info.usage=PIPE_USAGE_DEFAULT;
  const uint64_t modifier=DRM_FORMAT_MOD_APPLE_GPU_TILED;
  s->Target=screen->resource_create_with_modifiers(screen,&rt_info,&modifier,1);
  s->VertexBuffer=screen->resource_create(screen,&vb_info);
  if(!s->Target || !s->VertexBuffer) goto rejected;
  struct agx_resource *target=agx_resource(s->Target);
  if(!AgxWin32AsahiIdentity(backend,target->bo,&s->Receipt.TargetIdentity)) goto rejected;
  s->Receipt.TargetConstructionAddress=agx_map_gpu(target);
  s->Receipt.TargetBytes=target->layout.size_B;
  s->Receipt.TargetLayerStride=target->layout.layer_stride_B;
  s->Receipt.Width=16;s->Receipt.Height=16;
  s->Receipt.ExpectedBackgroundBgra8=scene_bgra8(scene_background);
  s->Receipt.ExpectedForegroundBgra8=scene_bgra8(scene_foreground);
  s->Receipt.Tiling=target->layout.tiling;s->Receipt.Format=AppleAgxWin32FormatBgra8Unorm;
  s->Receipt.BootGeneration=backend->Buffers.Screen->Info.BootGeneration;
  s->Receipt.PageBytes=backend->Buffers.Screen->Info.PageBytes;
  const float vertices[12]={-1,-1,0,1,1,-1,0,1,0,1,0,1};
  ctx->buffer_subdata(ctx,s->VertexBuffer,PIPE_MAP_WRITE,0,sizeof(vertices),vertices);
  struct pipe_framebuffer_state fb={0};fb.width=16;fb.height=16;fb.nr_cbufs=1;
  fb.cbufs[0].texture=s->Target;fb.cbufs[0].format=PIPE_FORMAT_B8G8R8A8_UNORM;
  ctx->set_framebuffer_state(ctx,&fb);
  struct pipe_shader_state vs={0},fs={0};vs.type=fs.type=PIPE_SHADER_IR_NIR;
  vs.ir.nir=scene_shader(MESA_SHADER_VERTEX);fs.ir.nir=scene_shader(MESA_SHADER_FRAGMENT);
  s->VertexShader=ctx->create_vs_state(ctx,&vs);s->FragmentShader=ctx->create_fs_state(ctx,&fs);
  /* Native agx_create_shader_state returns before consuming NIR if its initial
   * state allocation fails; a successful call consumes/frees its input. */
  if(!s->VertexShader) ralloc_free(vs.ir.nir);
  if(!s->FragmentShader) ralloc_free(fs.ir.nir);
  if(!s->VertexShader || !s->FragmentShader) goto rejected;
  ctx->bind_vs_state(ctx,s->VertexShader);ctx->bind_fs_state(ctx,s->FragmentShader);
  struct pipe_blend_state blend={0};blend.rt[0].colormask=0xf;
  s->Blend=ctx->create_blend_state(ctx,&blend);if(!s->Blend) goto rejected;
  ctx->bind_blend_state(ctx,s->Blend);
  struct pipe_rasterizer_state raster={0};
  raster.fill_front=raster.fill_back=PIPE_POLYGON_MODE_FILL;
  raster.depth_clip_near=raster.depth_clip_far=true;raster.half_pixel_center=true;
  raster.point_size=1;raster.line_width=1;
  s->Rasterizer=ctx->create_rasterizer_state(ctx,&raster);if(!s->Rasterizer) goto rejected;
  ctx->bind_rasterizer_state(ctx,s->Rasterizer);
  struct pipe_depth_stencil_alpha_state depth={0};
  s->Depth=ctx->create_depth_stencil_alpha_state(ctx,&depth);if(!s->Depth) goto rejected;
  ctx->bind_depth_stencil_alpha_state(ctx,s->Depth);
  struct pipe_vertex_element element={0};element.src_format=PIPE_FORMAT_R32G32B32A32_FLOAT;element.src_stride=16;
  s->Elements=ctx->create_vertex_elements_state(ctx,1,&element);if(!s->Elements) goto rejected;
  ctx->bind_vertex_elements_state(ctx,s->Elements);
  struct pipe_vertex_buffer vertex_buffer={0};vertex_buffer.buffer.resource=s->VertexBuffer;
  ctx->set_vertex_buffers(ctx,1,&vertex_buffer);
  struct pipe_viewport_state vp={0};vp.scale[0]=vp.scale[1]=8;vp.scale[2]=1;
  vp.translate[0]=vp.translate[1]=8;ctx->set_viewport_states(ctx,0,1,&vp);
  struct pipe_scissor_state scissor={0};scissor.maxx=scissor.maxy=16;ctx->set_scissor_states(ctx,0,1,&scissor);
  if(backend->Failed || native->any_faults) goto rejected;
  s->Phase=AgxAsahiScenePrepared;return 1;
rejected:
  s->Phase=AgxAsahiSceneRejected;return 0;
}
int AgxWin32AsahiSceneDraw(AGX_WIN32_ASAHI_SCENE *s) {
  AGX_WIN32_ASAHI_BACKEND *backend=scene_backend(s);
  if(!backend || backend->Closing || s->Phase!=AgxAsahiScenePrepared) return 0;
  struct pipe_context *ctx=s->Context;
  struct agx_context *native=agx_context(ctx);
  union pipe_color_union clear;
  memcpy(clear.f,scene_background,sizeof(scene_background));
  ctx->clear(ctx,PIPE_CLEAR_COLOR0,0xf,0,NULL,&clear,0,0);
  s->Batch=native->batch;
  if(backend->Failed || native->any_faults || !s->Batch) goto rejected;
  struct pipe_draw_info info={0};info.mode=MESA_PRIM_TRIANGLES;info.instance_count=1;
  struct pipe_draw_start_count_bias draw={0};draw.count=3;
  ctx->draw_vbo(ctx,&info,0,NULL,&draw,1);
  if(backend->Failed || native->any_faults || native->batch!=s->Batch ||
      s->Batch->draws!=1 || !s->Batch->windows_batch) goto rejected;
  s->Phase=AgxAsahiSceneDrawn;return 1;
rejected:
  s->Phase=AgxAsahiSceneRejected;return 0;
}
int AgxWin32AsahiSceneSubmit(AGX_WIN32_ASAHI_SCENE *s) {
  AGX_WIN32_ASAHI_BACKEND *backend=scene_backend(s);
  if(!backend || backend->Closing || s->Phase!=AgxAsahiSceneDrawn || !s->Batch) return 0;
  s->Context->flush(s->Context,NULL,0);
  AGX_WIN32_ASAHI_BATCH *c=s->Batch->windows_batch;
  if(!c || !c->Submitted) {s->Phase=AgxAsahiSceneRejected;s->SubmitStatus=c?c->Status:-1;return 0;}
  /* Explicit Windows retirement keeps this capsule stable through flush even
   * if the ordered event was signalled before the callback returned. */
  s->Receipt.Request=c->Request;
  s->Receipt.CommandVersion=c->Capture.Capture.CommandVersion;
  s->Receipt.References=c->Capture.Capture.ReferenceCount;
  s->Receipt.Relocations=c->Capture.Capture.RelocationCount;
  s->Receipt.EncoderBytes=c->Capture.Capture.References[c->Draw.EncoderReference].Bytes;
  s->Receipt.NativeRoots=c->Render;
  s->SubmitStatus=c->Status;s->Phase=AgxAsahiSceneSubmitted;
  return s->SubmitStatus>=0;
}
int AgxWin32AsahiSceneRetire(AGX_WIN32_ASAHI_SCENE *s,APPLE_AGX_U32 timeout) {
  if(!scene_backend(s)) return 0;
  if(s->Phase==AgxAsahiSceneRetired) return 1;
  if(s->Phase!=AgxAsahiSceneSubmitted || !s->Batch || !s->Receipt.Request) return 0;
  AGX_WIN32_ASAHI_BATCH *c=s->Batch->windows_batch;
  if(!c || !c->Submitted || c->Request!=s->Receipt.Request) return 0;
  int complete=AgxWin32AsahiBatchPoll(s->Batch,timeout);
  s->RetireStatus=c->Status;
  if(!complete) return 0;
  /* Poll proved the same transaction retired. The normal native cleanup now
   * releases its pools/writer records; it cannot enqueue or replay a draw. */
  agx_sync_batch(agx_context(s->Context),s->Batch);
  if(s->Batch->windows_batch || agx_batch_is_active(s->Batch) || agx_batch_is_submitted(s->Batch)) return 0;
  s->Batch=NULL;s->Phase=AgxAsahiSceneRetired;return 1;
}
int AgxWin32AsahiSceneCleanup(AGX_WIN32_ASAHI_SCENE *s,APPLE_AGX_U32 timeout) {
  if(!s) return 0;
  if(s->Phase==AgxAsahiSceneReleased) return 1;
  if(s->Phase==AgxAsahiSceneEmpty) {s->Phase=AgxAsahiSceneReleased;return 1;}
  if(!scene_backend(s)) return 0;
  if(s->Phase==AgxAsahiSceneSubmitted && !AgxWin32AsahiSceneRetire(s,timeout)) return 0;
  struct agx_context *native=agx_context(s->Context);
  for(unsigned i=0;i<AGX_MAX_BATCHES;++i) {
    struct agx_batch *b=&native->batches.slots[i];
    AGX_WIN32_ASAHI_BATCH *c=b->windows_batch;
    if(c && c->Submitted && !c->Retired) return 0;
    if(c || BITSET_TEST(native->batches.active,i) || BITSET_TEST(native->batches.submitted,i))
      agx_batch_reset(native,b);
    if(b->windows_batch || BITSET_TEST(native->batches.active,i) || BITSET_TEST(native->batches.submitted,i)) return 0;
  }
  struct pipe_context *ctx=s->Context;
  ctx->bind_vs_state(ctx,NULL);ctx->bind_fs_state(ctx,NULL);
  if(s->VertexShader) ctx->delete_vs_state(ctx,s->VertexShader);
  if(s->FragmentShader) ctx->delete_fs_state(ctx,s->FragmentShader);
  ctx->bind_vertex_elements_state(ctx,NULL);if(s->Elements) ctx->delete_vertex_elements_state(ctx,s->Elements);
  ctx->bind_blend_state(ctx,NULL);if(s->Blend) ctx->delete_blend_state(ctx,s->Blend);
  ctx->bind_rasterizer_state(ctx,NULL);if(s->Rasterizer) ctx->delete_rasterizer_state(ctx,s->Rasterizer);
  ctx->bind_depth_stencil_alpha_state(ctx,NULL);if(s->Depth) ctx->delete_depth_stencil_alpha_state(ctx,s->Depth);
  ctx->set_vertex_buffers(ctx,0,NULL);
  struct pipe_framebuffer_state fb={0};ctx->set_framebuffer_state(ctx,&fb);
  pipe_resource_reference(&s->VertexBuffer,NULL);pipe_resource_reference(&s->Target,NULL);
  s->VertexShader=s->FragmentShader=s->Elements=s->Blend=s->Rasterizer=s->Depth=NULL;
  s->Batch=NULL;s->Phase=AgxAsahiSceneReleased;return 1;
}
