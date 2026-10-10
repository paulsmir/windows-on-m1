"""EXP1171: the GL present hands frames to a worker thread through a
three-slot mailbox.

Invariants (drive the policy as agx_wgl_icd.cpp does, with a worker that
finishes at random presents): the GPU copy never targets the slot the worker
is reading (that would tear the shown image), a copy slot always exists, at
most one slot is showing, shown frames are strictly newer than the previous
shown frame, the worker always gets the newest copied frame, and with a
worker that keeps up every frame N-1 is shown and nothing is dropped.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICD = ROOT / "drivers/apple-agx/windows/icd"

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include "agx_wgl_present_ring.h"
static unsigned rng = 12345u;
static unsigned next(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static void run(unsigned finish_percent, int expect_no_drops) {
  unsigned state[AGX_WGL_RING_SLOTS] = {0};
  long long seq[AGX_WGL_RING_SLOTS] = {0};
  unsigned dropped = 0, shown_count = 0;
  long long last_shown = 0;
  for (long long frame = 1; frame <= 20000; ++frame) {
    /* the worker may finish its frame before this present */
    for (unsigned i = 0; i < AGX_WGL_RING_SLOTS; ++i)
      if (state[i] == AGX_WGL_SLOT_SHOWING && next() % 100u < finish_percent)
        state[i] = AGX_WGL_SLOT_FREE;
    int showing = -1;
    for (unsigned i = 0; i < AGX_WGL_RING_SLOTS; ++i)
      if (state[i] == AGX_WGL_SLOT_SHOWING) { assert(showing < 0); showing = (int)i; }
    int w = agx_wgl_slot_for_copy(state, seq, AGX_WGL_RING_SLOTS, &dropped);
    assert(w >= 0 && w != showing && state[w] == AGX_WGL_SLOT_FREE);
    state[w] = AGX_WGL_SLOT_COPIED; seq[w] = frame;
    if (showing < 0) {
      int p = agx_wgl_slot_to_show(state, seq, AGX_WGL_RING_SLOTS, w);
      if (p >= 0) {
        for (unsigned i = 0; i < AGX_WGL_RING_SLOTS; ++i)
          if ((int)i != w && state[i] == AGX_WGL_SLOT_COPIED) assert(seq[i] <= seq[p]);
        assert(seq[p] > last_shown && seq[p] < frame);
        if (expect_no_drops) assert(seq[p] == frame - 1);
        last_shown = seq[p];
        state[p] = AGX_WGL_SLOT_SHOWING;
        dropped += agx_wgl_drop_older(state, seq, AGX_WGL_RING_SLOTS, p);
        ++shown_count;
      }
    }
  }
  if (expect_no_drops) assert(dropped == 0 && shown_count == 19999u);
  else assert(dropped > 0 && shown_count > 0);
}
int main(void) {
  run(100u, 1);  /* the worker always finishes before the next present */
  run(30u, 0);   /* a slow worker: frames are replaced, never torn */
  run(0u, 0);    /* a stuck worker: the game still always finds a slot */
  puts("PASS");
  return 0;
}
'''


class PresentMailbox(unittest.TestCase):
    def test_mailbox_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "t.c"
            exe = Path(tmp) / "t"
            src.write_text(PROGRAM)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", "-I", str(ICD),
                            str(src), "-o", str(exe)], check=True)
            out = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
            self.assertIn("PASS", out.stdout)

    def test_present_uses_the_mailbox_worker(self):
        text = (ICD / "agx_wgl_icd.cpp").read_text()
        self.assertIn('#include "agx_wgl_present_ring.h"', text)
        for call in ("agx_wgl_slot_for_copy(", "agx_wgl_slot_to_show(",
                     "agx_wgl_drop_older(", "wgl_present_worker", "WindowFromDC("):
            self.assertIn(call, text)


if __name__ == "__main__":
    unittest.main()
