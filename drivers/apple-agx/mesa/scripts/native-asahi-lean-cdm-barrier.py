#!/usr/bin/env python3
"""rc101: lean CDM barriers between consecutive application dispatches
(mesa/winsys/agx_win32_cdm_barrier.h, APPLE_AGX_D3D12_LEAN_CDM_BARRIER).

Projected after the GPUVA indirect stream (native-asahi-indirect-stream.py,
which adds windows_uses_draw_id) and the batch lifecycle (agx_flush_compute's
fault guard). New fields are appended at the end of agx_batch, agx_context and
the uncompiled shader info, so the offsets of existing fields are unchanged.
"""
import hashlib


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous lean CDM barrier anchor: ' + old[:100])
    return text.replace(old, new, 1)


def project_state_header(h):
    h = once(h, '      bool windows_uses_draw_id;',
             '      bool windows_uses_draw_id;\n'
             '      /* rc101: AGX_WIN32_CDM_NO_* bits (agx_win32_cdm_barrier.h) */\n'
             '      unsigned windows_cdm_class;')
    h = once(h, '   unsigned cs_preamble_scratch;\n};',
             '   unsigned cs_preamble_scratch;\n\n'
             '   /* rc101: the provisional lean barrier after the last launch, and\n'
             '    * that launch\'s class (agx_win32_cdm_barrier.h) */\n'
             '   uint32_t *windows_cdm_lean;\n'
             '   unsigned windows_cdm_lean_writer;\n};')
    h = once(h, '   struct agx_scratch scratch_cs;\n};',
             '   struct agx_scratch scratch_cs;\n\n'
             '   /* rc101: the class of the application dispatch being launched\n'
             '    * (agx_launch_grid) and of the launch being emitted (0: none);\n'
             '    * barriers left lean and full since the last receipt, the mode\n'
             '    * of the last launch and the last lean word */\n'
             '   unsigned windows_cdm_app_class, windows_cdm_launch_class;\n'
             '   uint32_t windows_cdm_barriers[4];\n};')
    h = once(h, '#include "agx_win32_stream_scratch.h"',
             '#include "agx_win32_stream_scratch.h"\n\n'
             '/* rc101: the full barrier ends every CDM stream (agx_state.c) */\n'
             'void agx_win32_cdm_batch_finish(struct agx_batch *batch);')
    return h


def project_state(s):
    s = once(s, '#include "libagx_dgc.h"\n',
             '#include "libagx_dgc.h"\n#include "agx_win32_cdm_barrier.h"\n')
    s = once(s, '''   so->info.windows_uses_draw_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);
''', '''   so->info.windows_uses_draw_id =
      BITSET_TEST(nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID);
   so->info.windows_cdm_class = agx_win32_cdm_shader_class(nir);
''')
    s = once(s, '''   uint32_t *out = (uint32_t *)batch->cdm.current;

   out = agx_cdm_launch(out, dev->chip, grid, wg, launch, usc);
   out = agx_cdm_barrier(out, dev->chip);
''', '''   uint32_t *out = (uint32_t *)batch->cdm.current;

   /* rc101: lean barriers between application dispatches
    * (agx_win32_cdm_barrier.h); off, this is Asahi's sequence. */
   const unsigned lean_mode = agx_win32_lean_cdm_mode();
   unsigned lean_class = ctx->windows_cdm_launch_class;
   ctx->windows_cdm_launch_class = 0;
   ctx->windows_cdm_barriers[2] = lean_mode;
   if (grid.mode != AGX_CDM_MODE_DIRECT)
      lean_class |= AGX_WIN32_CDM_INDIRECT;
   agx_win32_cdm_before_launch(batch, dev->chip, lean_mode, lean_class);
   out = agx_cdm_launch(out, dev->chip, grid, wg, launch, usc);
   out = agx_win32_cdm_after_launch(batch, out, dev->chip, lean_mode, lean_class);
''')
    # Only the final launch of agx_launch is the application's dispatch:
    # precompiled kernels that descriptor updates may launch first keep class 0.
    s = once(s, '''   agx_launch_internal(batch, grid, wg, launch, stage, usc);
}
''', '''   ctx->windows_cdm_launch_class = ctx->windows_cdm_app_class;
   agx_launch_internal(batch, grid, wg, launch, stage, usc);
   ctx->windows_cdm_launch_class = 0;
}

void
agx_win32_cdm_batch_finish(struct agx_batch *batch)
{
   agx_win32_cdm_finish(batch, agx_device(batch->ctx->base.screen)->chip);
}
''')
    # Compute blits (asahi_compute_blit) also launch through launch_grid.
    s = once(s, '''   agx_launch(batch, grid, wg, cs, NULL, MESA_SHADER_COMPUTE,
              info->variable_shared_mem);
''', '''   ctx->windows_cdm_app_class = ctx->compute_blitter.active ? 0u :
      AGX_WIN32_CDM_APP | uncompiled->info.windows_cdm_class;
   agx_launch(batch, grid, wg, cs, NULL, MESA_SHADER_COMPUTE,
              info->variable_shared_mem);
   ctx->windows_cdm_app_class = 0;
''')
    return s


def project_batch(s):
    return once(s, '   batch->incoherent_writes = false;\n   agx_bo_unreference(dev, batch->sampler_heap.bo);\n',
                '   batch->incoherent_writes = false;\n   batch->windows_cdm_lean = NULL;\n'
                '   agx_bo_unreference(dev, batch->sampler_heap.bo);\n')


def project_pipe(s):
    return once(s, '''   /* Finalize the encoder */
   agx_pack(batch->cdm.current, CDM_STREAM_TERMINATE, _)''', '''   /* Finalize the encoder (rc101: after the full barrier) */
   agx_win32_cdm_batch_finish(batch);
   agx_pack(batch->cdm.current, CDM_STREAM_TERMINATE, _)''')


FILES = {
    'src/gallium/drivers/asahi/agx_state.h': project_state_header,
    'src/gallium/drivers/asahi/agx_state.c': project_state,
    'src/gallium/drivers/asahi/agx_batch.c': project_batch,
    'src/gallium/drivers/asahi/agx_pipe.c': project_pipe,
}


def project_sources(out, project, overlays):
    for path, transform in FILES.items():
        text = transform((out / path).read_text())
        (out / path).write_text(text)
        overlays.setdefault(path, {})['after_lean_cdm_barrier'] = (
            hashlib.sha256(text.encode()).hexdigest())
