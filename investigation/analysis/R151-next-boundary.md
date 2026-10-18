# R151 — preserve completion identity across native scene reuse

The deterministic defect is **G4 scene refresh resetting the live queue's
stamp sequence**. The offline correction preserves Sequence at both G4 bind
and release. It does not establish that duplicate stamps caused EXP865's TA
stall; EXP866 is the proposed single-variable hardware discriminator.

## What EXP865 actually proves

See R151-dump.md for field-by-field EXP864 comparison and new typed dump reads.
EXP864 stopped before Resolve/materialization/firmware publication because
worker owner1 failed runtime owner63 validation. EXP865 has matching owner63,
backend Submitted220, prepared/materialized work addresses and persistent queue
pointers TA3/D3 4. The concrete submit path only reaches Submitted after both
write pointers and both firmware Run messages succeed. Thus it passed Resolve,
G4 materialization, buffer-manager initialization on the first job, and queue
publication; a prequeue rejection does not explain current220.

One joined TA+3D completion is corroborated at149 by independent G3 and queue
state. It is node0/engine0, the single WDDM graphics engine encompassing both
firmware queues. Its freed scene prevents proven Windows process attribution.
Current220 is LogonUI.exe PID1216, node0/engine0, graph6/root9d6490000/gen895,
lease2/JobInFlight1. Scheduler219 and PagingLastCompleted219 are watermarks,
not70 successful native jobs between149 and220. Queue pointers and lease
counter support two submissions total in this normal queue lifetime.

Current220's TA done pointer3 is consumed, but stamp7a000000 is below expected
7a000100 and event0 was not seen. D3 pointer4/stamp3d000100/event1 were marked
complete. Both expected stamps are first-job values again. A D3 event flag and
matching reused stamp cannot establish freshness. Actual absence of TA progress
may still have a hardware execution cause; the snapshot cannot resolve it.

Current native render is16x16, one color attachment, no depth/stencil,
one layer/sample, sample size8. TVB heap4MiB and other declared ranges meet
current source capacity calculations. First149's geometry/backing is gone;
EXP864's refused149 is not a successful same-run reference. No captured
GrowTVB, UAT fault, RTKit crash or queue-fault record selects overflow/exhaustion.
Relevant MMIO and reserve-backed command/list pages are absent from the dump.
Those states remain unknown. Original ETL was lost before SSH, so recovery ETL
is excluded. There is no correlated Present/composed-frame proof, and the one
original scanout snapshot remains all zero.

## Current primary-source contract

Saved machine evidence was inspected first; no live machine access occurred.
Primary Asahi reference commit77cb8f24c2381a8abb7272d7bbdec548d6426a8a:

* `drivers/gpu/drm/asahi/event.rs:58–120` advances each queue stamp by0x100
  and loads the current CPU-visible stamp with acquire semantics.
* `workqueue.rs:852–930` treats an event as notification, reads the queue's
  actual stamp and completes only commands whose expected value is reached.
* `queue/render.rs:541–578,1347–1354` uses per-queue next values in finalizers
  and job metadata; TA and fragment progress are separate.

Current m1n1 `proxyclient/m1n1/agx/render.py:293–301` saves previous stamps,
increments both by0x100 and event_count by2; `:446–458` builds the 3D barrier
against the current TA stamp. `src/hv_agx_gpuva_v5.c` owns slot/root leases and
JOB_BEGIN/END; it does not create job stamps or report WDDM completion.
`src/hv_agx_retained_platform.c:48–82` validates retained backing and orders UAT
visibility. Mu `J313AppleAgxAbiAdmission.asl.inc` owns APPL0002/resource exposure,
with synthetic scanout IRQ889; it does not own TA/3D retirement. The active KMD
polls firmware events in its worker. First149 completion and D3 event ingress
exclude a complete absence of this ingress path; changing IRQ routing is not
justified. Physical AIC mailbox routes from Asahi are not IRQ889.

In KMD `render_backend_image.c`, Prepare constructs a zeroed candidate.
Both BindG4Submission and ReleaseSubmission called it while the provider,
event allocator and firmware queues remained alive. StageJob therefore used
Sequence1 every time. `apple_agx_exp208_dynamic.c` faithfully serialized the
wrong lifetime: previous stamps at bases, expected stamps at bases+0x100,
barrier waiting for the same TA value, event count2 and old Start3D queue count.
The dump's image Sequence1/current stamps and second-job ring pointers match
this source defect without needing historic template-byte comparisons.

`apple_agx_render_shared_memory.c` preserves queue-owned objects but copies
per-submission objects9/10/12/14/15/17/18/19/26/27. Consequently the wrong
previous/current values reach firmware-visible stamp words, work, barrier,
finalizers and event_count. Object11 event-control state remains persistent;
object12 event_count is refreshed, so no independent event-count fix is needed.
`apple_agx_g13_queue_runtime.c` requires the event, stamp and done pointer;
`apple_agx_backend_runtime.c` gates success on both queues. BackendComplete
then ends the G3 job, publishes the completion transaction and uses synchronized
DxgkCbNotifyInterrupt with the actual fence/node/engine. No stage may invent
completion merely to unblock Windows.

Ownership: KMD owns preparation and lifetime identity, submission, polling,
DMA/resource retirement and Windows notifications; firmware owns execution
and observed stamps/events; m1n1 owns power/UAT/lease grants and retained
transport; Mu owns resource/memory exposure. This correction belongs in KMD.
No Asahi code was copied, no firmware or Windows interface was changed.

## Correction and causal limits

Preserve the existing Sequence around each successful G4 template rebuild.
Initial Prepare and explicit RestartQueueLifetime still reset the sequence;
failed bind/build still fail closed. No new admission values, timeout, reset,
capability, signer or firmware behavior is introduced. The existing stamp
limit remains checked, and stale completion is still rejected by the actual
queue completion predicate.

The RED replay aborts after G4 release because Sequence is0 rather than1.
GREEN exercises two G4 jobs, G4→legacy→G4, actual serialized fields, stale
stamp refusal with matching current done pointers, explicit queue restart,
wrong-fence release and exhaustion refusal. The active shared-memory replay
checks all four stamp words, barrier/finalizers and event_count while preserving
event-control and queue-pointer state. These are actual production image,
builder, materializer and completion-predicate functions with modeled firmware;
they do not simulate GPU execution or prove a recovered hardware desktop.

Cause ranking for current220:

1. Proven duplicate completion identity/reset of previous stamp words on the
   second job. It is the strongest causal difference at this lifetime boundary.
   It can admit stale identity and break barrier/progress reasoning; the exact
   firmware response is not inferred from the CPU snapshot.
2. GPU/firmware fault, mapping/content error, partial-render handling or TA
   finalizer failure remain possible. No retained fault receipt discriminates
   them; do not alter TVB, IRQ, shader, power or reset based on guesses.
3. A generic prequeue failure is excluded for current220. Its separate leak
   is documented in R151-prequeue-contract.md and left unchanged.

Microsoft's [interrupt-type contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_interrupt_type)
reserves DMA_FAULTED for system use. A truthful unpublished-job abort requires
an atomic publication disposition, separate lease rollback without GpuDone,
and a supported OS packet disposition. Returning success from a reset while
firmware work may still access memory violates the reset contract. R151 therefore
does not add a speculative failure notification or weaken real in-flight reset.

## Proposed EXP866 — not preregistered or authorized

WHY THIS HYPOTHESIS:

* The dump has one prior private149 completion and second-job ring positions,
  but Sequence1 and first-job expected stamps again: exact evidence of lifetime
  mismatch, closer to missing TA progress than unsupported TVB/IRQ hypotheses.
* The real G4 bind/release replay resets the sequence and the primary Asahi/m1n1
  contracts advance it; the corrected replay rejects stale first-job stamps.
* TVB sizes fit the current16x16 command, with no recorded overflow/fault;
  this does not prove TVB correctness but gives no stronger competing cause.

Single variable: apply only this R151 sequence correction to exact package865
source26cbd7e6, retaining owner correction, USC and error publication. Keep
R143 m1n1/Mu/AML exactly unchanged; exclude independent Flush0f51f3f5 and
prequeue handling. Do not package the combined integration branch as that
candidate. Record exact source, artifact, signer/catalog and manifest hashes
before any separately authorized preparation/run.

Checkpoint: first job retains Sequence1 success; second job uses Sequence2,
TA/D3 expected7a000200/3d000200, event_count4, previous stamps+0x100, no InitBM,
and reaches fresh TA+D3 completion and correct WDDM fence retirement. Capture
current expected/observed stamp pairs and event ownership before interpreting
D3 completion. If TA still stalls with fresh identities, reject sequence reuse
as a sufficient stall explanation and use that next exact receipt; do not retry.
A nonzero correlated Present and600s SSH/DWM stability remain separate gates.

The smallest additional diagnostic proposal if the corrected run remains
ambiguous is one bounded first-failure record tied to fence/process/root/scene
and queue sequence: publication mask, expected/actual CPU+firmware stamps,
event_count/control values, read/write/done pointers, last event kind/flags,
and existing queue-fault/UAT/RTKit status validity. Preserve first raw fault or
GrowTVB message before acknowledgment. This receipt is a separate variable,
not silently bundled into this correction; no MMIO reads with guessed offsets.
The current dump already discriminates Sequence1 versus2 for EXP866.

Recovery remains the saved EXP865 ordinary Code28 contract. If separately run,
collect original-boot evidence/dumps before exact-package cleanup, preserve
known recovery artifacts and durable transitions, use hidden emergency recovery
when necessary for a crashed active device, then restore ordinary Code28. No
package, Air transfer, boot, rearm or recovery operation occurs in R151.

## Verification and delivery

Targeted image3/shared1/queue2 tests pass under ASan/UBSan with the configured
Clang wrapper. The new G4 release regression failed before the fix at Sequence1
(expected1, actual0). Independent review has no blocking/Important findings;
both requested extra coverage cases were added and rerun successfully.

Full host suite1162 tests,161.885s,15 failures/38 errors/2 skips: all53 failing
identities exactly match R150, with no added/removed identity. This is not a
clean-suite claim. Exact names and full-log hash are in full-suite-result.json.
The final mixed-lifecycle and active-memory assertions were added during that
suite and run separately afterward; production code was unchanged throughout.

Affected render_backend_image.c compiled ARM64 /W4 /WX /analyze exit0,
0 compiler warnings/errors, MSVC14.44.35207 and WDK26100, using the persistent
builder's existing exact compile command with isolated output paths. No package,
link or signing operation. All536 inputs were hash-verified,6 changed integration
inputs copied initially (including pre-existing Flush files), then only2 expanded
test files synchronized. The one changed production TU is the R151 correction;
no claim that the combined integration tree is an EXP866 package. Compile input
and final input manifests are both retained, and the final list SHA256 is
32b94b98fe598151e1fe1b0ac202abaf55f4149595ac2d213d156c9bc0ff63ca.
Matching object hash and argv are in compile-result.json. Build-script Python
archive deprecation and SSH transport advisories are not compiler diagnostics.

Tandem review SHA256 remains753bdfe6; OPEN dispositions are recorded in
R151-review.md, preserving source-first/anti-loop/recovery requirements.
No hardware experiment or recovery was performed. Existing dirty m1n1/Mu trees
are untouched and excluded from the implementation commit.
