#!/usr/bin/env python3
"""RC73: D3D12 grid launches without the per-launch CDM barrier.

Projected last (GPUVA only). The host helper is
mesa/winsys/agx_win32_cdm_barrier.h. Native launches keep their barriers;
only the D3D12 owner's grid launches (ctx->windows_cdm_relaxed) defer theirs
to the next app barrier, non-relaxed launch or stream end.
"""
import hashlib


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous CDM barrier anchor: ' + old[:100])
    return text.replace(old, new, 1)


LAUNCH = '''   out = agx_cdm_launch(out, dev->chip, grid, wg, launch, usc);
   out = agx_cdm_barrier(out, dev->chip);
'''

LAUNCH_RELAXED = '''   /* RC73: native "launch; barrier", except that a D3D12 owner grid launch
    * defers its barrier (agx_win32_cdm_barrier.h). */
   (void)dev;
   out = agx_win32_cdm_launch(batch, out, grid, wg, launch, usc);
'''

GRID = '''   agx_launch(batch, grid, wg, cs, NULL, MESA_SHADER_COMPUTE,
              info->variable_shared_mem);
'''

GRID_RELAXED = '''   /* RC73: only this launch of an owner grid is relaxed, never the libagx
    * statistics kernel above or launches the descriptors may trigger. */
   ctx->windows_cdm_user_launch = ctx->windows_cdm_relaxed && !ctx->compute_blitter.active;
   agx_launch(batch, grid, wg, cs, NULL, MESA_SHADER_COMPUTE,
              info->variable_shared_mem);
   ctx->windows_cdm_user_launch = false;
'''


def project_sources(out, project, overlays):
    def save(path, text, key='after_cdm_barrier'):
        (out / path).write_text(text)
        overlays.setdefault(path, {})[key] = hashlib.sha256(text.encode()).hexdigest()

    path = 'src/gallium/drivers/asahi/agx_state.h'
    text = (out / path).read_text()
    if 'windows_cdm_owed' in text:
        raise RuntimeError('CDM barrier state was already projected')
    text = once(text, '   bool incoherent_writes;\n', '''   bool incoherent_writes;

   /* RC73: the last launch was a relaxed D3D12 grid launch whose CDM barrier
    * is still owed (agx_win32_cdm_barrier.h). */
   bool windows_cdm_owed;
''')
    text = once(text, '   unsigned windows_query_ia_records;\n', '''   unsigned windows_query_ia_records;
   /* RC73: owner grid launches may skip the barrier between them; the
    * one-shot mark selects the grid's own launch. Receipt counters. */
   unsigned windows_cdm_relaxed, windows_cdm_user_launch;
   unsigned windows_cdm_counts[6];
''')
    save(path, text)

    path = 'src/gallium/drivers/asahi/agx_batch.c'
    text = (out / path).read_text()
    text = once(text, '   batch->clear = 0;\n', '   batch->clear = 0;\n   batch->windows_cdm_owed = false;\n')
    save(path, text)

    path = 'src/gallium/drivers/asahi/agx_state.c'
    text = (out / path).read_text()
    text = once(text, '\nstatic void\nagx_launch_internal(',
                '\n#define AGX_WIN32_CDM_BARRIER_ENTRIES 1\n#include "agx_win32_cdm_barrier.h"\n'
                '\nstatic void\nagx_launch_internal(')
    begin = text.index('\nstatic void\nagx_launch_internal(')
    end = text.index('\nvoid\nagx_launch_precomp(', begin)
    body = text[begin:end]
    body = once(body, LAUNCH, LAUNCH_RELAXED)
    text = text[:begin] + body + text[end:]
    begin = text.index('\nagx_launch_grid(struct pipe_context *pipe, const struct pipe_grid_info *info)\n')
    end = text.index('\n}\n', begin)
    text = text[:begin] + once(text[begin:end], GRID, GRID_RELAXED) + text[end:]
    save(path, text)

    path = 'src/gallium/drivers/asahi/agx_pipe.c'
    text = (out / path).read_text()
    text = once(text, '#include "agx_state.h"\n', '#include "agx_state.h"\n#include "agx_win32_cdm_barrier.h"\n')
    text = once(text, '''   /* Finalize the encoder */
   agx_pack(batch->cdm.current, CDM_STREAM_TERMINATE, _)''', '''   /* RC73: a relaxed last launch still ends with the native barrier. */
   agx_win32_cdm_settle(batch, AGX_WIN32_CDM_SETTLED_FLUSH);
   /* Finalize the encoder */
   agx_pack(batch->cdm.current, CDM_STREAM_TERMINATE, _)''')
    save(path, text)
