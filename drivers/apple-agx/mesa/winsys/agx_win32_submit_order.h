/* SPDX-License-Identifier: MIT
 * rc105: a batch is never submitted before an unsubmitted batch that writes
 * a BO it uses. Included by the projected agx_pipe.c
 * (mesa/scripts/native-asahi-submit-order.py) at the top of agx_flush_batch.
 *
 * Why: every D3D12 ResourceBarrier is a no-op here (agx_d3d12_ddi.cpp), so
 * read-after-write order between batches comes only from Asahi's hazard
 * tracking: agx_batch_reads flushes the writer batch when a later batch
 * reads its resource (agx_batch.c agx_flush_writer_except). Asahi keeps
 * several batches open (a compute batch, one render batch per framebuffer)
 * and agx_flush_all submits them in slot order, not recording order. A use
 * that reaches the batch's BO list without agx_batch_reads -- agx_batch_add_bo
 * (residency, the GPU indirect stream's retained inputs) or a read of memory
 * a writer reached under another name -- leaves the writer open, and if its
 * slot index is higher the reader is submitted first on the in-order KMD
 * queue: it reads the previous frame's contents. That is a transient,
 * frame-to-frame error (slot indices change as batches recycle), the class
 * of the GravityMark D3D12 Earth/shadow flicker (rc105 TASK.md).
 *
 * Invariant: when agx_flush_batch(B) starts, no other ACTIVE batch is the
 * recorded writer (ctx->writer, keyed by BO handle) of a BO in B's BO list.
 * Under complete Asahi tracking this already holds -- the write-after-read
 * path (agx_batch_writes_internal -> agx_flush_readers_except) flushes every
 * active user of a BO before a new writer takes it, so an active writer of
 * B's BO wrote it before B used it, and B must run after it. Flushing that
 * writer first therefore only ever restores recording order; it adds no
 * submission (the writer would have been flushed by the end of the Execute
 * anyway).
 *
 * APPLE_AGX_D3D12_SUBMIT_ORDER (read once per process):
 *   unset, 1 or anything else: enforce (flush each such writer first);
 *   0    receipt only: count, keep Asahi's order (the pre-rc105 behaviour).
 * Receipt: AgxWin32PerfNote "Submit order: writer flushed first" (enforce) or
 * "Submit order: inversion kept" (0), one note per writer batch found; the
 * D3D12 owner reports them as measure-perf-reason lines.
 */
#ifndef AGX_WIN32_SUBMIT_ORDER_H
#define AGX_WIN32_SUBMIT_ORDER_H

#include <stdint.h>
#include <stdlib.h>

enum {
   AGX_WIN32_SUBMIT_ORDER_RECEIPT = 0,
   AGX_WIN32_SUBMIT_ORDER_ENFORCE = 1,
};

static inline unsigned
agx_win32_submit_order_parse(const char *value)
{
   return value && value[0] == '0' && !value[1] ? AGX_WIN32_SUBMIT_ORDER_RECEIPT
                                                : AGX_WIN32_SUBMIT_ORDER_ENFORCE;
}

/* A benign race: every thread computes the same value. */
static inline unsigned
agx_win32_submit_order_mode(void)
{
   static int mode = -1;
   if (mode < 0)
      mode = (int)agx_win32_submit_order_parse(getenv("APPLE_AGX_D3D12_SUBMIT_ORDER"));
   return (unsigned)mode;
}

/* The slot indices (0-based, ascending, each once) of the active batches
 * other than `self` that the writer map names for a BO set in `bo_set`.
 *   bo_set/bit_count: the batch's BO list (32-bit words, Mesa BITSET_WORD);
 *   writer/writer_count: ctx->writer, one byte per BO handle, slot + 1 or 0;
 *   active: ctx->batches.active (32-bit words, `slots` bits).
 * Returns the number written to out (at most `slots`). */
static inline unsigned
agx_win32_submit_order_writers(const uint32_t *bo_set, unsigned bit_count,
                               const uint8_t *writer, unsigned writer_count,
                               const uint32_t *active, unsigned slots,
                               unsigned self, uint8_t *out)
{
   uint32_t seen[8] = {0}; /* slots <= 256 */
   unsigned found = 0;
   unsigned limit = bit_count < writer_count ? bit_count : writer_count;
   if (!bo_set || !writer || !active || !out || slots > 256)
      return 0;
   for (unsigned word = 0; word * 32u < limit; ++word) {
      uint32_t bits = bo_set[word];
      for (unsigned bit = 0; bits && bit < 32u; ++bit) {
         unsigned handle = word * 32u + bit;
         if (!(bits & (1u << bit)))
            continue;
         bits &= ~(1u << bit);
         if (handle >= limit)
            break;
         unsigned value = writer[handle];
         if (!value)
            continue;
         unsigned slot = value - 1u;
         if (slot >= slots || slot == self || !(active[slot / 32u] & (1u << (slot % 32u))) ||
             (seen[slot / 32u] & (1u << (slot % 32u))))
            continue;
         seen[slot / 32u] |= 1u << (slot % 32u);
         ++found;
      }
   }
   unsigned n = 0;
   for (unsigned slot = 0; slot < slots && n < found; ++slot)
      if (seen[slot / 32u] & (1u << (slot % 32u)))
         out[n++] = (uint8_t)slot;
   return n;
}

#endif
