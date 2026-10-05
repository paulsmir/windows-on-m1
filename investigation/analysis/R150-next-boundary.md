# R150 — EXP864 stranded backend ownership, offline verdict

The original kernel dump proves **KMD admission and activation of render fence
149**, but the current-source replay identifies a refusal **before firmware
queue submission**. The G3 worker changes the backend owner from63 to1, while
its backend runtime retains63. The equality guard rejects the request before
Resolve/materialization/Run3d/RunTa. This is a KMD ownership-envelope defect,
not a lost GPU completion interrupt or a measured shader hang. The acquired
broker job lease remains outstanding; boundary preemption then cannot finish,
and the intentionally unsupported active-private reset returns C0000483.

Work is on integration/ad04-windows-compiler from96da01f0, with the requested
R148/R149 fixes retained. Executed package864 was sourcea9ecd3ea, without
Flush0f51f3f5. No Air access, package build/install, firmware change, launch,
reboot or recovery action took place in R150. Builder use is offline only.

## Original evidence and identity

Evidence root: main repository `.local/experiments/EXP864-r149-usc/`.
`hardware-evidence/Kernel-MEMORY.DMP` is a kernel bitmap dump,566937218 bytes,
SHA256 `c29d41e871c9ecc97cd2147ec037dcc4b2667b8bab430213816a810b211d404e`.
The debugger matched package864's private PDB (no forced symbol loading).
`lmvm` identifies basefffff802f6ed0000, timestamp6ABA47AA and checksum607B8.
The relevant worker, runtime and G3 source files match package864's source.zip
byte for byte before the fix. Raw commands/output and hashes are retained in
`investigation/evidence/R150/` and main `.local/experiments/R150-dump/`.

`!analyze -v` reports VIDEO_TDR_FAILURE at uptime29.063s:

| Parameter | Value | Meaning |
| --- | --- | --- |
| P1 | ffff998fcc15e010 | Internal recovery-context pointer; public type unavailable |
| P2 | fffff802f6ef6cb0 | AdmissionDdiResetFromTimeout owner address |
| P3 | ffffffffc0000483 | STATUS_DEVICE_HARDWARE_ERROR, failed recovery operation |
| P4 | 3 | Internal context-dependent data; no invented decoding |

Stack: nt!KeBugCheckEx → dxgkrnl!TdrBugcheckOnTimeout →
DXGADAPTER::PrepareToReset → DXGADAPTER::Reset → TdrResetFromTimeout → worker.
The earlier live141 at28.920s has VidSchiResetEngine → VidSchiResetEngines →
VidSchWaitForCompletionEvent → VidSchiWaitForCompletePreemption →
VidSchiCompletePreemption → VidSchiSwitchFromSuspendedDevices. Together these
show preemption wait and failed recovery, not initial hardware fault provenance.
[Microsoft 0x116 parameters](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x116---video-tdr-failure)
and [ResetFromTimeout](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetfromtimeout).

The APPL0002 device's driver extension identifies adapterffff998fcaa70000;
its PhysicalDeviceObject equals the ACPI stack'sffff998fc1aa5dc0. Matching
private types give the following internally cross-checked state:

| Owner | Dump value |
| --- | --- |
| Scheduler | Completed148; LastSubmitted/Active/Dispatched149; queued0 |
| Preemption | Fence1, cutoff149, active149, last-completed148, WaitCurrentBoundary |
| CPU paging | LastCompleted148, Pending/Workers/DPCs0, completion status0 |
| Render packet/context | Active, fence149, contextffff998fcaff7970; node0, engine-affinity1 |
| Native command | VA0x2c0000, bytes0x118, generation469; private envelope0x1c0 |
| Process graph | ProcessId5/generation1, root0x9d8600000, slot1, lease1, JobInFlight1, LastStatus0, Uncertain0 |
| Private scene | Queued1/Started1, GpuDone0/Reported0, Quarantined1;16x16 geometry |
| Backend | Ready, ContextIdentity63, zero PendingSubmission/PendingJob, TA/3D incomplete |
| Queue provider/runtime | Created/Ready, PendingFence0, TA/3D/compute FirstRun1, no staged work, guards0 |
| Platform worker | WorkersActive0/WorkScheduled0, ProgressValid0, CompletionContextNULL |
| KMD observation | SchedulerFaulted1; G4 reject claim/count0; correlation count0 |

**Process association:** graph5 points to DxgkProcessffff998fc8789670. Its raw
memory contains EPROCESSffff998fc8974080 and PID0x4c4; `!process` resolves that
EPROCESS to LogonUI.exe PID1220. Independently, the render context's
Win32Generation0x04c40001 encodes PID1220 per `umd_runtime_device.c:22`, and
the original UMD log records PID1220. This strongly corroborates LogonUI;
DXGPROCESS's private type is unavailable, so the raw internal offsets are not
claimed as a published ABI. DWM is a different EPROCESS/PID1228. Driver
Graph.ProcessId5 is a driver-local ID, not Windows PID5. Fence149 is the
observed outstanding render; completed148 includes CPU work and is not proof
of148 successful GPU renders.

## First lost-progress boundary

`AdmissionGpuvaG3BeginJob` successfully calls GraphBeginJob(...,1), leases the
process VM slot and sets scene Started **before** the worker calls
AppleAgxBackendRuntimeSubmit. Its next request has ContextIdentity1, whereas
both G3 start and reset runtime initialization sites use63. Actual
AppleAgxBackendSubmissionValid requires equality. It returns InvalidArgument
before touching Resolve, Relocate or any queue. The failure worker branch sets
SchedulerFaulted and exits, leaving the active scheduler fence/private lease.
That envelope error is deterministically implied by the executed source and
dump state; the local returned enum was not separately persisted. Existing
submit-result telemetry is compiled out in this G3 profile.

Thus broker JOB_BEGIN was accepted, but **no TA/3D work for149 reached firmware**.
Pristine pending/staged/first-run state agrees with the source path. There is
no job-specific expected stamp/event to compare, and no TA/3D completion to
notify. Firmware initialization happened earlier. SGX/broker MMIO pages are
absent from the dump and are not interpreted as zero or as proof of no global
firmware/UAT fault. Such a fault is unnecessary to explain this request's
pre-publication refusal. The single interrupt count is scanout; the active
completion path polls firmware events in a worker. See R150-contracts.md.

Fix only the violating owner: preserve backend identity63 in the G3 worker.
The hardware process slot remains independently1 in the G4 materializer and
broker. Do not weaken equality checking, change runtime ownership to1, fabricate
completion, or alter timeout, capability, power or firmware behavior.

## Reset and QUERY24

`AdmissionDdiResetFromTimeout` delegates ResetEngine. Its first G3 check invokes
AdmissionGpuvaG3PrivateReset. The dump's exact scene satisfies Queued && Started;
that branch quarantines/poisons and returns false, selecting
STATUS_DEVICE_HARDWARE_ERROR before the later runtime-reset path. This is
fail-closed by design because there is no proven GPU stop/TLB reclamation.
The historical state alone cannot distinguish whether the first engine reset
or later adapter reset first set the poison bit; both visit this predicate.

A recoverable real TDR requires stopping future DMA, quiescing the affected
TA/3D/firmware work and workers, reconciling broker leases/UAT, reporting correct
aborted fences, and a restart-ready engine. Merely returning success or
DEVICE_REMOVED cannot satisfy that. Clearing SupportPerEngineTDR would skip
one recovery tier but retain mandatory adapter-reset obligations; it would
not make this safe or eliminate0x116. No caps/reset edit is justified as this
fix. Pre-publication rollback is a separate potential improvement requiring an
explicit atomic no-publication ownership transition and negative race tests;
raw Ready/zero-pending alone must not release possibly live resources.
[ResetEngine](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetengine),
[per-engine TDR](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8).

QUERY receipt v3 guard24 is **process.Poisoned**. Its process5/root0x9d8600000/
generation469 matches the stranded job. Capture precedes context lookup and
range validation, explaining context-token0/query-bytes0; it is not a missing
context, zero-byte request or leaf failure. Reset poisoning is consistent with
this snapshot, but the immutable receipt has no timestamp/writer history to
prove temporal order. No query relaxation follows. R150-etl.md includes the
exact decoding and source hashes.

## ETL provenance correction and cause ranking

The saved ETL is from **GPU-hidden recovery**, not the crash. Its BootTime
11:06:42.7482369Z exactly matches recovery Event12. Only1414:008d/008c Microsoft
adapters occur; all767 DMA starts join Basic Render008c and complete. Its
pointer domain differs from the original dump. None of those DMA/fence/process
events may be joined to the original Apple packet. An append-only EXP864
correction records this; original result files remain intact. UMD445 lines has
no reject-seterror and no exact submit/fence timeline; lack of such logging is
not positive submission proof.

1. **Confirmed deterministic current-source defect:** wrong backend owner after
   broker lease acquisition. It predicts the observed stranded149/Ready/no-job
   state and prevents reaching firmware. Implement the minimal owner fix.
2. **Confirmed recovery limitation:** any Started private scene fails reset,
   even this pre-publication failure. It explains escalation, not the initial
   refusal. Retain fail-closed semantics pending a proven safe abort contract.
3. **Unproven later failures:** native materialization, firmware/UAT faults,
   event polling or WDDM completion notification. They are beyond the observed
   first refusal and are not targets for speculative changes.

The source audit also noticed bytewise monitored-fence publication. It is a
separate contract issue requiring an atomic-write replay and compiled-code
review; it does not explain render149 failing before backend execution and is
deferred to its own boundary. No timer, QUERY, shader or IRQ workaround is made.

## Proposed EXP865 — not a preregistration or authorization

WHY THIS HYPOTHESIS:

* Original dump has Active149 but Ready owner63/no materialized job; actual G3
  worker owner1 deterministically fails the real equality guard before queues.
* Broker slot1/Started1 is acquired before that guard, explaining stranded
  preemption and fail-closed reset without needing a GPU hang hypothesis.
* G4 slot1 selection is independent of backend owner, so retaining63 corrects
  ownership without changing GPU address placement or firmware.

Single variable: cherry-pick the R150 owner correction onto exact EXP864
sourcea9ecd3ea, preserving USC/error publication, pinned R143 firmware and signer,
and excluding integrated Flush0f51f3f5. Exact package/source/manifest hashes and
recovery artifacts must be recorded before a separately authorized build/run.
No artifact is built or preregistered here. Do not use combined integration HEAD
as that single-variable candidate.

Smallest checkpoint: fence corresponding to the first G4 render passes backend
owner validation and reaches materialization/queue admission; then collect its
actual next refusal or expected/observed TA+3D completion evidence. A later
failure is causal advance, not automatically regression of this correction.
No successful GPU render or recoverable TDR is claimed from offline testing.
If original-boot ETL cannot be preserved before automatic reboot, retain its
per-boot file separately; use matching kernel dump and the existing backend/
provider state, never the overwritten recovery ETL. One run, evidence first,
exact-package cleanup and ordinary Code28; GPU-hidden only for unrecoverable
GPU-visible state. No retry/rearm is authorized by this proposal.


## Verification and disposition

The new test extracts both real runtime initializers and the actual worker
envelope, then links production backend validation. Before correction G3 alone
fails backend63/submission1/InvalidArgument after BeginJob1, with Resolve,
Relocate, Run3d and RunTa all0. After correction legacy, B1 and G3 pass at both
start/reset sites, including null-process G3 contexts and foreign-owner refusal.
ASan/UBSan is enabled. Firmware/Windows boundaries are modeled; this is not a
real GPU completion test. The unchanged production G4 builder replay passes.

Affected backend_platform_windows.c compiles ARM64 /W4 /WX /analyze, exit0,
MSVC14.44.35207 and WDK26100.536 persistent source inputs were hash-verified;
only5 differing integration files transferred (the one KMD fix plus4 existing
Flush-retirement files). Only the affected KMD TU was compiled, with isolated
object/PDB outputs and no package/link/sign action. Final source-tree hash list
is evidence/R150/source-hashes.json; exact argv/object hash in compile-result.json.

Full host suite:1162 tests in154.927s,15F/38E/2S, exactly the R149 baseline
failing identities with no additions or removals. This is not a clean-suite
claim. All53 identities are listed in evidence/R150/full-suite-result.json;
original full log remains under main .local/experiments/R150-dump. Independent
review ACCEPT in R150-independent-review.md. Canonical tandem review is
unchanged753bdfe6; every OPEN item's disposition is in R150-review.md.

R150 source correction is implemented offline, not hardware-validated. The
original EXP864 recovery verdict remains accepted; its ETL/acceptance
interpretation is corrected append-only. No unrelated submodule changes are
included.
