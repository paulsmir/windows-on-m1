"""Project checked compute-blit allocations and balanced saved-state ownership.

Only the pinned external agx_blit.c body is transformed. Hash-cache admission
owns a newly created shader/key only after insertion succeeds; any refusal
marks the context and restores the caller's binding state before returning.
"""
import hashlib


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Compute blit allocation anchor changed: '+old[:100])
    return text.replace(old, new, 1)


def project_sources(out, project, overlays):
    path = 'src/gallium/drivers/asahi/agx_blit.c'
    text = (out/path).read_text()
    # These projected setters take references: the saved references are ours,
    # not transferred to set_constant_buffer/set_sampler_views.
    text = once(text, '''   pipe_sampler_view_reference(&blitter->saved_sampler_view,
                               &stage->textures[0]->base);''', '''   pipe_sampler_view_reference(&blitter->saved_sampler_view,
                               stage->textures[0] ? &stage->textures[0]->base : NULL);''')
    text = once(text, '''   /* take_ownership=true so do not unreference */
   pctx->set_constant_buffer(pctx, MESA_SHADER_COMPUTE, 0, &blitter->saved_cb);
   blitter->saved_cb.buffer = NULL;''', '''   pctx->set_constant_buffer(pctx, MESA_SHADER_COMPUTE, 0, &blitter->saved_cb);
   pipe_resource_reference(&blitter->saved_cb.buffer, NULL);''')
    text = once(text, '''      blitter->saved_sampler_view = NULL;''',
                '''      pipe_sampler_view_reference(&blitter->saved_sampler_view, NULL);''')
    text = once(text, '''   pctx->bind_compute_state(pctx, blitter->saved_cs);''', '''   if (!blitter->saved_num_sampler_states) {
      void *none = NULL;
      pctx->bind_sampler_states(pctx, MESA_SHADER_COMPUTE, 0, 1, &none);
   }

   pctx->bind_compute_state(pctx, blitter->saved_cs);''')
    text = once(text, '''   assert(asahi_compute_blit_supported(info));
   asahi_compute_save(agx_context(ctx));''', '''   if (agx_context(ctx)->any_faults)
      return;
   assert(asahi_compute_blit_supported(info));
   asahi_compute_save(agx_context(ctx));''')
    text = once(text, '''   ctx->set_constant_buffer(ctx, MESA_SHADER_COMPUTE, 0, &cb);''', '''   ctx->set_constant_buffer(ctx, MESA_SHADER_COMPUTE, 0, &cb);
   if (agx_context(ctx)->any_faults) goto windows_blit_failed;''')
    text = once(text, '''   ctx->set_shader_images(ctx, MESA_SHADER_COMPUTE, 0, 1, 0, &image);''', '''   ctx->set_shader_images(ctx, MESA_SHADER_COMPUTE, 0, 1, 0, &image);
   if (agx_context(ctx)->any_faults) goto windows_blit_failed;''')
    text = once(text, '''   ctx->bind_sampler_states(ctx, MESA_SHADER_COMPUTE, 0, 1,
                            &blitter->sampler[info->filter]);''', '''   if (!blitter->sampler[info->filter]) goto windows_blit_failed;
   ctx->bind_sampler_states(ctx, MESA_SHADER_COMPUTE, 0, 1,
                            &blitter->sampler[info->filter]);''')
    text = once(text, '''   src_view = ctx->create_sampler_view(ctx, src, &src_templ);''', '''   src_view = ctx->create_sampler_view(ctx, src, &src_templ);
   if (!src_view) goto windows_blit_failed;''')
    text = once(text, '''   ctx->sampler_view_release(ctx, src_view);''', '''   ctx->sampler_view_release(ctx, src_view);
   if (agx_context(ctx)->any_faults) goto windows_blit_failed;''')
    text = once(text, '''      cs = asahi_blit_compute_shader(ctx, &key);
      _mesa_hash_table_insert(
         blitter->blit_cs, ralloc_memdup(blitter->blit_cs, &key, sizeof(key)),
         cs);''', '''      cs = asahi_blit_compute_shader(ctx, &key);
      if (!cs) goto windows_blit_failed;
      void *cloned_key = ralloc_memdup(blitter->blit_cs, &key, sizeof(key));
      if (!cloned_key || !_mesa_hash_table_insert(blitter->blit_cs, cloned_key, cs)) {
         ralloc_free(cloned_key);
         ctx->delete_compute_state(ctx, cs);
         goto windows_blit_failed;
      }''')
    text = once(text, '''   assert(cs != NULL);''', '''   if (!cs) goto windows_blit_failed;''')
    text = once(text, '''   ctx->launch_grid(ctx, &grid_info);
   ctx->set_shader_images''', '''   ctx->launch_grid(ctx, &grid_info);
windows_blit_cleanup:
   ctx->set_shader_images''')
    text = once(text, '''   asahi_compute_restore(agx_context(ctx));
}''', '''   asahi_compute_restore(agx_context(ctx));
   return;

windows_blit_failed:
   AgxWin32AsahiMarkContextFault(agx_context(ctx), (5u << 16) | __LINE__);
   goto windows_blit_cleanup;
}''')
    (out/path).write_text(text)
    overlays.setdefault(path,{})['after_blit_allocation_failure'] = hashlib.sha256(text.encode()).hexdigest()
