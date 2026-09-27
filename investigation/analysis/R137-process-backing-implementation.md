# R137 offline process-backing implementation

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
