#!/usr/bin/env python3
"""RC33 projection onto the pinned native state producer and libagx bundle."""
import hashlib
import json
import re


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous indirect stream anchor: '+old[:100])
    return text.replace(old,new,1)


def generated(project):
    winsys=project/'drivers/apple-agx/mesa/winsys'
    directory=winsys/'indirect-stream-generated'
    manifest=json.loads((directory/'manifest.json').read_text())
    for name in ('agx_win32_indirect_stream.cl','agx_win32_indirect_stream.h'):
        if hashlib.sha256((winsys/name).read_bytes()).hexdigest()!=manifest['inputs'][name]:
            raise RuntimeError('Regenerate indirect stream kernel after changing '+name)
    for name,sha in manifest['outputs'].items():
        if hashlib.sha256((directory/name).read_bytes()).hexdigest()!=sha:
            raise RuntimeError('Indirect stream generated output changed: '+name)
    return directory


def project_library_c(text, project):
    extra=(generated(project)/'agx_win32_stream_shaders.c').read_text()
    begin=extra.index('static const uint32_t ')
    end=extra.index('const uint32_t *libagx_g13g[')
    text=once(text,'#include "libagx_shaders.h"',
              '#include "libagx_shaders.h"\n'+extra[begin:end])
    for chip in ('g13g','g13x'):
        anchor='const uint32_t *libagx_'+chip+'[LIBAGX_NUM_PROGRAMS] = {'
        entry=extra[extra.index(anchor)+len(anchor):].split('};',1)[0]
        text=once(text,anchor,anchor+entry)
    return text


def project_sources(out, project, overlays):
    def save(path,text):
        (out/path).write_text(text)
        overlays.setdefault(path,{})['after_indirect_stream']=hashlib.sha256(text.encode()).hexdigest()

    extra=(generated(project)/'agx_win32_stream_shaders.h').read_text()
    extra=extra[extra.index('struct libagx_win32_indirect_stream_args {'):]
    extra=re.sub(r'enum libagx_program \{.*?\};','',extra,flags=re.S)
    extra=extra[:extra.index('extern const uint32_t *')]
    path='src/asahi/lib/libagx_shaders.h'
    h=(out/path).read_text()
    h=once(h,'    LIBAGX_NUM_PROGRAMS,',
           '    LIBAGX_WIN32_INDIRECT_STREAM,\n    LIBAGX_NUM_PROGRAMS,')
    save(path,h+'\n'+extra)

    path='src/gallium/drivers/asahi/agx_state.h'
    h=(out/path).read_text()
    h=once(h,'      bool uses_fbfetch;',
           '      bool uses_fbfetch;\n      bool windows_uses_draw_id;')
    h=once(h,'   void *windows_draw_bindings_data;', '''   void *windows_draw_bindings_data;
   struct agx_win32_indirect_stream *windows_indirect_stream;
   unsigned windows_indirect_path, windows_indirect_reason;
   unsigned windows_indirect_encode_us;''')
    h=once(h,'struct agx_batch {', '''struct agx_batch {
   /* GPU-produced data only: never CPU-mapped or shadow-published. */
   struct agx_pool windows_stream_pool, windows_stream_usc_pool;''')
    save(path,h)

    path='src/gallium/drivers/asahi/agx_batch.c'
    s=(out/path).read_text()
    s=once(s,'   agx_pool_init(&batch->pool, dev, "Batch pool", AGX_WIN32_BO_SHADOW, true);',
           '''   agx_pool_init(&batch->windows_stream_pool, dev, "GPU indirect VDM",
                 AGX_WIN32_BO_GPU_ONLY, false);
   agx_pool_init(&batch->windows_stream_usc_pool, dev, "GPU indirect USC",
                 AGX_WIN32_BO_GPU_ONLY | AGX_BO_LOW_VA, false);
   agx_pool_init(&batch->pool, dev, "Batch pool", AGX_WIN32_BO_SHADOW, true);''')
    s=once(s,'   agx_pool_cleanup(&batch->pipeline_pool);',
           '''   agx_pool_cleanup(&batch->pipeline_pool);
   agx_pool_cleanup(&batch->windows_stream_pool);
   agx_pool_cleanup(&batch->windows_stream_usc_pool);''')
    save(path,s)

    path='src/asahi/lib/pool.c'
    s=(out/path).read_text()
    s=once(s,'#include "pool.h"','#include "pool.h"\n#include "agx_win32_asahi_bo.h"')
    s=once(s,'''   void *mapped = agx_bo_map(bo);
   if (!mapped) return (struct agx_ptr){0};''',
           '''   bool gpu_only = pool->create_flags & AGX_WIN32_BO_GPU_ONLY;
   void *mapped = gpu_only ? NULL : agx_bo_map(bo);
   if (!mapped && !gpu_only) return (struct agx_ptr){0};''')
    s=once(s,'      .cpu = (char *)mapped + offset,',
           '      .cpu = mapped ? (char *)mapped + offset : NULL,')
    save(path,s)

    path='src/gallium/drivers/asahi/agx_win32_pipeline.inc'
    s=(out/path).read_text()
    s=once(s,'      agx_usc_uniform(&b, cs->push[i].uniform, cs->push[i].length,', '''      if (phys_stage == MESA_SHADER_VERTEX && ctx->windows_indirect_stream &&
          table == AGX_SYSVAL_TABLE_PARAMS) {
         if (cs->push[i].offset || cs->push[i].length != 4) {
            AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
            return 0;
         }
         ctx->windows_indirect_stream->params_offset = b.head - (uint8_t *)t.cpu;
         ctx->windows_indirect_stream->params_uniform = cs->push[i].uniform;
      }
      agx_usc_uniform(&b, cs->push[i].uniform, cs->push[i].length,''')
    s=once(s,'   return agx_usc_addr(dev, t.gpu);', '''   if (phys_stage == MESA_SHADER_VERTEX && ctx->windows_indirect_stream) {
      ctx->windows_indirect_stream->usc_template = t.gpu;
      ctx->windows_indirect_stream->usc_bytes = b.head - (uint8_t *)t.cpu;
   }
   return agx_usc_addr(dev, t.gpu);''')
    save(path,s)

    path='src/gallium/drivers/asahi/agx_state.c'
    s=(out/path).read_text()
    s=once(s,'''   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   if (nir->info.stage == MESA_SHADER_FRAGMENT)''', '''   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   so->info.windows_uses_draw_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)''')
    s=once(s,'#include "agx_win32_pipeline.inc"',
           '#include "agx_win32_indirect_stream.h"\n#include "util/os_time.h"\n#include "agx_win32_pipeline.inc"')
    s=once(s,'#include "agx_win32_counted_indirect.h"\n\n','')
    s=once(s,'static void\nagx_draw_vbo_windows_body(',
           '#include "agx_win32_counted_indirect.h"\n\nstatic void\nagx_draw_vbo_windows_body(')
    s=once(s,'''   out = (void *)agx_vdm_draw((uint32_t *)out, 0 /* ignored for now */, draw,
                              agx_primitive_for_pipe(info->mode));''', '''   if (ctx->windows_indirect_stream) {
      out = agx_win32_emit_indirect_stream(batch, ctx->windows_indirect_stream,
                                            draw, agx_primitive_for_pipe(info->mode), out);
      if (!out) return;
   } else {
      out = (void *)agx_vdm_draw((uint32_t *)out, 0 /* ignored for now */, draw,
                                 agx_primitive_for_pipe(info->mode));
   }''')
    s=once(s,'   agx_dirty_reset_graphics(ctx);', '''   agx_dirty_reset_graphics(ctx);
   /* The last GPU-selected USC differs from the CPU's template. Restore it
    * on the next ordinary draw, even if application state is unchanged. */
   if (ctx->windows_indirect_stream)
      ctx->dirty |= AGX_DIRTY_VS;''')
    # Edit only the public wrapper; native fallback bodies remain intact.
    split=s.rindex('static void\nagx_draw_vbo(')
    prefix,s=s[:split],s[split:]
    s=once(s,'   if (indirect && indirect->indirect_draw_count) {', '''   struct agx_win32_indirect_stream stream = {0};
   struct pipe_draw_indirect_info stream_indirect;
   if (bindings && indirect && !indirect->count_from_stream_output &&
       (indirect->indirect_draw_count || indirect->draw_count > 1)) {
      unsigned reason = agx_win32_stream_fallback(ctx, info, drawid_offset, num_draws);
      ctx->windows_indirect_reason = reason;
      if (!reason) {
         if (!agx_win32_stream_inputs_valid(info, indirect)) {
            AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
            return;
         }
         ctx->windows_draw_bindings = NULL;
         agx_win32_stream_flush_inputs(ctx, indirect);
         ctx->windows_draw_bindings = bindings;
         if (ctx->any_faults) return;
         stream.indirect = indirect;
         stream.params_offset = UINT32_MAX;
         stream_indirect = *indirect;
         stream_indirect.draw_count = 1;
         stream_indirect.indirect_draw_count = NULL;
         stream_indirect.indirect_draw_count_offset = 0;
         indirect = &stream_indirect;
      } else {
         perf_debug_ctx(ctx, "GPU indirect stream fallback %u", reason);
      }
   }
   if (indirect && indirect->indirect_draw_count) {''')
    s=once(s,'         agx_win32_draw_indirect_count(ctx, info, drawid_offset, indirect, draws);',
           '         ctx->windows_indirect_path = 1;\n         agx_win32_draw_indirect_count(ctx, info, drawid_offset, indirect, draws);')
    s=once(s,'      perf_debug_ctx(ctx, "CPU counted indirect: patches");',
           '      ctx->windows_indirect_path = 2;\n      perf_debug_ctx(ctx, "CPU counted indirect: patches");')
    anchor='''   agx_draw_vbo_windows_body(pctx, info, drawid_offset, indirect, draws, num_draws);
   AgxWin32AsahiBatchTraceDraw(ctx, batch, 3u);'''
    s=once(s,anchor,'''   uint64_t encode_start = os_time_get_nano();
   if (stream.indirect) {
      ctx->windows_indirect_stream = &stream;
      ctx->dirty |= AGX_DIRTY_VS;
   }
   agx_draw_vbo_windows_body(pctx, info, drawid_offset, indirect, draws, num_draws);
   ctx->windows_indirect_stream = NULL;
   if (stream.indirect) {
      uint64_t us = (os_time_get_nano() - encode_start) / 1000u;
      ctx->windows_indirect_encode_us = MIN2(us, UINT32_MAX);
   }
   AgxWin32AsahiBatchTraceDraw(ctx, batch, 3u);''')
    save(path,prefix+s)
