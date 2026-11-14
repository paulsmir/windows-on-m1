/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * The production start policy must never re-start an adapter whose previous
 * start did not survive (stale pending -> latched, latched refuses without
 * touching any value), must stop protecting a start once it proved healthy
 * or stopped cleanly, must latch after three consecutive unclean boots and
 * forget them after a clean shutdown, must clear a latch only on an explicit
 * administrator value, and must leave policy 0 (the bench one-shot arm)
 * without any write. */
#include "apple_agx_start_policy.h"

#include <assert.h>
#include <string.h>

#define P_PENDING APPLE_AGX_START_POLICY_VALUE_PENDING
#define P_RUNNING APPLE_AGX_START_POLICY_VALUE_RUNNING
#define P_LATCHED APPLE_AGX_START_POLICY_VALUE_LATCHED
#define P_RESET APPLE_AGX_START_POLICY_VALUE_LATCH_RESET
#define P_UNCLEAN APPLE_AGX_START_POLICY_VALUE_UNCLEAN_COUNT
#define P_LATCHES APPLE_AGX_START_POLICY_VALUE_LATCH_COUNT

static APPLE_AGX_START_POLICY_STATE State(unsigned int policy) {
  APPLE_AGX_START_POLICY_STATE state;
  memset(&state, 0, sizeof(state));
  state.Policy = policy;
  return state;
}

static APPLE_AGX_START_POLICY_RESULT Run(APPLE_AGX_START_POLICY_STATE *state,
                                         unsigned int event,
                                         unsigned int healthy) {
  APPLE_AGX_START_POLICY_RESULT result;
  memset(&result, 0xcc, sizeof(result));
  assert(AppleAgxStartPolicyEvaluate(state, event, healthy, &result) == 1);
  /* The caller persists exactly UpdateMask; model that here. */
  *state = result.Next;
  return result;
}

/* Policy 0 (absent): every event is byte-for-byte today's behaviour: the
 * start consumes the arm, nothing is written, no timer, and the other events
 * ignore even stale markers. */
static void TestPolicyZeroIsTodaysArm(void) {
  unsigned int event;
  APPLE_AGX_START_POLICY_STATE state = State(0u);
  APPLE_AGX_START_POLICY_RESULT r;
  assert(AppleAgxStartPolicyParsePolicy(0u, 0u, 0u) == 0u);
  assert(AppleAgxStartPolicyParsePolicy(1u, 1u, 0u) == 0u);
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionArm);
  assert(r.Reason == AppleAgxStartPolicyReasonOneShotArm);
  assert(r.UpdateMask == 0u && r.ArmTimer == 0u);
  /* Stale policy-1 values left behind do not change a policy-0 start. */
  state.Pending = 1u;
  state.Latched = 1u;
  state.Running = 1u;
  state.UncleanCount = 7u;
  state.LatchReset = 1u;
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionArm && r.UpdateMask == 0u);
  for (event = AppleAgxStartPolicyEventSucceeded;
       event <= AppleAgxStartPolicyEventTimer; ++event) {
    r = Run(&state, event, 1u);
    assert(r.Decision == AppleAgxStartPolicyDecisionNone);
    assert(r.Reason == AppleAgxStartPolicyReasonPolicyZero);
    assert(r.UpdateMask == 0u && r.ArmTimer == 0u);
  }
}

static void TestInvalidPolicyFailsClosed(void) {
  APPLE_AGX_START_POLICY_STATE state;
  APPLE_AGX_START_POLICY_RESULT r;
  assert(AppleAgxStartPolicyParsePolicy(1u, 1u, 2u) ==
         APPLE_AGX_START_POLICY_INVALID);
  assert(AppleAgxStartPolicyParsePolicy(1u, 0u, 1u) ==
         APPLE_AGX_START_POLICY_INVALID);
  assert(AppleAgxStartPolicyParsePolicy(1u, 1u, 1u) == 1u);
  state = State(APPLE_AGX_START_POLICY_INVALID);
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionRefuse);
  assert(r.Reason == AppleAgxStartPolicyReasonPolicyInvalid);
  assert(r.UpdateMask == 0u);
}

/* Policy 1, clean boot: allow without an arm; pending and running are
 * persisted before the GPU is touched. */
static void TestPolicyOneStartsWithoutArm(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
  assert(r.Reason == AppleAgxStartPolicyReasonClean);
  assert(r.UpdateMask == (P_PENDING | P_RUNNING));
  assert(state.Pending == 1u && state.Running == 1u && state.Latched == 0u);
  r = Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  assert(r.ArmTimer == 1u && r.UpdateMask == 0u);
  assert(r.Reason == AppleAgxStartPolicyReasonArmTimer);
}

static void TestErrorClears(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  r = Run(&state, AppleAgxStartPolicyEventFailed, 1u);
  assert(r.UpdateMask == (P_PENDING | P_RUNNING) && r.ArmTimer == 0u);
  assert(state.Pending == 0u && state.Running == 0u);
  /* The next start is a clean one, not a latch. */
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
  assert(r.Reason == AppleAgxStartPolicyReasonClean);
}

static void TestCleanStopClears(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  r = Run(&state, AppleAgxStartPolicyEventCleanStop, 1u);
  assert(r.UpdateMask == (P_PENDING | P_RUNNING));
  assert(state.Pending == 0u && state.Running == 0u);
  /* Live disable/enable: the next start in the same boot needs no script. */
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
  assert(r.Reason == AppleAgxStartPolicyReasonClean);
}

static void TestHealthyTimerClearsOnlyPending(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  r = Run(&state, AppleAgxStartPolicyEventTimer, 1u);
  assert(r.UpdateMask == P_PENDING && r.Reason == AppleAgxStartPolicyReasonCleared);
  assert(state.Pending == 0u && state.Running == 1u);
  /* A second expiry has nothing to clear. */
  r = Run(&state, AppleAgxStartPolicyEventTimer, 1u);
  assert(r.UpdateMask == 0u && r.Reason == AppleAgxStartPolicyReasonNotPending);
}

static void TestFaultedTimerKeepsPending(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  r = Run(&state, AppleAgxStartPolicyEventTimer, 0u);
  /* Pending stays set, now marked "faulted at T" (value 2). */
  assert(r.UpdateMask == P_PENDING && r.Reason == AppleAgxStartPolicyReasonTimerFaulted);
  assert(state.Pending == APPLE_AGX_START_POLICY_PENDING_FAULTED);
  /* A second faulted expiry writes nothing. */
  r = Run(&state, AppleAgxStartPolicyEventTimer, 0u);
  assert(r.UpdateMask == 0u && r.Reason == AppleAgxStartPolicyReasonTimerFaulted);
  /* The next boot latches with a reason distinct from a crash latch. */
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionRefuse);
  assert(r.Reason == AppleAgxStartPolicyReasonFaultedPending);
  assert(state.Latched == 1u && state.Pending == 0u && state.LatchCount == 1u);
  /* A clean stop after a faulted timer is still a clean exit. */
  state = State(1u);
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventTimer, 0u);
  r = Run(&state, AppleAgxStartPolicyEventCleanStop, 1u);
  assert(state.Pending == 0u && state.Running == 0u);
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Reason == AppleAgxStartPolicyReasonClean);
}

/* Stop, hang or EL2 reset inside T: the next boot finds pending and
 * latches; a latched adapter then refuses without touching any value. */
static void TestStalePendingLatchesAndLatchedRefuses(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  /* reboot: no event reaches the driver */
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionRefuse);
  assert(r.Reason == AppleAgxStartPolicyReasonStalePending);
  assert(r.UpdateMask == (P_PENDING | P_RUNNING | P_LATCHED | P_LATCHES));
  assert(state.Latched == 1u && state.LatchCount == 1u);
  assert(state.Pending == 0u && state.Running == 0u);
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionRefuse);
  assert(r.Reason == AppleAgxStartPolicyReasonLatched);
  assert(r.UpdateMask == 0u && state.LatchCount == 1u);
  /* A latched adapter with a stale pending marker is not re-latched. */
  state.Pending = 1u;
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Reason == AppleAgxStartPolicyReasonLatched && r.UpdateMask == 0u);
  assert(state.Pending == 1u);
  /* Only the explicit administrator value clears the latch. */
  state.Pending = 0u;
  (void)Run(&state, AppleAgxStartPolicyEventShutdown, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventCleanStop, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventTimer, 1u);
  assert(state.Latched == 1u);
  state.LatchReset = 1u;
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
  assert(r.Reason == AppleAgxStartPolicyReasonLatchReset);
  assert(r.UpdateMask == (P_LATCHED | P_RESET | P_PENDING | P_RUNNING));
  assert(state.Latched == 0u && state.LatchReset == 0u);
  assert(state.LatchCount == 1u && state.Pending == 1u && state.Running == 1u);
}

/* Late crash loop: every start survives T (pending cleared) but the boot
 * ends without a shutdown action.  Boots 1-3 end unclean; the 4th start
 * counts 3 and latches.  A clean shutdown resets the count. */
static void TestUncleanCounterLatchesAtLimit(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  unsigned int boot;
  for (boot = 0u; boot < APPLE_AGX_START_POLICY_UNCLEAN_LIMIT; ++boot) {
    r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
    assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
    assert(r.Reason == (boot == 0u ? AppleAgxStartPolicyReasonClean
                                   : AppleAgxStartPolicyReasonUncleanCounted));
    assert(state.UncleanCount == boot);
    (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
    (void)Run(&state, AppleAgxStartPolicyEventTimer, 1u);
    assert(state.Pending == 0u && state.Running == 1u);
  }
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionRefuse);
  assert(r.Reason == AppleAgxStartPolicyReasonUncleanLimit);
  assert(state.UncleanCount == APPLE_AGX_START_POLICY_UNCLEAN_LIMIT);
  assert(state.Latched == 1u && state.LatchCount == 1u && state.Running == 0u);
  /* The administrator reset restarts the count. */
  state.LatchReset = 1u;
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
  assert(state.UncleanCount == 0u);
}

static void TestCleanShutdownResetsCounter(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  unsigned int boot;
  for (boot = 0u; boot < 2u * APPLE_AGX_START_POLICY_UNCLEAN_LIMIT; ++boot) {
    r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
    assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
    (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
    (void)Run(&state, AppleAgxStartPolicyEventTimer, 1u);
    if ((boot % 2u) == 0u) {
      /* unclean end */
      continue;
    }
    r = Run(&state, AppleAgxStartPolicyEventShutdown, 1u);
    assert(r.Reason == AppleAgxStartPolicyReasonCleared);
    assert(state.Running == 0u && state.UncleanCount == 0u);
  }
  assert(state.Latched == 0u);
  /* Architect decision 2026-11-13 (supersedes START-GATE "a shutdown
   * inside T latches"): an orderly shutdown/restart inside T is a clean
   * exit; the next start is clean. */
  state = State(1u);
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  r = Run(&state, AppleAgxStartPolicyEventShutdown, 1u);
  assert(r.UpdateMask == (P_PENDING | P_RUNNING));
  assert(r.Reason == AppleAgxStartPolicyReasonCleared);
  assert(state.Pending == 0u && state.Running == 0u);
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionAllow);
  assert(r.Reason == AppleAgxStartPolicyReasonClean);
  /* Power loss / reset / hang inside T: no event reaches the driver, the
   * next start latches. */
  state = State(1u);
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  (void)Run(&state, AppleAgxStartPolicyEventSucceeded, 1u);
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Decision == AppleAgxStartPolicyDecisionRefuse);
  assert(r.Reason == AppleAgxStartPolicyReasonStalePending);
  assert(state.Latched == 1u);
}

static void TestCleanStopResetsCounter(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  state.Running = 1u;
  state.UncleanCount = 1u;
  r = Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(r.Reason == AppleAgxStartPolicyReasonUncleanCounted);
  assert(state.UncleanCount == 2u);
  assert(r.UpdateMask == (P_PENDING | P_UNCLEAN));
  r = Run(&state, AppleAgxStartPolicyEventCleanStop, 1u);
  assert(r.UpdateMask == (P_PENDING | P_RUNNING | P_UNCLEAN));
  assert(state.UncleanCount == 0u);
}

static void TestParsersAndReceipt(void) {
  APPLE_AGX_START_POLICY_STATE state = State(1u);
  APPLE_AGX_START_POLICY_RESULT r;
  assert(AppleAgxStartPolicyParseMarker(0u, 0u, 0u) == 0u);
  assert(AppleAgxStartPolicyParseMarker(1u, 1u, 0u) == 0u);
  assert(AppleAgxStartPolicyParseMarker(1u, 1u, 5u) == 1u);
  assert(AppleAgxStartPolicyParseMarker(1u, 0u, 0u) == 1u); /* fail closed */
  assert(AppleAgxStartPolicyParsePending(0u, 0u, 0u) == 0u);
  assert(AppleAgxStartPolicyParsePending(1u, 1u, 0u) == 0u);
  assert(AppleAgxStartPolicyParsePending(1u, 1u, 1u) == 1u);
  assert(AppleAgxStartPolicyParsePending(1u, 1u, 2u) ==
         APPLE_AGX_START_POLICY_PENDING_FAULTED);
  assert(AppleAgxStartPolicyParsePending(1u, 1u, 7u) == 1u);
  assert(AppleAgxStartPolicyParsePending(1u, 0u, 2u) == 1u); /* fail closed */
  assert(AppleAgxStartPolicyParseExplicit(1u, 1u, 1u) == 1u);
  assert(AppleAgxStartPolicyParseExplicit(1u, 1u, 2u) == 0u);
  assert(AppleAgxStartPolicyParseExplicit(1u, 0u, 1u) == 0u);
  assert(AppleAgxStartPolicyParseCounter(1u, 1u, 9u) == 9u);
  assert(AppleAgxStartPolicyParseCounter(1u, 0u, 9u) == 0u);
  assert(AppleAgxStartPolicyParseCounter(0u, 1u, 9u) == 0u);
  state.Pending = 1u;
  state.LatchCount = 2u;
  assert(AppleAgxStartPolicyEvaluate(&state, AppleAgxStartPolicyEventAttempt,
                                     1u, &r) == 1);
  assert(AppleAgxStartPolicyInputs(&state) == (P_PENDING | P_LATCHES));
  assert(AppleAgxStartPolicyReceipt(&state, &r) ==
         (AppleAgxStartPolicyReasonStalePending | ((P_PENDING | P_LATCHES) << 8) |
          (AppleAgxStartPolicyDecisionRefuse << 16) | (1u << 24)));
  state.Policy = APPLE_AGX_START_POLICY_INVALID;
  assert((AppleAgxStartPolicyReceipt(&state, &r) >> 24) == 0xffu);
  assert(AppleAgxStartPolicyEvaluate(0, 1u, 1u, &r) == 0);
  assert(AppleAgxStartPolicyEvaluate(&state, 1u, 1u, 0) == 0);
  state = State(1u);
  assert(AppleAgxStartPolicyEvaluate(&state, 99u, 1u, &r) == 1);
  assert(r.Reason == AppleAgxStartPolicyReasonInvalidEvent && r.UpdateMask == 0u);
  /* Counters saturate instead of wrapping into "clean". */
  state.Running = 1u;
  state.UncleanCount = 0xffffffffu;
  state.LatchCount = 0xffffffffu;
  (void)Run(&state, AppleAgxStartPolicyEventAttempt, 1u);
  assert(state.Latched == 1u && state.LatchCount == 0xffffffffu);
  assert(state.UncleanCount == 0xffffffffu);
}

int main(void) {
  TestPolicyZeroIsTodaysArm();
  TestInvalidPolicyFailsClosed();
  TestPolicyOneStartsWithoutArm();
  TestErrorClears();
  TestCleanStopClears();
  TestHealthyTimerClearsOnlyPending();
  TestFaultedTimerKeepsPending();
  TestStalePendingLatchesAndLatchedRefuses();
  TestUncleanCounterLatchesAtLimit();
  TestCleanShutdownResetsCounter();
  TestCleanStopResetsCounter();
  TestParsersAndReceipt();
  return 0;
}
