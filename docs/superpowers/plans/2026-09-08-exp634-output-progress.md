# EXP634: post-fence output progress

Scope: keep EXP633 producer and functional KMD behavior; extend only existing
two-call diagnostics to localize the post-fence/pre-frame2-query watchdog.

Inspected: EXP633 raw correlation, host trace, matching private-symbol triage
dump; render_call_correlation{,_windows}.c, backend_platform_windows.c output
worker, scanout_windows.c query/present, render_completed_output.h, production
decoder. Microsoft System Worker Threads specifies PASSIVE work-item callbacks
and bounded work; it does not establish the cause of this watchdog:
https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/system-worker-threads

Observed: both fences Notify/DPC are durable; output stages are absent from
correlation; CPU4's worker callback is not available in the triage dump. No
evidence requests a firmware, Asahi, m1n1, Mu or hardware ABI modification.
Ownership remains existing Windows allocation/completed/display contract;
diagnostics own copied scalar records only. Existing exporter owns registry
persistence; it remains asynchronous and cannot guarantee crash survival.

Implementation: append four version3 records per target fence, preserving
independent validity, status, processor, pre-lock IRQL and timestamp. Sample
processor/time under the short correlation lock, export outside that lock.
Capture output entry before output-queue lock, verification after real reader,
and presentation entry/exit around the actual call. No disk flush added to
the functional worker. Keep v2 decoder support and fail closed on malformed
v3 size, validity, stage ordering and boot identity.

Review found a deterministic diagnostic defect: context lookup can relabel
an already-completed slot with a new fence on context reuse. Reject such
context lookups after worker exit; the first two slots stay immutable, later
calls trigger existing explicit overflow. This does not reject any DDI.

Verification: executable production helper tests and C-binary-to-decoder
roundtrip, malformed/old decoder cases, output ownership suite, render suite.
Freeze only changed files over immutable EXP633; pinned KMD/UMD/producer,
analysis, Universal, Inf2Cat/sign/version/hash gates before staging.

Hardware: one preregistered unchanged sixteen-frame workload with separate
live host collection of binary correlation and producer output. Diagnose only
what valid records prove. Missing asynchronously exported entry is not evidence
of absent execution. If still incomplete, improve observation offline instead
of guessing a functional cause. Preserve evidence then exact candidate cleanup
and established ordinary recovery; emergency only if needed.
