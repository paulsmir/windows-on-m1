"""rc105: GravityMark D3D12 Earth/shadow flicker -- a batch must never be
submitted before an unsubmitted batch that writes a BO it uses
(drivers/apple-agx/mesa/winsys/agx_win32_submit_order.h, projected into
agx_flush_batch by mesa/scripts/native-asahi-submit-order.py;
APPLE_AGX_D3D12_SUBMIT_ORDER=0 restores Asahi's order, receipt only).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- D3D12 ResourceBarrier is a no-op, so writer->reader order between batches
  rests on Asahi's hazard tracking; agx_flush_all submits the open batches
  in SLOT order. A reader whose use of a BO reached its BO list without
  agx_batch_reads (agx_batch_add_bo) and whose slot is lower than the open
  writer's runs first on the in-order KMD queue and reads last frame's data.
  The scan must name exactly the open writers of the batch's BOs (not
  itself, not submitted/inactive batches, each once) for any handle and slot
  position, and the projected agx_flush_batch must flush them before it
  encodes or submits the batch, re-checking the batch afterwards.
- The kill switch parses exactly "0" as receipt-only; everything else
  enforces.
- The projected agx_pipe.c must compile its injected code against the real
  Asahi types with every called function declared (EXP1929: implicit
  declarations break the Windows build).
"""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

from test_dxbc_to_nir import ROOT, local_tree, host_flags
from test_d3d10_shader_fields_init import project

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
HEADER = WINSYS / 'agx_win32_submit_order.h'
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-submit-order.py'

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "agx_win32_submit_order.h"
#define SLOTS 128u
static uint32_t bo[64], active[4];
static uint8_t writer[2048];
static void use(unsigned h) { bo[h / 32] |= 1u << (h % 32); }
static void open_slot(unsigned s) { active[s / 32] |= 1u << (s % 32); }
static unsigned scan(unsigned self, uint8_t *out) {
  return agx_win32_submit_order_writers(bo, 2048, writer, 2048, active, SLOTS, self, out);
}
int main(void) {
  uint8_t out[SLOTS];
  /* GravityMark frame shape: compute writer in slot 9 (u3 shadow data), the
   * shadow render batch in slot 2 uses the BO without a tracked read */
  use(700); writer[700] = 9 + 1; open_slot(9); open_slot(2);
  assert(scan(2, out) == 1 && out[0] == 9);
  /* the writer already submitted (not active): nothing to do */
  active[0] &= ~(1u << 9);
  assert(scan(2, out) == 0);
  /* the batch is its own writer */
  open_slot(9); writer[700] = 2 + 1;
  assert(scan(2, out) == 0);
  /* several BOs of one writer name it once; ascending slots; high slots */
  memset(writer, 0, sizeof(writer));
  use(3); use(31); use(32); use(2047);
  writer[3] = 101; writer[31] = 101; writer[32] = 6; writer[2047] = 128;
  open_slot(100); open_slot(5); open_slot(127);
  unsigned n = scan(2, out);
  assert(n == 3 && out[0] == 5 && out[1] == 100 && out[2] == 127);
  /* a writer map shorter than the BO list, BOs without a writer */
  assert(agx_win32_submit_order_writers(bo, 2048, writer, 32, active, SLOTS, 2, out) == 1 && out[0] == 100);
  /* out-of-range slot values are ignored */
  memset(writer, 0, sizeof(writer)); writer[3] = 200;
  assert(scan(2, out) == 0);
  assert(agx_win32_submit_order_writers(NULL, 0, writer, 1, active, SLOTS, 0, out) == 0);
  /* kill switch */
  assert(agx_win32_submit_order_parse("0") == AGX_WIN32_SUBMIT_ORDER_RECEIPT);
  assert(agx_win32_submit_order_parse(NULL) == AGX_WIN32_SUBMIT_ORDER_ENFORCE);
  assert(agx_win32_submit_order_parse("") == AGX_WIN32_SUBMIT_ORDER_ENFORCE);
  assert(agx_win32_submit_order_parse("1") == AGX_WIN32_SUBMIT_ORDER_ENFORCE);
  assert(agx_win32_submit_order_parse("00") == AGX_WIN32_SUBMIT_ORDER_ENFORCE);
  puts("PASS");
  return 0;
}
'''

def run_c(source, include, name, cflags=()):
    with tempfile.TemporaryDirectory(prefix='rc105-') as tmp:
        src = Path(tmp) / (name + '.c')
        exe = Path(tmp) / name
        src.write_text(source)
        r = subprocess.run(['clang', '-std=c99', '-Wall', '-Wextra', '-Werror', *cflags, '-I', str(include),
                            str(src), '-o', str(exe)], capture_output=True, text=True)
        if r.returncode:
            return r.returncode, r.stderr
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr


class SubmitOrderScan(unittest.TestCase):
    def test_scan_names_open_writers(self):
        code, out = run_c(HARNESS, WINSYS, 'scan')
        self.assertEqual(code, 0, out)
        self.assertIn('PASS', out)

    def test_header_is_plain_c(self):
        text = HEADER.read_text()
        for banned in ('<stdio.h>', '<windows.h>', '__builtin'):
            self.assertNotIn(banned, text)

    def test_projection_refuses_a_second_run(self):
        spec = importlib.util.spec_from_file_location('submit_order_again', SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        once = module.GUARD + ' rest'
        projected = module.project_pipe('#include "agx_state.h"\n' + once)
        self.assertIn('agx_win32_submit_order_writers(', projected)
        with self.assertRaises(RuntimeError):
            module.project_pipe(projected)


@unittest.skipUnless(local_tree('reference', 'mesa') and local_tree('accelerated-desktop-ad03', 'mesa-build'),
                     'pinned Mesa reference and host build not present')
class SubmitOrderProjection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='submit-order-projection-')
        cls.out = project(Path(cls.tmp.name) / 'native')
        cls.pipe = (cls.out / 'src/gallium/drivers/asahi/agx_pipe.c').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_writers_flush_before_the_batch_is_encoded(self):
        start = self.pipe.index('\nagx_flush_batch(struct agx_context *ctx, struct agx_batch *batch)')
        body = self.pipe[start:self.pipe.index('\n}\n', start)]
        order = [body.index(s) for s in (
            'agx_batch_is_submitted(batch)) { AgxWin32WaitEnd(wait_token); return; }',
            'agx_win32_submit_order_writers(',
            'agx_flush_batch(ctx, writer);',
            'AgxWin32AsahiBatchAbort(batch)',
            'agx_flush_compute(ctx, batch',
            'agx_batch_submit(ctx, batch')]
        self.assertEqual(order, sorted(order))
        self.assertEqual(body.count('agx_batch_is_submitted(batch)) { AgxWin32WaitEnd(wait_token); return; }'), 2)
        self.assertIn('#include "agx_win32_submit_order.h"', self.pipe)

    def test_projected_pipe_unit_declares_everything_it_calls(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        p = Path(self.tmp.name) / 'stub'
        p.mkdir(exist_ok=True)
        (p / 'xf86drm.h').write_text('#pragma once\nint drmSyncobjImportSyncFile(int, unsigned, int);\n')
        out = self.out
        includes = [p, out / 'src', out / 'include', out / 'include/drm-uapi', out / 'src/asahi/lib',
                    out / 'src/asahi/libagx', out / 'src/asahi/layout', out / 'src/asahi/compiler',
                    out / 'src/gallium/drivers/asahi', build / 'src/asahi', build / 'src/asahi/genxml',
                    build / 'src/asahi/libagx', build / 'src/poly', build / 'src/compiler/nir',
                    mesa / 'src/gallium/auxiliary', mesa / 'src/gallium/include', mesa / 'src/virtio/vdrm',
                    ROOT / 'drivers/apple-agx/mesa/winsys', ROOT / 'drivers/apple-agx/shared/include']
        r = subprocess.run(['clang', '-std=c11', '-DAPPLE_AGX_GPUVA_WINSYS', '-Wno-c23-extensions',
                            '-Werror=implicit-function-declaration', '-include', str(p / 'xf86drm.h'),
                            *['-I' + str(i) for i in includes], *host_flags(build), '-fsyntax-only',
                            str(out / 'src/gallium/drivers/asahi/agx_pipe.c')], capture_output=True, text=True)
        self.assertNotIn('fatal error', r.stderr, r.stderr[-4000:])
        start = self.pipe.index('/* rc105 (agx_win32_submit_order.h)')
        first = self.pipe[:start].count('\n') + 1
        errors = [l for l in r.stderr.splitlines() if 'error:' in l and 'agx_pipe.c:' in l]
        ours = [l for l in errors if first <= int(l.split('agx_pipe.c:')[1].split(':')[0]) <= first + 25]
        self.assertEqual(ours, [], r.stderr[-4000:])
        self.assertFalse([l for l in r.stderr.splitlines() if 'implicit' in l and 'agx_win32_submit_order' in l])


if __name__ == '__main__':
    unittest.main()
