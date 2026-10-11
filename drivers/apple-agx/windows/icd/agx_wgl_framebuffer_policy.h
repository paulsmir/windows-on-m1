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

#endif
