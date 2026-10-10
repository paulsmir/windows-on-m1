/* EXP1171: slot policy of the GL present mailbox. The game thread queues the
 * GPU copy of each frame into a staging slot and hands the newest finished
 * frame to the present worker; a frame the worker could not take is replaced
 * by a newer one. Invariants: the slot the worker is showing is never chosen
 * for a GPU copy, a copy slot always exists with three slots, the worker gets
 * the newest copied frame and older copied frames are dropped. Plain C so the
 * host suite can test it. */
#ifndef AGX_WGL_PRESENT_RING_H
#define AGX_WGL_PRESENT_RING_H

#define AGX_WGL_RING_SLOTS 3u

enum {
  AGX_WGL_SLOT_FREE = 0,
  AGX_WGL_SLOT_COPIED = 1,  /* GPU copy queued; the frame waits for the worker */
  AGX_WGL_SLOT_SHOWING = 2, /* owned by the worker until it frees the slot */
};

/* Slot for the next GPU copy: a free slot, else the oldest copied one, whose
 * frame is dropped (*dropped is incremented). Never the showing slot; -1 only
 * when every slot is showing. */
static inline int agx_wgl_slot_for_copy(unsigned *state, const long long *seq,
                                        unsigned count, unsigned *dropped) {
  int oldest = -1;
  for (unsigned i = 0; i < count; ++i) {
    if (state[i] == AGX_WGL_SLOT_FREE) return (int)i;
    if (state[i] == AGX_WGL_SLOT_COPIED &&
        (oldest < 0 || seq[i] < seq[oldest])) oldest = (int)i;
  }
  if (oldest >= 0) {
    state[oldest] = AGX_WGL_SLOT_FREE;
    ++*dropped;
  }
  return oldest;
}

/* Newest copied slot other than `except` (the copy just queued), or -1. */
static inline int agx_wgl_slot_to_show(const unsigned *state,
                                       const long long *seq, unsigned count,
                                       int except) {
  int newest = -1;
  for (unsigned i = 0; i < count; ++i)
    if ((int)i != except && state[i] == AGX_WGL_SLOT_COPIED &&
        (newest < 0 || seq[i] > seq[newest])) newest = (int)i;
  return newest;
}

/* After `shown` went to the worker: free the copied slots older than it
 * (their frames will never be shown). Returns the number dropped. */
static inline unsigned agx_wgl_drop_older(unsigned *state, const long long *seq,
                                          unsigned count, int shown) {
  unsigned dropped = 0;
  for (unsigned i = 0; i < count; ++i)
    if ((int)i != shown && state[i] == AGX_WGL_SLOT_COPIED &&
        seq[i] < seq[shown]) {
      state[i] = AGX_WGL_SLOT_FREE;
      ++dropped;
    }
  return dropped;
}

#endif
