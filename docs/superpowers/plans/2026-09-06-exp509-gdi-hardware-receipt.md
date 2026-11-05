# EXP509 Windows GDI to physical AGX completion receipt

## Boundary

EXP508 proves Windows CDD Present, full-frame CPU copy and exact software fence.
No Windows GDI RenderKm packet or physical AGX TA/3D execution has been observed.
The existing production path already translates GDI PATCOPY into an EXP208 job,
publishes TA/3D queues, observes both hardware events/stamps/done pointers and
reports the exact Windows fence.  The missing piece is hardware evidence.

WHY THIS HYPOTHESIS:

1. The current noninteractive boot produces only CDD Present; no RenderKm receipt
   exists.  The supported Win32 GDI producer is already built and source-reviewed,
   but session0 enumeration has no display.
2. The generic nonpaging private-range contract is now applied consistently to
   Present Patch/Submit and GDI Patch/SubmitRender, removing the documented
   pre-AGX failure that EXP507 exposed.
3. Existing backend source completes a fence only after both TA and 3D event,
   expected stamp and expected done-pointer observations.  A single detailed
   receipt can therefore distinguish every remaining production stage.

## Design

WINDOWS CONTRACT: RenderKm and Patch prepare/relocate driver-owned DMA data;
SubmitRender queues the exact Windows fence.  Completion is reported only through
DXGK_INTERRUPT_DMA_COMPLETED and the matching DPC.  All non-PASSIVE callbacks may
write only bounded nonpaged scalar state.

AGX/ASAHI CONTRACT: preserve the current EXP208 image, context63, TA/3D queue,
dual event/stamp/done-pointer and backend completion contracts.  No command,
firmware, UAT, IRQ, power, scanout or queue behavior changes.

TRANSLATION: one portable 144-byte receipt state machine follows a single context
and fence through RenderKm, Patch, SubmitRender, backend submit, physical
completion, final queue progress, NotifyInterrupt and DPC.  Existing callbacks
update it under a dedicated spin lock.  The existing PASSIVE platform worker
persists it after completion; StopDevice persists the final DPC state.  No pointer
is retained, only scalar identities.  An independent 0x5090 power-broker QUERY
trace records the first GDI SubmitRender arguments and return status before a
possible immediate bugcheck; no m1n1 ABI change.

WHAT IS STILL UNKNOWN: whether an interactive Win32 PATCOPY reaches RenderKm;
whether Patch/Submit accept the real packet; whether physical TA and 3D reach
their exact event/stamp/done targets; whether Windows receives the same fence.

## Verification and hardware

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?  The portable state-machine test
catches mixed context/fence ownership, out-of-order stages, false completion,
missing dual-queue progress and DPC-before-progress races.  Broker codec tests
catch stale/nonmonotonic SubmitRender trace words.

Run RED then GREEN, all render/shared tests, pinned normal+qualification builds,
analysis, Universal, signing, versions and hashes.  Stage one exact candidate on
clean ordinary Code28 and natural full-owner boot.  Then the operator signs into
the existing local Windows console.  Run the existing signed/static producer
first with `--list`; use only its exact attached non-mirror display and LUID for
one `--draw`.  Preserve producer output, host log, registry receipt, ETL/events
and exact package identity.  API success alone is not PASS.

PASS requires: RenderKm success -> Patch success -> SubmitRender success ->
backend submit success -> exact TA and 3D event/stamp/done progress -> backend
completion success -> same Windows fence NotifyInterrupt and DPC.  Cleanup the
experimental package afterward and restore ordinary baseline.  This is first
AGX acceleration proof but not present/OpenGL/CS1.6 acceptance.
