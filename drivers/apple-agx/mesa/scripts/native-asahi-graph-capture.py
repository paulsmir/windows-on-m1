"""Capture the complete supported graph at pinned native emission sites.

The transformed Mesa source remains the producer. This module adds ownership
records, typed edges and explicit subset rejection; it does not emit GPU work.
"""
import hashlib


GRAPH_HELPERS = r'''
/* Windows graph ownership, using the real native layouts above. */
#include "agx_win32_asahi_pipeline.h"
#include <stdio.h>

static AGX_WIN32_ASAHI_CAPTURE *
windows_graph_capture(struct agx_batch *batch)
{
   AGX_WIN32_ASAHI_BACKEND *b = agx_device(batch->ctx->base.screen)->windows_private;
   return b ? b->ActiveCapture : NULL;
}

static void
windows_graph_fail_at(struct agx_batch *batch, unsigned line)
{
   AGX_WIN32_ASAHI_BACKEND *b = agx_device(batch->ctx->base.screen)->windows_private;
   fprintf(stderr, "NATIVE_GRAPH_FAILURE: line=%u refs=%u relocs=%u\n", line,
       b && b->ActiveCapture ? b->ActiveCapture->Capture.ReferenceCount : 0,
       b && b->ActiveCapture ? b->ActiveCapture->Capture.RelocationCount : 0);
   if (b) b->Failed = 1;
}

#define windows_graph_fail(batch) windows_graph_fail_at(batch, __LINE__)

static int
windows_graph_field(AGX_WIN32_ASAHI_PIPELINE *scope, const void *field,
                    uint64_t address, uint64_t bytes, unsigned role)
{
   if (!address) return 1;
   unsigned index;
   uint64_t offset;
   if (!AgxWin32AsahiCaptureFind(scope->Capture, address, bytes, role, &index, &offset) &&
       AgxWin32AsahiCaptureAddress(scope->Capture, address, bytes, role,
          AppleAgxWin32AccessRead, &index) != AgxRelocOk) {
      fprintf(stderr,"NATIVE_GRAPH_FIELD: role=%u address=%llu bytes=%llu\n",role,(unsigned long long)address,(unsigned long long)bytes);
      scope->Failed = 1;
      return 0;
   }
   AgxWin32AsahiPipelineRecordRange(scope, (const uint8_t *)field + 8,
      AppleAgxWin32RelocationUniformAddress64, address, bytes, role);
   return !scope->Failed;
}

int
AgxWin32AsahiCaptureUniformBlock(struct agx_batch *batch, void *cpu,
                                uint64_t address, unsigned table)
{
   AGX_WIN32_ASAHI_CAPTURE *capture = windows_graph_capture(batch);
   if (!capture) return 1;
   struct agx_device *dev = agx_device(batch->ctx->base.screen);
   bool root = table == AGX_SYSVAL_TABLE_ROOT;
   size_t bytes = root ? sizeof(struct agx_draw_uniforms) :
                         sizeof(struct agx_stage_uniforms);
   AGX_WIN32_ASAHI_PIPELINE scope = {0};
   if (!AgxWin32AsahiEmissionBegin(dev, cpu, address, bytes,
          AppleAgxWin32RoleUniform, &scope)) goto fail;
   if (root) {
      struct agx_draw_uniforms *u = cpu;
      for (unsigned i = 0; i < AGX_NUM_SYSVAL_TABLES; ++i) {
         if (!u->tables[i]) continue;
         if (i != AGX_SYSVAL_TABLE_ROOT && i != AGX_SYSVAL_TABLE_VS &&
             i != AGX_SYSVAL_TABLE_FS && i != AGX_SYSVAL_TABLE_PARAMS) {
            fprintf(stderr,"NATIVE_ROOT_TABLE: index=%u address=%llu\n",i,(unsigned long long)u->tables[i]);
            scope.Failed = 1; break;
         }
         size_t table_bytes = i == AGX_SYSVAL_TABLE_ROOT ? sizeof(*u) :
             i == AGX_SYSVAL_TABLE_PARAMS ? 8 : sizeof(struct agx_stage_uniforms);
         windows_graph_field(&scope, &u->tables[i], u->tables[i], table_bytes,
             i == AGX_SYSVAL_TABLE_PARAMS ? AppleAgxWin32RoleConstant :
                                           AppleAgxWin32RoleUniform);
      }
      for (unsigned i = 0; i < PIPE_MAX_ATTRIBS; ++i) {
         if (!u->attrib_base[i]) continue;
         unsigned vb = batch->ctx->attributes->buffers[i];
         struct pipe_resource *resource = batch->ctx->vertex_buffers[vb].buffer.resource;
         if (!resource) { fprintf(stderr,"NATIVE_ROOT_VBO: attrib=%u vb=%u missing\n",i,vb); scope.Failed = 1; break; }
         struct agx_resource *rsrc = agx_resource(resource);
         unsigned index;
         if (AgxWin32AsahiCaptureReference(capture, rsrc->bo, AppleAgxWin32RoleVertex,
                AppleAgxWin32AccessRead, 0, rsrc->layout.size_B, &index) != AgxRelocOk) {
            fprintf(stderr,"NATIVE_ROOT_VBO: attrib=%u vb=%u capture-failed bytes=%llu\n",i,vb,(unsigned long long)rsrc->layout.size_B);
            scope.Failed = 1; break;
         }
         /* The native vertex lowering owns attribute offsets/clamps. Keep its
          * pointer into the exact VBO range, including nonzero offsets. */
         windows_graph_field(&scope, &u->attrib_base[i], u->attrib_base[i], 1,
                             AppleAgxWin32RoleVertex);
      }
      for (unsigned i = 0; i < PIPE_STAT_QUERY_MS_INVOCATIONS; ++i)
         if (u->pipeline_statistics[i]) { fprintf(stderr,"NATIVE_ROOT_QUERY: index=%u address=%llu\n",i,(unsigned long long)u->pipeline_statistics[i]); scope.Failed = 1; }
      if (u->vertex_params || u->tess_params || u->geometry_params) {
         fprintf(stderr,"NATIVE_ROOT_PARAMS: vertex=%llu tess=%llu geometry=%llu\n",(unsigned long long)u->vertex_params,(unsigned long long)u->tess_params,(unsigned long long)u->geometry_params);
         scope.Failed = 1;
      }
      windows_graph_field(&scope, &u->polygon_stipple, u->polygon_stipple,
                          sizeof(batch->ctx->poly_stipple), AppleAgxWin32RoleConstant);
   } else {
      struct agx_stage_uniforms *u = cpu;
      /* Application UBOs are admitted only from resource-backed slot zero in
       * the VS/FS tables. Native dirty-state initialization still installs
       * its real zero SSBO sink. */
      if (u->texture_base) { fprintf(stderr,"NATIVE_UNIFORM_TEXTURE: table=%u base=%llu\n",table,(unsigned long long)u->texture_base); scope.Failed = 1; }
      bool app_stage = table == AGX_SYSVAL_TABLE_VS ||
                       table == AGX_SYSVAL_TABLE_FS;
      mesa_shader_stage stage = app_stage ?
         (mesa_shader_stage)(table - AGX_SYSVAL_TABLE_VS) : MESA_SHADER_STAGES;
      struct agx_stage *native_stage = app_stage ? &batch->ctx->stage[stage] : NULL;
      for (unsigned i = 0; i < PIPE_MAX_CONSTANT_BUFFERS; ++i) {
         uint64_t base = u->ubo_base[i];
         uint32_t size = u->ubo_size[i];
         if (!base && !size) continue;
         if (!app_stage || i != 0 || !base || !size ||
             !(native_stage->cb_mask & BITFIELD_BIT(i))) {
            scope.Failed = 1; break;
         }
         struct pipe_constant_buffer *cb = &native_stage->cb[i];
         if (!cb->buffer || cb->user_buffer || cb->buffer_offset ||
             cb->buffer_size != size) { scope.Failed = 1; break; }
         struct agx_resource *rsrc = agx_resource(cb->buffer);
         if (rsrc->base.target != PIPE_BUFFER ||
             !(rsrc->base.bind & PIPE_BIND_CONSTANT_BUFFER) ||
             (rsrc->base.bind &
              ~(PIPE_BIND_CONSTANT_BUFFER | PIPE_BIND_SHADER_IMAGE)) ||
             rsrc->base.width0 != size ||
             base != agx_map_gpu(rsrc) + cb->buffer_offset) {
            scope.Failed = 1; break;
         }
         unsigned constant_index;
         if (AgxWin32AsahiCaptureReference(capture, rsrc->bo,
                AppleAgxWin32RoleConstant, AppleAgxWin32AccessRead,
                cb->buffer_offset, size, &constant_index) != AgxRelocOk ||
             !windows_graph_field(&scope, &u->ubo_base[i], base, size,
                                  AppleAgxWin32RoleConstant)) {
            scope.Failed = 1; break;
         }
         (void)constant_index;
      }
      for (unsigned i = 0; i < PIPE_MAX_SHADER_BUFFERS; ++i) {
         if (u->ssbo_size[i]) { scope.Failed = 1; break; }
         windows_graph_field(&scope, &u->ssbo_base[i], u->ssbo_base[i], 16,
                             AppleAgxWin32RoleConstant);
      }
   }
   if (AgxWin32AsahiPipelineFinish(&scope, (uint8_t *)cpu + bytes)) return 1;
fail:
   windows_graph_fail(batch);
   return 0;
}

static void
windows_graph_ppp(struct agx_batch *batch, struct agx_ptr ptr, size_t bytes,
                  const void *encoder_end)
{
   AGX_WIN32_ASAHI_CAPTURE *capture = windows_graph_capture(batch);
   if (!capture) return;
   AGX_WIN32_ASAHI_PIPELINE *encoder = capture->Backend->ActiveEmission;
   AGX_WIN32_ASAHI_PIPELINE scope = {0};
   if (!encoder || !AgxWin32AsahiEmissionBegin(capture->Backend->Native,
          ptr.cpu, ptr.gpu, bytes, AppleAgxWin32RolePppState, &scope) ||
       !AgxWin32AsahiPipelineFinish(&scope, (uint8_t *)ptr.cpu + bytes)) {
      windows_graph_fail(batch); return;
   }
   AgxWin32AsahiPipelineRecordCaptured(encoder, encoder_end,
       AppleAgxWin32RelocationPppStateAddress40, ptr.gpu, AppleAgxWin32RolePppState);
   if (encoder->Failed) windows_graph_fail(batch);
}

static void
windows_graph_attachment(struct agx_batch *batch, struct agx_ptr ptr,
                         struct agx_resource *rsrc, unsigned kind)
{
   AGX_WIN32_ASAHI_CAPTURE *capture = windows_graph_capture(batch);
   if (!capture) return;
   AGX_WIN32_ASAHI_PIPELINE scope = {0};
   unsigned index;
   if (rsrc->layout.compressed || rsrc->layout.level_offsets_B[0] ||
       rsrc->base.last_level || rsrc->base.array_size != 1 ||
       !AgxWin32AsahiEmissionBegin(capture->Backend->Native, ptr.cpu, ptr.gpu,
          AGX_TEXTURE_LENGTH, AppleAgxWin32RoleDescriptor, &scope)) {
      windows_graph_fail(batch); return;
   }
   if (AgxWin32AsahiCaptureReference(capture, rsrc->bo, AppleAgxWin32RoleRenderTarget,
       AppleAgxWin32AccessRead | AppleAgxWin32AccessWrite, 0,
       rsrc->layout.size_B, &index) != AgxRelocOk) scope.Failed = 1;
   AgxWin32AsahiPipelineRecordRange(&scope, (uint8_t *)ptr.cpu + 16, kind,
       agx_map_texture_gpu(rsrc, 0), rsrc->layout.size_B, AppleAgxWin32RoleRenderTarget);
   if (!AgxWin32AsahiPipelineFinish(&scope, (uint8_t *)ptr.cpu + AGX_TEXTURE_LENGTH))
      windows_graph_fail(batch);
}

static bool
windows_graph_index_list(struct agx_batch *batch, uint8_t *start, uint8_t *end,
                         const struct pipe_draw_info *info, uint64_t address,
                         size_t extent)
{
   AGX_WIN32_ASAHI_CAPTURE *capture = windows_graph_capture(batch);
   if (!capture || !info || info->index_size != 2 || !info->index.resource ||
       !start || end != start + 24 || extent != 8 || (address & 3) ||
       address >= (1ULL << 40)) return false;
   uint32_t *words = (uint32_t *)start;
   uint64_t encoded = ((uint64_t)(words[0] & 0xffu) << 32) | words[1];
   if ((words[0] & 0xffffff00u) != 0x61f20600u || words[2] != 3u ||
       words[3] != 1u || words[4] != 0u || words[5] != 2u ||
       encoded != address) return false;
   struct agx_resource *rsrc = agx_resource(info->index.resource);
   if (!rsrc->bo || address != agx_map_gpu(rsrc) ||
       AgxWin32RelocPromoteIndexed(&capture->Capture) != AgxRelocOk)
      return false;
   unsigned index;
   AGX_WIN32_ASAHI_PIPELINE scope = {0};
   if (AgxWin32AsahiCaptureReference(capture,rsrc->bo,
          AppleAgxWin32RoleIndex,AppleAgxWin32AccessRead,0,8,&index) !=
          AgxRelocOk ||
       !AgxWin32AsahiEncoderEmissionBeginCpu(
          capture->Backend->Native,start,24,&scope)) return false;
   AgxWin32AsahiPipelineRecordRange(&scope,start+8,
       AppleAgxWin32RelocationVdmIndexBufferAddress40,address,8,
       AppleAgxWin32RoleIndex);
   return AgxWin32AsahiPipelineFinish(&scope,end) != 0;
}

static bool
windows_graph_draw_supported(struct agx_context *ctx, const struct pipe_draw_info *info,
                             const struct pipe_draw_indirect_info *indirect,
                             const struct pipe_draw_start_count_bias *draws,
                             unsigned num_draws)
{
   struct agx_device *dev = agx_device(ctx->base.screen);
   AGX_WIN32_ASAHI_BACKEND *backend = dev->windows_private;
   if (!backend) return true;
   bool indexed = info && info->index_size != 0;
   bool valid = !backend->Failed && (!ctx->batch || !ctx->batch->draws) &&
      info && draws && !indirect && num_draws == 1 &&
      info->mode == MESA_PRIM_TRIANGLES &&
      !info->primitive_restart && info->instance_count == 1 &&
      !info->start_instance && draws->start == 0 && draws->count == 3 &&
      !draws->index_bias && ctx->framebuffer.nr_cbufs == 1 &&
      !ctx->framebuffer.zsbuf.texture && ctx->framebuffer.cbufs[0].texture &&
      ctx->framebuffer.cbufs[0].format == PIPE_FORMAT_B8G8R8A8_UNORM &&
      !ctx->framebuffer.cbufs[0].level && !ctx->framebuffer.cbufs[0].first_layer &&
      !ctx->framebuffer.cbufs[0].last_layer && !ctx->streamout.num_targets &&
      !ctx->cond_query && !ctx->occlusion_query && !ctx->time_elapsed &&
      !ctx->tf_any_overflow && !ctx->stage[MESA_SHADER_GEOMETRY].shader &&
      !ctx->stage[MESA_SHADER_TESS_CTRL].shader && !ctx->stage[MESA_SHADER_TESS_EVAL].shader &&
      ctx->stage[MESA_SHADER_VERTEX].shader && ctx->stage[MESA_SHADER_FRAGMENT].shader &&
      ctx->rast && !ctx->rast->depth_bias && ctx->attributes;
   if (valid && indexed) {
      struct pipe_resource *index = info->index.resource;
      struct agx_resource *rsrc = index ? agx_resource(index) : NULL;
      valid = info->index_size == 2 && index &&
              index->target == PIPE_BUFFER && index->width0 == 8 &&
              (index->bind & PIPE_BIND_INDEX_BUFFER) &&
              !(index->bind & ~(PIPE_BIND_INDEX_BUFFER | PIPE_BIND_SHADER_IMAGE)) &&
              rsrc->bo;
   }
   if (valid) {
      struct agx_resource *rt = agx_resource(ctx->framebuffer.cbufs[0].texture);
      valid = !rt->layout.compressed && rt->base.target == PIPE_TEXTURE_2D &&
              util_res_sample_count(&rt->base) == 1 && rt->base.array_size == 1 &&
              rt->base.last_level == 0;
   }
   /* active_queries is an enable switch initialized true, not a query count.
    * Reject real bound query objects before any unsupported producer work. */
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->pipeline_statistics); ++i)
      if (ctx->pipeline_statistics[i]) valid = false;
   for (unsigned i = 0; i < ARRAY_SIZE(ctx->prims_generated); ++i)
      if (ctx->prims_generated[i] || ctx->tf_prims_generated[i] || ctx->tf_overflow[i])
         valid = false;
   for (unsigned i = 0; i < MESA_SHADER_STAGES; ++i) {
      struct agx_stage *stage = &ctx->stage[i];
      bool app_stage = i == MESA_SHADER_VERTEX || i == MESA_SHADER_FRAGMENT;
      if (stage->texture_count || stage->image_mask || stage->ssbo_mask ||
          (app_stage ? (stage->cb_mask & ~BITFIELD_BIT(0)) : stage->cb_mask) ||
          stage->sampler_count || stage->custom_borders)
         valid = false;
   }
   if (!valid) backend->Failed = 1;
   return valid;
}
'''


def project_sources(out, project, overlays):
    state_path = out / 'src/gallium/drivers/asahi/agx_state.c'
    state = state_path.read_text()

    def replace(text, old, new):
        if text.count(old) != 1:
            raise RuntimeError('Native graph capture anchor ambiguous: ' + old[:100])
        return text.replace(old, new, 1)

    # Helpers are ownership code, compiled in the same translation unit as the
    # actual state producer, not a separately callable emission fixture.
    include = out / 'src/gallium/drivers/asahi/agx_win32_graph.inc'
    include.write_text(GRAPH_HELPERS)
    state = replace(state, 'static void\nagx_set_shader_images(',
                    '#include "agx_win32_graph.inc"\n\nstatic void\nagx_set_shader_images(')
    state = replace(state, '   agx_ppp_fini(out, &ppp);',
                    '   agx_ppp_fini(out, &ppp);\n   windows_graph_ppp(batch, T, size, *out);')
    # The dirty PPP has its own capture already. Only the initial PPP remains.
    state = replace(state, '   agx_ppp_fini(&out, &ppp);\n   batch->vdm.current = out;',
                    '   agx_ppp_fini(&out, &ppp);\n   windows_graph_ppp(batch, T, size, out);\n   batch->vdm.current = out;')
    state = replace(state,
        '   batch->uniforms.tables[AGX_SYSVAL_STAGE(stage)] =\n      agx_pool_upload_aligned(&batch->pool, unif, sizeof(*unif), 16);',
        '''   struct agx_ptr windows_stage = agx_pool_alloc_aligned(&batch->pool, sizeof(*unif), 16);
   if (!windows_stage.cpu) { windows_graph_fail(batch); return; }
   memcpy(windows_stage.cpu, unif, sizeof(*unif));
   batch->uniforms.tables[AGX_SYSVAL_STAGE(stage)] = windows_stage.gpu;
   if (!AgxWin32AsahiCaptureUniformBlock(batch, windows_stage.cpu,
          windows_stage.gpu, AGX_SYSVAL_STAGE(stage))) return;''')
    # A zero-byte pool allocation returns a nonzero slab cursor. There is no
    # descriptor object at that cursor; canonicalize the absent table at its
    # owning emission site, keeping every nonempty table subject to capture.
    state = replace(state, '   batch->stage_uniforms[stage].texture_base = T_tex.gpu;',
                    '   batch->stage_uniforms[stage].texture_base = nr_tex_descriptors ? T_tex.gpu : 0;')
    # In this admitted no-query draw, all counter consumers are absent: FS
    # statistics key and IA dispatch depend on nonnull query objects. Do not
    # carry Linux's unused fixed scratch sentinel into the Windows graph.
    state = replace(state,
        '               agx_get_query_address(batch, query);',
        '               query ? agx_get_query_address(batch, query) : 0;')
    start = state.index('static void\nagx_draw_vbo(')
    end = state.index('\n   if (unlikely(!agx_render_condition_check(ctx)))', start)
    state = state[:end] + '''
   if (!windows_graph_draw_supported(ctx, info, indirect, draws, num_draws)) return;
''' + state[end:]
    state = replace(state,
        '''   out = (void *)agx_vdm_draw((uint32_t *)out, 0 /* ignored for now */, draw,
                              agx_primitive_for_pipe(info->mode));''',
        '''   uint8_t *windows_index_list = out;
   out = (void *)agx_vdm_draw((uint32_t *)out, 0 /* ignored for now */, draw,
                              agx_primitive_for_pipe(info->mode));
   if (info->index_size && !windows_graph_index_list(
          batch,windows_index_list,out,info,ib,ib_extent)) {
      windows_graph_fail(batch); return;
   }''')
    # BG/partial/EOT preserve the real native compiler and builder.
    start = state.index('struct asahi_bg_eot\nagx_build_bg_eot(')
    end = state.index('\n/*\n * Return the standard sample positions', start)
    bg = state[start:end]
    bg = replace(bg, '   struct agx_context *ctx = batch->ctx;', '''   struct agx_context *ctx = batch->ctx;
   struct agx_device *windows_dev = agx_device(ctx->base.screen);
   AGX_WIN32_ASAHI_PIPELINE capture = {0};''')
    bg = replace(bg, '   struct agx_usc_builder b = agx_usc_builder(t.cpu, usc_size);', '''   if (!AgxWin32AsahiPipelineBegin(windows_dev, t.cpu, t.gpu, usc_size, &capture)) {
      windows_graph_fail(batch); return (struct asahi_bg_eot){0};
   }
   struct agx_usc_builder b = agx_usc_builder(t.cpu, usc_size);''')
    bg = replace(bg, '         agx_pack_texture(texture.cpu, rsrc, surf->format, &sampler_view);', '''         agx_pack_texture(texture.cpu, rsrc, surf->format, &sampler_view);
         windows_graph_attachment(batch, texture, rsrc, AppleAgxWin32RelocationTextureAddress40);''')
    bg = replace(bg, '            cfg.buffer = texture.gpu;\n         }', '''            cfg.buffer = texture.gpu;
         }
         AgxWin32AsahiPipelineRecordRange(&capture, b.head, AppleAgxWin32RelocationUscTableAddress39,
            texture.gpu, AGX_TEXTURE_LENGTH, AppleAgxWin32RoleDescriptor);''')
    bg = replace(bg, '         agx_usc_uniform(&b, 4 + (8 * rt), 8, batch->uploaded_clear_color[rt]);', '''         agx_usc_uniform(&b, 4 + (8 * rt), 8, batch->uploaded_clear_color[rt]);
         AgxWin32AsahiPipelineRecord(&capture, b.head, AppleAgxWin32RelocationUscBufferAddress40,
            batch->uploaded_clear_color[rt], 16, AppleAgxWin32RoleConstant);''')
    bg = replace(bg, '                              no_compress);', '''                              no_compress);
         windows_graph_attachment(batch, pbe, agx_resource(view.resource),
                                   AppleAgxWin32RelocationPbeAddress40);''')
    bg = replace(bg, '            cfg.buffer = pbe.gpu;\n         }', '''            cfg.buffer = pbe.gpu;
         }
         AgxWin32AsahiPipelineRecordRange(&capture, b.head, AppleAgxWin32RelocationUscTableAddress39,
            pbe.gpu, AGX_PBE_LENGTH, AppleAgxWin32RoleDescriptor);''')
    bg = replace(bg, '   if (needs_textures_for_spilled_rts) {', '''   if (needs_textures_for_spilled_rts) {
      capture.Failed = 1;
      (void)AgxWin32AsahiPipelineFinish(&capture, b.head);
      windows_graph_fail(batch);
      return (struct asahi_bg_eot){0};
   }
   if (needs_textures_for_spilled_rts) {''')
    bg = replace(bg, '         cfg.buffer = sampler.gpu;\n      }', '''         cfg.buffer = sampler.gpu;
      }
      AgxWin32AsahiPipelineRecord(&capture, b.head, AppleAgxWin32RelocationUscTableAddress39,
         sampler.gpu, AGX_SAMPLER_LENGTH, AppleAgxWin32RoleDescriptor);''')
    bg = replace(bg, '   assert(shader->info.rodata.size_16 == 0);', '''   assert(shader->info.rodata.size_16 == 0);
   unsigned windows_shader_reference;
   if (AgxWin32AsahiCaptureAddress(capture.Capture, shader->ptr,
          shader->info.binary_size, AppleAgxWin32RoleShader,
          AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
          &windows_shader_reference) != AgxRelocOk) capture.Failed = 1;''')
    bg = replace(bg, '      cfg.unk_2 = 0;\n   }', '''      cfg.unk_2 = 0;
   }
   AgxWin32AsahiPipelineRecordRange(&capture, b.head, AppleAgxWin32RelocationUscShaderOffset32,
      shader->ptr + shader->info.main_offset, shader->info.main_size, AppleAgxWin32RoleShader);''')
    bg = replace(bg, '            agx_usc_addr(dev, shader->ptr + shader->info.preamble_offset);\n      }', '''            agx_usc_addr(dev, shader->ptr + shader->info.preamble_offset);
      }
      AgxWin32AsahiPipelineRecordRange(&capture, b.head, AppleAgxWin32RelocationUscPreshaderOffset32,
         shader->ptr + shader->info.preamble_offset, 1, AppleAgxWin32RoleShader);''')
    bg = replace(bg, '   return ret;', '''   if (!AgxWin32AsahiPipelineFinish(&capture, b.head)) {
      windows_graph_fail(batch); return (struct asahi_bg_eot){0};
   }
   return ret;''')
    state = state[:start] + bg + state[end:]
    state_path.write_text(state)

    pipeline_path = out / 'src/gallium/drivers/asahi/agx_win32_pipeline.inc'
    pipeline = pipeline_path.read_text()
    pipeline = replace(pipeline,
        '''      AgxWin32AsahiPipelineRecord(&capture, b.head,
         AppleAgxWin32RelocationUscBufferAddress40,
         table_ptr + cs->push[i].offset, cs->push[i].length * 2,
         AppleAgxWin32RoleConstant);''',
        '''      if (table == AGX_SYSVAL_TABLE_PARAMS) {
         AgxWin32AsahiPipelineRecordRange(&capture, b.head,
            AppleAgxWin32RelocationUscBufferAddress40,
            table_ptr + cs->push[i].offset, cs->push[i].length * 2,
            AppleAgxWin32RoleConstant);
      } else {
         AgxWin32AsahiPipelineRecordRange(&capture, b.head,
            AppleAgxWin32RelocationUscBufferAddress40,
            table_ptr + cs->push[i].offset, cs->push[i].length * 2,
            AppleAgxWin32RoleUniform);
      }''')
    pipeline_path.write_text(pipeline)

    uniforms_path = out / 'src/gallium/drivers/asahi/agx_uniforms.c'
    uniforms = uniforms_path.read_bytes()
    expected = 'cf480b39d1fa323093673990c7127097e741247fe918e3ee295099512a0cd2ea'
    if hashlib.sha256(uniforms).hexdigest() != expected:
        raise RuntimeError('Pinned native uniforms source mismatch')
    uniforms = replace(uniforms.decode(), '#include "pool.h"',
                       '#include "pool.h"\n#include "agx_win32_asahi_pipeline.h"')
    uniforms = replace(uniforms,
        '''   struct agx_stage_uniforms *unif = &batch->stage_uniforms[stage];

   u_foreach_bit(cb, st->cb_mask) {''',
        '''   struct agx_stage_uniforms *unif = &batch->stage_uniforms[stage];

   for (unsigned cb = 0; cb < PIPE_MAX_CONSTANT_BUFFERS; ++cb) {
      unif->ubo_base[cb] = 0;
      unif->ubo_size[cb] = 0;
   }
   u_foreach_bit(cb, st->cb_mask) {''')
    uniforms = replace(uniforms, '   memcpy(root_ptr.cpu, &batch->uniforms, sizeof(batch->uniforms));',
        '''   if (!root_ptr.cpu) return;
   memcpy(root_ptr.cpu, &batch->uniforms, sizeof(batch->uniforms));
   (void)AgxWin32AsahiCaptureUniformBlock(batch, root_ptr.cpu, root_ptr.gpu,
                                       AGX_SYSVAL_TABLE_ROOT);''')
    uniforms_path.write_text(uniforms)
    for path in (state_path, pipeline_path, uniforms_path, include):
        key = path.relative_to(out).as_posix()
        overlays.setdefault(key, {})['final_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    overlays[uniforms_path.relative_to(out).as_posix()]['before'] = expected
