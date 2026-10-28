"""rc89: GPU stream outputs are sized by the GPU counts, not by MaxCount.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
GravityMark FHD 200k records seven stream draws per frame with MaxCount
240000 (120000 at 100k) although each executes about 150 records (rc67
EXP1989). Sized by MaxCount, every draw took about 11 MiB of VDM records
and 30-44 MiB of USC words: about 300 MiB per frame in flight at 200k
(rc88), which with the rest of the working set does not fit the ~2931 MiB
LOCAL budget. The projected stream path (real agx_pool, batch init/cleanup,
scratch retention and the emitter's allocation code; the GPU is modelled by
the kernel's feedback contract, which test_d3d12_indirect_stream.py checks
on the real kernel) must:
- size every output by MaxCount until a retired batch's dispatches reported
  (a batch that never ran, or no feedback BO, keeps MaxCount);
- then size outputs by min(MaxCount, the power of two >= 16384 and >= 4 x
  the largest reported count), never below a count a retired batch reported;
- give back the oversized warm-up outputs so the steady window really holds
  less (rc88 retention would otherwise keep the MaxCount BOs forever and
  hand them to the small draws);
- after an overflow (a GPU count above its capacity, counted once), size
  every later draw for that count, and create nothing in steady state
  before or after it.
"""
import re
import unittest
from test_rc71_stream_scratch import run_stream_harness

MAIN = r'''
#define FRAME_LAG 2u
#define FRAMES 240u
#define OVERFLOW_FRAME 120u
#define OVERFLOW_COUNT 100000u
static unsigned long long stream_bytes(const struct agx_context *ctx) {
  unsigned long long bytes = ctx->windows_stream_live[0] + ctx->windows_stream_live[1];
  for (unsigned k = 0; k < 2; ++k)
    for (unsigned i = 0; i < AGX_WIN32_STREAM_SPARES; ++i)
      if (ctx->windows_stream_spare[k][i]) bytes += ctx->windows_stream_spare[k][i]->size;
  return bytes;
}
/* The kernel's report (agx_win32_indirect_stream.cl, work-item 0). */
static void gpu_report(struct agx_context *ctx, unsigned gpu, unsigned capacity, unsigned max) {
  uint32_t *w = (uint32_t *)ctx->windows_stream_feedback->cpu;
  unsigned count = gpu < max ? gpu : max;
  if (count > w[AGX_WIN32_STREAM_FB_MAX_COUNT]) w[AGX_WIN32_STREAM_FB_MAX_COUNT] = count;
  w[AGX_WIN32_STREAM_FB_OVERFLOWS] += count > capacity;
  w[AGX_WIN32_STREAM_FB_DISPATCHES] += 1;
}
int main(void) {
  const unsigned max = @@MAX@@u, small_max = @@SMALL@@u;
  /* No feedback BO (creation failed, or APPLE_AGX_D3D12_STREAM_CAPACITY=0):
   * MaxCount, as before rc89. */
  { static struct agx_context none; none.base.screen = &device; fail_writeback = 1;
    agx_win32_stream_feedback_ensure(&none);
    assert(!none.windows_stream_feedback && agx_win32_stream_capacity(&none, max, 0) == max);
    none.windows_stream_reports = 1; none.windows_stream_observed = 5;
    assert(agx_win32_stream_capacity(&none, max, 0) == max);
    fail_writeback = 0; }
  static struct agx_context ctx; ctx.base.screen = &device;
  static struct agx_batch batches[FRAME_LAG][2];
  static struct agx_bo *used[FRAME_LAG][2][2 * CALLS];
  static unsigned caps[FRAME_LAG][2][CALLS], gpus[FRAME_LAG][2][CALLS];
  static const unsigned draws[2] = {CALLS - 1u, 1u};
  unsigned long long full_window = 0, steady_window = 0, regrown_window = 0, reported = 0;
  unsigned steady_created = 0, regrown_created = 0, small_draws = 0, overflowed = 0;
  for (unsigned f = 0; f < FRAMES + FRAME_LAG; ++f) {
    unsigned slot = f % FRAME_LAG, before_created = created;
    if (f >= FRAME_LAG)
      for (unsigned b = 0; b < 2; ++b) {
        /* Frame 0 never reached the GPU: its retire proves nothing. */
        for (unsigned d = 0; d < draws[b]; ++d) {
          if (f - FRAME_LAG) {
            unsigned draw_max = b == 0 && d == 5 ? small_max : max;
            gpu_report(&ctx, gpus[slot][b][d], caps[slot][b][d], draw_max);
            unsigned count = gpus[slot][b][d] < draw_max ? gpus[slot][b][d] : draw_max;
            if (count > reported) reported = count;
            overflowed += count > caps[slot][b][d];
          }
        }
        for (unsigned i = 0; i < 2 * draws[b]; ++i) { assert(used[slot][b][i]->busy); used[slot][b][i]->busy = 0; }
        batch_cleanup(&ctx, &batches[slot][b]);
      }
    if (f >= FRAMES) continue;
    for (unsigned b = 0, c = 0; b < 2; ++b) {
      struct agx_batch *batch = &batches[slot][b];
      batch->ctx = &ctx; batch_init(batch);
      for (unsigned d = 0; d < draws[b]; ++d, ++c) {
        struct agx_bo *vb, *ub;
        unsigned gpu = f == OVERFLOW_FRAME && c == 2 ? OVERFLOW_COUNT : 100u + (f * 7u + c * 13u) % 101u;
        /* Draw 5 may have a smaller MaxCount (mixed sizes in one frame). */
        unsigned draw_max = c == 5 ? small_max : max;
        stream_draw(batch, draw_max, 44u, 0x808u, c == 3 ? 192u : 128u, &vb, &ub);
        unsigned want = draw_max;
        if (ctx.windows_stream_reports) {
          unsigned long long w = AGX_WIN32_STREAM_CAPACITY_FLOOR;
          while (w < 4ull * ctx.windows_stream_observed) w <<= 1;
          want = w < draw_max ? (unsigned)w : draw_max;
        }
        assert(last_capacity == want && last_capacity <= draw_max);
        /* Never below a count that a retired batch reported. */
        assert(last_capacity >= (reported < draw_max ? reported : draw_max));
        /* Frame 0's retire (at f == FRAME_LAG) proves nothing: frames up
         * to FRAME_LAG are MaxCount-sized; frame 1's retire reports. */
        if (f <= FRAME_LAG) assert(last_capacity == draw_max);
        small_draws += last_capacity < draw_max;
        assert(!vb->busy && !ub->busy);
        vb->busy = ub->busy = 1;
        used[slot][b][2 * d] = vb; used[slot][b][2 * d + 1] = ub;
        caps[slot][b][d] = last_capacity; gpus[slot][b][d] = gpu;
      }
    }
    if (f == FRAME_LAG - 1u) full_window = stream_bytes(&ctx);
    if (f > 2u * FRAME_LAG + 2u && f < OVERFLOW_FRAME) steady_created += created - before_created;
    if (f == OVERFLOW_FRAME - 1u) steady_window = stream_bytes(&ctx);
    if (f > OVERFLOW_FRAME + 2u * FRAME_LAG + 2u) regrown_created += created - before_created;
    if (f == FRAMES - 1u) regrown_window = stream_bytes(&ctx);
  }
  uint32_t *w = (uint32_t *)ctx.windows_stream_feedback->cpu;
  printf("RC89 max=%u window_MiB full=%.1f steady=%.1f regrown=%.1f per_frame_MiB full=%.1f steady=%.1f "
         "small_draws=%u overflows=%u steady_created=%u regrown_created=%u created=%u\n",
         max, full_window / 1048576.0, steady_window / 1048576.0, regrown_window / 1048576.0,
         full_window / 1048576.0 / FRAME_LAG, steady_window / 1048576.0 / FRAME_LAG,
         small_draws, (unsigned)ctx.windows_stream_overflows, steady_created, regrown_created, created);
  assert(small_draws > 0 && overflowed == 1 && w[AGX_WIN32_STREAM_FB_OVERFLOWS] == 1);
  assert(ctx.windows_stream_overflows == 1 && ctx.windows_stream_observed == (OVERFLOW_COUNT < max ? OVERFLOW_COUNT : max));
  assert(steady_created == 0 && regrown_created == 0);
  /* Steady state: each draw's outputs hold 16384 records (1 MiB VDM, 2 or
   * 3 MiB USC), FRAME_LAG frames of seven draws. */
  assert(steady_window <= FRAME_LAG * (7ull * 1 + 6ull * 2 + 3ull) * 1048576ull);
  assert(steady_window * 3u < full_window);
  /* After the overflow every draw is sized for the reported count again. */
  assert(last_capacity == (524288u < max ? 524288u : max));
  @@FINI@@
  unsigned live = 0;
  for (unsigned i = 0; i < created; ++i) live += bos[i]->refcnt != 0;
  assert(live == 0 && winsys_releases == created);
  for (unsigned i = 0; i < created; ++i) { free(bos[i]->cpu); free(bos[i]); }
  puts("RC71 stream scratch: PASS");
  return 0;
}
'''


class StreamCapacity(unittest.TestCase):
    def run_case(self, max_count, small_max=None):
        main = MAIN.replace('@@MAX@@', str(max_count))
        main = main.replace('@@SMALL@@', str(small_max or max_count))
        out = run_stream_harness(self, max_count, defines=('RC89_CAPACITY',), main=main)
        line = next(l for l in out.splitlines() if l.startswith('RC89 '))
        print(line)
        return line

    def test_200k_outputs_follow_gpu_counts(self):
        self.run_case(240000)

    def test_100k_outputs_follow_gpu_counts(self):
        self.run_case(120000)

    def test_rc63g_sized_60000_outputs_follow_gpu_counts(self):
        """The RC63G/RC89S probe's MaxCount; the overflow count 100000 is
        clamped to MaxCount, so the regrown capacity is MaxCount."""
        line = self.run_case(60000)
        self.assertRegex(line, r'small_draws=[1-9]')


    def test_mixed_maxcounts_do_not_churn(self):
        """A draw whose MaxCount (2400) is below the capacity keeps its own
        size; the stale-output release must not then release and re-create
        either size every frame."""
        self.run_case(240000, 2400)


if __name__ == '__main__':
    unittest.main()
