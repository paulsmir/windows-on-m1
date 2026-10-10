"""Windows ownership projection of the pinned native batch/pipe lifecycle.

Original MIT sources stay in the build tree with their notices. Common native
initialization, state, resources and finalization remain the producer; only OS
submission/synchronization ownership is redirected to the existing UMD adapter.
"""
import hashlib
import re


def replace(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Native lifecycle anchor mismatch: '+old[:100])
    return text.replace(old,new,1)


def function(text, name):
    m=re.search(r'\n'+re.escape(name)+r'\s*\(',text)
    if not m: raise RuntimeError('Native function missing: '+name)
    a=text.index('{',m.end()); depth=1; i=a+1
    # Pinned functions have no brace literals; lexical comments may have braces.
    while depth:
        if text.startswith('/*',i): i=text.index('*/',i+2)+2;continue
        if text.startswith('//',i): i=text.index('\n',i)+1;continue
        if text[i] in '\"\'':
            q=text[i];i+=1
            while text[i]!=q:
                i+=2 if text[i]=='\\' else 1
            i+=1;continue
        depth += (text[i]=='{')-(text[i]=='}'); i+=1
    return a,i


def body(text,name,new):
    a,b=function(text,name)
    return text[:a]+'{\n'+new+'\n}'+text[b:]


def project_sources(out,project,overlays):
    def save(path,text):
        p=out/path;p.write_text(text)
        overlays.setdefault(path,{})['final_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
    # Windows enum bitfields are signed: ZERO=17 and MAX=4 must survive
    # their 5/3-bit storage. Keep the native packed word and public enum APIs.
    lp='src/asahi/lib/agx_linker.h';linker=(out/lp).read_text()
    start=linker.index('struct agx_blend_standard {')
    end=linker.index('\n};',start)+3
    original=linker[start:end]
    packed=original.replace('enum pipe_blend_func ', 'unsigned ').replace('enum pipe_blendfactor ', 'unsigned ')
    if original.count('enum pipe_blend_func ')!=2 or original.count('enum pipe_blendfactor ')!=4:
        raise RuntimeError('Native standard blend storage changed')
    save(lp,replace(linker,original,packed))
    hp='src/gallium/drivers/asahi/agx_state.h'
    h=(out/hp).read_text()
    h=replace(h,'struct agx_batch {','struct agx_batch {\n   void *windows_batch; /* stable request/capture/adapter capsule */')
    # EXP1070: on Windows every perf_debug message (flush/sync reasons,
    # shadowing, fallbacks) is counted per process, whatever AGX_DBG_PERF says.
    h,n=re.subn(r'#define perf_debug\(dev, \.\.\.\)[^\n]*\\\n(?:[^\n]*\\\n)*[^\n]*while \(0\)\n',
        lambda m:'#ifdef _WIN32\n#ifdef __cplusplus\nextern "C"\n#endif\nvoid AgxWin32PerfNote(const char *fmt, ...);\n'
                 '#define perf_debug(dev, ...) do { (void)(dev); AgxWin32PerfNote(__VA_ARGS__); } while (0)\n'
                 '#else\n'+m.group(0)+'#endif\n',h)
    if n!=1: raise RuntimeError('perf_debug definition changed')
    save(hp,h)
    dp='src/asahi/lib/agx_device.h';device=(out/dp).read_text()
    device=replace(device,'   return util_sparse_array_get(&dev->bo_map, handle);',
        '   extern struct agx_bo *AgxWin32AsahiLookupBo(struct agx_device *, uint32_t);\n   return AgxWin32AsahiLookupBo(dev, handle);')
    save(dp,device)
    original_device=(out/'src/asahi/lib/agx_device.c').read_text()
    pure=original_device[:original_device.index('#include')]+'#include "agx_device.h"\n#include "agx_compile.h"\n#include "util/bitscan.h"\n#include "util/u_tristate.h"\n'
    for name,decl in (('agx_get_num_cores','unsigned'),('agx_gather_device_key','struct agx_device_key')):
        a,b=function(original_device,name)
        start=original_device.rfind(decl+'\n'+name,0,a)
        if start<0: raise RuntimeError('Pure device declaration missing: '+name)
        pure+=original_device[start:b]+'\n'
    save('src/asahi/lib/agx_win32_device_key.c',pure)
    # EXP1069: per-device zero/scratch pages replace Asahi's fixed VM pages.
    ap='src/asahi/lib/agx_abi.h';abi=(out/ap).read_text()
    abi=replace(abi,'#define AGX_ZERO_PAGE_ADDRESS (((uint64_t)1) << 32)','''#ifdef _WIN32
/* EXP1069: no VM-wide fixed pages on Windows; the thread that builds a
 * device's commands publishes that device's zero/scratch page VAs. */
extern __declspec(thread) uint64_t agx_win32_zero_page_va;
extern __declspec(thread) uint64_t agx_win32_scratch_page_va;
#define AGX_ZERO_PAGE_ADDRESS (agx_win32_zero_page_va)
#else
#define AGX_ZERO_PAGE_ADDRESS (((uint64_t)1) << 32)
#endif''')
    abi=replace(abi,'#define AGX_SCRATCH_PAGE_ADDRESS (AGX_ZERO_PAGE_ADDRESS + AGX_ZERO_PAGE_SIZE)','''#ifdef _WIN32
#define AGX_SCRATCH_PAGE_ADDRESS (agx_win32_scratch_page_va)
#else
#define AGX_SCRATCH_PAGE_ADDRESS (AGX_ZERO_PAGE_ADDRESS + AGX_ZERO_PAGE_SIZE)
#endif''')
    save(ap,abi)
    bp='src/gallium/drivers/asahi/agx_batch.c'
    s=(out/bp).read_text()
    s=s.replace('#include <xf86drm.h>','/* Windows runtime owns synchronization. */')
    s=s.replace('#include "vdrm.h"','')
    s=s.replace('#include "asahi/lib/agx_device_virtio.h"','#include "agx_device.h"')
    s=replace(s,'#include "agx_state.h"','#include "agx_state.h"\n#include "agx_win32_asahi_batch.h"')
    a,b=function(s,'agx_batch_init');part=s[a:b]
    begin=part.index('   if (!batch->syncobj) {');end=part.index('   agx_batch_mark_active(batch);',begin)
    part=part[:begin]+'''   agx_batch_mark_active(batch);
   if (!AgxWin32AsahiBatchBegin(batch)) {
      ctx->any_faults = true;
      ((AGX_WIN32_ASAHI_BACKEND *)dev->windows_private)->Failed = 1;
   }
'''+part[end+len('   agx_batch_mark_active(batch);'):]
    s=s[:a]+part+s[b:]
    a,b=function(s,'agx_batch_cleanup');part=s[a:b]
    part=replace(part,'   assert(batch->ctx == ctx);','''   if (!AgxWin32AsahiBatchPoll(batch, 0) || !AgxWin32AsahiBatchRelease(batch)) return;
   assert(batch->ctx == ctx);''')
    part=part.replace('      agx_batch_print_stats(dev, batch);','      /* Windows completion is the existing ordered runtime marker. */')
    s=s[:a]+part+s[b:]
    s=body(s,'agx_cleanup_batches','''   unsigned i;
   foreach_submitted(ctx, i) {
      struct agx_batch *batch = &ctx->batches.slots[i];
      if (AgxWin32AsahiBatchPoll(batch, 0)) {
         agx_batch_cleanup(ctx, batch, false);
         if (!agx_batch_is_submitted(batch)) return i;
      }
   }
   return -1;''')
    a,b=function(s,'agx_get_in_sync');start=s.rfind('static int',0,a)
    s=s[:start]+'#ifndef _WIN32\n'+s[start:b]+'\n#endif\n'+s[b:]
    s=body(s,'agx_batch_submit','''   bool entered = render &&
      ((compute != NULL) == (batch->cdm.bo != NULL)) &&
      AgxWin32AsahiBatchFinish(batch, render);
   if (!entered && !AgxWin32AsahiBatchAbort(batch)) { ctx->any_faults = true; return; }
   agx_batch_mark_submitted(batch);
   if (ctx->batch == batch) ctx->batch = NULL;
   if (!entered) ctx->any_faults = true;
   /* The event may already be signalled before flush returns. Keep the
    * capsule until explicit Windows retirement so the owner can snapshot the
    * exact completed graph and submission result without a dangling pointer.
    * Normal explicit sync/next-native cleanup paths still perform retirement. */''')
    s=body(s,'agx_sync_batch','''   if (agx_batch_is_active(batch)) agx_flush_batch(ctx, batch);
   if (!agx_batch_is_submitted(batch)) return;
   if (!AgxWin32AsahiBatchPoll(batch, 1000)) { ctx->any_faults = true; return; }
   agx_batch_cleanup(ctx, batch, false);''')
    s=body(s,'agx_batch_reset','''   if (!AgxWin32AsahiBatchAbort(batch)) return;
   if (agx_batch_is_active(batch)) agx_batch_mark_submitted(batch);
   if (ctx->batch == batch) ctx->batch = NULL;
   /* Cancelled native clear/draw may already own writer records. */
   agx_batch_cleanup(ctx, batch, false);''')
    s=replace(s,'   /* Batch is now free */','   if (agx_batch_is_submitted(batch) || agx_batch_is_active(batch)) return NULL;\n\n   /* Batch is now free */')
    s=replace(s,'   assert(util_framebuffer_state_equal(&ctx->framebuffer, &ctx->batch->key));','   if (!ctx->batch) return NULL;\n   assert(util_framebuffer_state_equal(&ctx->framebuffer, &ctx->batch->key));')
    save(bp,s)
    qp='src/gallium/drivers/asahi/agx_query.c';query=(out/qp).read_text()
    a,b=function(query,'agx_end_query');part=query[a:b]
    old='''   case PIPE_QUERY_TIMESTAMP: {
      /* Timestamp logically written now, set up batches to MAX their finish
       * time in. If there are no batches, it's just the current time stamp.
       */
      agx_add_timestamp_end_query(ctx, query);

      uint64_t *value = query->ptr.cpu;
      *value = agx_get_gpu_timestamp(dev);

      return true;
   }'''
    part=replace(part,old,'   case PIPE_QUERY_TIMESTAMP:\n      return false; /* Windows GPU timestamp source is not implemented. */')
    save(qp,query[:a]+part+query[b:])
    # Real draw entrypoint is wrapped so every native early return unwinds the
    # stable root. No manually constructed command/capture substitutes this call.
    sp='src/gallium/drivers/asahi/agx_state.c';s=(out/sp).read_text()
    s=replace(s,'#include "agx_win32_pipeline.inc"','#include "agx_win32_pipeline.inc"\n#include "agx_win32_asahi_batch.h"')
    a,b=function(s,'agx_draw_vbo');decl=s.rfind('static void',0,a);signature=s[decl:a]
    renamed=signature.replace('agx_draw_vbo(','agx_draw_vbo_windows_body(')
    wrapper=signature+'''{
   struct agx_context *ctx = agx_context(pctx);
   AgxWin32AsahiPublishPages(agx_device(pctx->screen));
   /* EXP1061: Draw(0)/DrawIndexed(0) and zero-instance draws are D3D no-ops
    * that d3d10umd forwards unchanged; refusing them below poisoned the
    * context for good (Notepad: noise surfaces, no text). */
   if (!indirect && num_draws == 1 && draws &&
       (!draws[0].count || !info->instance_count))
      return;
   if (indirect && indirect->count_from_stream_output) {
      /* DrawAuto is expanded by upstream Asahi into a direct draw.  The graph
       * gate validates the exact count-from-XFB form before the CPU read; the
       * recursive direct call below owns normal batch creation and entry. */
      agx_draw_vbo_windows_body(pctx, info, drawid_offset, indirect, draws,
                                num_draws);
      return;
   }
   if (!AgxWin32AsahiBatchDrawAllowed(ctx, info, drawid_offset, indirect, draws, num_draws)) {
      AgxWin32AsahiBatchTraceDraw(ctx, ctx->batch, 1u);
      /* EXP1063: refused before any batch work; the backend decides whether
       * the draw is dropped or the context fails. */
      if (ctx->any_faults ||
          !AgxWin32AsahiBatchDrawRefused(ctx, info, indirect, draws, num_draws))
         return;
      ctx->any_faults = true; return;
   }
   struct agx_batch *batch = agx_get_batch(ctx);
   /* One check per line: the fault receipt site names the refusing check. */
   if (!batch) {
      AgxWin32AsahiBatchTraceDraw(ctx, batch, 2u);
      ctx->any_faults = true; return;
   }
   if (!AgxWin32AsahiBatchPrepareDraw(batch,info,draws)) {
      AgxWin32AsahiBatchTraceDraw(ctx, batch, 2u);
      ctx->any_faults = true; return;
   }
   if (!AgxWin32AsahiBatchEnter(batch)) {
      AgxWin32AsahiBatchTraceDraw(ctx, batch, 2u);
      ctx->any_faults = true; return;
   }
   agx_draw_vbo_windows_body(pctx, info, drawid_offset, indirect, draws, num_draws);
   AgxWin32AsahiBatchTraceDraw(ctx, batch, 3u);
   if (!AgxWin32AsahiBatchLeave(batch)) {
      AgxWin32AsahiBatchTraceDraw(ctx, batch, 4u);
      ctx->any_faults = true;
   }
}
'''
    s=s[:decl]+signature.rstrip()+';\n'+renamed+s[a:b]+'\n'+wrapper+s[b:]
    # A graphics GS batch temporarily owns the real CDM encoder. Switch the
    # active capture scope only around the upstream preraster launches.
    a,b=function(s,'agx_launch_gs_prerast');part=s[a:b]
    part=replace(part,'''   if (!batch->cdm.bo) {
      batch->cdm = agx_encoder_allocate(batch, dev);
   }
''','''   if (!batch->cdm.bo) {
      batch->cdm = agx_encoder_allocate(batch, dev);
   }
   if (!AgxWin32AsahiBatchComputeEnter(batch)) {
      ctx->any_faults = true; return;
   }
''')
    part=part[:-1]+'''   if (!AgxWin32AsahiBatchComputeLeave(batch)) {
      ctx->any_faults = true;
   }
}'''
    s=s[:a]+part+s[b:]
    save(sp,s)
    pp='src/gallium/drivers/asahi/agx_pipe.c';s=(out/pp).read_text()
    s=s.replace('#include <xf86drm.h>','/* Windows synchronization belongs to UMD. */')
    # u_drm.h contains pure modifier-array helpers and is portable.
    s=s.replace('#include "asahi/lib/decode.h"','#include "agx_device.h"\n#include "libagx_shaders.h"\n#include "asahi/lib/decode.h"')
    s=replace(s,'#include "agx_state.h"','#include "agx_state.h"\n#include "agx_win32_asahi_batch.h"')
    s=s.replace('#include "gallium/auxiliary/renderonly/renderonly.h"','')
    for excluded in ('agx_resource_from_handle','agx_resource_get_handle','agx_resource_get_param'):
        a,b=function(s,excluded);start=s.rfind('static ',0,a)
        s=s[:start]+'#ifndef _WIN32\n'+s[start:b]+'\n#endif\n'+s[b:]
    s=s.replace('   if (rsrc->scanout)\n      renderonly_scanout_destroy(rsrc->scanout, agx_screen->dev.ro);','')
    s=s.replace('   if (screen->dev.ro)\n      screen->dev.ro->destroy(screen->dev.ro);','')
    s=s.replace('strcasestr(util_get_process_name(), "ryujinx")', 'windows_strcasestr(util_get_process_name(), "ryujinx")')
    s=replace(s,'   caps->timer_resolution = agx_gpu_timestamp_to_ns(agx_device(pscreen), 1);','   caps->timer_resolution = 0; /* Timestamp capability is unavailable on this Windows profile. */')
    marker='#include "agx_win32_asahi_batch.h"'
    s=s.replace(marker,marker+'''\nstatic const char *windows_strcasestr(const char *s, const char *needle) {
   if (!s || !needle) return NULL;
   size_t n = strlen(needle);
   for (; *s; ++s) if (_strnicmp(s, needle, n) == 0) return s;
   return n ? NULL : s;
}
''',1)
    # Flush still calls the original native render finalization and cmdbuf
    # constructor. The internal DRM-named value is never sent to Linux APIs.
    s=body(s,'agx_flush_batch','''   if (!agx_batch_is_active(batch) || agx_batch_is_submitted(batch)) return;
   if (!batch->draws && !batch->cdm.bo &&
       !(batch->vdm.bo && (batch->clear || batch->initialized))) {
      if (!AgxWin32AsahiBatchAbort(batch)) { ctx->any_faults = true; return; }
      agx_batch_reset(ctx, batch);
      return;
   }
   if (ctx->any_faults || !batch->vdm.bo || !batch->initialized ||
       (!batch->draws && !batch->clear)) {
      ctx->any_faults = true;
      if (AgxWin32AsahiBatchAbort(batch)) agx_batch_reset(ctx, batch);
      return;
   }
   struct drm_asahi_cmd_compute compute_storage;
   struct drm_asahi_cmd_compute *compute = NULL;
   if (batch->cdm.bo) {
      if (!AgxWin32AsahiBatchComputeEnter(batch)) {
         ctx->any_faults = true; return;
      }
      agx_flush_compute(ctx, batch, &compute_storage);
      if (!AgxWin32AsahiBatchComputeLeave(batch)) {
         ctx->any_faults = true; return;
      }
      if (!AgxWin32AsahiBatchComputeFinalize(batch,batch->cdm.current)) {
         ctx->any_faults = true; return;
      }
      compute = &compute_storage;
   } else if (!AgxWin32AsahiBatchEnter(batch)) {
      ctx->any_faults = true;
      if (AgxWin32AsahiBatchAbort(batch)) agx_batch_reset(ctx, batch);
      return;
   }
   struct drm_asahi_cmd_render render;
   agx_flush_render(ctx, batch, &render);
   if (!AgxWin32AsahiBatchLeave(batch)) { ctx->any_faults = true; return; }
   agx_batch_submit(ctx, batch, compute, &render);''')
    # CS 1.6 ICD (EXP1146): Mesa's GL frontend asks for a fence on glFinish
    # and SwapBuffers and waits on screen->fence_finish; the D3D10 frontend
    # never asks (it would have faulted every context). Windows batches
    # complete by event + stamp polling (agx_sync_batch), so a fence request
    # waits for every submitted batch and returns a fence that is signalled
    # by construction; a failed wait leaves the context faulted, no fence.
    # EXP1148: st/mesa calls flush_resource on the window back buffer before
    # SwapBuffers. Upstream converts a non-shareable BO into a SHARED one
    # (Linux dma-buf export); the Windows BO layer refuses AGX_BO_SHAREABLE,
    # so transition_resource got no resource and asserted. Windows shares
    # through kernel allocations, never BO export, and the ICD displays by
    # CPU readback: flushing the resource's writer is the whole contract.
    # The D3D10 frontend never calls flush_resource.
    s=body(s,'agx_flush_resource','''   agx_flush_writer(agx_context(pctx), agx_resource(pres), "flush_resource");''')
    s=replace(s,'static void\nagx_flush(struct pipe_context *pctx,','''static char agx_windows_fence_storage;
#define AGX_WINDOWS_SIGNALLED_FENCE \\
   ((struct pipe_fence_handle *)&agx_windows_fence_storage)

static void
agx_windows_fence_reference(struct pipe_screen *screen,
                            struct pipe_fence_handle **ptr,
                            struct pipe_fence_handle *fence)
{
   (void)screen;
   *ptr = fence;
}

static bool
agx_windows_fence_finish(struct pipe_screen *screen, struct pipe_context *ctx,
                         struct pipe_fence_handle *fence, uint64_t timeout)
{
   (void)screen; (void)ctx; (void)timeout;
   return fence == AGX_WINDOWS_SIGNALLED_FENCE;
}

static void
agx_flush(struct pipe_context *pctx,''')
    s=body(s,'agx_flush','''   struct agx_context *ctx = agx_context(pctx);
   (void)flags;
   if (fence) {
      agx_sync_all(ctx, "Windows fence");
      *fence = ctx->any_faults ? NULL : AGX_WINDOWS_SIGNALLED_FENCE;
      return;
   }
   agx_flush_all(ctx, "Windows runtime flush");''')
    # Clear is native state mutation/upload; it uses the same batch capsule.
    a,b=function(s,'agx_clear');decl=s.rfind('static void',0,a);signature=s[decl:a]
    wrapper=signature+'''{
   struct agx_context *ctx = agx_context(pctx);
   /* Gallium clear masks: color_clear_mask carries 4 bits per colour buffer
    * (GL passes its whole ColorMask, 0xffffffff by default) and only buffer
    * 0's nibble applies here; stencil_clear_mask applies only with
    * PIPE_CLEAR_STENCIL (GL passes the stencil write mask, D3D10 passes 0).
    * Partial masks never reach pipe->clear (st/mesa clears with quads), and
    * the Asahi body ignores both masks. EXP1148: a GL glClear(COLOR|DEPTH)
    * was refused by the former D3D10-shaped guard and faulted the context.
    * Accepted: COLOR0, DEPTH and STENCIL-with-DEPTH, alone or together. */
   const unsigned supported = PIPE_CLEAR_COLOR0 | PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL;
   bool shape = buffers && !(buffers & ~supported) &&
                (!(buffers & PIPE_CLEAR_STENCIL) || (buffers & PIPE_CLEAR_DEPTH));
   bool color_ok = !(buffers & PIPE_CLEAR_COLOR0) || (color_clear_mask & 0xf) == 0xf;
   bool depth_ok = !(buffers & PIPE_CLEAR_DEPTH) || (depth >= 0.0 && depth <= 1.0);
   bool stencil_ok = !(buffers & PIPE_CLEAR_STENCIL) ||
                     stencil_clear_mask == 0 || stencil_clear_mask == 0xff;
   if (ctx->any_faults || !shape || !color_ok || !depth_ok || !stencil_ok ||
       !color || scissor_state) { ctx->any_faults = true; return; }
   struct agx_batch *batch = agx_get_batch(ctx);
   if (!batch || !AgxWin32AsahiBatchEnter(batch)) { ctx->any_faults = true; return; }
   agx_clear_windows_body(pctx, buffers, color_clear_mask, stencil_clear_mask, scissor_state, color, depth, stencil);
   if (!AgxWin32AsahiBatchLeave(batch)) ctx->any_faults = true;
}
'''
    s=s[:decl]+signature.replace('agx_clear(','agx_clear_windows_body(')+s[a:b]+'\n'+wrapper+s[b:]
    a,b=function(s,'agx_create_context');part=s[a:b]
    start=part.index('   enum drm_asahi_priority');end=part.index('   pctx->destroy',start)
    part=part[:start]+'   /* Existing Windows context owns queue and synchronization. */\n'+part[end:]
    part=part.replace('   int ret;','')
    for line in ('   pctx->create_fence_fd = agx_create_fence_fd;','   pctx->fence_server_sync = agx_fence_server_sync;','   agx_init_query_functions(pctx);'):
        part=part.replace(line,'')
    start=part.index('   struct agx_device *dev = agx_device(screen);');end=part.index('   /* By default all samples',start)
    part=part[:start]+'   ctx->in_sync_fd = -1;\n'+part[end:]
    s=s[:a]+part+s[b:]
    a,b=function(s,'agx_destroy_context');part=s[a:b]
    part=replace(part,'   agx_sync_all(ctx, "destroy context");','''   agx_sync_all(ctx, "destroy context");
   for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i)
      if (ctx->batches.slots[i].windows_batch) return; /* retains pending context */''')
    start=part.index('   /* Lock around the syncobj destruction');end=part.index('   pipe_resource_reference(&ctx->heap',start)
    part=part[:start]+part[end:]
    part=part.replace('   agx_destroy_command_queue(dev, ctx->queue_id);','')
    s=s[:a]+part+s[b:]
    # Build-only Linux entry is excluded; expose a Windows-native constructor
    # with explicit platform parameters and the existing allocation owner.
    a,b=function(s,'agx_screen_create');decl=s.rfind('struct pipe_screen *',0,a)
    old=s[decl:b]
    signature='''struct pipe_screen *
AgxWin32AsahiScreenCreate(AGX_WIN32_ASAHI_BACKEND *backend, AGX_WIN32_SCREEN *windows,
    const AGX_WIN32_ASAHI_OWNER_OPS *owners, void *owner,
    const AGX_WIN32_ASAHI_BATCH_OPS *batches,
    const struct drm_asahi_params_global *params)
'''
    original=s[a:b]
    start=original.index('   /* parse driconf');end=original.index('   screen->destroy =',start)
    original=original[:start]+'''   if (!params || params->gpu_generation != 13 || params->num_clusters_total != 1) {
      ralloc_free(agx_screen); return NULL;
   }
   if (!AgxWin32AsahiAttach(backend, &agx_screen->dev, windows, owners, owner, 0x1100000000ULL)) {
      ralloc_free(agx_screen); return NULL;
   }
   if (!AgxWin32AsahiBatchConfigure(backend, batches, owner)) {
      if (AgxWin32AsahiDetach(backend)) ralloc_free(agx_screen);
      return NULL;
   }
   agx_screen->dev.params = *params;
   agx_screen->dev.chip = AGX_CHIP_G13G;
   /* EXP1162: the Windows G4 builder encodes single-sample targets only
    * (the batch gate requires render->samples == 1); advertise no
    * multisampling, so st/mesa exposes no multisample framebuffer. */
   agx_screen->dev.debug |= AGX_DBG_NOMSAA;
   /* GL_RENDERER / adapter name: Linux builds it from the kernel's
    * parameters (agx_device.c); the compiled Windows contract is G13G. */
   snprintf(agx_screen->dev.name, sizeof(agx_screen->dev.name), "Apple M1 (G13G)");
   agx_screen->dev.libagx_programs = libagx_g13g;
   agx_screen->dev.fd = -1;
   glsl_type_singleton_init_or_ref();
   u_rwlock_init(&agx_screen->destroy_lock);
   simple_mtx_init(&agx_screen->flush_seqid_lock, mtx_plain);
   agx_screen->heap_memory_percent = 1.0f;
'''+original[end:]
    for field in ('get_screen_fd','query_dmabuf_modifiers','query_memory_info','is_dmabuf_modifier_supported','resource_from_handle','resource_get_handle','resource_get_param','get_timestamp','fence_reference','fence_finish','fence_get_fd','get_device_uuid','get_driver_uuid','get_cl_cts_version'):
        original=re.sub(r'   screen->'+field+r' = [^;]+;\n','',original)
    original=original.replace('   agx_disk_cache_init(agx_screen);','')
    original=replace(original,'   screen->destroy =','''   screen->fence_reference = agx_windows_fence_reference;
   screen->fence_finish = agx_windows_fence_finish;
   screen->destroy =''')
    original=original.replace('   agx_init_compute_caps(screen);','')
    original=replace(original,'   agx_init_screen_caps(screen);','''   agx_init_screen_caps(screen);
   struct pipe_caps *windows_caps = (struct pipe_caps *)&screen->caps;
   windows_caps->occlusion_query = false;
   windows_caps->query_timestamp = false;
   windows_caps->query_time_elapsed = false;
   windows_caps->query_so_overflow = false;
   windows_caps->query_memory_info = false;
   windows_caps->query_pipeline_statistics_single = false;
   windows_caps->query_buffer_object = false;
   windows_caps->compute = false;
   windows_caps->max_stream_output_buffers = 0;
   windows_caps->stream_output_pause_resume = false;
   windows_caps->stream_output_interleave_buffers = false;''')
    original=replace(original,'''      struct agx_bo *bo =
         agx_bo_create(&agx_screen->dev, 16384, 0, 0, "Rodata");

      agx_pack_txf_sampler((struct agx_sampler_packed *)agx_bo_map(bo));

      agx_pack(&agx_screen->dev.txf_sampler, USC_SAMPLER, cfg) {
         cfg.start = 0;
         cfg.count = 1;
         cfg.buffer = bo->va->addr;
      }

      agx_screen->rodata = bo;''','''      struct agx_bo *bo =
         agx_bo_create(&agx_screen->dev, 16384, 0, 0, "Rodata");
      void *map = bo ? agx_bo_map(bo) : NULL;
      if (!bo || !map) {
         if (bo) agx_bo_unreference(&agx_screen->dev, bo);
         screen->destroy(screen);
         return NULL;
      }

      agx_pack_txf_sampler((struct agx_sampler_packed *)map);

      agx_pack(&agx_screen->dev.txf_sampler, USC_SAMPLER, cfg) {
         cfg.start = 0;
         cfg.count = 1;
         cfg.buffer = bo->va->addr;
      }

      agx_screen->rodata = bo;

      /* EXP1069: Asahi reads unbound vertex buffers, null textures and XFB
       * offsets from a zero page and discards null-image writes into a
       * scratch page; Linux binds both at fixed VAs in every VM. Windows has
       * no such per-VM mapping (EXP1068: an unbound vertex buffer hung the
       * GPU), so each device owns one of each as ordinary BOs. */
      struct agx_bo *zero = agx_bo_create(&agx_screen->dev, AGX_ZERO_PAGE_SIZE,
                                          0, 0, "Zero page");
      struct agx_bo *scratch = agx_bo_create(&agx_screen->dev,
                                             AGX_ZERO_PAGE_SIZE, 0, 0,
                                             "Scratch page");
      void *zero_map = zero ? agx_bo_map(zero) : NULL;
      void *scratch_map = scratch ? agx_bo_map(scratch) : NULL;
      if (!zero_map || !scratch_map) {
         if (zero) agx_bo_unreference(&agx_screen->dev, zero);
         if (scratch) agx_bo_unreference(&agx_screen->dev, scratch);
         screen->destroy(screen);
         return NULL;
      }
      memset(zero_map, 0, AGX_ZERO_PAGE_SIZE);
      memset(scratch_map, 0, AGX_ZERO_PAGE_SIZE);
      agx_screen->dev.zero_bo = zero;
      agx_screen->dev.scratch_bo = scratch;''')
    s=s[:decl]+signature+original+s[b:]
    a,b=function(s,'agx_destroy_screen');part=s[a:b]
    part=part.replace('   drmSyncobjDestroy(screen->dev.fd, screen->flush_syncobj);','')
    part=replace(part,'   agx_bo_unreference(&screen->dev, screen->rodata);','''   struct agx_bo *rodata = screen->rodata;
   screen->rodata = NULL;
   /* The owner retains zero-ref backing if deallocation fails. Relinquish the
    * screen's reference once; Detach/Collect is the only retry owner. */
   agx_bo_unreference(&screen->dev, rodata);
   struct agx_bo *zero_page = screen->dev.zero_bo;
   struct agx_bo *scratch_page = screen->dev.scratch_bo;
   screen->dev.zero_bo = NULL;
   screen->dev.scratch_bo = NULL;
   if (zero_page) agx_bo_unreference(&screen->dev, zero_page);
   if (scratch_page) agx_bo_unreference(&screen->dev, scratch_page);''')
    part=replace(part,'   u_transfer_helper_destroy(pscreen->transfer_helper);','')
    part=replace(part,'   agx_close_device(&screen->dev);','''   AGX_WIN32_ASAHI_BACKEND *backend = screen->dev.windows_private;
   if (!AgxWin32AsahiDetach(backend)) return;
   u_transfer_helper_destroy(pscreen->transfer_helper);
   pscreen->transfer_helper = NULL;
   glsl_type_singleton_decref();''')
    s=s[:a]+part+s[b:]
    s=replace(s,'static struct pipe_context *\nagx_create_context(','''static void
agx_windows_set_active_query_state(struct pipe_context *pctx, bool enable)
{
   (void)pctx; (void)enable;
}

static struct pipe_context *
agx_create_context(''')
    s=replace(s,'   ctx->blitter = util_blitter_create(pctx);','''   ctx->blitter = util_blitter_create(pctx);
   pctx->set_active_query_state = agx_windows_set_active_query_state;
   if (ctx->blitter) ctx->blitter->use_single_triangle = true;''')
    save(pp,s)

    blit_path='src/gallium/drivers/asahi/agx_blit.c'
    blit=(out/blit_path).read_text()
    blit=replace(blit,'   if (asahi_compute_blit_supported(info)) {','''   if (!agx_device(pipe->screen)->windows_private &&
       asahi_compute_blit_supported(info)) {''')
    save(blit_path,blit)
