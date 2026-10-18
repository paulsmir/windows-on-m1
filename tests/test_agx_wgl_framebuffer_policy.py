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

EXP1209 pacing (agx_wgl_pace, WGL_EXT_swap_control): stw's 1.75 x sleep
alternates 12 and 20 ms swaps for 12 ms frames at 60 Hz. On the swap grid:
frames shorter than the period swap exactly one period apart; a timer
overshoot never accumulates into the cadence; a frame late by less than a
quarter period keeps the grid and the next swap catches up (average spacing
stays the period, no swap closer than three quarters of it); a later frame
restarts the grid without a burst; no pacing (interval 0) never waits.
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


PACE = r'''
#include <assert.h>
#include <stdio.h>
#include "agx_wgl_framebuffer_policy.h"
static long long grid, clock_us, last_swap;
/* One frame of `work` us, then SwapBuffers: wait, swap; returns the swap interval. */
static long long frame(long long work, long long period, long long overshoot) {
  clock_us += work;
  long long wait = agx_wgl_pace(&grid, clock_us, period);
  assert(wait >= 0 && wait <= period);
  if (wait) clock_us += wait + overshoot;
  long long interval = clock_us - last_swap; last_swap = clock_us;
  return interval;
}
int main(void) {
  const long long P = 16666;
  /* stw for comparison: 12 ms frames alternate */
  {
    long long prev = 0, t = 0, a[6];
    for (int i = 0; i < 6; ++i) {
      t += 12000; long long now = t;
      if (prev && now - prev < P) t += (long long)((P - (now - prev)) * 1.75f);
      a[i] = t; prev = now;
    }
    assert(a[3] - a[2] != a[4] - a[3]);
  }
  /* steady short frames: exactly one period apart, with or without overshoot */
  for (long long over = 0; over <= 900; over += 300) {
    grid = 0; clock_us = 1000000; last_swap = clock_us;
    (void)frame(12000, P, over);
    long long first = last_swap;
    for (int i = 0; i < 600; ++i) {
      long long iv = frame(12000, P, over);
      assert(iv >= P - over && iv <= P + over);
    }
    long long avg = (last_swap - first) / 600;
    assert(avg >= P - 1 && avg <= P + 1 + over / 600);
  }
  /* jittery frames around 13-17.5 ms: average spacing the period, none
   * closer than three quarters of it, no wait longer than a period */
  {
    unsigned rng = 9u; grid = 0; clock_us = 0; last_swap = 0;
    (void)frame(10000, P, 0);
    long long first = last_swap; int n = 0;
    for (int i = 0; i < 4000; ++i) {
      rng = rng * 1103515245u + 12345u;
      long long work = 13000 + (long long)((rng >> 8) % 4500);
      long long iv = frame(work, P, 0);
      assert(iv >= P * 3 / 4); ++n;
    }
    long long avg = (last_swap - first) / n;
    assert(avg >= P && avg <= P + 400);
  }
  /* a frame late by more than a quarter period restarts the grid: no burst */
  grid = 0; clock_us = 0; last_swap = 0;
  (void)frame(12000, P, 0); (void)frame(12000, P, 0);
  assert(frame(30000, P, 0) == 30000);
  assert(frame(12000, P, 0) == P);
  /* interval 0: never waits, no grid */
  grid = 0; clock_us = 0; last_swap = 0;
  for (int i = 0; i < 10; ++i) assert(frame(5000, 0, 0) == 5000 && grid == 0);
  /* a grid point more than one period ahead (interval 2 -> 1) restarts */
  grid = 0; clock_us = 0; last_swap = 0;
  (void)frame(1000, 2 * P, 0); (void)frame(1000, 2 * P, 0);
  assert(frame(1000, P, 0) == 1000);
  assert(frame(1000, P, 0) == P);
  printf("PACE PASS\n");
  return 0;
}
'''


VBLANK = r'''
#include <assert.h>
#include <stdio.h>
#include "agx_wgl_framebuffer_policy.h"
/* The display's vblanks at phase + k*P; a wait returns at the next one. */
static long long P = 16666, phase = 3210, clock_us, last_vblank, last_swap;
static long long next_vblank(long long t) { return phase + ((t - phase) / P + 1) * P; }
/* One frame of `work` us, then SwapBuffers; returns the swap interval. */
static long long frame(long long work, int interval, unsigned *waited) {
  clock_us += work;
  unsigned n = agx_wgl_vblank_waits(&last_vblank, clock_us, P, interval);
  assert(n <= (unsigned)(interval > 0 ? interval : 0));
  for (unsigned i = 0; i < n; ++i) clock_us = next_vblank(clock_us);
  if (n) last_vblank = clock_us;
  if (waited) *waited = n;
  long long iv = clock_us - last_swap; last_swap = clock_us;
  return iv;
}
static void reset(void) { clock_us = 1000000; last_vblank = 0; last_swap = clock_us; }
int main(void) {
  unsigned n;
  /* short frames, interval 1: one swap per vblank, on the vblank */
  reset(); (void)frame(12000, 1, &n); assert(n == 1);
  for (int i = 0; i < 600; ++i) {
    assert(frame(12000, 1, &n) == P && n == 1);
    assert((clock_us - phase) % P == 0);
  }
  /* interval 2: every other vblank */
  reset(); (void)frame(5000, 2, NULL);
  for (int i = 0; i < 100; ++i) assert(frame(5000, 2, NULL) == 2 * P);
  /* frames a little over the period: never wait a whole extra period, the
   * swaps follow the frames (adaptive), and the next fast frame is back on
   * a vblank */
  reset(); (void)frame(12000, 1, NULL);
  for (int i = 0; i < 300; ++i) { assert(frame(17500, 1, &n) == 17500 && n == 0); }
  assert(frame(5000, 1, &n) < P && n == 1 && (clock_us - phase) % P == 0);
  assert(frame(12000, 1, &n) == P && n == 1);
  /* jittery 13-17.5 ms frames: no swap interval above the frame time plus
   * one period, average spacing at most the period plus the late share */
  { unsigned rng = 9u; reset(); (void)frame(10000, 1, NULL);
    long long first = last_swap; int frames = 4000;
    for (int i = 0; i < frames; ++i) {
      rng = rng * 1103515245u + 12345u;
      long long work = 13000 + (long long)((rng >> 8) % 4500);
      long long iv = frame(work, 1, NULL);
      assert(iv >= work && iv < work + P);
    }
    long long avg = (last_swap - first) / frames;
    assert(avg >= P && avg <= P + 1200); }
  /* interval 0 never waits and forgets the vblank */
  reset(); (void)frame(12000, 1, NULL);
  for (int i = 0; i < 10; ++i) assert(frame(5000, 0, &n) == 5000 && n == 0 && last_vblank == 0);
  printf("VBLANK PASS\n");
  return 0;
}
'''


class FramebufferPolicy(unittest.TestCase):
    def test_vblank_pacing_follows_the_display(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'v.c').write_text(VBLANK)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-fsanitize=address,undefined',
                            '-I' + str(ICD), str(tmp / 'v.c'), '-o', str(tmp / 'v')], check=True)
            out = subprocess.run([str(tmp / 'v')], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            self.assertIn('VBLANK PASS', out.stdout)

    def test_swap_pacing_keeps_a_steady_grid(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'p.c').write_text(PACE)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Werror', '-fsanitize=address,undefined',
                            '-I' + str(ICD), str(tmp / 'p.c'), '-o', str(tmp / 'p')], check=True)
            out = subprocess.run([str(tmp / 'p')], capture_output=True, text=True)
            self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
            self.assertIn('PACE PASS', out.stdout)
        fb = (ICD / 'agx_wgl_framebuffer.c').read_text()
        present = fb[fb.index('static bool agx_wgl_framebuffer_present('):]
        self.assertLess(present.index('agx_wgl_framebuffer_pace(fb, interval);'),
                        present.index('agx_wgl_present_window('))
        pace = fb[fb.index('static void agx_wgl_framebuffer_pace('):fb.index('static bool agx_wgl_framebuffer_present(')]
        self.assertIn('agx_wgl_pace(&fb->next_swap_us', pace)
        self.assertIn('CREATE_WAITABLE_TIMER_HIGH_RESOLUTION', fb)

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
