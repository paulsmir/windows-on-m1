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
