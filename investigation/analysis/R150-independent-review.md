# R150 independent review

2026-09-28. Verdict: **ACCEPT for the scoped offline owner correction**, with
the validation and evidence limits below. No blocking code or regression issue
found. Review base `96da01f0f1ce550007a2f55d41a941ef0c13eda9`; reviewed the
uncommitted removal of the G3 `submission.ContextIdentity = 1u` assignment and
`tests/test_r150_backend_identity.py`. No production/report edits, commits,
builder access, package operations or hardware access were performed by this
reviewer. This file is the only review-authored repository change.

Read the project rules, compact current boundary, NEXT_TASK_R150, canonical
tandem review, R150 plan/contracts/ETL/main report/dispositions, relevant worker,
backend validator, G3 BeginJob/private-reset and scheduler-reset source, G4
materializer/VM-slot selection, and saved original-dump debugger outputs
`state.txt`, `job.txt`, `scene.txt`, `backend.txt`, `process.txt`, `discover.txt`.

The recorded dump state independently corroborates scheduler Active149 after
Completed148, pending preemption1, scene Queued/Started, JobInFlight1, backend
Ready/owner63 with no pending submission/job, provider PendingFence0 and queue
FirstRun flags. BeginJob precedes backend validation in the executed source.
The owner equality gate is before Resolve, relocation and both queue callbacks.
The mismatch therefore predicts the observed first refusal without requiring
a GPU-execution, shader, IRQ-loss or UAT-fault hypothesis. The report correctly
identifies the refusal as a source/state inference: the local returned enum was
not separately persisted. Broker lease acquisition is not firmware dispatch.

Keeping owner63 is the correct layer and minimal edit. Runtime start and reset
both select63 for G3; B1 has its own owner1 constructor. The G4 materializer
passes VM slot1 explicitly to AppleAgxG4BuildTa3d, whose VmSlot reaches
AppleAgxRenderTemplateSelectVmSlot independently of the submission owner.
BeginJob independently leases slot1. Weakening the equality guard or changing
the runtime owner to match a hardware slot would erase this separation.

Independent checks performed:

* `python3 -m unittest discover -s tests -p test_r150_backend_identity.py -v`
  passed. It extracts real start/reset initialization and worker construction,
  links the real portable validator, and verifies modeled Resolve/Relocate/
  Run3d/RunTa reachability. Legacy, B1, G3 and non-GPUVA G3 branches are covered.
* Replayed the same test with the baseline worker obtained through `git show
  HEAD:drivers/apple-agx/render-admission/src/backend_platform_windows.c` in
  memory, without modifying source files. Only G3 failed, with backend63,
  submission1, InvalidArgument, BeginJob1 and all four later callbacks0.
  This is a meaningful negative control, not a source-text assertion.
* Foreign-owner cases retain rejection before callbacks and preserve Ready/
  empty pending state. The fixture doubles BeginJob and firmware boundaries;
  it does not prove real firmware execution, broker concurrency, or safe abort.
* `python3 -m unittest discover -s tests -p test_apple_agx_g4_builder.py -v`
  passed against the production materializer. Slot separation is additionally
  established by unchanged production call sites; this test does not assert
  every encoded slot field as a new R150 end-to-end invariant.

Reset behavior is correctly described as an existing recovery limitation.
Queued+Started takes PrivateReset's fail-closed branch before runtime reset;
the dump cannot identify which reset invocation first poisoned the process.
QUERY24 is process poison before context lookup, not proof of missing context
or zero-length input. Saved ETL BootTime/software-adapter provenance must keep
it excluded from the original packet's timeline.

The capability recommendation is appropriate for this change. Microsoft's
[per-engine TDR contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8)
retains adapter-wide recovery obligations and escalates a failed engine reset;
[ResetEngine](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetengine)
requires completed reset, empty hardware queue and readiness for new packets.
Changing a capability bit or returning artificial success cannot supply those
guarantees. This review does not certify current recovery capability as complete.
Monitored-fence atomic publication remains a separate deferred boundary.

Minor documentation caveat: R150-contracts retains audit-phase wording such as
“No fix is made,” “does not prove this branch ran,” and a future regression
recommendation alongside its later correlated update. Label those as the
read-only contract investigator's scope, or reconcile them in the final report
set. They do not invalidate the main report's bounded conclusion.

Review follow-up: the coordinator reconciled the principal stale branch/fix
wording and the updated source correlation is consistent. The final remaining
validation paragraph may likewise be updated from its earlier prospective
regression wording; this remains editorial and nonblocking.

The coordinator owns final full-suite comparison. After review, inspected
`investigation/evidence/R150/compile-result.json`: ARM64 source compile has
exit0 with /W4 /WX /analyze and the G3 profile, source_count536 and isolated
object hash `4306cf2917e62162026a6ebc85ca39009a0e2bca781e125e4d1a1db690c94c70`.
The ARM64 compile was not independently rerun here. Acceptance is for
the reviewed offline correction, not a hardware/recovery success claim. EXP865
must remain a separately authorized, hash-gated single-variable candidate on
exact EXP864, excluding integrated Flush retirement. No new package or run is
authorized by this review.
