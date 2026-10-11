"""EXP1191: the winsys framebuffer presents the buffer stw rendered into.

Mesa's WGL frontend takes a window's colour buffers from the winsys
framebuffer once per size (get_resource FRONT_LEFT/BACK_LEFT), renders into
the one it holds as BACK_LEFT, calls present() on SwapBuffers and, only when
present succeeds, exchanges its FRONT_LEFT/BACK_LEFT pointers
(stw_st_swap_framebuffer_locked); after a resize it takes both buffers
again. A rotation that drifts from stw's would show a stale frame every
other swap. Invariants, over random sequences of swaps, failed presents and
resizes: present shows the buffer stw rendered into; get_resource(BACK_LEFT)
is the buffer stw holds as BACK_LEFT; FRONT_LEFT is the other buffer; the
winsys owns nothing else; the build compiles the framebuffer unit and the
winsys registers it.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICD = ROOT / 'drivers/apple-agx/windows/icd'

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include "agx_wgl_framebuffer_policy.h"
static unsigned rng = 7u;
static unsigned rnd(unsigned n) { rng = rng * 1103515245u + 12345u; return (rng >> 8) % n; }
int main(void) {
  for (unsigned trial = 0; trial < 2000; ++trial) {
    unsigned back = 1u, generation = 0;
    int buffers[2], stw_front, stw_back, rendered = -1;
    /* resize: new buffers, stw takes FRONT then BACK */
    buffers[0] = 100 * (int)generation; buffers[1] = 100 * (int)generation + 1;
    stw_front = buffers[agx_wgl_fb_slot(back, 0u)];
    stw_back = buffers[agx_wgl_fb_slot(back, 1u)];
    assert(stw_front != stw_back);
    for (unsigned step = 0; step < 64; ++step) {
      unsigned op = rnd(10);
      if (op == 0) {            /* resize */
        ++generation; back = 1u;
        buffers[0] = 100 * (int)generation; buffers[1] = 100 * (int)generation + 1;
        stw_front = buffers[agx_wgl_fb_slot(back, 0u)];
        stw_back = buffers[agx_wgl_fb_slot(back, 1u)];
        continue;
      }
      rendered = stw_back;      /* st draws the frame */
      int shown = buffers[agx_wgl_fb_slot(back, 1u)];
      assert(shown == rendered);
      if (op == 1) continue;    /* present failed: no rotation, no stw swap */
      back = agx_wgl_fb_after_present(back);
      int t = stw_front; stw_front = stw_back; stw_back = t;
      assert(buffers[agx_wgl_fb_slot(back, 1u)] == stw_back);
      assert(buffers[agx_wgl_fb_slot(back, 0u)] == stw_front);
    }
    for (unsigned s = 2; s < 6; ++s) assert(agx_wgl_fb_slot(back, s) == -1);
  }
  printf("PASS\n");
  return 0;
}
'''


class FramebufferPolicy(unittest.TestCase):
    def test_present_shows_the_buffer_stw_rendered(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 't.c').write_text(PROGRAM)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-I' + str(ICD), str(tmp / 't.c'),
                            '-o', str(tmp / 't')], check=True)
            out = subprocess.run([str(tmp / 't')], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            self.assertIn('PASS', out.stdout)

    def test_winsys_registers_the_framebuffer(self):
        script = (ICD / 'build-icd-x86.ps1').read_text()
        self.assertIn('agx_wgl_framebuffer.c', script)
        icd = (ICD / 'agx_wgl_icd.cpp').read_text()
        table = re.search(r'static const struct stw_winsys AgxWglWinsys = \{(.*?)\};', icd, re.S).group(1)
        table = re.sub(r'/\*.*?\*/', '', table, flags=re.S)
        entries = [e.strip() for e in table.split(',') if e.strip()]
        # stw_winsys order: create_screen, present, get_adapter_luid,
        # shared_surface_open, shared_surface_close, compose, create_framebuffer, get_name
        self.assertEqual(entries[6], '&agx_wgl_create_framebuffer')
        fb = (ICD / 'agx_wgl_framebuffer.c').read_text()
        # Only double-buffered, non-GDI, single-sample formats (as the d3d12 winsys).
        for flag in ('PFD_SUPPORT_GDI', 'PFD_DOUBLEBUFFER', 'samples > 1'):
            self.assertIn(flag, fb)


if __name__ == '__main__':
    unittest.main()
