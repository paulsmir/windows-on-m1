# R137 offline process-backing implementation

Status: steps 1–7 implemented and verified offline after the user-authorized
completion-owner amendment in R136 section 6. The historical stop and withdrawn
prototype below remain evidence; they are superseded by the continuation.
No hardware/package readiness or GO_EXP855 is claimed.

Plan: R136-process-backing-decision.md section 3, steps 1–7. Starting HEAD
871fb22602c390e94f398aeebbe875a7887521da. No hardware or package build;
Mu/m1n1 are read-only, with pre-existing dirty submodules.

Source-first: inspected the current R136/EXP854B compact boundary and decision,
Asahi buffer.rs ensure_blocks, m1n1 hv_agx_gpuva_v5.{c,h} and
hv_agx_retained_platform.c, Mu MemoryInitPeiLib.c, pinned WDK26100
CreateProcess/reserved-GPUVA definitions and Microsoft Learn reservation docs,
then the current memory runtime/segment and G3/G4 producer/parser/builder code.
The source/ownership/launch contract and REVIEW dispositions in R136 apply.
No live observation is asserted. The user approved this existing architecture
and explicit sequential commits; no new design approval is inferred or needed.

## Step 1 — R64 partition

The adapter reserves [0,40 MiB) for VidMm, [40,56 MiB) for private storage,
and [56,64 MiB) for the unchanged backend. Physical addresses continue to
come from the validated borrowed R64 receipt. The segment may shrink because
KMD declares its capacity before VidMm allocations; the firmware still excludes
the full 64 MiB from OS RAM. Both QUERYSEGMENT views and scanout/local paging
consume LocalAllocationBytes. Fixed backend aliases now use a separate offset.
The portable partition function rejects invalid alignment, overflow, overlapping
budgets, repeated partition and changes after publication, without mutation.
Full-reserve UAT lifetime remains unchanged. No private allocator exists yet.

RED: new partition API/fields absent and the runtime still exposes 56 MiB.
GREEN: five memory tests (portable C with ASan/UBSan), three reserve tests,
one caps/profile test, 17 real-DDI/v5 G3 replay tests and seven G4 replay tests.
Full unittest discover: baseline and changed tree each ran 1130 tests with
15 failures, 41 errors and two skips. Exact failure/error names are identical;
see .local/experiments/R137-offline/unchanged-failures.txt. Logs and SHA-256
manifest: .local/experiments/R137-offline/step1-manifest.json.

Remaining steps: bounded pool/reservation, initialization, private subtree
lifecycle, production escape, submission binding, retirement/recovery.
First hardware checkpoint remains the separately authorized EXP855 checkpoint
in the decision document; capacity reduction and CPU checks prove no GPU work.

## Step 2 — reservation and bounded allocator

CreateProcess now calls the pinned callback at PASSIVE_LEVEL before taking the
state mutex, passing hDxgkProcess, size/alignment 32 MiB and zero flags/base.
System processes follow the same path. Returned addresses must fit the 39-bit
space outside root entry zero and respect leaf coverage. Callback failure
frees unpublished metadata. No manager/data allocation occurs in CreateProcess.
The adapter owns a 256-unit (16-MiB) allocator, with a 128-unit per-process quota,
64-KiB charged extents and monotonically increasing nonzero generations.
Exact owner/extent/generation is required for release; table/data callers will
share this one allocator. Free requires prior unlink/TLB/revoke and zeroing;
the primitive does not itself claim those later steps.

RED→GREEN: real outer CreateProcess/v5 replay first failed its callback-count
assertion, then passed system/user/failure/invalid-address paths. Allocator
ASan/UBSan replay covers two full-quota owners, exhaustion, stale/forged free,
rounding, invalid requests and generation wrap. All 18 G3 tests pass.
Full suite: 1132 tests, identical 15 failures/41 errors/two skips vs baseline.

Pinned WDK26100 ARM64 compilation exposed a pre-existing R135 C4242 in the
InitialUpdate argument; explicit BOOLEAN normalization fixes that warning,
with unchanged R135 replay behavior. Isolated compile and direct link succeed;
no INF/CAT/package/signing target ran. Intermediate missing include/library
paths and the initial stale builder paging file are preserved in the logs.
Changed-file synchronization and a full driver-source hash comparison identify
that stale file before the final compile; final source hashes are recorded in
.local/experiments/R137-offline/source-hashes.json. Unsigned compile-only SYS:
C:/Users/pauls/R137-offline/R137-kmd.sys,
SHA256 d2191db887c4ff172e5c6cd197f7629454456c585604b5a20f01d34f424a7758.
This is no package or launch candidate. Logs: step2-manifest.json.

## Step 3 — kernel construction primitives

The kernel-only private view derives CPU/IPA/PA from the borrowed receipt and
checks both the partition and allocation bounds. It exposes no user mapping or
shared fixed GPUVA. PreparePrivateStorage uses that view under the caller's
state lock, rejects poisoned/leased/in-flight state and orders CPU stores before
later publication. The shared construction primitive zeroes every charged byte,
writes TVB list words from the reserved heap VA, preserves an existing manager,
and gives each scene distinct scratch. OOM before publication rolls back all
new extents and zeroes them before reuse. Current builder limits (32 blocks,
one layer/sample) are preserved.

Primitive tests pass under ASan/UBSan: all nine poisoned extents/padding, page
and block words, unchanged firmware-owned manager, simultaneous scratch and
third-scene OOM, plus budget failure at every construction stage. Initial RED
was the missing construction API. The OOM test's endpoint was corrected from
94 to 93 units: 93 units is exactly a complete set, not an allocation failure.
Full suite: 1133 tests with the same 15 failures/41 errors/two skips. Pinned
WDK26100 ARM64 compile/link passes. Compile-only SYS SHA256
 af5b3d1e34ed0c28224eea89f807b1591c456928d99fe283ab879b42b821e208.
The section-4 producer/handoff gate remains pending steps 5–6; these are kernel
construction primitives, not proof that UMD has stopped allocating its BOs.

## Stopping verdict and review

The exact-scope retirement conflict and the withdrawn step-4 root-detach defect
are recorded in R136-process-backing-decision.md section 5. Step4's 19 passing
G3 tests did not cover retained mapping on the bootstrap conflict. Its full
suite ran 1134 tests with unchanged baseline failure names; those results do
not establish a safe private lifecycle. No step-4 implementation is retained.

Final review dispositions: ACCEPT the missing owner interface as the task's
explicit stop condition; WITHDRAW the complete uncommitted root/grant prototype
including its important transactional defect. Do not reinterpret the latter as
fixed. The committed primitives have no reviewer-identified must-fix defect.
Windows admission/capacity, private publication/teardown, escape/UMD/submission
binding, and GPU/EXP855 readiness remain unproven because those gates were not
implemented or run. The reviewer did not independently rerun test/build logs;
the verification reported here was executed by the implementer.

Final artifact policy: the unsigned standalone SYS files are compile/link
checks only; no INF/CAT/package/signing target ran. Earlier generic builder
output paths were overwritten by later checks; their historical hashes remain
in the logs/CHANGES rows. A final immutable host copy is recorded below.

## Final verification of retained prerequisites

Evidence paths in this R137 report are relative to this worktree, not the main
repository's .local: /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler.
Final focused command (CC=clang) loads test_apple_agx_render_memory,
test_g3_kmd_local_reserve, test_gpuva_g3_caps_contract, test_g3_vidmm_replay,
test_g3_private_pool, test_g3_private_storage, test_g4*replay and test_change_ledger:
**38 tests PASS**. Final `python3 -m unittest discover -s tests -v`:
**1133 tests, 15 failures, 41 errors, two skips**. Exact failure/error names match
the initial 1130-test HEAD baseline; final-failure-comparison.json lists all 56.
The full suite is not green and is not reported as such.

Final pinned WDK26100 ARM64 compile/link of the retained source: zero warnings
and errors, with 533 driver-source hashes verified against the persistent
builder (mismatch0). Immutable host copy:
.local/experiments/R137-offline/final-steps1-3.sys, SHA256 `1a7b7f2a83f0345daa813314fd9ccf571461ddf9b180ed686f2da2ddce843c6d`.
Exact commands are in compile.ps1; source hashes, logs, rejected attempts,
withdrawn patch and review dispositions are bound by final-manifest.json,
SHA256 `adba478c95f1d543c39013ec141d0f7c149afa421b2d9a2e6dbc4f5d2ddea78e`. This unsigned standalone link check is not a package.

m1n1 and Mu HEADs/diff hashes still exactly equal the pre-existing values in
R136 section 1. No Air control plane, USB/proxy endpoint, install, hardware
experiment or recovery action was used. No EXPERIMENTS entry or hardware
validation is claimed for this offline source task.


## Authorized continuation: step 4 — private graph ownership

R136 section 6 supersedes the prior scope stop. The withdrawn prototype was
restored as a reference implementation and its reviewed bootstrap-conflict
regression failed at the private ContainsRange assertion (step4-review-red.log).
The final implementation preflights both roots before detaching either; failed
broker relocation restores both previous links or poisons the graph. Successful
parking reattaches private access immediately. Existing populated-root and
job/lease restrictions remain. Explicit PrivateBacking uses exclusive grants
without changing SharedBackingGeneration; data/table collisions, cross-owner
shared registration and generation exhaustion fail closed. Private access is
restored after parent updates and SetRootPageTable, through the actual root.

Verification: G3 replay 19 tests pass including profiles16/64, ordinary 4K/64K
transitions, private reattachment, rejected-bootstrap mapping/generation
preservation, broker-busy restoration, exclusive grants and teardown. Pinned
WDK26100 standalone ARM64 ClCompile/link passes with zero warnings/errors;
no INF/CAT/signing/package targets. SYS `.local/experiments/R137-offline/step4.sys`
SHA256 `8104ee6e37c6ce6dbc661fff5d895aa2ffbb891249854fb20cb4b3020c4ccbe7`.
Full-suite comparison is recorded in step4-full.log before the commit.
REVIEW.md SHA256 remains 753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5;
section 4 R136 dispositions stand. No hardware validation is claimed.

## Step 5 — production private escape

Version 1, fixed-size acquire/prepare/release payload accepts geometry and opaque
identities only. The runtime-supplied KMD process, device and context handles
must match attached objects; missing handles, unrecognized flags, input ranges,
reserved words, stale identities and cross-context release fail closed. Payload
is copied once. HardwareAccess (and no other flag) is mandatory because this
operation publishes broker tables. See pinned WDK26100 d3dkmddi.h and Microsoft's
[D3DDDI_ESCAPEFLAGS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_escapeflags)
and [DXGKARG_ESCAPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_escape).

The first valid request charges two 64-KiB private table extents, constructs
manager/scene data and publishes exclusive 16-KiB leaves into the reserved
subtree. Empty tables are a process cache charged until DestroyProcess; ordinary
OOM leaves no public scene token. Known publication failures unlink/revoke new
scene and manager storage; uncertain failures retain charged records and poison
the process. Release of an unqueued scene unlinks/revokes before zero/free.
GraphDestroy acknowledgement precedes release of remaining process extents.
Manager/context retirement and accepted submissions are completed in steps 6–7.
Legacy AGX4 access to private VA is rejected for all resource kinds, so the new
escape does not enable unowned native jobs before the versioned submit step.

RED: real AdmissionDdiEscape returned C00000BB for production acquisition.
Additional RED: missing HardwareAccess incorrectly admitted acquisition.
GREEN: 20 G3 tests, both page profiles, real broker publication, authenticated
handles, stale release, exact payload size, two disjoint full-primary scenes,
third-scene quota failure, varied 1919x1079 geometry and process cleanup.
Standalone pinned WDK ARM64 compile/link: zero warnings/errors, SYS SHA256
`ba31d205c242016d172cf84e3c7896e87d1f4295fa3b0ec11eed0cc3a1e290c4`,
`.local/experiments/R137-offline/step5.sys`. Full-suite final comparison is in
step5-final-full.log; no hardware/package. REVIEW.md unchanged; R136 recorded
dispositions remain applicable.

## Step 6 — AGX4 v3 production scene identity

The Mesa producer now acquires the nine KMD ranges through the real UMD escape
callback and sends manager/scene IDs and generations in AGX4 v3. Those ranges
have no ordinary BO allocation, CPU Lock/map, residency-list entry or UMD list
initialization. Normal command/resource BOs retain their existing path. UMD
release requests are sent after a known retired or rejected batch; uncertain
accepted submissions keep the terminal owner. Kernel submit authenticates the
attached context, exact lease/ranges/geometry and graph; queue acceptance holds
the scene, failed prepare/bind/queue rolls it back. BeginJob checks the lease,
root, mapping generation, geometry and access again. Internal backend image
retains a trusted lease beside its normalized v2 command bytes; image release
is not scene retirement. Context destruction refuses a retained private hold.
Step 7 supplies the OS completion owner which clears that hold.

RED: the real Mesa producer allocated nine ordinary BOs. GREEN: production
prepare + real storage-constructor sanitizer replay uses only the private escape,
preserves firmware-owned manager lists and refuses quota overflow. The combined
G3 test uses actual UMD prepare -> actual KMD escape -> real graph/wire/v5 ->
AGX4 v3 parser -> TA/3D builder -> actual BeginJob/CompleteJob in profiles16/64.
Its ordinary CPU envelope uses logical 4-KiB entries (existing CPU contract),
while its full-primary local target uses 64-KiB entries. Queue hold is retained
after CompleteJob; its temporary fixture teardown is replaced by the actual
notification owner in step 7. The legacy v2 real-broker builder test now supplies
ordinary ranges directly; private producer coverage lives in the combined test.
Outer SubmitCommandVirtual replay rejects stale/foreign leases, geometry drift,
failed image bind and stale BeginJob mapping generation.

Verification: G3 21 tests; G4 replay 7 tests; full suite1136 with exactly the
baseline 15 failures/41 errors/2 skips, no added/removed failure names. Pinned
WDK26100 ARM64 KMD compile/link passes; standalone SYS step6.sys SHA256
`44fa1031cc19f4a31f41483bbf56db619f5ddaa87786f05e7a71eba53c82e44a`.
UMD ClCompile and six affected native Mesa bridge translation units compile for
ARM64; no DLL/package/signing/hardware claim. Logs are step6-*.log under
`.local/experiments/R137-offline`. REVIEW.md unchanged; R136 dispositions stand.


## Step 7 — completion, cancellation and reclamation

The real backend completion owner now requires exact TA+3D before EndJob and
keeps the context's fence outstanding until Windows notification is Reported.
It retains CompletionContext across notification retry even after packet/image
release. PrivateCompletionFence prevents another job from crossing this pending
adapter boundary. Scene queue references clear only after broker EndJob/release
and the reported notification; release remains a request until then. Reclaim
unlinks/revokes exclusive leaves, zeroes complete extents, then returns them to
the pool. Context retirement first closes new acquisitions, drains or refuses
retained scenes, and drops its manager reference; the last reference releases
manager storage. Process destruction releases tables only after GraphDestroy.

Cancellation/preemption/reset owners publish matching cancellation markers at
raised IRQL. PASSIVE reaping returns only proven never-started work. The separate
Submitting reference prevents a cancellation from freeing the pointer still held
by Submit's construction/rollback path; queue publication drops it under the G3
lock. Active or pending private reset has no new quiescence/TLB proof: it fails
closed and quarantines charged storage. Uncertain broker revoke similarly
retains ownership and refuses context/process destruction; there is no software
queue-reset shortcut to reuse and no new TDR-success claim.

Observed RED→GREEN gates:

- Real AdmissionBackendComplete previously ended the broker job after TA alone.
  It now requires both completions. Failed synchronized notify retains scene,
  context and pool charge; a retry after packet/image release completes safely.
- BeginJob previously crossed the notify-retry boundary after EndJob. The
  adapter completion hold now rejects it until the exact Reported event.
- A cancellation could reap a scene while Submit still held its local pointer.
  Submitting now pins it through publication/rollback; the DPC marker itself
  never acquires the G3 fast mutex or reclaims memory.
- The combined replay verifies release-pending data survives failed notify,
  zero-before-return after successful notify, reused-VA/new-generation stale
  token rejection, never-started cancellation, last-context manager release,
  and real-broker uncertain-revoke/active-reset quarantine. Intentional
  quarantine is retained in the host fixture rather than inventing reset proof.

Verification: G3 22 tests and G4 replay 7 tests pass; full suite1137 preserves
exactly the baseline15 failures/41 errors/2 skips. Pinned WDK26100 standalone
ARM64 KMD compile/link has zero warnings/errors. Compile-only step7.sys SHA256
`4839dc5960fb3295c3e4a7e745146dc724ebc48480e388806a2725dda2af1383`.
All 534 tracked driver files match the persistent Windows builder; source hash
manifest `final-r137-source-hashes.json` SHA256
`a85159bf1b54a9dc5968b148f829cdf03c83fbd050031407dd278a69eec5e2bc`.
Evidence is under `.local/experiments/R137-offline/step7-*.log` and
`final-r137-source-verify.log`. REVIEW.md unchanged; R136 dispositions stand.

Independent final whole-branch review of base871fb226 through step6 plus the
final step7 diff found no Critical/Important issues; its five focused replays
passed independently. Review was done before the step7 commit to preserve the
user's one implementation commit per step. It did not independently repeat the
full suite/Windows compile. No minor fixes were deferred. Live VidMm admission,
firmware execution, DWM progress, real concurrent WDDM timing, hardware reset
success and package readiness remain unproven. Existing framework shims model
scheduler/OS boundaries, and the combined replay models queue acceptance while
the separate outer-DDI replay executes its real owner.

## Next hardware checkpoint — EXP855, separately authorized only

No Air, package, install or launch occurred. Mu and m1n1 commit IDs and dirty
hashes are unchanged from the start of R137. Before any candidate, a separate
hash-bound package gate must rebuild/link all current production sources. The
first approved bounded native job must record the 40/16/8-MiB partition and VA
reservation, nine exclusive private ranges in the actual process root, successful
BeginJob, exact TA/3D and Windows fence notification, then unlink/revoke/zero and
safe reuse with a new generation. Ordinary command/attachment checks remain
active. A 40-MiB VidMm segment's DWM capacity/admission and all live timing remain
risks; unknown reset/revoke deliberately consumes quota until a proven recovery.
Recovery remains ordinary EXP377/392 Code28, with GPU-hidden only for emergency
rollback. This offline implementation does not authorize EXP855 or set GO_EXP855.
