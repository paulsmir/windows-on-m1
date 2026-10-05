#include "apple_agx_gfx_handoff.h"

#define AGX_NULL ((void *)0)

static unsigned char io_valid(const APPLE_AGX_GFX_HANDOFF_IO *io) {
  return (unsigned char)(io != AGX_NULL && io->Read8 != AGX_NULL &&
      io->Read32 != AGX_NULL && io->Read64 != AGX_NULL &&
      io->Write8 != AGX_NULL && io->Write32 != AGX_NULL &&
      io->Write64 != AGX_NULL && io->Barrier != AGX_NULL &&
      io->Relax != AGX_NULL && io->Now != AGX_NULL);
}
static unsigned char read8(APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned int o,
                           unsigned char *v) {
  if (!s->Io.Read8(s->Io.Context, o, v)) return 0u;
  s->Io.Barrier(s->Io.Context); return 1u;
}
static unsigned char read32(APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned int o,
                            unsigned int *v) {
  if (!s->Io.Read32(s->Io.Context, o, v)) return 0u;
  s->Io.Barrier(s->Io.Context); return 1u;
}
static unsigned char write8(APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned int o,
                            unsigned char v) {
  if (!s->Io.Write8(s->Io.Context, o, v)) return 0u;
  s->Io.Barrier(s->Io.Context); return 1u;
}
static unsigned char write32(APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned int o,
                             unsigned int v) {
  if (!s->Io.Write32(s->Io.Context, o, v)) return 0u;
  s->Io.Barrier(s->Io.Context); return 1u;
}
static unsigned char read64(APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned int o,
                            unsigned long long *v) {
  if (!s->Io.Read64(s->Io.Context, o, v)) return 0u;
  s->Io.Barrier(s->Io.Context); return 1u;
}
static unsigned char write64(APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned int o,
                             unsigned long long v) {
  if (!s->Io.Write64(s->Io.Context, o, v)) return 0u;
  s->Io.Barrier(s->Io.Context); return 1u;
}
static unsigned char timed_out(APPLE_AGX_GFX_HANDOFF_STATE *s,
    unsigned long long start, unsigned long long timeout) {
  return (unsigned char)((s->Io.Now(s->Io.Context) - start) >= timeout);
}
static APPLE_AGX_GFX_HANDOFF_RESULT relinquish(
    APPLE_AGX_GFX_HANDOFF_STATE *s, APPLE_AGX_GFX_HANDOFF_RESULT result) {
  if (!write8(s, APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET, 0u))
    return AppleAgxGfxHandoffResultAccessFailed;
  return result;
}

APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffBindJ313(
    APPLE_AGX_GFX_HANDOFF_STATE *s, const APPLE_AGX_GFX_HANDOFF_REGION *r,
    const APPLE_AGX_GFX_HANDOFF_IO *io) {
  if (s == AGX_NULL || r == AGX_NULL || !io_valid(io) || s->Bound)
    return AppleAgxGfxHandoffResultInvalidArgument;
  if (r->PhysicalBase != J313_AGX_G2_HANDOFF_BASE ||
      r->Length != (unsigned int)J313_AGX_G2_HANDOFF_SIZE ||
      r->Length < APPLE_AGX_GFX_HANDOFF_MINIMUM_SIZE)
    return AppleAgxGfxHandoffResultInvalidGeometry;
  s->Region = *r; s->Io = *io; s->Locked = 0u; s->Initialized = 0u;
  s->Bound = 1u;
  return AppleAgxGfxHandoffResultOk;
}

APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffInitialize(
    APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned long long deadline) {
  APPLE_AGX_GFX_HANDOFF_RESULT result;
  unsigned long long magic;
  unsigned long long now;
  unsigned int index;

  if (s == AGX_NULL || !s->Bound || s->Locked)
    return AppleAgxGfxHandoffResultInvalidState;
  if (s->Initialized)
    return AppleAgxGfxHandoffResultOk;
  now = s->Io.Now(s->Io.Context);
  if (now >= deadline)
    return AppleAgxGfxHandoffResultTimeout;
  if (!write64(s, APPLE_AGX_GFX_HANDOFF_MAGIC_AP_OFFSET,
               APPLE_AGX_GFX_HANDOFF_PPL_MAGIC))
    return AppleAgxGfxHandoffResultAccessFailed;
  result = AppleAgxGfxHandoffAcquire(s, deadline - now);
  if (result != AppleAgxGfxHandoffResultOk)
    return result;
  for (;;) {
    if (!read64(s, APPLE_AGX_GFX_HANDOFF_MAGIC_FW_OFFSET, &magic)) {
      (void)AppleAgxGfxHandoffRelease(s);
      return AppleAgxGfxHandoffResultAccessFailed;
    }
    if (magic == APPLE_AGX_GFX_HANDOFF_PPL_MAGIC)
      break;
    if (s->Io.Now(s->Io.Context) >= deadline) {
      (void)AppleAgxGfxHandoffRelease(s);
      return AppleAgxGfxHandoffResultTimeout;
    }
    s->Io.Relax(s->Io.Context);
  }
  result = AppleAgxGfxHandoffRelease(s);
  if (result != AppleAgxGfxHandoffResultOk)
    return result;
  for (index = 0u; index < APPLE_AGX_GFX_HANDOFF_FLUSH_COUNT; ++index) {
    unsigned int offset = index * APPLE_AGX_GFX_HANDOFF_FLUSH_STRIDE;
    if (!write64(s, APPLE_AGX_GFX_HANDOFF_FLUSH_STATE_OFFSET + offset, 0ULL) ||
        !write64(s, APPLE_AGX_GFX_HANDOFF_FLUSH_ADDR_OFFSET + offset, 0ULL) ||
        !write64(s, APPLE_AGX_GFX_HANDOFF_FLUSH_SIZE_OFFSET + offset, 0ULL))
      return AppleAgxGfxHandoffResultAccessFailed;
  }
  s->Initialized = 1u;
  return AppleAgxGfxHandoffResultOk;
}

APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffAcquire(
    APPLE_AGX_GFX_HANDOFF_STATE *s, unsigned long long timeout) {
  unsigned long long start; unsigned char fw; unsigned int turn;
  if (s == AGX_NULL || !s->Bound || s->Locked)
    return AppleAgxGfxHandoffResultInvalidState;
  start = s->Io.Now(s->Io.Context);
  if (!write8(s, APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET, 1u))
    return AppleAgxGfxHandoffResultAccessFailed;
  for (;;) {
    if (!read8(s, APPLE_AGX_GFX_HANDOFF_LOCK_FW_OFFSET, &fw))
      return relinquish(s, AppleAgxGfxHandoffResultAccessFailed);
    if (!fw) { s->Locked = 1u; return AppleAgxGfxHandoffResultOk; }
    if (!read32(s, APPLE_AGX_GFX_HANDOFF_TURN_OFFSET, &turn))
      return relinquish(s, AppleAgxGfxHandoffResultAccessFailed);
    if (turn != 0u) {
      if (!write8(s, APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET, 0u))
        return AppleAgxGfxHandoffResultAccessFailed;
      for (;;) {
        if (!read32(s, APPLE_AGX_GFX_HANDOFF_TURN_OFFSET, &turn))
          return AppleAgxGfxHandoffResultAccessFailed;
        if (turn == 0u) break;
        if (timed_out(s, start, timeout))
          return AppleAgxGfxHandoffResultTimeout;
        s->Io.Relax(s->Io.Context);
      }
      if (!write8(s, APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET, 1u))
        return AppleAgxGfxHandoffResultAccessFailed;
      continue;
    }
    if (timed_out(s, start, timeout))
      return relinquish(s, AppleAgxGfxHandoffResultTimeout);
    s->Io.Relax(s->Io.Context);
  }
}

APPLE_AGX_GFX_HANDOFF_RESULT AppleAgxGfxHandoffRelease(
    APPLE_AGX_GFX_HANDOFF_STATE *s) {
  if (s == AGX_NULL || !s->Bound || !s->Locked)
    return AppleAgxGfxHandoffResultInvalidState;
  if (!write32(s, APPLE_AGX_GFX_HANDOFF_TURN_OFFSET, 1u) ||
      !write8(s, APPLE_AGX_GFX_HANDOFF_LOCK_AP_OFFSET, 0u))
    return AppleAgxGfxHandoffResultAccessFailed;
  s->Locked = 0u;
  return AppleAgxGfxHandoffResultOk;
}
