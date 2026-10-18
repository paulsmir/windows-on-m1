/* EXP1191: buffer policy of the AGX WGL winsys framebuffer
 * (agx_wgl_framebuffer.c).
 *
 * Mesa's WGL frontend (stw_st.c) takes a window's colour buffers from a
 * winsys framebuffer once per size (get_resource for FRONT_LEFT and
 * BACK_LEFT), renders into the one it holds as BACK_LEFT, and on SwapBuffers
 * calls present() and, when present succeeds, exchanges its FRONT_LEFT and
 * BACK_LEFT pointers. The winsys owns two buffers and the index of the one
 * stw holds as BACK_LEFT; present shows that buffer and moves the index to
 * the other one, as a two-buffer DXGI flip swapchain does for the d3d12
 * winsys. Invariant: the buffer present shows is the buffer stw rendered
 * into, and get_resource(BACK_LEFT) is the buffer stw holds as BACK_LEFT.
 * Plain C so the host suite can test it. */
#ifndef AGX_WGL_FRAMEBUFFER_POLICY_H
#define AGX_WGL_FRAMEBUFFER_POLICY_H

/* Index of the buffer for an attachment (0 FRONT_LEFT, 1 BACK_LEFT of
 * enum st_attachment_type), or -1 for attachments the winsys does not own. */
static inline int agx_wgl_fb_slot(unsigned back, unsigned statt) {
  if (statt == 1u) return (int)(back & 1u);
  if (statt == 0u) return (int)((back ^ 1u) & 1u);
  return -1;
}

/* After a successful present: stw swaps its pointers, so BACK_LEFT is now
 * the other buffer. */
static inline unsigned agx_wgl_fb_after_present(unsigned back) {
  return (back ^ 1u) & 1u;
}

/* EXP1209: WGL_EXT_swap_control pacing on a steady swap grid.
 *
 * stw paces its GDI path by sleeping 1.75 x (period - time since the last
 * swap), measured before the sleep (stw_framebuffer.c wait_swap_interval).
 * A frame shorter than the period therefore sleeps too long and the next
 * one not at all: with CS 1.6's 12 ms frames and a 60 Hz interval the
 * swaps alternate 12 and 20 ms (EXP1207: median 17.1 ms, and the present
 * worker drops the frame after each short interval). Here the swaps follow
 * a grid of one period: a swap before its grid point waits for it; a swap
 * late by less than a quarter period keeps the grid (the next one has less
 * to wait, so a timer overshoot never accumulates); a later swap, the first
 * swap, a grid point more than one period ahead (interval or clock change)
 * or no pacing (period <= 0) restarts it. Returns the microseconds to wait
 * and moves *next_us to the next grid point (0: no grid). */
static inline long long agx_wgl_pace(long long *next_us, long long now_us,
                                     long long period_us) {
  long long next = *next_us;
  if (period_us <= 0) { *next_us = 0; return 0; }
  if (!next || now_us - next >= period_us / 4 || next - now_us > period_us) {
    *next_us = now_us + period_us;
    return 0;
  }
  *next_us = next + period_us;
  return next > now_us ? next - now_us : 0;
}

/* G3: WGL_EXT_swap_control on the display's vertical blanks
 * (D3DKMTWaitForVerticalBlankEvent, the WDDM wait of an OpenGL ICD), so the
 * swaps follow the vsync that DWM composes on instead of a CPU clock.
 * *last_us is when the previous swap's vblank happened (0: none yet). A
 * swap waits for the vblanks still missing from `interval` since then; a
 * later swap (a frame over its budget) presents at once and moves *last_us
 * to the latest vblank already passed, so a slow frame costs its own time,
 * never an extra period. Returns the vblanks to wait; after waiting, the
 * caller stores the time the last wait returned in *last_us. No pacing
 * (interval or period <= 0) never waits and forgets the vblank. */
static inline unsigned agx_wgl_vblank_waits(long long *last_us, long long now_us,
                                            long long period_us, int interval) {
  long long last = *last_us, passed;
  if (interval <= 0 || period_us <= 0) { *last_us = 0; return 0; }
  if (!last || now_us < last) return 1u;
  passed = (now_us - last) / period_us;
  if (passed >= interval) { *last_us = last + passed * period_us; return 0u; }
  return (unsigned)(interval - passed);
}

#endif
