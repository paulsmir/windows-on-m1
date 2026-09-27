# R137 offline process-backing implementation

Status: stopped for the retirement-owner scope correction in R136 section 5.
Committed prerequisites 1–3 only; step 4 withdrawn; steps 4–7 remain incomplete.

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
