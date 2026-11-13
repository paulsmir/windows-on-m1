#ifndef APPLE_AGX_MPO_HOLD_H
#define APPLE_AGX_MPO_HOLD_H

/* HOLD-1: one pending plane set while a scan-out commit is outstanding.
 * Pure (host-tested); the kernel serialises every call with the scan-out
 * interrupt (Set3 runs at device IRQL, the latch handler in the ISR).
 *
 * EXP2375: a full-screen two-plane frame arrives as two one-layer Set3
 * calls one vsync apart; with one commit per latch each layer latched every
 * other vsync.  A flip that finds the gate held is merged into the pending
 * set when its layer has no pending flip yet (STATUS_SUCCESS, committed by
 * the latch handler); a second flip for a layer that already has one keeps
 * DEVICE_BUSY (dxgkrnl re-offers it; EXP2375: 254/254 re-offers latched).
 * No flip is ever replaced inside the driver, so every accepted PresentId
 * latches and is reported for its layer. */

#define ADMISSION_MPO_HOLD_LAYERS 2u

typedef struct _ADMISSION_MPO_HOLD {
  unsigned int Valid;
  unsigned int ListedMask;           /* layers with a pending flip */
  unsigned int Count;                /* completion entries */
  unsigned int Layer[ADMISSION_MPO_HOLD_LAYERS];
  unsigned long long PresentId[ADMISSION_MPO_HOLD_LAYERS];
} ADMISSION_MPO_HOLD;

/* 1 when a flip listing ListedMask may join the pending set (no layer of
 * it already pending, at least one layer listed, only layers 0/1). */
unsigned int AdmissionMpoHoldAdmit(const ADMISSION_MPO_HOLD *Hold,
                                   unsigned int ListedMask);
/* Adds the flip's completion entries (Count <= 2); requires Admit. */
void AdmissionMpoHoldMerge(ADMISSION_MPO_HOLD *Hold, unsigned int ListedMask,
                           unsigned int Count, const unsigned int *Layer,
                           const unsigned long long *PresentId);
/* Hands the pending set to the caller exactly once (1), else 0. */
unsigned int AdmissionMpoHoldTake(ADMISSION_MPO_HOLD *Hold,
                                  ADMISSION_MPO_HOLD *Out);
/* Puts a taken set back (its commit met the gate again). */
void AdmissionMpoHoldRestore(ADMISSION_MPO_HOLD *Hold,
                             const ADMISSION_MPO_HOLD *Taken);
void AdmissionMpoHoldClear(ADMISSION_MPO_HOLD *Hold);

/* HOLD-2: Set3 never submits.  Every accepted flip joins the pending set;
 * the commit is issued by a DPC queued from Set3 when no swap is
 * outstanding (it runs after dxgkrnl's vsync DPC pass, so every Set3 of the
 * pass is in the set: EXP2375 trace, all two-plane Set3 on one CPU in that
 * pass) or by the latch handler when one is.  Outcome of one Set3: */
#define ADMISSION_MPO_SET3_BUSY 0u
#define ADMISSION_MPO_SET3_MERGED_FIRST 1u  /* the set was empty */
#define ADMISSION_MPO_SET3_MERGED_SECOND 2u /* joined a pending set */
/* Acceptable = the merged set passed the Set3 table (dry run).  Merges on
 * Admit && Acceptable; *QueueCommit = 1 when the caller must queue the
 * commit DPC (merged and GateFree). */
unsigned int AdmissionMpoHoldSet3(ADMISSION_MPO_HOLD *Hold,
                                  unsigned int ListedMask,
                                  unsigned int Acceptable, unsigned int Count,
                                  const unsigned int *Layer,
                                  const unsigned long long *PresentId,
                                  unsigned int GateFree,
                                  unsigned int *QueueCommit);

/* HOLD-3 (EXP2384: DWM's pointer layer flips 2.4-4.9 ms and the
 * application 0.8-1.8 ms after the latch, in different dxgkrnl passes):
 * the pending set is committed once per frame at a deadline, so every flip
 * of the frame rides one swap; a swap latches at the vblank whatever its
 * submit phase, so a flip that arrives before the deadline loses nothing.
 * Times in QPC units.  Frame = [LastVsync + k*Period, + Period) containing
 * Now; Deadline = frame start + Period - Lead.  Returns 1 and *Wait (> 0)
 * when Now is before the deadline (arm the timer), 0 when it has passed or
 * no vsync is known yet (commit now). */
unsigned int AdmissionMpoHoldDeadline(long long Now, long long LastVsync,
                                      long long Period, long long Lead,
                                      long long *Deadline, long long *Wait);

#endif
