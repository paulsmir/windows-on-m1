#ifndef APPLE_AGX_VSYNC_H
#define APPLE_AGX_VSYNC_H

#include "apple_agx_scanout.h"

/* Exact nonvirtual J313 timing2: EXP495 TimingElements, not rounded pixel Hz.
 * The fixed-panel owner in EXP907 applies timing2/color1 and confirms D589.
 * This models observation phase; the physical D589 phase offset is unmeasured. */
#define APPLE_AGX_VSYNC_RATE_NUMERATOR 3932151u
#define APPLE_AGX_VSYNC_RATE_DENOMINATOR 65536u
#define APPLE_AGX_VSYNC_PERIOD_NUMERATOR 655360000000ULL
#define APPLE_AGX_VSYNC_MAX 0xffffffffffffffffULL

typedef struct _APPLE_AGX_VSYNC_TIMELINE {
  APPLE_AGX_SCANOUT_U64 Phase100ns, LastPeriod;
  APPLE_AGX_SCANOUT_U64 ActiveSequence, ActiveAddress;
  APPLE_AGX_SCANOUT_BOOL ActiveValid, Enabled, Running;
} APPLE_AGX_VSYNC_TIMELINE;

static inline APPLE_AGX_SCANOUT_BOOL AppleAgxVsyncLatch(
    APPLE_AGX_VSYNC_TIMELINE *T, APPLE_AGX_SCANOUT_U64 Sequence,
    APPLE_AGX_SCANOUT_U64 Address, APPLE_AGX_SCANOUT_U64 Now100ns) {
  if (!Sequence || !Address || Sequence <= T->ActiveSequence)
    return APPLE_AGX_SCANOUT_FALSE;
  /* EXP1106: every latch is a hardware vblank; the period grid restarts at
   * it. Anchored only at the first latch, the grid drifted 25 us/s from the
   * panel (EXP1105). The latch's own notification is this period's vsync. */
  T->Phase100ns = Now100ns;
  T->LastPeriod = 0ULL;
  T->ActiveSequence = Sequence;
  T->ActiveAddress = Address;
  T->ActiveValid = APPLE_AGX_SCANOUT_TRUE;
  return APPLE_AGX_SCANOUT_TRUE;
}

/* Split products keep all intermediates representable for UINT64 time.
 * Missed periods are coalesced, never emitted as a fictitious burst. */
static inline APPLE_AGX_SCANOUT_BOOL AppleAgxVsyncAdvance(
    APPLE_AGX_VSYNC_TIMELINE *T, APPLE_AGX_SCANOUT_U64 Now100ns) {
  APPLE_AGX_SCANOUT_U64 elapsed, period;
  if (!T->Running || !T->ActiveValid || Now100ns < T->Phase100ns)
    return APPLE_AGX_SCANOUT_FALSE;
  elapsed = Now100ns - T->Phase100ns;
  period = (elapsed / APPLE_AGX_VSYNC_PERIOD_NUMERATOR) *
               APPLE_AGX_VSYNC_RATE_NUMERATOR +
           ((elapsed % APPLE_AGX_VSYNC_PERIOD_NUMERATOR) *
            APPLE_AGX_VSYNC_RATE_NUMERATOR) /
               APPLE_AGX_VSYNC_PERIOD_NUMERATOR;
  if (period <= T->LastPeriod)
    return APPLE_AGX_SCANOUT_FALSE;
  T->LastPeriod = period;
  return T->Enabled;
}

static inline APPLE_AGX_SCANOUT_BOOL AppleAgxVsyncDeadline(
    const APPLE_AGX_VSYNC_TIMELINE *T, APPLE_AGX_SCANOUT_U64 *Deadline100ns) {
  APPLE_AGX_SCANOUT_U64 n, whole, fraction, rem, delta;
  if (!T->Running || !T->Enabled || !T->ActiveValid ||
      T->LastPeriod == APPLE_AGX_VSYNC_MAX)
    return APPLE_AGX_SCANOUT_FALSE;
  n = T->LastPeriod + 1ULL;
  whole = APPLE_AGX_VSYNC_PERIOD_NUMERATOR / APPLE_AGX_VSYNC_RATE_NUMERATOR;
  rem = APPLE_AGX_VSYNC_PERIOD_NUMERATOR % APPLE_AGX_VSYNC_RATE_NUMERATOR;
  if (n > APPLE_AGX_VSYNC_MAX / whole)
    return APPLE_AGX_SCANOUT_FALSE;
  delta = n * whole;
  fraction = (n / APPLE_AGX_VSYNC_RATE_NUMERATOR) * rem +
      (((n % APPLE_AGX_VSYNC_RATE_NUMERATOR) * rem +
        APPLE_AGX_VSYNC_RATE_NUMERATOR - 1ULL) /
       APPLE_AGX_VSYNC_RATE_NUMERATOR);
  if (fraction > APPLE_AGX_VSYNC_MAX - delta)
    return APPLE_AGX_SCANOUT_FALSE;
  delta += fraction;
  if (delta > APPLE_AGX_VSYNC_MAX - T->Phase100ns)
    return APPLE_AGX_SCANOUT_FALSE;
  *Deadline100ns = T->Phase100ns + delta;
  return APPLE_AGX_SCANOUT_TRUE;
}

#define APPLE_AGX_VSYNC_QUERY_MAGIC 0x53565041u
#define APPLE_AGX_VSYNC_RECEIPT_CAPACITY 64u
typedef struct _APPLE_AGX_VSYNC_EVENT {
  APPLE_AGX_SCANOUT_U64 Sequence, Time100ns, Period;
  APPLE_AGX_SCANOUT_U64 PendingSequence, PendingAddress, ActiveSequence, ActiveAddress;
  APPLE_AGX_SCANOUT_U64 NotifyOrdinal;
  APPLE_AGX_SCANOUT_U32 Kind, Enabled, Status, Irql;
} APPLE_AGX_VSYNC_EVENT;
typedef struct _APPLE_AGX_VSYNC_QUERY {
  APPLE_AGX_SCANOUT_U32 Magic, Version, Bytes, Generation;
  APPLE_AGX_SCANOUT_U64 EventCount, NotifyCount, DpcCount, AcknowledgedNotifyCount, Phase100ns;
  APPLE_AGX_SCANOUT_U64 ActiveSequence, ActiveAddress, LastPeriod;
  APPLE_AGX_SCANOUT_U32 RateNumerator, RateDenominator, Enabled, Stopping, Running, Paused;
  APPLE_AGX_VSYNC_EVENT Events[APPLE_AGX_VSYNC_RECEIPT_CAPACITY];
} APPLE_AGX_VSYNC_QUERY;

#endif
