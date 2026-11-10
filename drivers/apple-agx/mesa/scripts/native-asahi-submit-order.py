#!/usr/bin/env python3
"""rc105: a batch is never submitted before an unsubmitted batch that writes a
BO it uses (mesa/winsys/agx_win32_submit_order.h,
APPLE_AGX_D3D12_SUBMIT_ORDER).

Projected after the batch lifecycle, whose agx_flush_batch body
(native-asahi-batch-lifecycle.py) starts with the active/submitted guard this
script extends, as the RC42 wait timing rewrote its returns (every exit ends
the AgxWaitFlush scope). agx_pipe.c already includes agx_win32_asahi_bo.h (the wait
timing projection), which declares AgxWin32PerfNote. No field is added.
"""
import hashlib

PATH = 'src/gallium/drivers/asahi/agx_pipe.c'

GUARD = '''   if (!agx_batch_is_active(batch) || agx_batch_is_submitted(batch)) { AgxWin32WaitEnd(wait_token); return; }
   if (!batch->draws && !batch->cdm.bo &&'''

ORDER = '''   if (!agx_batch_is_active(batch) || agx_batch_is_submitted(batch)) { AgxWin32WaitEnd(wait_token); return; }
   /* rc105 (agx_win32_submit_order.h): an open writer of a BO this batch
    * uses was recorded before it; submit that writer first. */
   {
      /* IE1a (agx_win32_expand_ahead.h): a render's expansion batch is
       * submitted before it; an empty expansion batch is reset instead. */
      if (agx_win32_expand_flush_first(ctx, batch)) { AgxWin32WaitEnd(wait_token); return; }
      uint8_t writers[AGX_MAX_BATCHES];
      const unsigned enforce = agx_win32_submit_order_mode();
      const unsigned found = agx_win32_submit_order_writers(
         (const uint32_t *)batch->bo_list.set, batch->bo_list.bit_count,
         (const uint8_t *)ctx->writer.data, ctx->writer.size,
         (const uint32_t *)ctx->batches.active, AGX_MAX_BATCHES,
         agx_batch_idx(batch), writers);
      for (unsigned i = 0; i < found && !ctx->any_faults; ++i) {
         struct agx_batch *writer = &ctx->batches.slots[writers[i]];
         if (!agx_batch_is_active(writer) || agx_batch_is_submitted(writer))
            continue;
         /* PF1: the render that depends on this folded compute batch */
         if (agx_win32_fold_follows(batch, writer))
            continue;
         AgxWin32PerfNote(enforce ? "Submit order: writer flushed first"
                                  : "Submit order: inversion kept");
         if (enforce)
            agx_flush_batch(ctx, writer);
      }
      if (!agx_batch_is_active(batch) || agx_batch_is_submitted(batch)) { AgxWin32WaitEnd(wait_token); return; }
   }
   if (!batch->draws && !batch->cdm.bo &&'''

INCLUDE_ANCHOR = '#include "agx_state.h"\n'


def once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Ambiguous submit order anchor: ' + old[:100])
    return text.replace(old, new, 1)


def project_pipe(s):
    s = once(s, INCLUDE_ANCHOR, INCLUDE_ANCHOR + '#include "agx_win32_submit_order.h"\n')
    return once(s, GUARD, ORDER)


def project_sources(out, project, overlays):
    text = project_pipe((out / PATH).read_text())
    (out / PATH).write_text(text)
    overlays.setdefault(PATH, {})['after_submit_order'] = hashlib.sha256(text.encode()).hexdigest()
