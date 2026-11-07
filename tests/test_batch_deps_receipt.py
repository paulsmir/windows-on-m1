"""rc118: measure-batch-deps classifies consecutive batches by buffer conflicts.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The receipt sizes possible GPU overlap: a pair reported independent must not
share a written buffer (the Bloom sets may over-report conflicts, never
under-report), and the history must age batches in submission order.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'

SRC = r'''
#include <assert.h>
#include "agx_win32_batch_deps.h"
static AGX_WIN32_BATCH_DEPS d;
static unsigned batch(unsigned kind, const unsigned *r, unsigned nr, const unsigned *w, unsigned nw) {
  unsigned i;
  AgxWin32BatchDepsBegin(&d, kind);
  for (i = 0; i < nr; ++i) AgxWin32BatchDepsAdd(&d, r[i], 0);
  for (i = 0; i < nw; ++i) AgxWin32BatchDepsAdd(&d, w[i], 1);
  return AgxWin32BatchDepsCommit(&d);
}
int main(void) {
  const unsigned gbuf[] = {10}, light_in[] = {10, 11}, light_out[] = {12};
  const unsigned sim_in[] = {20}, sim_out[] = {21}, comp_in[] = {12};
  unsigned m;
  m = batch(1, 0, 0, gbuf, 1);           /* R writes 10 */
  assert(m == 0);                        /* no history */
  m = batch(2, light_in, 2, light_out, 1); /* C reads 10: RAW on the render */
  assert((m & 1u) && (m & 0x100u) && ((m >> 16) & 0xfu) == 1u);
  m = batch(2, sim_in, 1, sim_out, 1);   /* C on unrelated buffers */
  assert(!(m & 3u) && (m & 0x300u));
  m = batch(1, comp_in, 1, 0, 0);        /* R reads 12: RAW two back */
  assert(!(m & 1u) && (m & 2u));
  /* WAR: a batch writing what the previous one only read */
  m = batch(2, 0, 0, comp_in, 1);
  assert(m & 1u);
  return 0;
}
'''


class BatchDepsReceipt(unittest.TestCase):
    def test_conflicts_and_history(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'deps.c'
            src.write_text(SRC)
            exe = Path(tmp) / 'deps'
            subprocess.run(['clang', '-std=c99', '-Wall', '-Werror', '-I', str(WINSYS),
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_submit_paths_report_both_kinds(self):
        text = (WINSYS / 'agx_win32_gpuva_batch.c').read_text()
        self.assertIn('note_batch_deps(batch,1u);', text)
        self.assertIn('note_batch_deps(batch,2u);', text)
        owner = (WINSYS / 'agx_d3d12_windows.cpp').read_text()
        self.assertIn('AgxWin32BatchDepsHook = owner_batch_deps;', owner)


if __name__ == '__main__':
    unittest.main()
