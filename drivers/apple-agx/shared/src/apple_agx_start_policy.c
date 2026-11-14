#include "apple_agx_start_policy.h"

unsigned int AppleAgxStartPolicyParsePolicy(unsigned int Present,
                                            unsigned int DwordValid,
                                            unsigned int Value) {
  if (!Present)
    return APPLE_AGX_START_POLICY_ONE_SHOT_ARM;
  if (!DwordValid)
    return APPLE_AGX_START_POLICY_INVALID;
  if (Value == APPLE_AGX_START_POLICY_ONE_SHOT_ARM ||
      Value == APPLE_AGX_START_POLICY_CRASH_LATCH)
    return Value;
  return APPLE_AGX_START_POLICY_INVALID;
}

unsigned int AppleAgxStartPolicyParseMarker(unsigned int Present,
                                            unsigned int DwordValid,
                                            unsigned int Value) {
  if (!Present)
    return 0u;
  if (!DwordValid)
    return 1u;
  return Value != 0u ? 1u : 0u;
}

unsigned int AppleAgxStartPolicyParsePending(unsigned int Present,
                                             unsigned int DwordValid,
                                             unsigned int Value) {
  if (Present && DwordValid && Value == APPLE_AGX_START_POLICY_PENDING_FAULTED)
    return Value;
  return AppleAgxStartPolicyParseMarker(Present, DwordValid, Value);
}

unsigned int AppleAgxStartPolicyParseExplicit(unsigned int Present,
                                              unsigned int DwordValid,
                                              unsigned int Value) {
  return Present && DwordValid && Value == 1u ? 1u : 0u;
}

unsigned int AppleAgxStartPolicyParseCounter(unsigned int Present,
                                             unsigned int DwordValid,
                                             unsigned int Value) {
  return Present && DwordValid ? Value : 0u;
}

static unsigned int AppleAgxStartPolicyIncrement(unsigned int Value) {
  return Value == 0xffffffffu ? Value : Value + 1u;
}

static unsigned int AppleAgxStartPolicyDiff(
    const APPLE_AGX_START_POLICY_STATE *Old,
    const APPLE_AGX_START_POLICY_STATE *New) {
  unsigned int mask = 0u;
  if (Old->Pending != New->Pending)
    mask |= APPLE_AGX_START_POLICY_VALUE_PENDING;
  if (Old->Running != New->Running)
    mask |= APPLE_AGX_START_POLICY_VALUE_RUNNING;
  if (Old->Latched != New->Latched)
    mask |= APPLE_AGX_START_POLICY_VALUE_LATCHED;
  if (Old->LatchReset != New->LatchReset)
    mask |= APPLE_AGX_START_POLICY_VALUE_LATCH_RESET;
  if (Old->UncleanCount != New->UncleanCount)
    mask |= APPLE_AGX_START_POLICY_VALUE_UNCLEAN_COUNT;
  if (Old->LatchCount != New->LatchCount)
    mask |= APPLE_AGX_START_POLICY_VALUE_LATCH_COUNT;
  return mask;
}

/* Latch: the adapter stays off until an administrator sets
 * Wom1StartLatchReset = 1 (or removes Wom1StartLatched). */
static void AppleAgxStartPolicyLatch(APPLE_AGX_START_POLICY_RESULT *Result,
                                     unsigned int Reason) {
  Result->Decision = AppleAgxStartPolicyDecisionRefuse;
  Result->Reason = Reason;
  Result->Next.Latched = 1u;
  Result->Next.LatchCount = AppleAgxStartPolicyIncrement(Result->Next.LatchCount);
  Result->Next.Pending = 0u;
  Result->Next.Running = 0u;
}

static void AppleAgxStartPolicyAttempt(APPLE_AGX_START_POLICY_RESULT *Result,
                                       unsigned int OrderlyExit) {
  APPLE_AGX_START_POLICY_STATE *next = &Result->Next;
  unsigned int reason = AppleAgxStartPolicyReasonClean;
  if (next->Policy == APPLE_AGX_START_POLICY_ONE_SHOT_ARM) {
    /* Today's path: the caller consumes G3Armed; nothing else is read or
     * written. */
    Result->Decision = AppleAgxStartPolicyDecisionArm;
    Result->Reason = AppleAgxStartPolicyReasonOneShotArm;
    return;
  }
  if (next->Policy != APPLE_AGX_START_POLICY_CRASH_LATCH) {
    Result->Decision = AppleAgxStartPolicyDecisionRefuse;
    Result->Reason = AppleAgxStartPolicyReasonPolicyInvalid;
    return;
  }
  if (next->LatchReset) {
    /* Explicit administrator reset: forget the latch and both markers and
     * restart the unclean-boot count; LatchCount stays as history. */
    next->LatchReset = 0u;
    next->Latched = 0u;
    next->Pending = 0u;
    next->Running = 0u;
    next->UncleanCount = 0u;
    reason = AppleAgxStartPolicyReasonLatchReset;
  } else if (next->Latched) {
    /* Refuse without touching any value. */
    Result->Decision = AppleAgxStartPolicyDecisionRefuse;
    Result->Reason = AppleAgxStartPolicyReasonLatched;
    return;
  } else {
    if (OrderlyExit != 0u &&
        next->Pending != APPLE_AGX_START_POLICY_PENDING_FAULTED &&
        (next->Pending != 0u || next->Running != 0u ||
         next->UncleanCount != 0u)) {
      /* F1K-STAMP: the previous run ended in an orderly Windows
       * shutdown/restart, which ended the start and the run.  No shutdown
       * event can record that (the registry is closed before the device S5
       * pass, EXP2405), so it is derived here, before today's rules.  A
       * start faulted at T (Pending 2) still latches below. */
      next->Pending = 0u;
      next->Running = 0u;
      next->UncleanCount = 0u;
      reason = AppleAgxStartPolicyReasonOrderlyByShutdownStamp;
    }
    if (next->Pending) {
      /* The previous start neither proved healthy nor exited cleanly: a
       * crash, hang, forced power-off or reset inside T, or (value 2) a
       * scheduler fault at T. */
      AppleAgxStartPolicyLatch(Result,
          next->Pending == APPLE_AGX_START_POLICY_PENDING_FAULTED
              ? AppleAgxStartPolicyReasonFaultedPending
              : AppleAgxStartPolicyReasonStalePending);
      return;
    }
    if (next->Running) {
      /* The previous boot ended without an orderly exit or clean stop. */
      next->UncleanCount = AppleAgxStartPolicyIncrement(next->UncleanCount);
      if (next->UncleanCount >= APPLE_AGX_START_POLICY_UNCLEAN_LIMIT) {
        AppleAgxStartPolicyLatch(Result, AppleAgxStartPolicyReasonUncleanLimit);
        return;
      }
      reason = AppleAgxStartPolicyReasonUncleanCounted;
    }
  }
  /* Both markers are persisted before the first GPU or broker access. */
  next->Pending = 1u;
  next->Running = 1u;
  Result->Decision = AppleAgxStartPolicyDecisionAllow;
  Result->Reason = reason;
}

int AppleAgxStartPolicyEvaluate(const APPLE_AGX_START_POLICY_STATE *Current,
                                unsigned int Event, unsigned int Healthy,
                                APPLE_AGX_START_POLICY_RESULT *Result) {
  APPLE_AGX_START_POLICY_STATE *next;
  if (Current == 0 || Result == 0)
    return 0;
  Result->Decision = AppleAgxStartPolicyDecisionNone;
  Result->Reason = AppleAgxStartPolicyReasonNone;
  Result->UpdateMask = 0u;
  Result->ArmTimer = 0u;
  Result->Next = *Current;
  next = &Result->Next;
  /* An input of this evaluation, never state carried to the next one. */
  next->OrderlyExit = 0u;
  if (Event == AppleAgxStartPolicyEventAttempt) {
    AppleAgxStartPolicyAttempt(Result, Current->OrderlyExit);
  } else if (Event < AppleAgxStartPolicyEventSucceeded ||
             Event > AppleAgxStartPolicyEventTimer) {
    Result->Reason = AppleAgxStartPolicyReasonInvalidEvent;
  } else if (Current->Policy != APPLE_AGX_START_POLICY_CRASH_LATCH) {
    Result->Reason = AppleAgxStartPolicyReasonPolicyZero;
  } else if (Event == AppleAgxStartPolicyEventSucceeded) {
    if (Current->Pending) {
      Result->ArmTimer = 1u;
      Result->Reason = AppleAgxStartPolicyReasonArmTimer;
    } else {
      Result->Reason = AppleAgxStartPolicyReasonNotPending;
    }
  } else if (Event == AppleAgxStartPolicyEventFailed) {
    /* An error return is no crash: nothing to protect against. */
    next->Pending = 0u;
    next->Running = 0u;
    Result->Reason = Current->Pending || Current->Running
                         ? AppleAgxStartPolicyReasonCleared
                         : AppleAgxStartPolicyReasonNotPending;
  } else if (Event == AppleAgxStartPolicyEventCleanStop ||
             Event == AppleAgxStartPolicyEventShutdown) {
    /* A clean stop and an orderly shutdown/restart are both clean exits:
     * they end the start and the run (architect decision 2026-11-13,
     * supersedes START-GATE's "a shutdown inside T latches"). */
    next->Pending = 0u;
    next->Running = 0u;
    next->UncleanCount = 0u;
    Result->Reason = AppleAgxStartPolicyReasonCleared;
  } else if (!Healthy) {
    /* Keep the start pending (the next boot latches) and mark why. */
    if (Current->Pending)
      next->Pending = APPLE_AGX_START_POLICY_PENDING_FAULTED;
    Result->Reason = AppleAgxStartPolicyReasonTimerFaulted;
  } else if (Current->Pending) {
    next->Pending = 0u;
    Result->Reason = AppleAgxStartPolicyReasonCleared;
  } else {
    Result->Reason = AppleAgxStartPolicyReasonNotPending;
  }
  Result->UpdateMask = AppleAgxStartPolicyDiff(Current, next);
  return 1;
}

unsigned int AppleAgxStartPolicyOrderlyExit(unsigned int ShutdownTimeReadable,
                                            unsigned long long ShutdownTime,
                                            unsigned int StampPresent,
                                            unsigned long long Stamp) {
  return ShutdownTimeReadable != 0u && StampPresent != 0u &&
                 ShutdownTime != Stamp
             ? 1u
             : 0u;
}

unsigned int AppleAgxStartPolicyStampAction(unsigned int Decision,
                                            unsigned int ShutdownTimeReadable) {
  if (Decision != AppleAgxStartPolicyDecisionAllow)
    return APPLE_AGX_START_POLICY_STAMP_KEEP;
  return ShutdownTimeReadable != 0u ? APPLE_AGX_START_POLICY_STAMP_SET
                                    : APPLE_AGX_START_POLICY_STAMP_DELETE;
}

unsigned long long AppleAgxStartPolicyStampReceipt(unsigned int ReadStatus,
                                                   unsigned int StampPresent,
                                                   unsigned int Equal,
                                                   unsigned int OrderlyExit,
                                                   unsigned int StampAction) {
  return (unsigned long long)ReadStatus |
         ((unsigned long long)(StampPresent != 0u ? 1u : 0u) << 32) |
         ((unsigned long long)(Equal != 0u ? 1u : 0u) << 33) |
         ((unsigned long long)(OrderlyExit != 0u ? 1u : 0u) << 34) |
         ((unsigned long long)(StampAction & 3u) << 35);
}

unsigned int AppleAgxStartPolicyInputs(
    const APPLE_AGX_START_POLICY_STATE *State) {
  unsigned int inputs = 0u;
  if (State == 0)
    return 0u;
  if (State->Pending)
    inputs |= APPLE_AGX_START_POLICY_VALUE_PENDING;
  if (State->Running)
    inputs |= APPLE_AGX_START_POLICY_VALUE_RUNNING;
  if (State->Latched)
    inputs |= APPLE_AGX_START_POLICY_VALUE_LATCHED;
  if (State->LatchReset)
    inputs |= APPLE_AGX_START_POLICY_VALUE_LATCH_RESET;
  if (State->UncleanCount != 0u)
    inputs |= APPLE_AGX_START_POLICY_VALUE_UNCLEAN_COUNT;
  if (State->LatchCount != 0u)
    inputs |= APPLE_AGX_START_POLICY_VALUE_LATCH_COUNT;
  return inputs;
}

unsigned int AppleAgxStartPolicyReceipt(
    const APPLE_AGX_START_POLICY_STATE *Current,
    const APPLE_AGX_START_POLICY_RESULT *Result) {
  unsigned int policy;
  if (Current == 0 || Result == 0)
    return 0u;
  policy = Current->Policy > 0xffu ? 0xffu : Current->Policy;
  return (Result->Reason & 0xffu) |
         ((AppleAgxStartPolicyInputs(Current) & 0xffu) << 8) |
         ((Result->Decision & 0xffu) << 16) | (policy << 24);
}
