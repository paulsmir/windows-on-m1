"""EXP1166: a draw that re-enters draw_vbo on the same batch is legal.

The Asahi draw path re-enters pipe->draw_vbo on the current batch for
num_draws > 1 (util_draw_multi), unrolled or emulated indirect draws,
transform-feedback draws and primitive-restart emulation. GoldSrc's
immediate-mode rendering produces such multi-draws; with a one-level Enter
flag the nested Enter was refused (reject-batch kind 7, bits 8), the GL
context faulted and every later draw was dropped: CS 1.6 froze after the
team choice. Invariants: Enter nests (depth count) and Leave unwinds one
level; Release refuses while any level is entered; a submitted or
rejected batch still refuses Enter.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from g3_vidmm_replay import body  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"

PROGRAM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint64_t Fence; unsigned Entered, Submitted, Retired, Rejected; } AGX_G4_BATCH;
struct agx_batch { void *windows_batch; unsigned draws; };
static unsigned refusals, last_kind, last_site;
void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);
static void hook(unsigned kind, unsigned site, unsigned d0, unsigned d1) {
  (void)d0; (void)d1; ++refusals; last_kind = kind; last_site = site;
}
@@FUNCTIONS@@
int main(void) {
  AgxWin32BatchRefusalHook = hook;
  AGX_G4_BATCH g; memset(&g, 0, sizeof(g));
  struct agx_batch batch = {&g, 0};
  /* util_draw_multi: the outer draw enters, each inner draw enters again. */
  assert(AgxWin32AsahiBatchEnter(&batch));
  assert(AgxWin32AsahiBatchEnter(&batch) && AgxWin32AsahiBatchEnter(&batch));
  assert(refusals == 0 && g.Entered == 3);
  assert(AgxWin32AsahiBatchLeave(&batch) && AgxWin32AsahiBatchLeave(&batch));
  assert(g.Entered == 1);
  assert(AgxWin32AsahiBatchLeave(&batch) && g.Entered == 0);
  /* An unbalanced Leave is still refused. */
  assert(!AgxWin32AsahiBatchLeave(&batch));
  /* A submitted or rejected batch refuses Enter. */
  g.Submitted = 1;
  assert(!AgxWin32AsahiBatchEnter(&batch) && refusals == 1 && last_kind == 7u && last_site == 2u);
  g.Submitted = 0; g.Rejected = 1;
  assert(!AgxWin32AsahiBatchEnter(&batch) && last_site == 4u);
  g.Rejected = 0;
  struct agx_batch empty = {NULL, 0};
  assert(!AgxWin32AsahiBatchEnter(&empty) && last_site == 1u);
  puts("PASS");
  return 0;
}
'''


class NestedEnter(unittest.TestCase):
    def test_nested_draws_enter_and_leave_by_depth(self):
        source = SRC.read_text()
        functions = "\n".join(body(source, name) for name in (
            "capsule", "batch_refuse", "AgxWin32AsahiBatchEnter", "AgxWin32AsahiBatchLeave"))
        program = PROGRAM.replace("@@FUNCTIONS@@", functions)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "enter.c"
            exe = Path(directory) / "enter"
            path.write_text(program)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                                    str(path), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_release_refuses_while_entered_and_finish_unwinds_all_levels(self):
        source = SRC.read_text()
        self.assertIn("|| g->Entered)", body(source, "AgxWin32AsahiBatchRelease"))
        finish = body(source, "AgxWin32AsahiBatchFinish")
        self.assertIn("g->Entered=0;", finish)
        self.assertNotIn("AgxWin32AsahiBatchLeave(batch)", finish)

if __name__ == "__main__":
    unittest.main()
