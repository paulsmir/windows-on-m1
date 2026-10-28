"""rc89: the RC89S hardware probe tells a clamped, a sized and a wrong run apart.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC89S runs the RC63G scene (MaxCount 60000, GPU-copied count) six times on
one device: three runs of 150 records, then three of 60000. With rc89 the
fourth run overflows outputs of 16384 records: records 0..4095 already cover
every pixel, so only the statistics can show that 16384 records were drawn.
The probe's verdict must accept exactly that (CLAMPED) or a complete run
(SIZED), and must FAIL a run that drew any other number of records, a small
run that drew MaxCount records instead of the GPU count (outputs ignored the
count), a small run that lost records, and a final run that is still
clamped (the capacity never grew). The per-run count must reach the GPU
through the same upload copy before each ExecuteIndirect. This executes the
probe's real verdict helpers against the RC63G reference rasterizer.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_d3d12_rc63_gravitymark_probe import MODEL, PROBE

ROOT = Path(__file__).resolve().parents[1]
SCRATCH = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_stream_scratch.h'

CHECK = r'''
static unsigned render_n(UINT drawn) {
  UINT first;
  (void)first;
  for (UINT p = 0; p < RC63_PIXELS; ++p) {
    unsigned char *px = rt + (p / 64u) * 256u + (p % 64u) * 4u;
    px[0] = 255; px[1] = px[2] = 0; px[3] = 255; depth[p] = 1.0f;
  }
  for (UINT k = 0; k < drawn; ++k) draw(k, CORRECT);
  return 0;
}
static struct rc63_run run_of(UINT count, UINT drawn) {
  struct rc63_run r;
  D3D12_QUERY_DATA_PIPELINE_STATISTICS q;
  memset(&r, 0, sizeof(r)); memset(&q, 0, sizeof(q));
  r.count = count;
  render_n(drawn);
  r.hr = 0;
  r.bad = rc63_bad_pixels_n(rt, &r.first, count);
  r.pixels_ok = r.bad == 0;
  q.IAVertices = q.VSInvocations = 6ull * drawn;
  q.IAPrimitives = q.CInvocations = q.CPrimitives = 2ull * drawn;
  q.PSInvocations = drawn ? (drawn < RC63_PIXELS ? drawn : RC63_PIXELS) : 0;
  r.statistics_ok = rc63_statistics_ok_n(&q, count);
  r.statistics_read = 1;
  r.draws = q.IAVertices / 6u;
  return r;
}
int main(void) {
  for (UINT q = 0; q < RC63_PIXELS; ++q) rc63_quad(q, positions + q * 8u);
  const UINT counts[] = {0, 1, RC89_SMALL, 4095, 4096, 4097, RC89_CAPACITY_FLOOR, RC63_MAX};
  for (unsigned i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
    struct rc63_run r = run_of(counts[i], counts[i]);
    assert(rc89_run_exact(&r) && rc89_overflow_kind(&r) == 1);
  }
  /* The overflow run: 16384 of 60000 records drawn. */
  struct rc63_run clamped = run_of(RC63_MAX, RC89_CAPACITY_FLOOR);
  assert(clamped.pixels_ok && !clamped.statistics_ok && !rc89_run_exact(&clamped));
  assert(rc89_overflow_kind(&clamped) == 2);
  /* Any other shortfall is a failure, even with every pixel right. */
  const UINT wrong[] = {RC63_PIXELS, 8192, RC89_CAPACITY_FLOOR - 1, RC89_CAPACITY_FLOOR + 1, 32768, RC63_MAX - 1};
  for (unsigned i = 0; i < sizeof(wrong) / sizeof(wrong[0]); ++i) {
    struct rc63_run r = run_of(RC63_MAX, wrong[i]);
    assert(r.pixels_ok && rc89_overflow_kind(&r) == 0);
  }
  /* A small run must draw exactly its GPU count. */
  struct rc63_run all = run_of(RC89_SMALL, RC63_MAX), lost = run_of(RC89_SMALL, RC89_SMALL - 50);
  assert(!all.pixels_ok && !rc89_run_exact(&all) && !lost.pixels_ok && !rc89_run_exact(&lost));
  /* A failed execution never passes. */
  struct rc63_run failed = run_of(RC63_MAX, RC63_MAX); failed.hr = -1;
  assert(!rc89_run_exact(&failed) && !rc89_overflow_kind(&failed));
  return 0;
}
'''


def function(source, marker):
    start = source.index(marker)
    return source[start:source.index('\n}\n', start) + 3]


class Rc89StreamCapacityProbe(unittest.TestCase):
    def test_verdict_separates_clamped_sized_and_wrong_runs(self):
        source = PROBE.read_text()
        helpers = source[source.index('#define RC63_MAX'):source.index('static HRESULT rc63_stream_runs(')]
        rc89 = source[source.index('#define RC89_RUNS'):source.index('static int rc89_stream_capacity(')]
        floor = re.search(r'#define AGX_WIN32_STREAM_CAPACITY_FLOOR (\d+)u', SCRATCH.read_text()).group(1)
        self.assertIn('#define RC89_CAPACITY_FLOOR %su' % floor, rc89)
        model = MODEL.replace('@HELPERS@', '#define SUCCEEDED(h) ((h) >= 0)\n' + helpers + rc89)
        model = model[:model.index('static unsigned render(int mode)')] + CHECK
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'rc89.c').write_text(model)
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-O1',
                            '-fsanitize=address,undefined', str(p / 'rc89.c'), '-lm', '-o', str(p / 'rc89')],
                           check=True)
            subprocess.run([str(p / 'rc89')], check=True, timeout=120)

    def test_step_runs_the_sequence_and_copies_each_count(self):
        source = PROBE.read_text()
        step = function(source, 'static int rc89_stream_capacity(')
        self.assertIn('{{RC89_SMALL}, {RC89_SMALL}, {RC89_SMALL}, {RC63_MAX}, {RC63_MAX}, {RC63_MAX}}', step)
        self.assertIn('rc63_stream_runs(device, queue, fence, base, "RC89S", runs, RC89_RUNS)', step)
        self.assertIn('overflow = rc89_overflow_kind(&runs[3]);', step)
        self.assertIn('rc89_run_exact(&runs[5])', step)
        self.assertIn('{"RC89S", rc89_stream_capacity}', source)
        runs = function(source, 'static HRESULT rc63_stream_runs(')
        loop = runs[runs.index('for (UINT run = 0; run < run_count; ++run) {'):]
        # Each run writes its count, then copies it to the GPU before the draw.
        order = ['memcpy(bytes + RC63_COUNT, &count, sizeof(count));',
                 'ID3D12GraphicsCommandList_Reset(list, allocator, pso)',
                 'transition(list, gpu_args, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COPY_DEST);',
                 'CopyBufferRegion(list, gpu_args, RC63_GPU_COUNT, upload, RC63_COUNT, 4);',
                 'transition(list, gpu_args, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);',
                 'ExecuteIndirect(list, signature, RC63_MAX, gpu_args, 0, gpu_args, RC63_GPU_COUNT)',
                 'run_list(queue, fence, list, base + 1 + run, step)',
                 'rc63_bad_pixels_n(bytes, &r->first, r->count)',
                 'rc63_statistics_ok_n(q, r->count)']
        at = [loop.index(n) for n in order]
        self.assertEqual(at, sorted(at))
        self.assertIn('transition(list, target, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);', loop)


if __name__ == '__main__':
    unittest.main()
