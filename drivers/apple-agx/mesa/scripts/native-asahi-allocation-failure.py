"""Checked Windows allocation consumers in the pinned native Gallium producer.

External MIT bodies stay in the prepared tree. Refuse changed anchors rather
than silently omit a failure edge. A pool failure is terminal for this context;
its owned allocations remain attached to the batch for normal abort/cleanup.
"""
import hashlib
import re


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Allocation failure anchor changed: '+old[:100])
    return text.replace(old, new, 1)


def bounds(text, name):
    start = re.search(r'\n'+name+r'\([^;]*?\n\{', text).end()
    # Pinned native bodies close at column zero.
    return start, text.index('\n}', start)


def edit(text, name, old, new):
    a,b=bounds(text,name)
    return text[:a]+once(text[a:b],old,new)+text[b:]


POOL_FUNCTIONS = {
 'agx_state.c': {
  'agx_upload_viewport_scissor':'', 'agx_upload_textures':'',
  'agx_upload_samplers':'', 'agx_update_descriptors':'', 'agx_launch_precomp':'',
  'agx_build_bg_eot':'(struct asahi_bg_eot){0}', 'agx_batch_init_state':'',
  'agx_encode_state':'NULL', 'agx_index_buffer_direct_ptr':'0',
  'agx_ensure_cmdbuf_has_space':'', 'agx_batch_heap':'0',
  'agx_batch_geometry_params':'0', 'agx_draw_without_restart':'',
  'agx_upload_draw_params':'', 'agx_draw_patches':'',
  'agx_draw_vbo_windows_body':'', 'agx_launch':'', 'agx_decompress_inplace':''},
 'agx_uniforms.c': {'agx_upload_uniforms':'', 'agx_set_ssbo_uniforms':''},
 'agx_pipe.c': {'agx_clear_windows_body':'', 'agx_flush_render':''},
}


def checked_pool_functions(text, filename):
    for name, result in POOL_FUNCTIONS[filename].items():
        a,b=bounds(text,name); part=text[a:b]
        if name=='agx_build_bg_eot':
            part=once(part, '''      agx_usc_uniform(&b, 0, 4,
                      agx_pool_upload_aligned(&batch->pool, &descs.gpu, 8, 8));''',
                '''      uint64_t descriptors = agx_pool_upload_aligned(&batch->pool, &descs.gpu, 8, 8);
      agx_usc_uniform(&b, 0, 4, descriptors);''')
        pattern = (r'(?m)^(?P<indent> +)(?P<lhs>(?:struct agx_ptr |uint64_t )?'
                   r'[\w.>\-\[\]()]+\s*=\s*|return\s+)'
                   r'(?P<call>agx_pool_(?:alloc|upload)\w*\([^;]+);')
        expected=len(re.findall(r'agx_pool_(?:alloc|upload)\w*\(',part))
        def guard(m):
            indent=m['indent']; lhs=m['lhs']; call=m['call']
            isptr=lhs.startswith('struct agx_ptr ')
            if lhs.startswith('return'):
                var='windows_upload'; statement='uint64_t '+var+' = '+call+';'
            else:
                var=lhs.split('=')[0].strip().removeprefix('struct agx_ptr ').removeprefix('uint64_t ')
                statement=lhs+call+';'
            check=f'!{var}.cpu || !{var}.gpu' if isptr else f'!{var}'
            failure=(f'\n{indent}if ({check}) {{\n'
                     f'{indent}   AgxWin32AsahiMarkContextFault(batch->ctx, (5u << 16) | __LINE__);\n'
                     f'{indent}   return {result};\n{indent}}}')
            return indent+statement+failure+(f'\n{indent}return {var};' if lhs.startswith('return') else '')
        part,count=re.subn(pattern,guard,part)
        if count!=expected or not count:
            raise RuntimeError(f'Unclassified pool consumer {name}: {count}/{expected}')
        text=text[:a]+part+text[b:]
    return text


def project_sources(out, project, overlays):
    base='src/gallium/drivers/asahi/'
    def save(path,text):
        (out/path).write_text(text)
        overlays.setdefault(path,{})['after_allocation_failure']=hashlib.sha256(text.encode()).hexdigest()
    for filename in POOL_FUNCTIONS:
        path=base+filename
        text=(out/path).read_text()
        if filename=='agx_uniforms.c':
            text=once(text,'#include "agx_state.h"','#include "agx_state.h"\n#include "agx_win32_asahi_bo.h"')
        save(path,checked_pool_functions(text,filename))
    path=base+'agx_state.c';s=(out/path).read_text()
    for kind in ('agx_blend','agx_zsa','agx_rasterizer','agx_sampler_state','agx_sampler_view'):
        old=f'   struct {kind} *so = CALLOC_STRUCT({kind});'
        s=once(s,old,old+'\n   if (!so) return NULL;')
    old='   struct agx_vertex_elements *so = calloc(1, sizeof(*so));'
    s=once(s,old,old+'\n   if (!so) return NULL;')
    s=edit(s,'agx_sampler_heap_add','''      assert(heap->count == 0);''','''      if (!heap->bo)
         return UINT16_MAX;
      assert(heap->count == 0);''')
    s=edit(s,'agx_sampler_heap_add','''   assert(heap->count < AGX_SAMPLER_HEAP_SIZE);''','''   if (heap->count >= AGX_SAMPLER_HEAP_SIZE)
      return UINT16_MAX;''')
    s=edit(s,'agx_sampler_heap_add','''   struct agx_sampler_packed *samplers = agx_bo_map(heap->bo);''','''   struct agx_sampler_packed *samplers = agx_bo_map(heap->bo);
   if (!samplers)
      return UINT16_MAX;''')
    save(path,s)
    path=base+'agx_batch.c';s=(out/path).read_text()
    s=edit(s,'agx_encoder_allocate','''   uint8_t *map = agx_bo_map(bo);''','''   uint8_t *map = agx_bo_map(bo);
   if (!map) {
      agx_bo_unreference(dev, bo);
      return (struct agx_encoder){0};
   }''')
    s=edit(s,'agx_get_batch','''   if (!ctx->batch) return NULL;''','''   if (ctx->any_faults) return NULL;
   if (!ctx->batch || !ctx->batch->vdm.bo || !ctx->batch->vdm.current) {
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      return NULL;
   }''')
    a,b=bounds(s,'agx_get_batch');s=s[:a]+'\n   if (ctx->any_faults) return NULL;'+s[a:]
    save(path,s)
    path=base+'agx_uniforms.c';s=(out/path).read_text()
    s=once(s,'''         unif->sampler_handle[s] =
            28 +
            agx_sampler_heap_add(dev, &batch->sampler_heap,
                                 &st->samplers[s]->desc_without_custom_border);''','''         uint16_t index = agx_sampler_heap_add(dev, &batch->sampler_heap,
                                 &st->samplers[s]->desc_without_custom_border);
         if (index >= AGX_SAMPLER_HEAP_SIZE) {
            AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
            return;
         }
         unif->sampler_handle[s] = 28 + index;''')
    save(path,s)
    path=base+'agx_streamout.c';s=(out/path).read_text()
    s=once(s,'''   return &target->base;''','''   if (!target->offset) {
      pipe_resource_reference(&target->base.buffer, NULL);
      ralloc_free(target);
      return NULL;
   }
   return &target->base;''')
    save(path,s)
    path=base+'agx_pipe.c';s=(out/path).read_text()
    s=once(s,'''      memcpy(agx_bo_map(new_), agx_bo_map(old), size);''','''      void *dst = agx_bo_map(new_);
      void *src = agx_bo_map(old);
      if (!dst || !src) {
         agx_bo_unreference(dev, new_);
         AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
         return false;
      }
      memcpy(dst, src, size);''')
    s=edit(s,'agx_transfer_map','''   agx_prepare_for_map(ctx, rsrc, level, usage, box, staging_blit);''','''   *out_transfer = NULL;
   agx_prepare_for_map(ctx, rsrc, level, usage, box, staging_blit);
   if (ctx->any_faults) return NULL;''')
    s=edit(s,'agx_transfer_map','''         agx_sync_writer(ctx, staging, "GPU read staging blit");''','''         agx_sync_writer(ctx, staging, "GPU read staging blit");
         if (ctx->any_faults) goto map_failed;''')
    save(path,s)

    # Stop the encoding call chain after a callee faults the context. These
    # functions own no independent temporary allocations at these edges; batch
    # abort owns pool/encoder storage. Outer Enter/Leave wrappers stay intact.
    calls = set().union(*(v.keys() for v in POOL_FUNCTIONS.values()))
    calls |= {'agx_set_sampler_uniforms', 'agx_ia_update', 'agx_launch_gs_prerast',
              'agx_index_buffer_ptr', 'agx_launch_precomp'}
    returns = dict(POOL_FUNCTIONS['agx_state.c'])
    returns.update({'agx_ia_update':'', 'agx_launch_gs_prerast':'',
                    'agx_launch_grid':'', 'agx_legalize_feedback_loop_surf':''})
    for filename in ('agx_state.c','agx_pipe.c'):
        path=base+filename;s=(out/path).read_text()
        parents = returns if filename=='agx_state.c' else {
            'agx_clear_windows_body':'', 'agx_flush_render':'', 'asahi_clear_buffer':''}
        for name,result in parents.items():
            a,b=bounds(s,name);part=s[a:b]
            edits=[]
            for m in re.finditer(r'\b('+ '|'.join(sorted(calls))+r')\(',part):
                line=part[part.rfind('\n',0,m.start())+1:m.start()]
                if 'return ' in line or 'assert(' in line:
                    continue
                end=part.index(';',m.end())+1
                if end not in edits: edits.append(end)
            for end in reversed(edits):
                ctx='ctx' if name in ('agx_legalize_feedback_loop_surf',) else 'batch->ctx'
                part=part[:end]+f'\n   if ({ctx}->any_faults) return {result};'+part[end:]
            s=s[:a]+part+s[b:]
        save(path,s)
    path=base+'agx_state.c';s=(out/path).read_text()
    # Every lazy CDM acquisition precedes a CPU encoder write.
    old='      batch->cdm = agx_encoder_allocate(batch, dev);'
    replacement=old+'''
      if (!batch->cdm.bo || !batch->cdm.current) {
         AgxWin32AsahiMarkContextFault(batch->ctx, (5u << 16) | __LINE__);
         return;
      }'''
    if s.count(old)!=5:
        raise RuntimeError('Lazy CDM consumer count changed')
    s=s.replace(old,replacement)
    # Graphics acquisition is now fallible just like compute acquisition.
    for name in ('agx_draw_patches','agx_legalize_feedback_loop_surf',
                 'agx_draw_vbo_windows_body'):
        a,b=bounds(s,name);part=s[a:b]
        old='struct agx_batch *batch = agx_get_batch(ctx);'
        part=once(part,old,old+'\n   if (!batch) return;')
        s=s[:a]+part+s[b:]
    s=edit(s,'agx_launch_precomp',
        '      agx_get_precompiled(&batch->ctx->bg_eot, program);',
        '''      agx_get_precompiled(&batch->ctx->bg_eot, program);
   if (!cs) {
      AgxWin32AsahiMarkContextFault(batch->ctx, (5u << 16) | __LINE__);
      return;
   }''')
    s=edit(s,'agx_build_bg_eot',
        '   struct agx_bg_eot_shader *shader = agx_get_bg_eot_shader(&ctx->bg_eot, &key);',
        '''   struct agx_bg_eot_shader *shader = agx_get_bg_eot_shader(&ctx->bg_eot, &key);
   if (!shader) {
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      return (struct asahi_bg_eot){0};
   }''')
    s=edit(s,'asahi_fast_link',
        '                 &prolog->b, &epilog->b, key->nr_samples_shaded);',
        '''                 prolog ? &prolog->b : NULL, epilog ? &epilog->b : NULL,
                 key->nr_samples_shaded);
   if (!linked->bo) {
      ralloc_free(linked);
      agx_win32_shader_failed(ctx);
      return NULL;
   }''')
    s=edit(s,'asahi_fast_link',
        '   _mesa_hash_table_insert(so->linked_shaders, cloned_key, linked);',
        '''   if (!cloned_key || !_mesa_hash_table_insert(so->linked_shaders, cloned_key, linked)) {
      ralloc_free(cloned_key);
      agx_bo_unreference(dev, linked->bo);
      ralloc_free(linked);
      agx_win32_shader_failed(ctx);
      return NULL;
   }''')
    s=edit(s,'agx_build_meta_shader_internal',
        '   memcpy(cloned_key, key, total_key_size);',
        '''   if (!cloned_key) {
      agx_delete_compiled_shader(dev, shader);
      return NULL;
   }
   memcpy(cloned_key, key, total_key_size);''')
    s=edit(s,'agx_build_meta_shader_internal',
        '   _mesa_hash_table_insert(ctx->generic_meta, cloned_key, shader);',
        '''   if (!_mesa_hash_table_insert(ctx->generic_meta, cloned_key, shader)) {
      ralloc_free(cloned_key);
      agx_delete_compiled_shader(dev, shader);
      return NULL;
   }''')
    save(path,s)
    path=base+'agx_win32_pipeline.inc';s=(out/path).read_text()
    s=once(s,'   AGX_WIN32_ASAHI_PIPELINE capture;', '''   if (!t.cpu || !t.gpu) {
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      return 0;
   }
   AGX_WIN32_ASAHI_PIPELINE capture;''')
    for stage in ('fs','vs','cs'):
        old=f'         agx_scratch_alloc(&ctx->scratch_{stage}, max_scratch_size, max_subgroups);'
        s=once(s,old,old+f'''
         if (!ctx->scratch_{stage}.buf || !ctx->scratch_{stage}.header) {{
            capture.Failed = 1;
            (void)AgxWin32AsahiPipelineFinish(&capture, b.head);
            AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
            return 0;
         }}''')
    save(path,s)
    path=base+'agx_pipe.c';s=(out/path).read_text()
    # Render finalization may fail after Enter. Always Leave, then abort without
    # reading or submitting the incomplete command descriptor.
    s=once(s,'   agx_flush_render(ctx, batch, &render);','''   memset(&render, 0, sizeof(render));
   agx_flush_render(ctx, batch, &render);''')
    s=once(s,'   agx_batch_submit(ctx, batch, compute, &render);','''   if (ctx->any_faults) {
      agx_batch_reset(ctx, batch);
      return;
   }
   agx_batch_submit(ctx, batch, compute, &render);''')
    for field in ('c->vertex_helper.binary','c->fragment_helper.binary','cmdbuf->helper.binary'):
        old=f'      {field} = agx_helper_program(&batch->ctx->bg_eot);'
        s=once(s,old,old+f'''
      if (!{field}) {{
         AgxWin32AsahiMarkContextFault(batch->ctx, (5u << 16) | __LINE__);
         return;
      }}
      /* The helper address bypasses normal shader binding. Its cached BO
       * must join this consuming batch even when scratch was allocated by
       * an earlier batch; a cache pointer is not a Windows residency hold. */
      agx_batch_add_bo(batch,
         agx_get_precompiled(&batch->ctx->bg_eot, LIBAGX_HELPER)->bo);''')
    # Complete context construction or unwind all initialized components.
    s=once(s,'''   struct pipe_context *pctx = &ctx->base;


   if (!ctx)
      return NULL;''','''   if (!ctx)
      return NULL;
   struct pipe_context *pctx = &ctx->base;''')
    s=edit(s,'agx_create_context','''      FREE(pctx);''','''      ralloc_free(ctx);''')
    old='   agx_scratch_init(agx_device(screen), &ctx->scratch_cs);'
    s=once(s,old,old+'''
   if (!ctx->timestamps || !ctx->blitter || !ctx->compute_blitter.blit_cs ||
       !ctx->generic_meta || !ctx->bg_eot.ht) {
      agx_destroy_context(pctx);
      return NULL;
   }''')
    save(path,s)
    # Failure declarations must precede early state and uniform consumers.
    for filename in ('agx_state.c','agx_uniforms.c'):
        path=base+filename;s=(out/path).read_text()
        s=once(s,'#include "agx_state.h"','#include "agx_state.h"\n#include "agx_win32_asahi_batch.h"')
        save(path,s)
    path=base+'agx_state.h';s=(out/path).read_text()
    old='''      batch->bo_list.set = rerzalloc(
         batch->ctx, batch->bo_list.set, BITSET_WORD,
         batch->bo_list.bit_count / bits_per_word, bit_count / bits_per_word);
      batch->bo_list.bit_count = bit_count;'''
    s=once(s,old,'''      BITSET_WORD *grown = rerzalloc(
         batch->ctx, batch->bo_list.set, BITSET_WORD,
         batch->bo_list.bit_count / bits_per_word, bit_count / bits_per_word);
      if (!grown) {
         batch->ctx->any_faults = true;
         return;
      }
      batch->bo_list.set = grown;
      batch->bo_list.bit_count = bit_count;''')
    s=edit(s,'agx_batch_add_bo_internal','''   /* Double the size''','''   if (!bo || batch->ctx->any_faults) {
      batch->ctx->any_faults = true;
      return;
   }
   /* Double the size''')
    s=once(s,'''   assert(agx_batch_uses_bo(batch, bo));''','''   if (batch->ctx->any_faults) return;
   assert(agx_batch_uses_bo(batch, bo));''')
    save(path,s)
    path=base+'agx_batch.c';s=(out/path).read_text()
    s=once(s,'''      batch->bo_list.bit_count = 128 * sizeof(BITSET_WORD) * 8;''','''      batch->bo_list.bit_count = batch->bo_list.set ? 128 * sizeof(BITSET_WORD) * 8 : 0;
      if (!batch->bo_list.set)
         AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);''')
    # Leave initialization/active ownership intact so ordinary batch abort can
    # release the rodata, encoders and pools even after failed initial bitset.
    s=once(s,'''   if (!AgxWin32AsahiBatchBegin(batch)) {''','''   if (ctx->any_faults || !AgxWin32AsahiBatchBegin(batch)) {''')
    s=edit(s,'agx_batch_timestamps',
        '   struct agx_timestamps *ts = agx_bo_map(batch->ctx->timestamps);',
        '''   struct agx_timestamps *ts = agx_bo_map(batch->ctx->timestamps);
   if (!ts) {
      AgxWin32AsahiMarkContextFault(batch->ctx, (5u << 16) | __LINE__);
      return NULL;
   }''')
    s=edit(s,'agx_batch_print_stats',
        '   struct agx_timestamps *ts = agx_batch_timestamps(batch);',
        '   struct agx_timestamps *ts = agx_batch_timestamps(batch);\n   if (!ts) return;')
    s=edit(s,'agx_batch_cleanup','''      if (batch->cdm.bo) {''','''      if (ts && batch->cdm.bo) {''')
    s=edit(s,'agx_batch_cleanup','''      if (batch->vdm.bo) {''','''      if (ts && batch->vdm.bo) {''')
    save(path,s)
    path=base+'agx_state.c';s=(out/path).read_text()
    s=edit(s,'agx_destroy_compute_blitter',
        '   hash_table_foreach(bl->blit_cs, ent) {',
        '   if (bl->blit_cs)\n   hash_table_foreach(bl->blit_cs, ent) {')
    s=edit(s,'agx_destroy_meta_shaders',
        '   hash_table_foreach(ctx->generic_meta, ent) {',
        '   if (ctx->generic_meta)\n   hash_table_foreach(ctx->generic_meta, ent) {')
    s=edit(s,'agx_batch_heap',
        '                                        PIPE_USAGE_DEFAULT, size);',
        '''                                        PIPE_USAGE_DEFAULT, size);
         if (!ctx->heap) {
            AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
            return 0;
         }''')
    # The GS helper owns an entered compute scope. Failure must unwind it.
    a,b=bounds(s,'agx_launch_gs_prerast');part=s[a:b]
    part=part.replace('if (batch->ctx->any_faults) return ;',
                      'if (batch->ctx->any_faults) goto windows_compute_done;')
    part=once(part,'   if (!AgxWin32AsahiBatchComputeLeave(batch)) {',
                   'windows_compute_done:\n   if (!AgxWin32AsahiBatchComputeLeave(batch)) {')
    s=s[:a]+part+s[b:]
    # A state emission is stack-owned. Restore its parent before unwinding;
    # all new failure returns occur before the nested PPP capture starts.
    a,b=bounds(s,'agx_encode_state');part=s[a:b]
    part=part.replace('return NULL;', 'goto windows_state_failed;')
    pos=part.rfind('   return out;')
    part=part[:pos]+part[pos:].replace('   return out;', '''   return out;
windows_state_failed:
   if (capture_active) {
      state_capture.Failed = 1;
      (void)AgxWin32AsahiPipelineFinish(&state_capture, out);
   }
   return NULL;''',1)
    s=s[:a]+part+s[b:]
    s=edit(s,'agx_launch',
        '                                     variable_shared_mem, subgroups_per_core);',
        '''                                     variable_shared_mem, subgroups_per_core);
   if (!usc || ctx->any_faults) {
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      return;
   }''')
    save(path,s)
    path=base+'agx_pipe.c';s=(out/path).read_text()
    s=edit(s,'agx_invalidate_resource',
        '   struct agx_batch *batch = agx_get_batch(ctx);',
        '   struct agx_batch *batch = agx_get_batch(ctx);\n   if (!batch) return;')
    save(path,s)
    path=base+'agx_state.h';s=(out/path).read_text()
    s=edit(s,'agx_writer_add',
        '''      memset(util_dynarray_grow(&ctx->writer, uint8_t, grow), 0,
             grow * sizeof(uint8_t));''',
        '''      uint8_t *added = util_dynarray_grow(&ctx->writer, uint8_t, grow);
      if (!added) {
         ctx->any_faults = true;
         return;
      }
      memset(added, 0, grow * sizeof(uint8_t));''')
    save(path,s)
    path=base+'agx_batch.c';s=(out/path).read_text()
    s=edit(s,'agx_batch_reads',
        '   /* Hazard: read-after-write */',
        '   if (batch->ctx->any_faults) return;\n\n   /* Hazard: read-after-write */')
    s=edit(s,'agx_batch_writes_internal',
        '   agx_batch_reads(batch, rsrc);',
        '   agx_batch_reads(batch, rsrc);\n   if (ctx->any_faults) return;')
    save(path,s)
    path=base+'agx_state.c';s=(out/path).read_text()
    for name,old,var in (
        ('agx_upload_viewport_scissor',
         '      util_dynarray_grow_bytes(&batch->scissor, count, AGX_SCISSOR_LENGTH);','scissors'),
        ('agx_upload_depth_bias',
         '      util_dynarray_grow_bytes(&batch->depth_bias, 1, AGX_DEPTH_BIAS_LENGTH);','ptr')):
        s=edit(s,name,old,old+f'''
   if (!{var}) {{
      AgxWin32AsahiMarkContextFault(batch->ctx, (5u << 16) | __LINE__);
      return;
   }}''')
    s=edit(s,'agx_encode_state',
        '      agx_upload_depth_bias(batch, &ctx->rast->base);',
        '      agx_upload_depth_bias(batch, &ctx->rast->base);\n   if (ctx->any_faults) goto windows_state_failed;')
    s=edit(s,'agx_set_constant_buffer',
        '                        &constants->buffer);',
        '''                        &constants->buffer);
      if (!constants->buffer) {
         AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
         return;
      }''')
    a,b=bounds(s,'agx_encode_state');part=s[a:b]
    part=once(part,'   AGX_WIN32_ASAHI_PIPELINE state_capture = {0};',
        '''   AGX_WIN32_ASAHI_PIPELINE state_capture = {0};
   AGX_WIN32_ASAHI_PIPELINE *nested_capture = NULL;
   void *nested_end = NULL;''')
    for stage,anchor in (
        ('vs','                               MESA_SHADER_VERTEX, 0, 0);'),
        ('fs','                                           MESA_SHADER_FRAGMENT, 0, 0);')):
        part=once(part,anchor,anchor+f'''
         if (!windows_{stage}_pipeline || ctx->any_faults) {{
            AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
            goto windows_state_failed;
         }}''')
    part=once(part,'''   if (dirty.fragment_control) {''','''   if (capture_active) {
      nested_capture = &ppp_capture;
      nested_end = ppp.head;
   }
   if (dirty.fragment_control) {''')
    part=once(part,'''   agx_ppp_fini(&out, &ppp);''','''   nested_capture = NULL;
   agx_ppp_fini(&out, &ppp);''')
    part=once(part,'''windows_state_failed:
   if (capture_active) {''','''windows_state_failed:
   if (nested_capture) {
      nested_capture->Failed = 1;
      (void)AgxWin32AsahiPipelineFinish(nested_capture, nested_end);
   }
   if (capture_active) {''')
    s=s[:a]+part+s[b:]
    save(path,s)
    path=base+'agx_state.c';s=(out/path).read_text()
    s=edit(s,'agx_create_shader_state',
        '   /* The D3D10 frontend carries stream output beside TGSI.',
        '''   if (!nir) {
      ralloc_free(so);
      return NULL;
   }
   /* The D3D10 frontend carries stream output beside TGSI.''')
    old='''   if (nir->info.stage == MESA_SHADER_TESS_EVAL ||
       nir->info.stage == MESA_SHADER_TESS_CTRL) {'''
    s=edit(s,'agx_create_shader_state',old,'''   bool needs_linked = nir->info.stage == MESA_SHADER_VERTEX ||
                       nir->info.stage == MESA_SHADER_TESS_EVAL ||
                       nir->info.stage == MESA_SHADER_FRAGMENT;
   if (!so->variants || (needs_linked && !so->linked_shaders)) {
      _mesa_hash_table_destroy(so->variants, NULL);
      ralloc_free(nir);
      ralloc_free(so);
      return NULL;
   }

'''+old)
    s=edit(s,'agx_create_compute_state',
        '   union asahi_shader_key key = {0};',
        '''   if (!so->variants) {
      if (cso->ir_type == PIPE_SHADER_IR_NIR)
         ralloc_free((void *)cso->prog);
      ralloc_free(so);
      return NULL;
   }
   union asahi_shader_key key = {0};''')
    s=edit(s,'agx_get_shader_variant',
        '   _mesa_hash_table_insert(so->variants, cloned_key, compiled);',
        '''   if (!_mesa_hash_table_insert(so->variants, cloned_key, compiled)) {
      ralloc_free(cloned_key);
      agx_delete_compiled_shader(&screen->dev, compiled);
      return NULL;
   }''')
    save(path,s)
    path=base+'agx_query.c';s=(out/path).read_text()
    s=once(s,'      util_dynarray_append(&batch->timestamps, q->ptr);',
        '''      struct agx_ptr *entry = util_dynarray_grow(&batch->timestamps, struct agx_ptr, 1);
      if (!entry) {
         AgxWin32AsahiMarkContextFault(batch->ctx, (5u << 16) | __LINE__);
         return;
      }
      *entry = q->ptr;''')
    # Query already includes the batch failure declaration in the lifecycle.
    save(path,s)
    path='src/asahi/lib/pool.c';s=(out/path).read_text()
    s=once(s,'   util_dynarray_append(&pool->bos, bo);',
        '''   struct agx_bo **entry = util_dynarray_grow(&pool->bos, struct agx_bo *, 1);
   if (!entry) {
      agx_bo_unreference(pool->dev, bo);
      return NULL;
   }
   *entry = bo;''')
    save(path,s)
    path=base+'agx_pipe.c';s=(out/path).read_text()
    s=edit(s,'agx_flush_compute','   /* Finalize the encoder */',
        '''   uint8_t *encoder_map = batch->cdm.bo ? agx_bo_map(batch->cdm.bo) : NULL;
   if (!encoder_map || !batch->cdm.current || ctx->any_faults) {
      AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
      return;
   }
   /* Finalize the encoder */''')
    s=once(s,'(batch->cdm.current - (uint8_t *)agx_bo_map(batch->cdm.bo))',
               '(batch->cdm.current - encoder_map)')
    old='''      if (!AgxWin32AsahiBatchComputeLeave(batch) ||
          !AgxWin32AsahiBatchComputeFinalize(batch, batch->cdm.current)) {'''
    s=once(s,old,'''      if (!AgxWin32AsahiBatchComputeLeave(batch)) {
         AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
         return;
      }
      if (ctx->any_faults) {
         agx_batch_reset(ctx, batch);
         return;
      }
      if (!AgxWin32AsahiBatchComputeFinalize(batch, batch->cdm.current)) {''')
    s=once(s,'      if (!AgxWin32AsahiBatchComputeFinalize(batch,batch->cdm.current)) {',
        '''      if (ctx->any_faults) {
         agx_batch_reset(ctx, batch);
         return;
      }
      if (!AgxWin32AsahiBatchComputeFinalize(batch,batch->cdm.current)) {''')
    save(path,s)
    path=base+'agx_state.c';s=(out/path).read_text()
    s=edit(s,'util_draw_multi_upload_indirect',
        '   struct pipe_draw_indirect_info indirect_ = *indirect;',
        '''   struct pipe_draw_indirect_info indirect_ = *indirect;
   /* The incoming count buffer is borrowed; the upload result is our ref. */
   indirect_.indirect_draw_count = NULL;''')
    s=edit(s,'util_draw_multi_upload_indirect',
        '   pctx->draw_vbo(pctx, info, 0, &indirect_, draws, 1);',
        '''   if (!indirect_.indirect_draw_count) {
      AgxWin32AsahiMarkContextFault(agx_context(pctx), (5u << 16) | __LINE__);
      return;
   }
   pctx->draw_vbo(pctx, info, 0, &indirect_, draws, 1);
   pipe_resource_reference(&indirect_.indirect_draw_count, NULL);''')
    a,b=bounds(s,'agx_draw_without_restart');part=s[a:b]
    old='                        &indirect_synthesized.buffer);'
    part=once(part,old,old+'''
      if (!indirect_synthesized.buffer) {
         AgxWin32AsahiMarkContextFault(ctx, (5u << 16) | __LINE__);
         goto windows_restart_done;
      }''')
    start=part.index('   struct pipe_draw_indirect_info indirect_synthesized')
    part=part[:start]+part[start:].replace('return ;','goto windows_restart_done;').replace('return;','goto windows_restart_done;')
    part+='''
windows_restart_done:
   pipe_resource_reference(&indirect_synthesized.buffer, NULL);
'''
    s=s[:a]+part+s[b:]
    # Formatting is kept conventional in generated C as well as the projector.
    s=s.replace('return ;','return;')
    save(path,s)
    # These also compile as independent translation units (the pipeline include
    # is shared by native_pipeline_contract.c), not only within agx_state.c.
    for filename,anchor in (
            ('agx_query.c','#include "agx_state.h"'),
            # EXP1929 x86 build: agx_blit.c calls AgxWin32AsahiMarkContextFault too.
            ('agx_blit.c','#include "agx_state.h"'),
            ('agx_win32_pipeline.inc','#include "agx_win32_asahi_pipeline.h"')):
        path=base+filename;s=(out/path).read_text()
        s=once(s,anchor,anchor+'\n#include "agx_win32_asahi_batch.h"')
        save(path,s)
