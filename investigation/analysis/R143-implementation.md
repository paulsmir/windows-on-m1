# R143 offline implementation ledger

Plan: investigation/analysis/R143-reserve-1g-design.md; authorized task:
`/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R143_IMPL.md`.
No Air access. No recovery artifact modifications. Source baseline and immutable
artifact hashes: `.local/experiments/EXP856-r143-reserve1g/` in the main repo.
Root baseline c7879b16c552c14bb3a665a540056b5982c7b0d3. Nested baseline commits
and diff hashes exactly match the design. Initial nested dirt includes executable
mode changes; preserve those outside the implementation index.

## Steps

1. m1n1: explicit v2 profile, 1GiB/64KiB selector, identity/full-span/protection;
   legacy profile retained; real-C geometry and publication tests.
2. Mu + shared receipt: v2 identity/size/alignment, PEI allocation exclusions,
   generated ACPI and executable AML tests.
3. KMD: 1000/16/8, separate local/scanout views, upper-offset and lifetime replay;
   DCP stays 56MiB, v5 capacity remains explicit.
4. Preregister offline builds; new firmware/package856 with source/provenance
   hashes; prepare launch/recovery only. Full review and final artifact manifest.

Inspected sources/specs: the design's Asahi mmu.rs/pgtable.rs/t8103.dtsi,
m1n1 selector, power caller, retained backing/translation, stage2 and launch,
Mu validation/MemoryPeim/PrePi, shared ABI and source tests; Microsoft
MmMapIoSpaceEx and DXGK_SEGMENTDESCRIPTOR4 documentation checked online.
UEFI specification fetch denied (403); design's contract retained.
Archived ADT and EXP855E state replace live observation under the explicit
no-Air instruction. Further sources are recorded alongside each step.

Ownership and checkpoint: unchanged from the approved design. m1n1 owns
identity/protection; Mu OS exclusion/ACPI; VidMm residency; KMD borrowing,
partition/copies/lifetime; DCP owns fixed window/latch. No IRQ, DMA grant,
power, UMD placement or recovery changes. Smallest later hardware checkpoint
is exact v2 receipts/1GiB, segment1000MiB and Code0/Start12 in one separately
authorized cold full-owner boot, 600 seconds, then dump-first hidden Code45
cleanup and immutable ordinary Code28. Not executed here.

Pre-flight interfaces: v2 selector receipt -> Mu v2 reservation -> KMD v2
borrow must agree; fixed scanout56 must be independent of local1000/backend1016.
Ruling: user already approved the written design and ordered implementation;
no second design approval is needed. Missing standalone reference means no
standalone build/launch, as required by the design.

Tandem dispositions: all OPEN entries were answered in the task commentary;
retain the design's dispositions, with R64 implementation now authorized and
R86 range grants explicitly deferred by this task. Review snapshot is saved
in the experiment directory. No historical hardware result is reinterpreted.

## Step 1 completed offline

m1n1 commit db98c4e1 (full hash in CHANGES). Explicit build flag
`AGX_LOCAL_RESERVE_V2=1 IOMFB_FULL_OWNER=1`; default remains receiptv1/64MiB.
The selector checks every 4KiB start/end, identity, 64KiB alignment, protected
launch owners, MCC exclusions and ADT carveouts. Region24 is classified as
normal DRAM, with containment validation; other/unknown region owners cannot
overlap. Stage2 checks raw normal/RW attributes before publication.
Legacy and v2 real-C geometry, stage2 permissions, retained exclusions and
assisted post-pt_update publication pass. Full1143: baseline56 names unchanged;
one new ABI test failure was its textual first-define parser choosing inactive
v2. Changed that test to preprocess the actual default profile; affected test
passes. No hidden suite-green claim. Logs: worktree `.local/r143/`.
Review SHA remains 753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5.

## Step 2 verified offline

Mu explicit `J313_AGX_RESERVE_V2_PROFILE=TRUE` selects receipt2/1GiB/64KiB;
default stays64MiB. New profile refuses missing/old/unpublished receipt before
DXE. Identity, PEI arena/PHIT, allocation HOB and FV/FV2/FV3 overlap are checked.
Ruling: publish the real Stack HOB before MemoryPeim instead of extending every
platform's MemoryPeim signature; the same explicit live stack range is then
validated by the allocation-HOB walker. A wrong ordering would defeat overlap
protection; its regression test enforces the order.

Ruling: the pre-existing admission generator referenced a stale G2 digest and
incorrectly required handoff in G2 enumeration. Resolve handoff from the G1
contract authenticated by G2, update the digest, preserve all four admission
resources/IRQs. This repairs the required generation gate without changing G2.
Resource regression and generated-file check pass. Risk: source provenance
must include both authenticated contracts; the tests enforce their geometry.

Mu234 tracked input hashes verified on persistent builder (224 differing files
synchronized; primarily checkout byte differences). CLANGPDB RELEASE build PASS,
actual FV contains the collected AML. acpiexec on that emitted AML passes v2
min8e0000000/max91fffffff/length40000000; old version, unpublished receipt and
PA alias expose four baseline resources only. Firmware/artifact hashes in
EXP856-r143-reserve1g/mu-artifact-sha256.txt. No EBS/live memory map proof.
Full1146 after KMD integration:15 failures/38 errors/2 skips; exact old names
preserved except three generator errors now repaired; no new failure names.

## Step 3 verified offline

KMD now borrows1GiB, reports1000MiB in QuerySegment4/5, keeps private16/backend8
at1000/1016MiB, including fixed backend/shader aliases. Full-local and initial
W=0 scanout56 views are separate; high primary returns INVALID_ADDRESS before
broker I/O. Both descriptors declare reserved DDR, no host aperture. Real-C
borrow checks all262144 translations and rolls back map/interior/final-page
failures; UAT inventory is36 pages with fixed aliases and last native leaf.

Fresh reviewer found a real coverage gap: the old replay reconstructed memory
Start instead of calling it. ACCEPT. Extended replay now executes production
MemoryRuntimeStart/Destroy/Stop plus real memory/residency/UAT/publication,
with only OS allocation/HVC/register services simulated. Nonzero backing proves
full zero-before-publication; deliberately removing production zeroing fails.
Faults after borrow, during table allocation, alias allocation, publication and
post-publication contract setup unwind every table and unmap borrowed RAM once.
Unpublication is asserted before any backing/table release.

This replay exposed a pre-existing teardown defect: fixed aliases remained
registered after local unmap; ContextDestroy returned BUSY. RED→GREEN fix removes
owned aliases before table release and marks local mapping ownership immediately
so later alias failures also unwind. This is in KMD, the violated lifetime owner.
No successful hardware restart is inferred.

v5 retains8192 backing records; real8192+1 test proves CAPACITY and byte-identical
state on refusal. Real scanout service suite covers two-DART unwind/in-flight
release. G3 replay25 PASS (includes16/64 subcases), G4 suite31 PASS,
reserve-focused16 PASS. Final full1146:15 failures/38 errors/2 skips; no new
failure/error names, three repaired admission-generator errors removed from the
baseline56. Complete names: EXP856-r143-reserve1g/host-test-comparison.json.
Review R45/R47: ACCEPT — this offline lifetime evidence now covers alias cleanup
and partial initialization; prior hardware results remain unchanged.

## Step 4 completed: build-only handoff

Package856 from0400dd3209702d502ce0b7e9780d6b95cf1e6db1 passed534 source hashes,
native archive/Resource.cpp provenance and ARM64 UMD/KMD0 warnings0errors.
Mu and m1n1 built; actual emitted AML and fixed profile manifest checked.
New artifacts and all hashes are indexed in investigation/evidence/R143-offline-build.json.
Full-owner/hidden/ordinary scripts were prepared and verify-only tested; bad
version and mixed-artifact hashes reject before serial. No Air access.

Signer is unchanged from EXP855E. Native CAT membership passes for SYS/UMD;
builder chain trust rejects its test root. No certificate stores were changed;
guest signing/trust verification remains a later authorized preparation gate.
Pre-existing nested dirty diffs are byte-identical to the initial snapshots.
No standalone artifact was selected or built; no EBS memory map or hardware
admission is claimed. All R143 code remains in this worktree with step commits;
no merge/push is requested. The user's recorded baseline policy governs the
53 pre-existing test failures/errors, rather than treating this as a green suite.
