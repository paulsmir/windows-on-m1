"""OPTWIN twin probes (D3D12 agx_d3d12_probe.c optwin_run + agx_d3d12_probe_cs_optwin.h, Metal
tools/macos/optwin_metal.m).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- The two sides drift apart: a constant, shape, R, input or oracle edited on one side only (the shared
  block must be byte-identical), or a kernel body using a macro that only one language preamble defines
  (the HLSL is compiled on the guest only, so this is the one offline check).
- The Wom1GpuPerf validity rule reads the wrong bytes (offsets restated from the KMD header) or
  computes the P6 share wrongly.
- Shapes that break the comparison: unequal total threads across S1 group sizes, out-of-range row or
  region indices, output larger than the buffer, a G1d permutation with short cycles (cache-resident
  chains), or an output line whose value contains a space (unparseable key=value).
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICD = ROOT / 'drivers/apple-agx/windows/icd'
HEADER = ICD / 'agx_d3d12_probe_cs_optwin.h'
PROBE = ICD / 'agx_d3d12_probe.c'
METAL = ROOT / 'tools/macos/optwin_metal.m'
PERF = ROOT / 'drivers/apple-agx/render-admission/include/render_gpu_perf.h'
BEGIN, END = '/* OPTWIN-SHARED-BEGIN */', '/* OPTWIN-SHARED-END */'


def shared(text):
    return text[text.index(BEGIN):text.index(END) + len(END)]


def run_c(code):
    with tempfile.TemporaryDirectory() as tmp:
        c = Path(tmp) / 'p.c'
        exe = Path(tmp) / 'p'
        c.write_text(code)
        subprocess.run(['clang', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-fsanitize=address,undefined', '-I', str(ICD), str(c), '-o', str(exe), '-lm'], check=True)
        return subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout


class OptwinProbe(unittest.TestCase):
    def test_shared_block_is_identical_on_both_sides(self):
        self.assertEqual(shared(HEADER.read_text()), shared(METAL.read_text()))

    def test_bodies_use_only_macros_both_preambles_define(self):
        header, metal = HEADER.read_text(), METAL.read_text()
        hlsl = header[header.index('optwin_hlsl_preamble[]'):]
        msl = metal[metal.index('optwin_msl_preamble'):]
        defined = lambda text: set(re.findall(r'#define (\w+)', text))
        bodies = ''.join(re.findall(r'"([^"\n]*)\\n"', shared(header)))
        used = (set(re.findall(r'\b([A-Z][A-Z0-9_]+)\b', bodies)) | set(re.findall(r'\b([A-Z][A-Z0-9_]*)\(', bodies))) \
            - {'NV', 'NTX', 'NTY', 'ALT'}
        self.assertIn('P', used)
        self.assertTrue(used)
        self.assertLessEqual(used, defined(hlsl), 'macro missing from the HLSL preamble')
        self.assertLessEqual(used, defined(msl), 'macro missing from the MSL preamble')
        self.assertIn('#define NV 20u', shared(header))
        self.assertIn('#define OPTWIN_S1O_NV 20u', shared(header))
        self.assertIn('#include "agx_d3d12_probe_cs_optwin.h"', PROBE.read_text())
        for step in ('OPTWIN', 'OPTWINS1', 'OPTWING1', 'OPTWINT1', 'OPTWINB1', 'OPTWINC1'):
            self.assertIn('{"%s", optwin_' % step, PROBE.read_text())

    def test_wom1gpuperf_offsets_match_the_kmd_header(self):
        out = run_c(r'''
#include <stddef.h>
#include <stdio.h>
#include "%s"
#include "agx_d3d12_probe_cs_optwin.h"
int main(void) {
 printf("%%d %%d %%d %%d %%d %%d %%d %%d\n",
  sizeof(ADMISSION_GPU_PERF_STATE)==OPTWIN_PERF_BYTES, ADMISSION_GPU_PERF_VERSION==OPTWIN_PERF_VERSION,
  offsetof(ADMISSION_GPU_PERF_STATE,Samples)==OPTWIN_PERF_OFF_SAMPLES,
  offsetof(ADMISSION_GPU_PERF_STATE,QpcFrequency)==OPTWIN_PERF_OFF_FREQUENCY,
  offsetof(ADMISSION_GPU_PERF_STATE,LastQpc)==OPTWIN_PERF_OFF_LAST_QPC,
  offsetof(ADMISSION_GPU_PERF_STATE,SampledTicks)==OPTWIN_PERF_OFF_SAMPLED,
  offsetof(ADMISSION_GPU_PERF_STATE,ActualTicks)==OPTWIN_PERF_OFF_ACTUAL,
  offsetof(ADMISSION_GPU_PERF_STATE,Gaps)==OPTWIN_PERF_OFF_GAPS);
 return 0;
}''' % PERF)
        self.assertEqual(out.split(), ['1'] * 8)

    def test_p6_share_from_two_snapshots(self):
        out = run_c(r'''
#include <assert.h>
#include <stdio.h>
#include "%s"
#include "agx_d3d12_probe_cs_optwin.h"
static void snap(ADMISSION_GPU_PERF_STATE *s, unsigned samples, unsigned long long p6, unsigned long long p5, unsigned gaps) {
 AdmissionGpuPerfInitialize(s, 1000000ull);
 s->Samples = samples; s->ActualTicks[6] = p6; s->ActualTicks[5] = p5; s->SampledTicks = p6 + p5; s->Gaps = gaps;
}
int main(void) {
 ADMISSION_GPU_PERF_STATE a, b; double pct, ms; unsigned gaps; int valid;
 snap(&a, 10, 5000000, 1000, 1); snap(&b, 90, 5000000 + 1990000, 1000 + 10000, 3);
 assert(optwin_perf_delta((unsigned char *)&a, (unsigned char *)&b, &pct, &ms, &gaps, &valid) == 1);
 assert(gaps == 2 && valid == 1 && ms > 1999.99 && ms < 2000.01 && pct > 99.49 && pct < 99.51);
 snap(&b, 90, 5000000 + 1970000, 1000 + 30000, 1);
 assert(optwin_perf_delta((unsigned char *)&a, (unsigned char *)&b, &pct, &ms, &gaps, &valid) == 1 && valid == 0);
 assert(pct > 98.49 && pct < 98.51);
 snap(&b, 10, 5000000, 1000, 1); /* no new sample: unusable, not "valid" */
 assert(optwin_perf_delta((unsigned char *)&a, (unsigned char *)&b, &pct, &ms, &gaps, &valid) == 0 && valid == 0);
 snap(&b, 90, 6000000, 1000, 1); b.Version = 2;
 assert(optwin_perf_delta((unsigned char *)&a, (unsigned char *)&b, &pct, &ms, &gaps, &valid) == 0 && pct < 0);
 puts("ok");
 return 0;
}''' % PERF)
        self.assertEqual(out.strip(), 'ok')

    def test_shapes_inputs_and_output_lines(self):
        out = run_c(r'''
#include <assert.h>
#include "agx_d3d12_probe_cs_optwin.h"
int main(void) {
 unsigned i, m, s1_threads[OPTWIN_MAX_CASES] = {0}, s1_seen = 0;
 assert(optwin_region_offset(OPTWIN_REGIONS - 1u) + optwin_region_elems(OPTWIN_REGIONS - 1u) * 16u == OPTWIN_DATA_BYTES);
 assert(OPTWIN_VIEW * OPTWIN_ROWS_PER_VIEW + 31u < OPTWIN_ROWS);
 for (i = 0; i < OPTWIN_VARIANTS; ++i) {
  const optwin_variant *v = &optwin_variants[i];
  optwin_case c[OPTWIN_MAX_CASES];
  const unsigned n = optwin_cases(v, c);
  assert(n >= 2 && n <= OPTWIN_MAX_CASES && v->kernel < OPTWIN_KERNELS && v->region < OPTWIN_REGIONS);
  for (m = 0; m < n; ++m) {
   const optwin_params p = optwin_case_params(v, &c[m]);
   assert(c[m].R > 0 && c[m].nb > c[m].na && c[m].ntx * c[m].nty <= 1024u);
   assert(optwin_threads(&c[m]) * 4u + 32u * 4u <= OPTWIN_OUT_BYTES);
   if (!strcmp(c[m].shape, "L1") || !strcmp(c[m].shape, "L8") || !strcmp(c[m].shape, "W56") || !strcmp(c[m].shape, "W224"))
    assert(optwin_threads(&c[m]) == (!strcmp(c[m].shape, "L1") ? 1024u : !strcmp(c[m].shape, "L8") ? 8192u
                                     : !strcmp(c[m].shape, "W56") ? 57344u : 229376u));
   if (!strcmp(v->probe, "S1") && !v->alt && !v->indirect && v->kernel == OPTWIN_K_S1) {
    if (!s1_seen) s1_threads[m] = optwin_threads(&c[m]);
    assert(s1_threads[m] == optwin_threads(&c[m])); /* equal total threads across group sizes */
   }
   if (v->indirect) assert(p.n == c[m].gx && p.view == c[m].gy);
   assert(p.w == c[m].gx * c[m].ntx && p.mask + 1u == optwin_region_elems(v->region));
  }
  if (!strcmp(v->probe, "S1") && !v->alt && !v->indirect && v->kernel == OPTWIN_K_S1) ++s1_seen;
 }
 assert(s1_seen == 4);
 {
  /* every G1d region is one cycle through all elements */
  float *data = (float *)malloc(OPTWIN_DATA_BYTES);
  unsigned r;
  assert(data && optwin_fill_data(data));
  for (r = 0; r < OPTWIN_REGIONS; ++r) {
   const float *base = data + optwin_region_offset(r) / 4u;
   const unsigned n = optwin_region_elems(r);
   unsigned idx = 0, steps = 0;
   do { idx = (unsigned)base[4u * idx + 3u]; ++steps; assert(idx < n); } while (idx != 0u && steps <= n);
   assert(steps == n);
  }
  free(data);
 }
 {
  const double best[2] = {1.0, 5.0}, wall[2] = {1.5, 5.5}, missing[2] = {1e30, 5.0};
  optwin_case c[OPTWIN_MAX_CASES];
  (void)optwin_cases(&optwin_variants[0], c);
  optwin_print_case(&optwin_variants[0], &c[5], best, wall, 99.5);
  optwin_print_case(&optwin_variants[0], &c[5], missing, wall, -1.0);
  optwin_print_rep(2, &optwin_variants[0], &c[5], 5, 0, 0.0, 7.0, 0);
  optwin_print_p6(&optwin_variants[0], 99.5, 2000.0, 0, 1);
 }
 return 0;
}''')
        lines = out.splitlines()
        self.assertEqual(len(lines), 4)
        keys = None
        for line in lines:
            tag, *fields = line.split(' ')
            self.assertTrue(tag.startswith('OPTWIN_'))
            self.assertTrue(all(re.fullmatch(r'[A-Za-z0-9_]+=\S+', f) for f in fields), line)
            if tag == 'OPTWIN_CASE':
                k = [f.split('=')[0] for f in fields]
                self.assertTrue(keys is None or keys == k)
                keys = k
        first = dict(f.split('=') for f in lines[0].split(' ')[1:])
        self.assertEqual((first['variant'], first['shape'], first['groups'], first['nt']), ('S1_64', 'GM', '160x90', '8x8'))
        self.assertEqual(first['per_dispatch_ms'], '1.0000')
        self.assertEqual(dict(f.split('=') for f in lines[1].split(' ')[1:])['per_dispatch_ms'], '-1.0000')


if __name__ == '__main__':
    unittest.main()
