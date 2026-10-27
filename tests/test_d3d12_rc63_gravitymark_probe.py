"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC63G is the only stream probe whose pixels depend on each record's
BaseVertexLocation and StartInstanceLocation (the per-record PARAMS uniform
the GPU stream rewrites) and on draw order under a depth test. This executes
the probe's actual record, texture-coordinate, quad and verdict code against
a small reference rasterizer: the correct D3D12 result must PASS, and a
stream that dropped the base instance, dropped the base vertex, reordered the
records or lost the tail must FAIL. It also rejects wrong statistics. Without
it a probe could pass on hardware while the stream ignored GPU arguments, as
every zero-based RC38/RC43/RC57/RC59 record would.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'drivers/apple-agx/windows/icd/agx_d3d12_probe.c'

MODEL = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned UINT;
typedef uint64_t UINT64;
typedef struct {UINT64 IAVertices,IAPrimitives,VSInvocations,GSInvocations,GSPrimitives,
 CInvocations,CPrimitives,PSInvocations,HSInvocations,DSInvocations,CSInvocations;}
 D3D12_QUERY_DATA_PIPELINE_STATISTICS;
@HELPERS@
enum {CORRECT, NO_BASE_VERTEX, NO_BASE_INSTANCE, REVERSED, LOST_TAIL};
static unsigned char rt[64 * 256];
static float depth[RC63_PIXELS];
static float positions[RC63_PIXELS * 8];
/* D3D12 semantics: vertex = BaseVertexLocation + index, per-instance element
 * = StartInstanceLocation + 0, depth LESS at z = 0, point-clamp sampling. */
static void draw(UINT r, int mode) {
  UINT record[5];
  unsigned char uv[4];
  rc63_record(r, record);
  const UINT vertex = mode == NO_BASE_VERTEX ? 0 : record[3];
  const UINT instance = mode == NO_BASE_INSTANCE ? 0 : record[4];
  const float *v = positions + vertex * 2;
  float x0 = v[0], x1 = v[0], y0 = v[1], y1 = v[1];
  for (int i = 1; i < 4; ++i) {
    x0 = fminf(x0, v[2 * i]); x1 = fmaxf(x1, v[2 * i]);
    y0 = fminf(y0, v[2 * i + 1]); y1 = fmaxf(y1, v[2 * i + 1]);
  }
  rc63_uv(instance, uv);
  int tu = (int)floorf((float)uv[0] / 255.0f * 64.0f), tv = (int)floorf((float)uv[1] / 255.0f * 64.0f);
  tu = tu < 0 ? 0 : tu > 63 ? 63 : tu; tv = tv < 0 ? 0 : tv > 63 ? 63 : tv;
  /* Only pixel centres inside the quad's bounds can be covered. */
  int px0 = (int)floorf((x0 + 1.0f) * 32.0f - 0.5f), px1 = (int)ceilf((x1 + 1.0f) * 32.0f - 0.5f);
  int py0 = (int)floorf((1.0f - y1) * 32.0f - 0.5f), py1 = (int)ceilf((1.0f - y0) * 32.0f - 0.5f);
  px0 = px0 < 0 ? 0 : px0; py0 = py0 < 0 ? 0 : py0; px1 = px1 > 63 ? 63 : px1; py1 = py1 > 63 ? 63 : py1;
  for (int y = py0; y <= py1; ++y) for (int x = px0; x <= px1; ++x) {
    const UINT p = (UINT)y * 64u + (UINT)x;
    const float cx = ((float)x + 0.5f) / 32.0f - 1.0f, cy = 1.0f - ((float)y + 0.5f) / 32.0f;
    if (cx < x0 || cx > x1 || cy < y0 || cy > y1 || !(0.0f < depth[p])) continue;
    depth[p] = 0.0f;
    rc63_texel((UINT)tv * 64u + (UINT)tu, rt + (p / 64u) * 256u + (p % 64u) * 4u);
  }
}
static unsigned render(int mode) {
  UINT first;
  for (UINT p = 0; p < RC63_PIXELS; ++p) {
    unsigned char *px = rt + (p / 64u) * 256u + (p % 64u) * 4u;
    px[0] = 255; px[1] = px[2] = 0; px[3] = 255; depth[p] = 1.0f;
  }
  for (UINT k = 0; k < RC63_MAX; ++k) {
    if (mode == LOST_TAIL && k >= RC63_MAX - 2048u) break;
    draw(mode == REVERSED ? RC63_MAX - 1u - k : k, mode);
  }
  return rc63_bad_pixels(rt, &first);
}
int main(void) {
  /* Layout: no overlap, D3D12 placement and view alignments, count past
   * every record of the DEFAULT copy. */
  assert(RC63_TEXELS % 512u == 0 && RC63_TEXELS + 64u * 256u <= RC63_POSITIONS);
  assert(RC63_POSITIONS + RC63_PIXELS * 32u <= RC63_INDICES && RC63_INDICES % 4u == 0);
  assert(RC63_INDICES + 24u <= RC63_INSTANCES && RC63_INSTANCES % 4u == 0);
  assert(RC63_INSTANCES + RC63_MAX * 4u <= RC63_RECORDS && RC63_RECORDS + RC63_MAX * 20u <= RC63_COUNT);
  assert(RC63_COUNT + 4u <= RC63_UPLOAD_BYTES && RC63_GPU_COUNT == RC63_MAX * 20u);
  for (UINT q = 0; q < RC63_PIXELS; ++q) rc63_quad(q, positions + q * 8u);
  /* Every quad covers exactly its own pixel centre, so placement depends on
   * BaseVertexLocation alone. */
  for (UINT q = 0; q < RC63_PIXELS; ++q) {
    const float *v = positions + q * 8u;
    unsigned covered = 0;
    for (UINT p = 0; p < RC63_PIXELS; ++p) {
      const float cx = ((float)(p % 64u) + 0.5f) / 32.0f - 1.0f, cy = 1.0f - ((float)(p / 64u) + 0.5f) / 32.0f;
      if (cx >= v[0] && cx <= v[4] && cy >= v[1] && cy <= v[3]) { assert(p == q); ++covered; }
    }
    assert(covered == 1);
  }
  /* Each first-record texture coordinate addresses its own texel; a later
   * record of the same pixel never does. */
  for (UINT r = 0; r < RC63_MAX; ++r) {
    unsigned char uv[4];
    rc63_uv(r, uv);
    const UINT texel = (UINT)floorf((float)uv[1] / 255.0f * 64.0f) * 64u + (UINT)floorf((float)uv[0] / 255.0f * 64.0f);
    assert(r < RC63_PIXELS ? texel == r : texel != r % RC63_PIXELS);
  }
  unsigned bad[5];
  for (int mode = CORRECT; mode <= LOST_TAIL; ++mode) bad[mode] = render(mode);
  fprintf(stderr, "bad pixels: correct=%u no-base-vertex=%u no-base-instance=%u reversed=%u lost-tail=%u\n",
          bad[0], bad[1], bad[2], bad[3], bad[4]);
  assert(bad[CORRECT] == 0);
  assert(bad[NO_BASE_VERTEX] >= RC63_PIXELS - 1u);
  assert(bad[NO_BASE_INSTANCE] >= RC63_PIXELS - 1u);
  assert(bad[REVERSED] == RC63_PIXELS);
  assert(bad[LOST_TAIL] == 0); /* pixels alone cannot see a lost tail ... */
  /* ... so the statistics must: every field is checked. */
  D3D12_QUERY_DATA_PIPELINE_STATISTICS q = {0};
  q.IAVertices = q.VSInvocations = 6ull * RC63_MAX;
  q.IAPrimitives = q.CInvocations = q.CPrimitives = 2ull * RC63_MAX;
  q.PSInvocations = RC63_PIXELS;
  assert(rc63_statistics_ok(&q));
  UINT64 *fields = (UINT64 *)&q;
  for (unsigned i = 0; i < sizeof(q) / sizeof(UINT64); ++i) {
    ++fields[i];
    assert(&fields[i] == &q.PSInvocations ? rc63_statistics_ok(&q) : !rc63_statistics_ok(&q));
    --fields[i];
  }
  q.IAVertices = q.VSInvocations = 6ull * (RC63_MAX - 2048u);
  q.IAPrimitives = q.CInvocations = q.CPrimitives = 2ull * (RC63_MAX - 2048u);
  assert(!rc63_statistics_ok(&q));
  q.IAVertices = q.VSInvocations = 6ull * RC63_MAX;
  q.IAPrimitives = q.CInvocations = q.CPrimitives = 2ull * RC63_MAX;
  q.PSInvocations = 0;
  assert(!rc63_statistics_ok(&q));
  return 0;
}
'''


class Rc63GravityMarkProbe(unittest.TestCase):
    def test_probe_pixels_depend_on_every_record_argument(self):
        source = PROBE.read_text()
        begin = source.index('#define RC63_MAX')
        helpers = source[begin:source.index('static int rc63_gravitymark_stream(', begin)]
        step = source[source.index('static int rc63_gravitymark_stream('):]
        step = step[:step.index('\n}\n')]
        # The hardware step must issue exactly the modeled shape.
        for needle in ('ExecuteIndirect(list, signature, RC63_MAX, gpu_args, 0, gpu_args, RC63_GPU_COUNT)',
                       'D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1}',
                       'D3D12_COMPARISON_FUNC_LESS;', 'DXGI_FORMAT_R32_UINT',
                       'D3D12_COMMAND_SIGNATURE_DESC sd = {20u,',
                       'D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT',
                       'BeginQuery(list, statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS',
                       'rc63_bad_pixels(bytes, &first)', 'rc63_statistics_ok(q)'):
            self.assertIn(needle, step)
        self.assertLess(step.index('BeginQuery'), step.index('ExecuteIndirect'))
        self.assertLess(step.index('ExecuteIndirect'), step.index('EndQuery'))
        self.assertIn('{"RC63G", rc63_gravitymark_stream}', source)
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'rc63.c').write_text(MODEL.replace('@HELPERS@', helpers))
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O1',
                            '-fsanitize=address,undefined', str(p / 'rc63.c'), '-lm',
                            '-o', str(p / 'rc63')], check=True)
            subprocess.run([str(p / 'rc63')], check=True, timeout=60)


if __name__ == '__main__':
    unittest.main()
