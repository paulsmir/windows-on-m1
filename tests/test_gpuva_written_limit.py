"""A submission may write more allocations than the WDK's written primaries.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1851 ModelViewer: with
the per-dispatch barrier gone, one compute batch holds a frame's dispatches
and writes more than 16 BOs; the winsys refused it (AGX_G4_WRITTEN_LIMIT 16,
copied from D3DDDI_MAX_WRITTEN_PRIMARIES), the backend failed and Asahi's next
agx_bo_create returned NULL inside agx_fast_link (c0000005). The written list
marks every GPU-written slot; only direct displayable slots are
WrittenPrimaries, which the WDK bounds by 16. The winsys and UMD limits must
agree, exceed 16, and the primaries bound must stay on WrittenPrimaries. A
D3D12-only process must report refusals (reject-batch).
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c'
UMD = ROOT / 'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'


class GpuvaWrittenLimit(unittest.TestCase):
    def test_written_limits_agree_and_exceed_the_primaries_bound(self):
        winsys = int(re.search(r'#define AGX_G4_WRITTEN_LIMIT (\d+)u', WINSYS.read_text()).group(1))
        umd_text = UMD.read_text()
        umd = int(re.search(r'#define ADMISSION_UMD_WRITTEN_LIMIT (\d+)u', umd_text).group(1))
        self.assertEqual(winsys, umd)
        self.assertGreater(winsys, 16)
        submit = umd_text[umd_text.index('static int submit(void *context, const uint64_t *written,'):]
        submit = submit[:submit.index('\n}\n')]
        self.assertIn('written_count > ADMISSION_UMD_WRITTEN_LIMIT ||', submit)
        self.assertNotIn('written_count > D3DDDI_MAX_WRITTEN_PRIMARIES', submit)
        self.assertIn('if (request.NumPrimaries==D3DDDI_MAX_WRITTEN_PRIMARIES) {', submit)

    def test_a_batch_writing_130_buffers_is_admitted(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        EXP1994 RC73B: one D3D12 compute batch wrote 128 UAV buffers, the
        readback copy target and its timestamp (130 BOs). add_written refused
        the 129th (reject-batch {3, AgxWin32AsahiBatchFinishCompute writer
        add_written}), the backend failed and the device was removed, in
        relaxed and native CDM barrier modes alike. Run the real add_written
        with the real limit over that batch; the stack array stays small."""
        import subprocess, tempfile
        text = WINSYS.read_text()
        limit = re.search(r'#define AGX_G4_WRITTEN_LIMIT (\d+)u', text).group(0)
        start = text.index('static int add_written(')
        body = text[start:text.index('\n}\n', start) + 3]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef struct { uint64_t Allocation; } AGX_WIN32_GPUVA_BO;
typedef struct { int unused; } AGX_WIN32_ASAHI_BACKEND;
struct agx_bo { AGX_WIN32_GPUVA_BO View; };
static const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(AGX_WIN32_ASAHI_BACKEND *b, struct agx_bo *bo) {
  (void)b; return &bo->View; }
static unsigned AgxWin32AsahiVirtualBackingCount(struct agx_bo *bo) { (void)bo; return 0; }
static struct agx_bo *AgxWin32AsahiVirtualBacking(struct agx_bo *bo, unsigned i) { (void)i; return bo; }
static int AgxWin32AsahiIsTileScratch(AGX_WIN32_ASAHI_BACKEND *b, struct agx_bo *bo) { (void)b; (void)bo; return 0; }
''' + limit + '\n' + body + r'''
int main(void) {
  static struct agx_bo bos[130];
  AGX_WIN32_ASAHI_BACKEND b = {0};
  const AGX_WIN32_GPUVA_BO *written[AGX_G4_WRITTEN_LIMIT];
  unsigned n = 0;
  assert(sizeof(written) <= 8192 && "stack array per submission");
  for (unsigned i = 0; i < 130; ++i) { bos[i].View.Allocation = i + 1; assert(add_written(&b, written, &n, &bos[i])); }
  for (unsigned i = 0; i < 130; ++i) assert(add_written(&b, written, &n, &bos[i]));
  assert(n == 130);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / 'w.c'
            src.write_text(program)
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Werror', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(Path(d) / 'w')],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(Path(d) / 'w')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_the_d3d12_owner_reports_batch_refusals(self):
        owner = OWNER.read_text()
        create = owner[owner.index('owner->Context = AgxWin32AsahiContextCreate(owner->Screen, &owner->Owner);'):]
        create = create[:create.index('\n  }\n')]
        self.assertIn('AgxWin32BatchRefusalHook = owner_batch_refusal;', create)
        self.assertIn('AgxWin32BackendFailHook = owner_backend_fail;', create)
        self.assertIn('AdmissionUmdDiagnostic("reject-batch", E_FAIL, values, 4u);', owner)


if __name__ == '__main__':
    unittest.main()
