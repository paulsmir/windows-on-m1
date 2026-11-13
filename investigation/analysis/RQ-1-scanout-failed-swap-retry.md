# RQ-1: scan-out survives a failed swap (KMD, no m1n1 change)

Spec: architect handoff 2026-11-12T18:07:57Z E5-RQ (TRANSLATION RQ-1), base arch/hold1 623590848.

## WINDOWS CONTRACT
- Set3 answers SUCCESS or DEVICE_BUSY (S3-B); a flip answered SUCCESS must eventually be reported in a
  CRTC_VSYNC_WITH_MULTIPLANE_OVERLAY2 interrupt with its PresentId, or dxgkrnl's flip queue for that layer stalls.
- Reporting a later PresentId on a layer retires the earlier entries of that layer (the HOLD-1 merges rely on the same rule).

## AGX/ASAHI/m1n1 CONTRACT (m1n1 gpu/rq2-latch-deadline 4215928f, hv_agx_scanout_broker.c)
- Synchronous present failure (complete_present, result != OK or swap_id 0): RESULT = failure code
  (PRESENT_FAILED when the service said OK without a swap), RECEIPT_SEQUENCE = the failed sequence, IRQ_ERROR,
  broker state back to state_before_pending; later commits are accepted.
- RQ-2 latch failure (fail_latch: 100 ms deadline or latch poll error): applied sequence, swap id and active offsets
  rolled back, RESULT = PRESENT_FAILED, IRQ_ERROR; RECEIPT_SEQUENCE stays the failed sequence (set when the service
  accepted it).
- RELEASE failure also raises IRQ_ERROR (receipt = the RELEASE sequence), not a present.
- Today's KMD: any IRQ_ERROR sets runtime->Faulted, stops the timeline and returns; PresentGate/PendingValid and the
  client's PresentPending stay set, so every later commit is DEVICE_BUSY and StopDevice fails (E5-RQ).

## TRANSLATION
1. Shared client: AppleAgxScanoutTakePresentFailure(Client, &Sequence, &BrokerResult) reads RESULT and
   RECEIPT_SEQUENCE; when a present is pending, RECEIPT == its sequence and RESULT != OK, it clears PresentPending
   and returns the failed sequence; otherwise the error is not about the pending present.
2. Runtime keeps the last asynchronous submission (V3 records + count + MPO kind, or V2 segment/offset/format) at
   the three asynchronous submit sites (MpoQueueHeld, QueueDeferred, QueuePresent), with the physical address.
3. ISR, IRQ_ERROR for the pending sequence: PendingValid/PendingSequence cleared, the timeline keeps running, the
   pending PresentIds (PendingMpo*) are kept, PresentGate STAYS held (deviation from the spec text, needed with
   HOLD-1: a released gate would let a new Set3 overwrite the failed flip's PresentIds; with the gate held new flips
   merge into the held set or answer DEVICE_BUSY), RetryArmed = 1.
4. Vertical tick (ISR): RetryArmed and Tries < 3 -> re-submit the saved submission with a new sequence (one per
   vsync); success -> PendingValid again, the same PendingMpo ids latch and are reported as usual; Tries reset at a
   latch.  Tries reach 3 -> SwapRetryExhaustedReported++: the pending PresentIds move to the plane-off completion
   (PlaneOff*, reported at the next vsync in the same MPO2 entries the latch would have carried, exactly once),
   then PresentGate is released so the held set (HOLD-1) or the next Set3 commits.  Architect 2026-11-12: do not
   drop the ids (a static layer would wait forever); Asahi completes a failed swap with a fake vblank
   (iomfb_template.c ~1149-1164).  Cost: dxgkrnl believes a surface is shown that is not, until the next commit.
5. IRQ_ERROR not about the pending present (RELEASE failure, stale error): counted, no state change.  A transport
   failure of ConsumeInterrupt itself keeps today's Faulted behaviour (MMIO lost).
6. Receipts (Wom1MpoReceipt tail after the Ids): SwapFailures, SwapRetries, SwapRetryLatched, SwapRetryExhaustedReported,
   LastSwapFailureResult, ForeignErrors.

## WHAT IS STILL UNKNOWN
- DCP state after A407 without A408, and whether DCP accepts a new swap while an older one never latched
  (hardware; RQ-2 injection boot).

## Checkpoint
Host: shared client test (pending failure cleared only for the matching receipt) and an ISR replay test (IRQ_ERROR
then retry then latch reports the original PresentIds; exhaustion reports the ids exactly once, releases the gate,
and the next commit latches and reports only its own ids; a held set present during the retry is committed after
the retry resolves, not lost).
Hardware: one boot on the RQ-2 m1n1 with proxy injection (drop next D589 once; refuse next A408 once): expected
SwapFailures 1, SwapRetryLatched 1, no BUSY storm, StopDevice clean.
