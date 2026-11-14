#ifndef APPLE_AGX_START_POLICY_H
#define APPLE_AGX_START_POLICY_H

/* F1-k: production start policy of the AppleAgx adapter
 * (investigation/analysis/COMPLETENESS-F1-standalone-gpu-boot.md 4.C,
 * START-GATE-PRODUCTION-CONTRACT.md).  A pure decision function: the KMD
 * reads the device-key values into APPLE_AGX_START_POLICY_STATE, evaluates
 * one event, writes/deletes exactly the values in UpdateMask, flushes the
 * key, and acts on Decision / ArmTimer.  No Windows headers.
 *
 * Policy 0 (Wom1StartPolicy absent or 0): the one-shot G3Armed arm of the
 * bench, unchanged; no other event has an effect.  Policy 1: crash latch
 * plus unclean-boot counter, no arm. */

#define APPLE_AGX_START_POLICY_ONE_SHOT_ARM 0u
#define APPLE_AGX_START_POLICY_CRASH_LATCH 1u
/* Any other policy value (wrong type or number): refuse to start. */
#define APPLE_AGX_START_POLICY_INVALID 0xffffffffu

/* Healthy-start window after a successful start (design: T = 120 s). */
#define APPLE_AGX_START_POLICY_TIMER_SECONDS 120u
/* Consecutive boots that ended with Wom1StartRunning still set. */
#define APPLE_AGX_START_POLICY_UNCLEAN_LIMIT 3u

/* Wom1StartPending = 2: the start reached T with the scheduler faulted; it
 * still latches the next boot, with its own reason. */
#define APPLE_AGX_START_POLICY_PENDING_FAULTED 2u

/* UpdateMask bits: which value of Next the caller must persist.  Marker
 * values (PENDING, RUNNING, LATCHED, LATCH_RESET) are written as REG_DWORD
 * of the Next field when it is non-zero and deleted when it is 0; counters
 * are written as REG_DWORD. */
#define APPLE_AGX_START_POLICY_VALUE_PENDING 0x01u
#define APPLE_AGX_START_POLICY_VALUE_RUNNING 0x02u
#define APPLE_AGX_START_POLICY_VALUE_LATCHED 0x04u
#define APPLE_AGX_START_POLICY_VALUE_LATCH_RESET 0x08u
#define APPLE_AGX_START_POLICY_VALUE_UNCLEAN_COUNT 0x10u
#define APPLE_AGX_START_POLICY_VALUE_LATCH_COUNT 0x20u
#define APPLE_AGX_START_POLICY_VALUE_MARKERS                              \
  (APPLE_AGX_START_POLICY_VALUE_PENDING |                                \
   APPLE_AGX_START_POLICY_VALUE_RUNNING |                                \
   APPLE_AGX_START_POLICY_VALUE_LATCHED |                                \
   APPLE_AGX_START_POLICY_VALUE_LATCH_RESET)

#define AppleAgxStartPolicyEventNone 0u
#define AppleAgxStartPolicyEventAttempt 1u   /* StartDevice gate, before GPU */
#define AppleAgxStartPolicyEventSucceeded 2u /* StartDevice returned success */
#define AppleAgxStartPolicyEventFailed 3u    /* StartDevice returned an error */
#define AppleAgxStartPolicyEventCleanStop 4u /* StopDevice returned success */
#define AppleAgxStartPolicyEventShutdown 5u  /* SetPowerState, shutdown action: clean exit */
#define AppleAgxStartPolicyEventTimer 6u     /* T seconds after a success */

#define AppleAgxStartPolicyDecisionNone 0u   /* not a start event */
#define AppleAgxStartPolicyDecisionArm 1u    /* policy 0: consume G3Armed */
#define AppleAgxStartPolicyDecisionAllow 2u  /* policy 1: start, no arm */
#define AppleAgxStartPolicyDecisionRefuse 3u /* fail closed before GPU access */

#define AppleAgxStartPolicyReasonNone 0u
#define AppleAgxStartPolicyReasonOneShotArm 1u     /* policy 0 start */
#define AppleAgxStartPolicyReasonPolicyZero 2u     /* policy 0, event ignored */
#define AppleAgxStartPolicyReasonPolicyInvalid 3u  /* refuse */
#define AppleAgxStartPolicyReasonClean 4u          /* allow, last boot clean */
#define AppleAgxStartPolicyReasonUncleanCounted 5u /* allow, counter below N */
#define AppleAgxStartPolicyReasonLatchReset 6u     /* allow after admin reset */
#define AppleAgxStartPolicyReasonLatched 7u        /* refuse, latch set */
#define AppleAgxStartPolicyReasonStalePending 8u   /* refuse, latch now (crash/hang/power loss) */
#define AppleAgxStartPolicyReasonUncleanLimit 9u   /* refuse, latch now */
#define AppleAgxStartPolicyReasonArmTimer 10u      /* success, timer armed */
#define AppleAgxStartPolicyReasonCleared 11u       /* pending/running cleared (clean exit) */
#define AppleAgxStartPolicyReasonTimerFaulted 12u  /* timer, scheduler faulted: pending = 2 */
#define AppleAgxStartPolicyReasonNotPending 13u    /* nothing to clear */
#define AppleAgxStartPolicyReasonInvalidEvent 14u
#define AppleAgxStartPolicyReasonFaultedPending 15u /* refuse, latch now (faulted at T) */

typedef struct _APPLE_AGX_START_POLICY_STATE {
  unsigned int Policy;       /* Wom1StartPolicy (parsed) */
  unsigned int Pending;      /* Wom1StartPending, 0/1/2 (faulted at T) */
  unsigned int Running;      /* Wom1StartRunning, 0/1 */
  unsigned int Latched;      /* Wom1StartLatched, 0/1 */
  unsigned int LatchReset;   /* Wom1StartLatchReset, 0/1 (administrator) */
  unsigned int UncleanCount; /* Wom1StartUncleanCount */
  unsigned int LatchCount;   /* Wom1StartLatchCount (receipt, never reset) */
} APPLE_AGX_START_POLICY_STATE;

typedef struct _APPLE_AGX_START_POLICY_RESULT {
  unsigned int Decision;   /* APPLE_AGX_START_POLICY_DECISION */
  unsigned int Reason;     /* APPLE_AGX_START_POLICY_REASON */
  unsigned int UpdateMask; /* APPLE_AGX_START_POLICY_VALUE_* to persist */
  unsigned int ArmTimer;   /* 1: arm the T-second healthy-start timer */
  APPLE_AGX_START_POLICY_STATE Next;
} APPLE_AGX_START_POLICY_RESULT;

/* Registry value parsers.  Present = the query succeeded; DwordValid = type
 * REG_DWORD with 4 data bytes; Value = those bytes.  A query that failed for
 * any reason other than "not found" is passed as Present with DwordValid 0. */
unsigned int AppleAgxStartPolicyParsePolicy(unsigned int Present,
                                            unsigned int DwordValid,
                                            unsigned int Value);
/* Pending/Running/Latched: a malformed value counts as set (fail closed). */
unsigned int AppleAgxStartPolicyParseMarker(unsigned int Present,
                                            unsigned int DwordValid,
                                            unsigned int Value);
/* Pending: 0, 1 or APPLE_AGX_START_POLICY_PENDING_FAULTED; any other set or
 * malformed value counts as 1 (fail closed). */
unsigned int AppleAgxStartPolicyParsePending(unsigned int Present,
                                             unsigned int DwordValid,
                                             unsigned int Value);
/* LatchReset: only an explicit REG_DWORD 1 counts. */
unsigned int AppleAgxStartPolicyParseExplicit(unsigned int Present,
                                              unsigned int DwordValid,
                                              unsigned int Value);
/* Counters: a malformed or absent value reads as 0. */
unsigned int AppleAgxStartPolicyParseCounter(unsigned int Present,
                                             unsigned int DwordValid,
                                             unsigned int Value);

/* Healthy is read only by AppleAgxStartPolicyEventTimer (1: scheduler not
 * faulted).  Returns 0 for a NULL argument, 1 otherwise. */
int AppleAgxStartPolicyEvaluate(const APPLE_AGX_START_POLICY_STATE *Current,
                                unsigned int Event, unsigned int Healthy,
                                APPLE_AGX_START_POLICY_RESULT *Result);

/* Receipt bit mask of the inputs (bit n = UpdateMask bit n of a value that
 * was set / non-zero). */
unsigned int AppleAgxStartPolicyInputs(
    const APPLE_AGX_START_POLICY_STATE *State);

/* One DWORD per event: Reason | Inputs << 8 | Decision << 16 |
 * min(Policy, 0xff) << 24. */
unsigned int AppleAgxStartPolicyReceipt(
    const APPLE_AGX_START_POLICY_STATE *Current,
    const APPLE_AGX_START_POLICY_RESULT *Result);

#endif
