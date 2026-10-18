# R154 independent code review

Reviewed 2026-09-28 against base f92e19dd after the final production/test freeze.
Scope: epoch correction, queued-preemption resubmission lifetime, affected tests,
and R154 plan/native-state/next-boundary/resubmission/UMD reports. Production
and test files were read only. No Air access, package, firmware, or commit action.

Critical: none found.

Important: none found in the reviewed correction.

The epoch change retains current parser, logical envelope, root, exact private
scene and cached output-backing validation under the state mutex. GraphBeginJob
still rejects uncertainty and conflicting leases/jobs and pins the accepted
graph before the mutex is released. The context epoch is refreshed only after
successful JOB_BEGIN. The real KMD/broker unrelated-unmap regression and separate
production output-validator replay exercise complementary parts of this contract.

Resubmission admits only the documented bit while retaining unsupported-bit
rejection. A v3 resubmission must match the exact context, manager generation,
scene generation and suspended old fence; started, uncertain, quarantined and
cancelled work is excluded. The existing queued hold survives deferred release;
full parsing precedes transfer to a new fence. Prepare/bind/queue rollback restores
the suspended hold while Submitting protects its local pointer. Final cancellation
and uncertain quarantine remain separate. Teardown discharges only the certain
never-started suspended transaction. The DPC publisher performs interlocked marker
operations only and is called after queued discard/image release succeeds.

The changes add no synthetic DMA completion, no old-fence completion and no GPU
execution shortcut. Microsoft's Resubmission and new nonpaging fence contracts
were checked directly:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_submitcommandflags
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-preemption

Minor / coverage limits:

- The regressions are complementary component replays, not one end-to-end
  execution of the complete Windows DDI lifecycle. G4 replay links the real shared
  scheduler and real submit-transfer functions but models the preempt DDI's
  packet-discard/image-release wiring. Combined replay executes actual private
  storage/reaping/BeginJob/completion/report/teardown, but models submit transfer.
  Existing implementation report acknowledges the latter. Keep these limits in
  validation claims; a future single combined DDI replay would strengthen coverage.
- The epoch tests could explicitly assert that failed validation/JOB_BEGIN leaves
  GpuvaG3MappingGeneration unchanged. Source placement currently guarantees it;
  this is a nonblocking regression-strengthening opportunity.

Independent test run after freeze:
`PYTHONPATH=tests python3 -m unittest test_g4_submit_virtual_replay test_g3_vidmm_replay.G3VidMmReplayTests.test_r154_preempted_private_scene_survives_deferred_release`
Result: 3 tests PASS, 5.243 seconds, including all four private-lifetime modes.
Inspected retained epoch/resubmission RED/GREEN evidence and the epoch ARM64
compile result. Final full-suite baseline comparison and all-dependent-TU ARM64
build were being completed by the main task and are not independently claimed
here.

Attribution is appropriately bounded: epoch mismatch is a demonstrated software
defect and a leading EXP867 hypothesis, not established crash causation. Flags0x80
receipt is not joined to stalled fence36357. UMD error counts are not per-DDI
failure probabilities or failed-GPU-job counts. Black primary latch proves neither
successful rendering nor that Present never ran. Recovery ETL stays excluded.

Verdict: no blocking code finding; suitable for offline implementation commits
once the main task's required final build/full-suite comparison and ledger work
are complete. Hardware, visible-frame, and 600-second gates remain unvalidated.
