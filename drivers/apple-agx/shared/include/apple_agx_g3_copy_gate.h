#ifndef APPLE_AGX_G3_COPY_GATE_H
#define APPLE_AGX_G3_COPY_GATE_H
#include "apple_agx_g3_copy_abi.h"

/* RC68: when a buffered copy escape (QUERY/UPLOAD/DOWNLOAD) must wait for a
 * GPU job of its own process. Plain C, shared by the KMD and the UMD.
 *
 * What the R157/EXP1002 wait protects (gpuva_g3_windows.c CopyEscape):
 * - the data: an UPLOAD stores into, a DOWNLOAD reads, canonical memory a
 *   running job may read or write;
 * - nothing else. The copy changes no broker table (it reads the logical PTE
 *   shadow under the G3 lock and stores through the KMD's CPU view), the
 *   lease only pins the graph for the job (BeginJob/EndJob set and clear
 *   LeaseToken, JobInFlight and ActiveProcess together), and paging
 *   quiescence (R161) is a separate term the KMD keeps for UPLOAD/DOWNLOAD.
 *
 * The KMD cannot see which allocations a job references (GPU-VA submissions
 * carry no allocation list). The UMD can: every submission holds its whole
 * residency set (SubmissionHolds) until the winsys retires it after its
 * render fence. It sets UNREFERENCED only when no submission of its device
 * that may still run holds the slot. The KMD accepts that proof only for a
 * job of the caller's own context (the device whose holds the UMD counted)
 * and never for a presentation allocation (present/flip work reads it
 * outside the holds). Any other active job of the process is waited for. */

/* Request input bit (APPLE_AGX_G3_COPY_REQUEST.Reserved). */
#define APPLE_AGX_G3_COPY_FLAG_UNREFERENCED 0x1u
/* Output bits, written by the KMD into Reserved on success only. */
#define APPLE_AGX_G3_COPY_RESULT_OVERLAPPED 0x100u /* ran while own job ran */
#define APPLE_AGX_G3_COPY_RESULT_WAITED 0x200u     /* waited for a job */
/* Bits a request may carry: an older UMD that reuses a request echoes the
 * output bits back, which the KMD ignores on input. */
#define APPLE_AGX_G3_COPY_RESERVED_MASK (APPLE_AGX_G3_COPY_FLAG_UNREFERENCED | \
    APPLE_AGX_G3_COPY_RESULT_OVERLAPPED | APPLE_AGX_G3_COPY_RESULT_WAITED)

#define APPLE_AGX_G3_COPY_GATE_IDLE 0u    /* no job of this process active */
#define APPLE_AGX_G3_COPY_GATE_OVERLAP 1u /* proceed beside the own job */
#define APPLE_AGX_G3_COPY_GATE_WAIT 2u    /* wait for (or refuse on) the job */

static inline unsigned int AppleAgxG3CopyJobGate(unsigned int operation,
    unsigned int flags, int processJobActive, int activeIsCallerContext,
    int presentation) {
  if (!processJobActive) return APPLE_AGX_G3_COPY_GATE_IDLE;
  if (operation > APPLE_AGX_G3_COPY_DOWNLOAD ||
      !(flags & APPLE_AGX_G3_COPY_FLAG_UNREFERENCED) ||
      !activeIsCallerContext || presentation)
    return APPLE_AGX_G3_COPY_GATE_WAIT;
  return APPLE_AGX_G3_COPY_GATE_OVERLAP;
}

/* UMD proof for one slot, evaluated under its screen-buffer state.
 * submissionHolds counts the device's unretired submissions holding the
 * slot plus the hold being built (copyHeld: not submitted yet, or, for a
 * download, already completed: both callers wait for its fence first).
 * Every other holder is queued and its fence is at most lastUseFence (the
 * newest queued holder; one context completes in order). completedFence
 * is the highest render fence this device has seen reached. Shared
 * (borrowed), presentation-direct and present-source slots have users
 * outside these holds and never carry the proof. */
static inline int AppleAgxG3CopySlotUnreferenced(unsigned int submissionHolds,
    int copyHeld, unsigned int sourceHolds, int borrowed, int direct,
    int terminal, unsigned long long lastUseFence,
    unsigned long long completedFence) {
  unsigned int others = submissionHolds;
  if (borrowed || direct || sourceHolds || terminal) return 0;
  if (copyHeld) {
    if (!others) return 0; /* a held slot always counts its own hold */
    --others;
  }
  if (!others) return 1;
  return lastUseFence != 0ULL && lastUseFence <= completedFence;
}
#endif
