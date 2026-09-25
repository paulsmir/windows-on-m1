# GPU current boundary — 2026-09-23

## 2026-09-25 EXP796: R79 FlushTlb passed; VidMm Fill is the next boundary

Package799 G1b16 (R79 plus PASSIVE receipt correction `16eaf6e2`; frozen
R70/R75/R76/R77/R78, caps, signer, m1n1 and Mu) cold-booted once. Host vUART
printed `ARM_CONSUMED version=1 seq=340148747` before AGX power/broker. The
durable `Wom1G3FlushInput` has branch 5, process-owned original root
`0x9b8050000`, graph root `0x9dddf8000`, VA `0x2030000..0x25b0000`,
ResolveStatus0 and BrokerStatus0: R79's inactive-root no-op occurred on
hardware. This input is replayed by test commit `b4128e2b`; real-broker
replay was RED `C000000D` before R79 and GREEN after it (10/10 affected).
The full host suite was run once: 1054 tests, 15 failures, 67 errors,
2 skips from the known baseline. WDK26100 package799 built 0 warnings/errors.

Windows advanced beyond the EXP795 `FlushGpuVaTlb` stop, then bugchecked
`0x10E/0xB`, P3=`C00000BB`, in `dxgmms2!FillAllocationUsingGpuVa ->
FillAllocationInternal -> CommitResource` on a System VidMm worker. Last
`Wom1G3PagingStatus` is `C00000BB`; exact Fill input fields were not captured.
`CreateCddDevice` is still unproven. New dump SHA256
`6f5cad789e074ec4fc41e45d0592cac3ebc8f7f06b27ce78404c6127a310e7ad`,
CDB `ca1a0dd6592114ac835711dadee10fd1dc5c4e73dbd6ddfa1e81efb5d2bdf31f`,
ETL `68850beeaa307f376fc991e6a23e74522bd223d5d139cba0314fe17448169f8a`
in `.local/experiments/EXP796-g3-flush-owned/hardware-evidence`.

R60 same-profile reboot returned pinned SSH; dump, receipts and ETL were
hash-verified before exact oem5/package799 cleanup. Frozen ordinary recovery
now passes pinned SSH/CPU8/storage2/USB7 with one inert APPL0002 Code28,
staged0 and no arm, SYS/UMD/service or signer. Next phase: capture the exact
VidMm Fill BuildPagingBuffer input, replay it RED, then fix only the owning
KMD operation if the input confirms the `STATUS_NOT_SUPPORTED` path. Do not
infer Fill arguments from the stack or change firmware/caps/recovery.

## 2026-09-25 EXP795: R77 host proof works; FlushGpuVaTlb remains the boundary

Package798 (G1b16; R78 range normalization, R77 observational HVC; R70/R75/R76,
caps, signer and Mu frozen) cold-booted once. Host vUART printed
`ARM_CONSUMED version=1 seq=238631669` before AGX power or broker use.
Windows then bugchecked `0x10E/0xB`, P3=`C000000D`, again in
`dxgmms2!CompleteBuildPagingBufferIteration -> FlushGpuVaTlb ->
CommitVirtualAddressRangeSystemCommand`, before CDD. The same-profile R60
reboot returned pinned SSH without rearming. Exact oem5/package798 and dump,
ETL, registry were collected and hash-verified before package cleanup.
The new dump SHA256 is
`70af90a16269fa37ca89b28771ce42859a5c4cc283d806340bc012f4ad4cd2b3`;
CDB `886d6097ea4ad71556918613678e54f156d5fcbcab414c9f18d90a8447a783f5`;
ETL `2b746379be06441767b11e3223299a6b527d52809233b5f16de489862ddd86c4`
in `.local/experiments/EXP795-g3-flush-arm/hardware-evidence`.

`Wom1G3FlushInput` was absent because `AdmissionRecordGpuvaG3Flush` ran
inside `ExAcquireFastMutex` at APC_LEVEL while its registry writer requires
PASSIVE_LEVEL. Host replay reproduced that missing receipt, then passed after
commit `16eaf6e2` moved the write after mutex release. **R78 range cause is
unresolved**: the exact root and VA bounds were not captured. Do not infer
an inclusive End from this run. R77 is hardware-proven; R60 avoided hidden
recovery. The ordinary GPU-visible profile is restored with pinned SSH,
CPU8, storage2/USB7, one inert APPL0002 Code28, staged0 and no arm,
SYS/UMD/service or signer.

Next causal discriminator: one diagnostic package with the PASSIVE receipt
fix, preserving package798 behavior. Capture hProcess, raw/resolved root,
graph root, raw/normalized VA bounds and branch. Replay the **observed** input
before changing root ownership or range policy. Windows VidMm supplies the
flush request; KMD owns normalization and process/root validation; m1n1
owns ASID invalidation and host arm receipt; Mu is unchanged. Smallest
checkpoint is a durable FlushInput receipt followed by the same or advanced
VidMm boundary; on bugcheck use R60 only with durable host arm proof and exact
package, otherwise GPU-hidden dump-first exact cleanup.

## 2026-09-25 EXP794: system leaf passed; FlushGpuVaTlb is next

Package797 (R76, G1b16; R70/R75 and firmware/caps/signer frozen) cold-booted
once. R76 keeps VidMm's logical 4-KiB system PTEs but publishes a native
leaf only for an aligned, contiguous group in the KMD-owned local reserve;
`GraphUpdateLeaf` registers that process/generation's backing before broker
`UPDATE_LEAF`. No aperture/system backing grant exists in this path. Real-broker
replay was RED `C0000483` for a contiguous unregistered system group, then
GREEN with no broker call; scattered groups and local publishing also pass.
WDK26100 package797 built with 0 warnings/errors. Affected 25 host tests pass;
the full 1052-test suite had 15 failures/67 errors/2 skips, including one
source-layout assertion repaired and retested.

Hardware advanced past EXP793's level0 system leaf: durable
`Wom1G3UnpublishedGroups[0]=22`, no new leaf failure receipt. It then
bugchecked `0x10E/0xB`, P3=`C000000D`, in
`dxgmms2!CompleteBuildPagingBufferIteration -> FlushGpuVaTlb ->
CommitVirtualAddressRangeSystemCommand`; CDD was not reached. Last receipted
update was level2 Start0 Count8/status not the failing FlushTlb input.
New dump SHA256 `33b172accc8ba9616ab5ca20b555411fc2ad5375ba028f53244bd8f0f8a425bf`,
CDB SHA256 `b8d35a39ff83b5c1412c3d6e726b7a404201eda97060f5de921d33f9806f1629`,
ETL SHA256 `a0937c4d45588809afa7a9b8750ad093e310dea45929e11c635bfea12193d0e9`
in `.local/experiments/EXP794-g3-unpublished-system/hardware-evidence`.
Arm consumption was not durable at SSH loss, so GPU-hidden dump-first captured
hash-verified evidence and removed exact oem5/package797 before frozen
ordinary recovery. Pinned SSH/CPU8/storage2/USB7, one inert APPL0002 Code28,
staged0 SYS/UMD/service/arm0 are restored.

Next causal discriminator: receipt the exact FlushTlb process/root address,
resolved root IPA, graph root IPA, VA bounds and graph/broker result, then
replay that observed input. Do not revisit leaf geometry or CDD allocation
until FlushTlb succeeds. R71 remains deferred: there is no GPU access to an
unpublished non-DMA system page, and the 16-KiB path rejects system
`Use64KBPages`; do not advertise unsupported SysMem64KB caps.

## 2026-09-25 EXP793: virtual DMA aperture exposed an earlier paging OWNERSHIP stop

Package796 changed only virtual `CreateContext.DmaBufferSegmentSet` from 0 to
aperture segment1 mask1; G1b16, R70, firmware, caps, signer and recovery were
frozen. Pinned WDK26100 and Microsoft Learn require an aperture segment in this
set; local memory segment2 is disallowed. Host real-broker replay was RED→GREEN
and all 10 targeted tests passed; the full 1052-test host suite retained 14
failures/67 errors/2 skips outside this contract. GDI flags6 receipt confirms
DMA `0x50000`, mask1, private `0x51000`, lists256/256. Private bytes are a
separate nonpaged allocation, so their size exceeding DMA bytes is permitted;
Caps and PagingCompanionNodeId remain zero.

One stage→cold full-owner launch reached StartDevice Stage12/status0, CPU8,
5000Hz and G3 retained op624/status0, then bugchecked `0x10E/0xB` with
KMD paging `C0000483`. The flushed failure is level0 branch7 LeafGraph
index`0x5c`, valid read-only system PTE flags`0x9`, page number`0x851000`
(IPA`0x851000000`), GraphLastStatus4 OWNERSHIP, TableIpa`0x97f7c4000`,
BrokerTableIpa`0x9d6a58000`. Last input was Start16 Count80 Flags2,
UpdateMode2 (`GPU_PHYSICAL`). CDB places the `0x10E` in
`dxgmms2!CompleteBuildPagingBufferIteration -> UpdatePageTable ->
CommitVirtualAddressRangeSystemCommand`, before CreateCddDevice. This run
cannot confirm or reject R75 as the cause of EXP792's CDD fault.

Evidence `.local/experiments/EXP793-g3-context-dma-aperture/hardware-evidence`:
new dump SHA256 `d035812fb6bda4d2f534bde2987e93c526e0e41727fe0cb7e9a80cf6a40dd26b`,
ETL SHA256 `0fa6f7e2ffa3aef1eed5e462580f0a86d2921197f495aba4362993b14f81e35e`,
state SHA256 `719eb0cfe62cd2f1fac8e57e923391bf675e6026daa4df93a6b2c5f61cde9916`.
No durable arm consumption before SSH loss, so GPU-hidden dump-first collected
and verified the evidence, removed exact package796/oem5, then frozen ordinary
recovery restored pinned SSH/CPU8/storage2/USB7, one inert APPL0002 Code28,
staged0 SYS/UMD/service/arm0.

Next smallest discriminator: one diagnostic-only early broker-call receipt for
the failing LeafGraph operation, distinguishing `REGISTER_BACKING` from
`UPDATE_LEAF` and recording request IPA, generation, returned status and
callback phase. Do not run the EXP792 allocation-wrapper receipt until paging
again reaches CDD. No G4 merge plan yet because `CreateCddDevice` has not
passed.

## 2026-09-25 EXP792: same CDD DMA-pool fault; allocation DDI attribution unresolved

Diagnostic-only package795, G1b16 with R70 unchanged, was staged from ordinary
Code28 and cold-booted once. G3 paging again returned status0. Durable GDI
`CreateContext` receipt recorded flags6 (GdiContext+VirtualAddressing), node0,
engine1, DMA size `0x50000`, `DmaBufferSegmentSet=0`, private bytes `0x51000`,
allocation/patch lists 256/256. [Microsoft DXGK_CONTEXTINFO](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_contextinfo)
permits segment set 0 (contiguous locked memory) and requires GDI allocation
list 256; no offline proof supports changing these values. REVIEW R74: DEFER —
no named context-info violation; use hardware evidence to choose the next field.

Windows again stopped `0x3B/C0000005` in
`dxgmms2!AddDmaBufferToPool+0x304 -> VidMmInitDmaPool ->
DXGCONTEXT::Initialize -> CreateCddDevice` (csrss). New dump SHA256
`1f9187aa056e62df99e54b346aac95e86a8c3cdff078e52407a878af7e5d6103`;
CDB SHA256 `580432802376066a4e8d8ee1ac6ce33c22553082600a8ebc9a7384384ed20142`.
No `Wom1G3DmaOp*` registry receipt survived. ETL has generic
`DdiCreate/OpenAllocation` events after the GDI receipt, but does not identify
whether they belong to AppleAgx; exact package PDB confirms the diagnostic
calls are compiled into its wrappers. The minidump omits the diagnostic
globals. **Verdict: inconclusive on KMD output handles; G3 admission remains
unproven.** Evidence `.local/experiments/EXP792-g3-dma-receipt/hardware-evidence`
(`state.json` SHA256 `ed667b62f264300f30137d012835e0b810497e93d8c5cafe7f65fdbf182b7380`,
ETL SHA256 `0f120ae1588362a02e6397c8df2ade652f31b34462952474bb2fac0720529a02`).
Exact package795/arm0 cleanup restored pinned SSH/CPU8, one inert APPL0002
Code28, staged0 SYS/UMD0 arm0.

Next smallest discriminator: prove whether AppleAgx `Create/OpenAllocation`
wrappers execute at all in the CDD interval, independently of the GDI arm and
registry-write guard. Capture a bounded early-call receipt with callback IRQL,
registry-open status and timestamp, then correlate with the GDI receipt. Do
not change `DmaBufferSegmentSet`, page size, firmware, or mappings from this
inconclusive run. Start a new thread for that phase.

## 2026-09-25 EXP791: 16 KiB is the main G3 profile; CDD DMA pool is next

Package794 corrected only the EXP790 16 KiB G3 contract self-veto. One staged
then cold full-owner boot passed DRIVERCAPS, Q13/Q14, G3 process/context and
level0 4 KiB Count1 paging (last Start33/status0). It then reached the **same**
`0x3B/C0000005` in `dxgmms2!AddDmaBufferToPool+0x304` during
`VidMmInitDmaPool -> DXGCONTEXT::Initialize -> CreateCddDevice` as EXP789's
64 KiB profile. This satisfies R72's same-boundary rule: **G1b 16 KiB is the
main profile**, superseding EXP790's provisional 64 KiB decision. It proves
VidMm progressed with the 16 KiB/GpuMmu declaration, not G3 admission. New
dump `.local/experiments/EXP791-g3-16k-validator/hardware-evidence/
092526-14968-01.dmp` SHA256
`4b35aca88816362c0dc2ffab63cacc2b16eb5b97013ab6f3cf09cd52b3bff5a8`;
CDB SHA256 `9c5b5bade38a1a30042bd089a0af10b1f45c5168d4f4cc1887b5e0b1fe714b7b`.
GPU-hidden dump-first confirmed exact package794/arm0; exact cleanup and frozen
ordinary recovery restored pinned SSH/CPU8 and one inert APPL0002 Code28,
staged0 SYS/UMD0 arm0. Do not rerun page-size comparison.

R71 decision: **defer both `SysMem64KBPageSupported` and
`OpportunisticSysMem64KBPageSupported`**. In the selected 16 KiB profile,
`gpuva_g3_paging_windows.c` rejects `Use64KBPages`, the G3 caps declare no
64 KiB leaf table, and allocation hints are 16 KiB. Advertising 64 KiB system
pages would claim translation the current driver does not implement. Microsoft
documents 64 KiB leaf-table/`Use64KBPages` conversion and notes that system
memory uses 4 KiB GPUVA granularity; see
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages
and https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-address.
R70's logical 4 KiB shadow remains required. `BuildPagingBuffer` encodes
Transfer/Fill for CPU execution in `AdmissionMemoryRuntimeExecutePaging`;
the paging process does not need AGX access to incomplete logical groups.

Next causal target: the common CDD privileged DMA-pool access violation. In
both dumps `dxgmms2!AddDmaBufferToPool+0x304` reads `[x8+8]` with `x8=0`;
`x8` came from its new pool object's field `+0x38`. Disassembly shows this
field is the output of `VIDMM_GLOBAL::OpenOneAllocation`, which returned
success, and the ordinary success path writes a nonnull `VIDMM_ALLOC` there.
The dump cannot show whether a later OS call cleared it. ETL has no nearby
DDI failure, but does not capture KMD output handles. One diagnostic package
should receipt `CreateContext` DMA settings and `Create/OpenAllocation`
status and nonnull outputs, with no mapping or caps change. If those are
correct, investigate the OS-private DMA-pool path; do not guess a KMD fix.
Keep 16 KiB and all firmware/caps/recovery frozen.

## 2026-09-25 EXP790: 16-KiB probe stopped by our DRIVERCAPS self-veto

Package793 changed only G1b 64→16 behavior (plus package version metadata)
from EXP789. Cold full-owner boot reached StartDevice Stage12/status0, then
APPL0002 Code43 with pinned SSH/CPU8 alive. No G3 paging input was recorded.
DxgKrnl ETL names `DdiQueryAdapterInfo(DXGKQAITYPE_DRIVERCAPS)` returning
`C0000184` from the miniport, followed by `StartAdapter_AddAdapterFailed`.
Current `AppleAgxGpuvaG3AdmissionContractValid` unconditionally requires
`LocalUse64KBPages` and nonzero `Leaf64KBytes` even when the selected G1b
profile is 16 KiB. This is a KMD self-veto before VidMm evaluates the segment;
EXP790 therefore cannot answer whether VidMm accepts 16 KiB with GpuMmu.
Evidence `.local/experiments/EXP790-g3-g1b16/hardware-evidence/EXP790-trace.csv`
SHA256 `2df93942a3463e34c52381fcc6bf924073ca41871afa0e65765aa110896a4f38`.

R72 p.4 decision: retain 64 KiB as the provisional main profile. Add a
regression for the 16 KiB admission contract, remove only this self-veto, and
run one new cold 16 KiB discriminator with R70 and all other layers frozen.
Select 16 KiB only if it reaches at least EXP789's CDD DMA-pool boundary;
select 64 KiB plus R71 evaluation only after a named dxgkrnl rejection of
the corrected 16 KiB declaration. Do not interpret EXP790 Code43 as such a
rejection. R71 remains unadvertised.

## 2026-09-25 EXP789: R70 clears Count1 guard; new 0x3B after paging success

Package792 (R70, G1b 64 KiB) was staged from ordinary Code28 and cold-booted
once under the frozen full-owner profile. StartDevice reached Stage12/status0.
The last durable G3 input is level0 Start65 Count1 VA `0x2041000` with paging
status0 and no paging-failure receipt; the EXP788 Start64 Count1 `C000000D`
boundary was passed. Windows then stopped `0x3B` with `C0000005` and
instruction address `0xffffbf8e1c68e268`. CDB places the fault in
`dxgmms2!AddDmaBufferToPool+0x304` during
`VidMmInitDmaPool -> DXGCONTEXT::Initialize -> CreateCddDevice` in csrss
`SetDisplayConfig`. The reason for the access violation remains unproven;
this is the next lifecycle boundary after successful Count1 paging.
Dump `.local/experiments/EXP789-g3-logical-shadow/hardware-evidence/
092526-9031-01.dmp` SHA256
`ffa58ebc2e598c378dc43dcf28f4941465f8346a22ff78ea3f16b2b61ff1f8a5`.
GPU-hidden dump-first collected hash-verified dump/ETL/receipts and confirmed
exact package792 and cleared arm; exact cleanup restored frozen ordinary
GPU-visible SSH/CPU8, one inert APPL0002 Code28, staged0 and no SYS/UMD/arm.
G3 admission is not yet proven. R72 next: one cold G1b 16 KiB discriminator
against this 64 KiB package with only page profile changed; decide the main
profile from the hardware boundary, not from EXP772/773's PASID-confounded
AddAdapter result. R71 SysMem64KBPageSupported remains unadvertised pending
translation and WDK contract proof.

## 2026-09-25 EXP788: Count1 is a valid read-only system 4-KiB page

Diagnostic-only package791 cold-booted with the frozen full-owner profile.
StartDevice reached Stage12/status0, then Windows stopped `0x10E/0xB` with
KMD `C000000D` in `CompleteBuildPagingBufferIteration`. The new flushed
branch10 receipt proves level0 CPU_VIRTUAL Start64 Count1 Flags0,
FirstPteVA `0x2040000`, **PTE Flags `0x9` (valid, read-only, segment0),
PageAddress `0x9916a0`** (byte address `0x9916a0000`). This is a single
system-memory mapping, not an unmap. The KMD guard rejects it before graph
work. The real broker accepts only complete 16-KiB leaf groups, so a single
4-KiB mapping cannot be published directly. Next offline target: a logical
4-KiB PTE shadow with native leaves published only for complete contiguous
16-KiB groups, plus fail-closed GPU access to incomplete groups; add actual
Count1 input and completion/invalidation cases to real-broker replay. Do not
repeat package791 unchanged. Analysis `.local/experiments/EXP788-g3-count1-
receipt/hardware-evidence/EXP788-analysis.json` SHA256
`960ef6979f191e63023ebcbba2e245bd0934b8c38730f08b7f240dec1aa1efe9`.

GPU-hidden dump-first returned pinned SSH/CPU8, APPL0002 absent, arm absent,
exact package791 oem5.inf/SYS/UMD; dump/ETL/receipts were collected and
hash-verified before cleanup. R69 stage → cold boot was followed. Exact
package791 cleanup and frozen ordinary EXP377/392 recovered pinned SSH/CPU8,
storage2/USB7, one inert APPL0002 Code28, staged0, SYS/UMD/service/signer0,
arm0.

## 2026-09-24 EXP787: level0 4-KiB Count1 update refused before PTE inspection

Package790 used distinct KMD-owned broker pages for VidMm tables. Real-broker
replay of EXP786's self-table backing passed, and hardware advanced beyond
that leaf. Live bind reached StartDevice Stage12/status0 and then Windows
`0x10E/0xB`, KMD `C000000D` in `CompleteBuildPagingBufferIteration`.
Last paging input: CPU_VIRTUAL level0 **Start64 Count1 Flags0**,
FirstPteVA `0x2040000`, DMA/private pointers present. The only matching
early guard in current KMD rejects a level0 4-KiB update unless both start
and count are multiples of four; no failure receipt or raw PTE was captured.
The new real-broker replay Count1 geometry with an explicitly synthetic
invalid PTE is RED at `C000000D`. The broker currently permits only complete
16-KiB leaf groups (valid mask 0 or 15), so do not assume arbitrary 4-KiB
mapping can be represented. Next offline discriminator: capture the actual
Count1 PTE before the guard, then distinguish a no-op/unmap from a valid
partial mapping before changing behavior. No unchanged package790 rerun.

GPU-hidden dump-first recovered pinned SSH/CPU8, APPL0002 absent, arm absent,
exact package790 oem5.inf/SYS/UMD; dump/ETL/receipts were collected and
hash-verified before cleanup. Analysis `.local/experiments/EXP787-g3-table-
shadow/hardware-evidence/EXP787-analysis.json` SHA256
`d45f34bdfce26771371a5023b98c10c5a3cd2c5299303f6d004f3b77d75eea53`.
R69: use stage → cold Windows boot for subsequent G3 candidates; no live
bind until the separate R64 memory-owner work. Exact package790 cleanup
followed by frozen ordinary EXP377/392 recovered pinned SSH/CPU8,
storage2/USB7, one inert APPL0002 Code28, staged0, SYS/UMD/service/signer0,
arm0.

## 2026-09-24 EXP786: first leaf maps the same physical page as its table

Package789 (R67 KMD zeroing and receipt) passed EXP785's level0 TableGraph
OWNERSHIP boundary. The next flushed failure is CPU_VIRTUAL level0 Start0
Count32 Flags0 VA `0x2000000`, branch7 LeafGraph index0: PTE flags `0x41`,
`PageAddress=0xC`, resolved backing IPA `0x9bbffc000`, **equal to the
registered table IPA**. The real m1n1 broker rejects a backing whose PA is
already a table (OWNERSHIP4); KMD returned `C0000483`, Windows stopped
`0x10E/0xB` in `CompleteBuildPagingBufferIteration`. This proves a table/
backing identity conflict, not a leaf-size or table-content failure. The first
nonzero index in the last receipt is MAXULONG and TableAddBranch3 means the
table was already in the graph. Only first PTE was receipted; other Count32
PTEs remain unknown. Analysis `.local/experiments/EXP786-g3-table-zero/
hardware-evidence/EXP786-analysis.json` SHA256
`8235986eea6b8db009201ba0caa517ddf3215082d510d1ddd1126deb4751cff1`.

R60 same-profile disarmed reboot returned pinned J313-WIN SSH/CPU8, exact
package789 oem5.inf Code43, staged1, arm absent; dump/ETL/receipts were
collected before any package change. Host replay with the exact first PTE and
synthetic adjacent PTEs is RED at C0000483 against the real broker. Next
causal target: distinct broker-owned table storage while preserving VidMm's
original page as a writable backing; do not weaken broker table/backing
ownership or silently omit the Windows mapping. R64 persistent 64-MiB
firmware reservation remains a separate memory-owner change and does not by
itself remove this same-PA conflict.

## 2026-09-24 EXP785: level-0 TableGraph broker OWNERSHIP before leaf PTEs

Package788 added only the flushed leaf-failure receipt to package787's source;
WDK26100 incremental build was clean. First full-owner boot left exact package
staged/unbound. A live bind reached StartDevice Stage12/status0, then Windows
bugchecked `0x10E/0xB` with KMD `C0000141` in
`CompleteBuildPagingBufferIteration`. The first failure receipt is **branch2
TableGraph**, not leaf: CPU_VIRTUAL level0 Start0 Count8192 Flags3
(`Repeat|InitialUpdate`), FirstPteVA `0x2000000`, table IPA `0x90f1fc000`,
broker LastStatus4 `OWNERSHIP`, GraphUncertain0. The captured launch contract
places this IPA in guest RAM, but no broker subreason or table contents were
recorded. EXP784 Count32 leaf failure was not reproduced. Do not assume a
specific ownership check failed. Next offline discriminator: receipt of the
first nonzero qword/index in the resolved 16-KiB table immediately before
RegisterTable; if all zero, inspect stage-2 translation/alias checks. No
unchanged package788 rerun. Analysis `.local/experiments/EXP785-g3-leaf-receipt/
hardware-evidence/EXP785-analysis.json` SHA256
`403c643a65c72a88ae6aff8366d965a66a92d76b3464439090b2e4b2b3f1dfd1`.

R54/R60: SSH was lost at bugcheck without pre-reboot proof of arm consumption,
so GPU-hidden dump-first collected the exact package788 dump/ETL/receipts and
removed `oem5.inf`/signer. Frozen ordinary EXP377/392 is again pinned SSH,
CPU8/storage2/USB7, one inert APPL0002 Code28, staged0 SYS/UMD/service0,
arm0. R64 firmware reserve remains deferred until this paging boundary is
resolved; R65 G4 contracts remain after admission.

## 2026-09-24 R66 real-broker offline verdict

Host replay now compiles the actual m1n1 v5 broker, MMIO wire, platform
`gpuva_execute` dispatch, UAT encoder and retained guest-RAM eligibility with
an identity stage-2/reserve model. The EXP784 Count32 / VA `0x2000000`
projection passes through broker; its 32 PTE values remain synthetic, so the
hardware `C0000483` cause is not determined. A real-broker RED exposed
`REVOKE_TABLE` using `AuxIpa` although dispatch reads `TableIpa`; graph/client
field correction is GREEN. Next G3 step is one diagnostic-only package788
with the existing flushed leaf branch/index/PTE/IPA/graph-status receipt,
then EXP785 under R54/R60. R64 memory reservation stays after that verdict;
R65 G4 KMD contract stays after G3 admission.

## 2026-09-24 EXP784C advances to level-0 leaf GraphUpdateLeaf failure

Package787 (mixed level1 parent PTE 4K/64K child sizes independent of update
flag) cold-booted past EXP783's `ParentFlags` index2 rejection. The next
flushed input is `BuildPagingBuffer(UpdatePageTable)`, CPU_VIRTUAL mode0,
**level0 Start0 Count32 Flags0**, FirstPteVA `0x2000000`, both DMA pointers.
KMD returned `C0000483` (`STATUS_DEVICE_HARDWARE_ERROR`); Windows bugchecked
`0x10E/0xB` in `CompleteBuildPagingBufferIteration`. This status comes from
`AdmissionG3UpdateLeaf` when `AppleAgxGpuvaG3GraphUpdateLeaf` returns false.
No leaf first-failure receipt exists in package787, so local graph check and
m1n1 broker response remain open. The old host replay mocked graph too
broadly; current tree compiles real graph/client with broker I/O mock and
records Count32 geometry with explicitly synthetic PTEs. Next offline gate:
flushed leaf branch/index/raw PTE/IPA/graph status receipt, RED→GREEN, then
one diagnostic-only package. No package787 unchanged rerun.

R60 same-profile disarmed recovery returned pinned SSH/CPU8, exact package787
`oem5.inf`/Code43 and absent G3Armed. Package remains known/staged in the
full-owner guest; AutoLogger stopped/removed. Dump SHA256
`fa6f41290add4ebd501b45192dbdcd3cda2e9f4cd469572be2745ece41d7de12`;
ETL `b8b56ea73b40662e0c2d1486e44b493d5ce951ed56398e015b6c670ef76f9556`;
analysis `.local/experiments/EXP784-g3-parent-size/hardware-evidence/EXP784-analysis.json`
SHA256 `68cc8e072a4a5c369165ef4843a2e529382403a627e3da74bd3c2dc7419b9e0e`.
EXP784B live bind also confirmed StartDevice Stage4 `C000009A` while requesting
67,174,400 bytes; the same package passed allocation on cold boot. R63
firmware reservation remains design only.

## 2026-09-24 R62 offline gate after EXP783

Commit `941e644f` adds a real-C host replay of the recorded EXP776–783 G3 DDI
sequence and the documented leaf/root/flush/ordinary-process continuation.
Historical PASID, context flags, Repeat/DMA, PTE page-number and EXP783
parent-size guards fail in the replay; the current tree passes the affected
15-test gate. The EXP783 `ParentFlags` check now treats a level1 parent's
per-PTE 4K/64K child size independently of the update-wide leaf flag, while
level2 still rejects that bit. This is **offline only**: package787 has not
been built or launched, and VidMm progress beyond index2 remains unproven.
Next checkpoint is one hash-verified package under the frozen EXP783
firmware/caps/signing profile, after exact-package and R60 preflight. R63
firmware-owned local-memory reservation is designed separately in
`docs/superpowers/specs/2026-09-24-g3-firmware-local-memory-reservation.md`;
no memory owner has changed.

## 2026-09-24 EXP783 advances past EXP782 child address; 64K parent PTE refused

Package786 converted VidMm PTE page numbers to byte offsets with a checked
shift. One full-owner boot passed the EXP782 level1 index1 `ChildAddress`
boundary and logged 624 successful retained GPU operations. The first rejected
entry is now **level1 index2**, branch3 `ParentFlags`: raw PTE flags `0x20041`
(valid, segment2, `PageTablePageSize=1` / 64K), raw child page number `0x2C`
(byte offset `0x2C000`). The UpdatePageTable input has `Flags=0`, including
`Use64KBPages=0`. Current code incorrectly requires PTE page-size bit17 to
match the update-wide `Use64KBPages` flag and returned `C00000BB`, causing
Windows `0x10E/0xB` in `InitPagingProcessVaSpace`. This confirms an R61 causal
advance; full graphics desktop remains unproven. Next offline target: derive
parent `PageTablePageSize` validation independently of the update's leaf-page
flag from pinned WDK/observed geometry, then RED→GREEN before package787.
Do not repeat package786 unchanged.

R60 recovery worked: durable host GPU operations plus `StartDevice`'s flushed
one-shot arm consumption proved disarm before GPU access. One bounded reboot of
the identical full-owner profile without rearm returned pinned SSH/CPU8,
`G3Armed` absent, exact package786 `oem5.inf`/Code43. WER dump, PTE receipts,
ETL and CDB were collected before package changes. Package786 remains known
and staged in the reachable full-owner guest for the next hash-verified series
candidate. Analysis `.local/experiments/EXP783-g3-pte-units/hardware-evidence/EXP783-analysis.json`
SHA256 `79f71d02ae3b0e62768b27ea4ec47cf19039b7f43979d88573d2fbf0e2f40b53`; dump SHA256 `2d8fefc4fa2f91721eccd108866fd4a46b2f9e453e92e47c415070932bca0b49`;
ETL SHA256 `a7a3ffb48aed13dfa4b24d566fd58d33f7e7b636afa6f9044fa6cd5b9ca5c719`.

## 2026-09-24 EXP782 identifies first rejected level-1 child PTE

Package785 corrected the false 64K-leaf declaration from 8192 to 16384
bytes and added a flushed first-failure receipt. One full-owner G3 boot still
bugchecked `0x10E/0xB`, KMD `C0000141`, during VidMm paging-process VA
initialization. The first rejected entry is **level1 index1**, flags `0x41`
(valid, segment2), `PageTablePageSize=0` (4K), raw `PageTableAddress=0xC`.
Branch4 is `ChildAddress`: `AdmissionGpuvaG3ResolveTable` rejects the PTE
before graph registration or broker execution. Current table resolved to IPA
`0x9bbff4000`; graph status/uncertainty remain zero. The 8KB 64K-leaf cap
was a real declaration defect, but R59 is **rejected as the cause of EXP782**:
VidMm selected a 4K child here. Do not repeat package785.

WER minidump SHA256 `f230526463fe832fb3c89052ab0b6d237a1b7a4d8458192894971330229b280a`;
CDB confirms `CompleteBuildPagingBufferIteration -> UpdatePageTable ->
CommitVirtualAddressRange -> InitPagingProcessVaSpace`. Decoded receipt and
recovery are in `.local/experiments/EXP782-g3-level1/hardware-evidence/EXP782-analysis.json`
SHA256 `665646a8fcc1bde351e286bb914a8ddf5f8c4b032d19537cd3baa9fc246aea7d`.
R54 bugcheck recovery collected dump/ETL first, deleted exact `oem5.inf`
package785 and signer, then frozen ordinary EXP377/392 returned pinned SSH,
CPU8, one inert APPL0002 Code28, no AppleAgx residue, storage2/USB7.

Next causal target is **offline**: reconcile raw `PageTableAddress=0xC` with
pinned WDK/official `DXGK_PTE` address semantics (high 52 bits, low 12 zero),
VidMm allocation geometry, and the first two PTEs. Do not guess units or
shift the value without primary evidence. R57.2 stays fail-closed: only
success, genuine busy allocation, and genuine insufficient DMA buffer are
documented retry/status choices; internal refusal leaves flushed receipts.

## 2026-09-24 EXP781 reaches level-1 page-table update

Package784 (R58 WDK input audit plus EXP780 CPU_VIRTUAL DMA-pointer fix) booted
StartDevice Stage12/status0 and passed the earlier level-0 metadata veto.
The last flushed KMD receipt is `BuildPagingBuffer(UpdatePageTable)` operation11,
CPU_VIRTUAL mode0, **level1**, Start0/Count2048, Flags2 (`InitialUpdate`),
PTE present, dual PTE absent, non-NULL DMA/private pointers of 4096 bytes,
DriverProtection0 and `Wom1G3PagingStatus=C0000141` (`INVALID_ADDRESS`).
Windows bugchecked `0x10E/0xB` in
`dxgmms2!VIDMM_GLOBAL::CompleteBuildPagingBufferIteration` while committing
the paging-process VA range. The named KMD return is now the causal boundary;
AddAdapter, rendering and DWM admission remain unproven. WER dump SHA256
`078b1dc41e0338eb1b9a22bb79f55e32961740da9a91be53122cf2db88582948`;
decoded receipt `.local/experiments/EXP781-g3-input-audit/hardware-evidence/EXP781-analysis.json`
SHA256 `9cb2d4088e4d0b39999bc981fb722baf381e8eb5793c00b3338fb15f09da1199`.
The exact internal INVALID_ADDRESS branch is not yet observed: possible table
address/child-PTE range or alignment validation, or graph table registration/
parent link failure. Next offline target is a durable per-branch receipt of
level1 table address, first failing PTE index/flags/address and graph step;
derive a correction only after that discrimination. Do not rerun package784.

SSH was lost on the bugcheck, ending R54 series. GPU-hidden recovered the
dump/ETL/receipts, deleted exact package784 `oem5.inf`, then frozen ordinary
EXP377/392 returned pinned SSH, CPU8 and one inert APPL0002 Code28 with no
AppleAgx residue; final baseline SHA256
`6b2a3410d0494dd2f11b3eaf09e983bd0127c99c97a48e83d58ef18c11a7767d`.
R55 repeated StartDevice path is still untested on hardware.

## 2026-09-24 EXP780 identifies exact paging bootstrap veto

Package782 booted StartDevice Stage12/status0, then bugchecked `0x10E/0xB`
with KMD `C000000D` on the first UpdatePageTable. The durable 88-byte
`Wom1G3PagingInput` receipt records operation11, IRQL0, CPU_VIRTUAL mode0,
level0, Start0/Count8192, Flags3 (`Repeat|InitialUpdate`), one PTE pointer,
no 64K dual PTE, system process, and **non-NULL DMA and private-data pointers**
(4096 bytes each). `Wom1G3PagingStatus` is `C000000D`. In
`gpuva_g3_paging_windows.c` the remaining explicit guard rejects CPU_VIRTUAL
when either pointer is non-NULL, before resolving/updating a table. Microsoft
Learn describes the usual paging-process CPU_VIRTUAL request with NULL DMA
pointer, but this observed Windows 26100 request carries both pointers; the
driver must perform the immediate CPU update without writing into them. This
is the next single correction, with host RED→GREEN; package782 must not be
rerun unchanged. No successful paging update or rendering is proven yet.

R54 series ended on bugcheck. GPU-hidden exact package782 cleanup followed by
frozen ordinary EXP377/392 returned pinned SSH/CPU8 and one inert APPL0002
Code28, no AppleAgx residue; baseline SHA256
`cad4578c969025fcbc3a32aaa391efcb5244290a8af3e4b55e170acf68f175dd`.

## 2026-09-24 EXP779 reaches first BuildPagingBuffer and bugchecks

Package780's truthful CPU-visible local segment advanced VidMm beyond the
`GetCpuVisibleAddress` 0x10E/0x49 boundary. StartDevice Stage12/status0,
system CreateProcess and paging context flags5 were observed. Windows then
bugchecked `0x10E` subtype `0xB`, parameter3 `0xC000000D`:
`dxgmms2!VIDMM_GLOBAL::CompleteBuildPagingBufferIteration` received an
invalid error code from our `DxgkDdiBuildPagingBuffer` while
`UpdatePageTableInvalidate` initialized the paging process VA space. Dump
SHA256 `fcb73c05c2ffcc0a71b1c08a7f1f5f1fecc8faa3069d3a8a9101727f23677892`.
This is the first proven paging DDI boundary; no successful UpdatePageTable,
FlushTlb, SetRootPageTable, rendering, or DWM admission yet.

Microsoft's `DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE` contract says paging
process initialization forces CPU_VIRTUAL mode with no DMA buffer and immediate
updates. Its `Repeat` flag may provide one PTE to replicate. Local G3 code
accepts the mode in its outer guard but resolves CpuVirtual only if it lies in
the KMD's separate CPU mapping, and rejects Repeat. The actual flags/first
failing guard were not recorded. Next offline target is a documented bootstrap
path with first-input/guard receipt and host RED→GREEN; no caps guess or repeat
of package780. R54 ended on bugcheck. GPU-hidden exact cleanup then frozen
ordinary EXP377/392 recovered pinned SSH/CPU8 and one inert APPL0002 Code28,
no AppleAgx residue; baseline SHA256
`38e0f964cea884e17286e172838f617f70828936700e17958d25295255bbe68e`.

## 2026-09-24 EXP778 reaches VidMm page-table bootstrap and bugchecks

Package779 admitted the paging SystemContext. Persisted `Wom1G3ContextInput`
is `(v1, 32, flags5, node0, affinity1, private0, runtime handle0, IRQL0)`:
the exact WDK `SystemContext|VirtualAddressing` combination. StartDevice was
Stage12/status0, then Windows bugchecked `0x10E` subtype `0x49` during AddAdapter.
WER minidump SHA256
`cc6e68c34a48b3eeb07bebc56eca5385f54877f22d295eb9459de5f19ebf0e01`
symbolizes to `dxgmms2!VIDMM_PAGE_TABLE_BASE::GetCpuVisibleAddress` →
`GetDriverUpdateAddress` → `UpdatePageTableInvalidate` →
`InitPagingProcessVaSpace`. The page-table allocation is in local segment2;
both QUERYSEGMENT4/5 currently report it with CpuVisible0 and
CpuTranslatedAddress0, and no CPU host aperture. This is the next causal
boundary. The local allocation is a contiguous Windows physical memory object
with a mapped CPU address and ADL GuestIpaBase; determine offline whether its
GPU-segment range can truthfully publish `CpuVisible` with that guest IPA as
`CpuTranslatedAddress`. Do not advertise it without checking alignment, size,
and ownership. No UpdatePageTable DDI or rendering is proven yet.

SSH was lost on bugcheck, so R54 series ended. Immutable GPU-hidden recovery
collected dump and receipts, deleted exact package779 and signer, then frozen
ordinary EXP377/392 returned pinned SSH and one inert APPL0002 Code28 with no
AppleAgx package/service/files/module/certificate. Baseline SHA256
`4f86c7732006dec99de0bfbc3841c7d35173ba1096d46e0b1acd8de612b09d35`.
R55 repeated StartDevice route remains untested on hardware.

## 2026-09-24 EXP777B reaches paging-context creation

Package778 removed the `NumPasid != 0` CreateProcess veto. EXP777's first cold
boot remained unbound after stage-only package replacement; one live bind
reached StartDevice Stage4 but failed the 64-MB physical allocation with
`STATUS_NO_MEMORY`. A second cold boot with the same package already bound
reached StartDevice Stage12/status0. `Wom1G3CreateInput` records system process,
NumPasid=1, IRQL0. DxgKrnl created a system GPU VA allocator and then failed
to create paging context 0: ETW at −0.436 ms records KMD
`STATUS_NOT_SUPPORTED`, at −0.412 ms "Paging context 0 creation failed", and
Event549 returns `0xc0000001`. The current boundary is
`DxgkDdiCreateContext` for the paging SystemContext. `render_objects.h` admits
only SystemContext/GdiContext bits; pinned WDK26100 also defines bit2
VirtualAddressing, expected under GpuMmu. Confirm exact input flags with a
receipt and host RED→GREEN before changing any other contract. No first VidMm
paging DDI or rendering yet. AutoLogger is stopped/removed, pinned SSH/CPU8
alive, exact package778 oem5.inf Code43, G3Armed absent; R54 series remains
active. R55 no-POST restart route is still untested on hardware.

## 2026-09-24 EXP776 boot AutoLogger names AddAdapter failure

Package777 kept package774's G3 64-KiB caps and full-owner m1n1/Mu. The R54
arm key was consumed before GPU access; R55 receipt showed successful POST
acquisition, valid 2560×1600/10240 geometry and route2. One cold boot reached
StartDevice Stage12/status0 and pinned SSH/CPU8/storage2/USB7, then APPL0002
Code43/Event549 `0xc000000d` FailureReason3. Boot AutoLogger ETL SHA256
`a8aee41f74dc61b9204aceaf2598f8bba289837d78f532462410375f59d79ac3`
shows driver `STATUS_INVALID_PARAMETER` at −0.541 ms and DxgKrnl AzureTriage
"Failed to create KMD process handle for system process" at −0.533 ms before
Event549. Node metadata and segment reporting followed before teardown.
QAI45/46 and first VidMm paging DDI remain unobserved. This names the current
boundary: `DxgkDdiCreateProcess` for the system process. The source guard in
`gpuva_g3_windows.c` rejects `Args->NumPasid != 0`; pinned WDK26100 and Learn
define NumPasid/pPasid as an input array, so that guard is the primary offline
candidate. Confirm by host RED→GREEN and a CreateProcess input receipt before
the next hash-pinned series run; do not change caps.

AutoLogger was stopped and its registry key removed after evidence collection.
The full-owner armed guest remains reachable with exact package777 Code43 and
no G3Armed value. Under R54, retain it for the next hash-verified package while
SSH and package identity remain certain; recovery to ordinary Code28 occurs at
series end or loss of control.

## 2026-09-24 R53 / EXP775 ETW observation verdict and recovery

Saved EXP771-774 Admin EVTX each contain exactly one current-boot Event549 with
GraphicsVendorId=0x4c505041, Status=0xc000000d and FailureReason=3
(`StartAdapter_AddAdapterFailed`), without a sub-status or check name. The
retained DxgKrnl-Operational channel has zero records. Type16/status0 is the
last overwritten QueryAdapterInfo receipt, but the receipts have no global
timestamps and cannot establish the last DDI before refusal. No further caps
candidate is justified by these logs.

EXP775 reused package774 and the EXP774 full-owner 64-KiB/debug-off/no-KD boot
bytes. Its cold boot again reached StartDevice Stage12/status0 then Code43 and
Event549 AddAdapter INVALID_PARAMETER. One all-keyword DxgKrnl ETW trace around
one live `pnputil /restart-device` was captured without lost buffers (ETL
SHA256 `061d7de27d99df19c57098a489191192d3dfb8819c0bf7b9000494ef96782410`).
The restart diverged: ETW records `DdiStartDevice` returning 0xc01e0002,
Event549 FailureReason=1, and driver Stage9/PostDisplay. The saved receipt
cannot distinguish failure of `DxgkCbAcquirePostDisplayOwnership` from the
subsequent geometry check. No AddAdapter validation was reached under ETW,
so no dxgkrnl check or documentation-backed caps fix is established. Do not
repeat live restart. Next separate diagnostic candidate is boot-time ETW
AutoLogger after offline verification of its startup contract.

G3Armed was removed; immutable GPU-hidden cleanup deleted exact oem5.inf,
signer and transfer. Frozen ordinary EXP377/392 returned pinned SSH and one
inert APPL0002 Code28 with no AppleAgx residue, CPU8/storage2/USB7.
Final baseline SHA256
`f8c3d69fd4b8e33a8555cb76f9327e70cd85b398e03fc46d7aa48cd43869cf60`.
G4 Mesa VA winsys still waits for AddAdapter and first VidMm DDI.

## 2026-09-24 EXP774 atomic GpuMmu declaration verdict and recovery

Pinned WDK26100 offline contract and RED→GREEN host validator are in
`docs/superpowers/specs/2026-09-24-gpuva-g3-addadapter-contract.md` and
implementation commit `1226a473`. The one EXP774 cold boot retained EXP773's
64-KiB pages, debug-off/no-KD/no-WPR, full-owner m1n1 and Mu. It reached
Scanout ABI v2, SSH, StartDevice Stage12/status0, QAI13/14 success. New
GetNodeMetadata receipt proves node0 GpuMmuSupported1/IoMmuSupported0; QAI13
proves Leaf64K8192/DualPte0. QAI45/46 were not observed. DxgKrnl Event549
still rejected AddAdapter with STATUS_INVALID_PARAMETER and APPL0002 Code43.
No first VidMm process/paging DDI or render was proven. Armed state SHA256
`ad13c95e4c76a469e2c88dd245360a9c7bf047c485b3707f59f120695edace10`.

G3Armed was removed. Direct immutable GPU-hidden recovery removed exact oem5.inf,
signer and transfer; frozen ordinary EXP377/392 returned pinned SSH and one
inert APPL0002 Code28, no AppleAgx residue, CPU8/storage2/USB7/autologon1.
Final baseline SHA256
`2c9858afedaa1283e3d1efa5bbf23794c855d60198786ac75616f88140ce37fa`.
Verdict: node0's missing GpuMmu flag and zero source MMU inventory were not
sufficient sole causes. No hardware conclusion about QAI45/46 because no calls
were observed. Remaining AddAdapter candidates: other caps/node/segment or
QAI15 physical-adapter interpretation, then package/PnP/persisted state;
internal dxgkrnl validation is not documented. Next action is one offline
comparison of exact admission inputs and exported GraphicsDrivers/service state.
Do not repeat EXP774. G4 Mesa VA winsys waits for AddAdapter/first VidMm DDI.

## 2026-09-24 EXP773 64-KiB G1b profile verdict and recovery

One preregistered G3 full-owner, debug-off/no-KD/no-WPR cold boot changed only
the G1b local segment profile from 16 to documented 64 KiB. Pinned WDK26100
package773 compiled with zero warnings/errors after RED→GREEN caps and real-C
translator tests; implementation commit `567cc88a`. Air reached Scanout ABI
v2, pinned SSH, StartDevice Stage12/status0, QAI13 and all QAI14 levels
status0 with VA39 and 128/32/16-KiB tables. APPL0002 still became Code43;
DxgKrnl Admin Event549 again reported `StartAdapter_AddAdapterFailed` and
`STATUS_INVALID_PARAMETER`. The QAI13 receipt omits Leaf64K, so 8192 bytes is
source/build-verified, not directly read back. No first VidMm process/paging
DDI or render was proven. Armed state SHA256
`a13b033cf49ae8241b7213e0d7db2d2c56bb6b1f3778a75ee2a4a71f9ffb3e2f`.

G3Armed was removed. Direct immutable GPU-hidden recovery removed exact
`oem5.inf`, service, signer and transfer; hidden check passed. Frozen ordinary
EXP377/392 returned pinned SSH and exactly one inert APPL0002 Code28, no
AppleAgx residue, CPU8/storage2/USB7/autologon1. Final baseline SHA256
`86760b30a891142cb3c55d934819417360363b64d735d51c89a370d49fafd4ab`.
Verdict: 16-KiB local segment pages are not the sole AddAdapter cause. Next
offline target, by proximity: current `QUERYMMUCOUNT=0`/invalid QUERYMMUS
against advertised GpuMmu; then other QUERYSEGMENT5/DRIVERCAPS relationships;
then captured GraphicsDrivers/service state. No repeat 64-KiB hardware run
without a new causal discriminator. G4 Mesa VA winsys remains offline until
AddAdapter and first VidMm DDI are proven.

## 2026-09-24 EXP772 corrected caps hardware verdict and recovery

One EXP772 debug-off/no-KD cold boot with ABI6 full-owner m1n1 and signed
package772 reached pinned SSH, Scanout ABI v2 and KMD StartDevice Stage12/status0.
QAI13 and QAI14 L0/L1/L2 all returned success with VA39, three levels and
128/32/16-KiB tables. DxgKrnl Admin Event549 still rejected AddAdapter with
`STATUS_INVALID_PARAMETER`; APPL0002 Code43, with no proven first VidMm DDI or
render. The table-capacity correction is necessary by the pinned WDK contract
but insufficient for adapter admission. Armed state SHA256
`02010c1e266d92063b65ee990760b24c737bdf3da383febcfde9a095539264d3`.

G3Armed was removed; direct immutable GPU-hidden recovery deleted exact
`oem5.inf`, signer and transfer. Frozen ordinary EXP377/392 returned pinned SSH
and one inert APPL0002 Code28, no AppleAgx package/service/files/arm,
CPU8/storage2/USB7. Final baseline SHA256
`ce4a7db03fa7a83338ea45022ea65c47ce9b1e6a0cc47afead5d91ac715ac910`.
Next causal target (R51): Microsoft GPU-segment and GpuMmu documentation
describes VidMm memory-segment pages as 4 or 64 KiB; our G3 local segment
still advertises 16 KiB. This is a source-backed candidate for the remaining
AddAdapter rejection, not a proven cause. Implement and host-test a truthful
64-KiB segment, `Use64KBPages` translator and 64-KiB leaf descriptor offline
before any EXP773 preregistration. Audit DRIVERCAPS/segment/MMU-count
relationships and preserve first-failure receipts. No B1 hardware. G4 Mesa VA
winsys remains offline after this admission boundary.

## 2026-09-24 R49/R50 offline gate for EXP772

Pinned WDK26100 defines `DXGK_PTE` as two 64-bit words. The original G3
13/11/3 logical levels required at least 128/32/16 KiB allocation sizes,
but QAI14 returned 16 KiB at every level. Commit `489e677f` corrects the
sizes, pins 16-KiB local pages, and passes a real-C RED→GREEN caps validator;
QAI13/14 receipts are in `d90f1d3e`. EXP772 tested the correction under the
EXP771 full-owner/debug-off/no-KD environment; QAI succeeded, then AddAdapter
still failed.

R50's single offline pass over saved EXP770/771 armed devnode and SetupAPI
snapshots found the same oem5 package/service; EXP771 adds only later Wom1
receipts and fresh VideoID/AOCID GUIDs. Neither snapshot exported
`Control\\GraphicsDrivers` or service registry, so R35's persistent-state
owner remains unknown. EXP772 recovery used immutable GPU-hidden exact-package
removal after armed evidence, then ordinary GPU-visible Code28.

## 2026-09-24 EXP771 post-Start G3 admission verdict and recovery

The corrected ABI6 m1n1 full-owner build (`IOMFB_FULL_OWNER=1`) produced
Scanout ABI v2; frozen package770 reached natural StartDevice Complete/status0
with Windows SSH, CPU8, USB7 and storage2. DxgKrnl then rejected AddAdapter
with `STATUS_INVALID_PARAMETER` (Admin Event549), leaving APPL0002 Code43.
The last generic QAI receipt was Type16/status0; current receipts do not show
the GPUMMUCAPS/PageTableLevelDesc requests or first VidMm process/paging DDI,
so the exact invalid contract and whether those DDIs ran remain unknown. No
render was intentionally submitted. Evidence
`.local/experiments/EXP771-g3-vidmm/hardware-evidence/armed/state.json`
SHA256 `ed6505378f8f380d57faae9ca229fd9903d1acc08e540e72b1418fe969a6b087`;
DxgKrnl Admin EVTX SHA256
`f81e3d3cf5e3d4a30ee06283e21a259cffc755d70ad57526c5a18cf64c5c2a12`.

G3Armed was removed before shutdown. Ordinary recovery with the disarmed
package did not reach SSH in its bounded window; immutable GPU-hidden recovery
then removed exact `oem5.inf`, signer and transfer. Final ordinary GPU-visible
EXP377/392 returned pinned SSH and one inert APPL0002 Code28 with no AppleAgx
package/service/files/signer/arm, CPU8/storage2/USB7/autologon1. Final baseline
SHA256 `ae51264e063f0f71d2c66e896a042dd85e64d969f5f68a610f1d1663599d3d13`.
The earlier prelaunch serial collision was recovered by one proxy reboot; it
never launched an EXP771 payload. Next causal target: first-failure QAI
13/14 descriptors/status plus first G3 DDI receipts, then one justified G3
admission rerun. No B1 hardware runs. G4 Mesa VA winsys can proceed offline.

## 2026-09-24 EXP770 pre-G3 Scanout gate and ordinary recovery

The first hash-pinned G3 package770 was staged in the ordinary GPU-visible
Code28 guest and cold-booted once with ABI6 m1n1, debug off and no KD. Air
reached SSH/CPU8/USB7/storage2, but APPL0002 was Code43. Durable StartStage10
returned `STATUS_NOT_SUPPORTED` in ScanoutStart before G3Start/caps/VidMm.
The m1n1 host log says Scanout ABI v1; source shows the required proven latch
source is compiled only with `IOMFB_FULL_OWNER=1`, omitted from the G2 macho
build. This is an m1n1 build-profile defect and yields no G3 verdict. Evidence
`.local/experiments/EXP770-g3-vidmm/hardware-evidence/armed/state.json`
SHA256 `c40aad28ed28ec46d2e0d5f1f322c072d940fc3fadb65da5330cb2c27a99470c`.
G3Armed was removed; exact `oem5.inf` and transfer/certificate were removed
on the ordinary GPU-visible recovery guest. Final baseline SHA256
`f363da06d85414971911b9d50b81dbd607be0df7d7a229f8095f93331dc79549`
shows one APPL0002 Code28, package/service/files/signer/arm absent,
CPU8/USB7/storage2 and SSH alive. Next causal step: rebuild only m1n1 with
`IOMFB_FULL_OWNER=1`, then preregister one corrected G3 VidMm run against
the same package/Mu/environment; do not run B1 diagnostics.

## 2026-09-24 G3b offline candidate and B1 closure

The user closed B1 as a diagnostic after EXP767. No more B1 cleanup or
context0-hash hardware runs. EXP769's no-job BUSY result is explained by live
tables at DESTROY; the real broker host test now proves the required
leaf/backing/parent/child-table/root teardown order. G2 ABI6 remains offline.

G3b now has a separate WDDM3.2/16-KiB qualification profile: process-owned
broker graphs, local/system/aperture IPA resolution, SetRootPageTable,
UPDATE_PAGE_TABLE, FLUSH_TLB, and a bounded SubmitCommandVirtual path using
the existing prepared GDI DMA packet and B1 lease/JOB_BEGIN/JOB_END/RELEASE
verbs. Only this profile advertises GpuMmu caps. The default profile remains
physical. `G3Armed=1` on the devnode is required before any G3 runtime/MMIO;
ordinary GPU-visible recovery keeps it absent. Pinned WDK26100 ARM64 package766
build has 0 warnings/errors; shared real-C sanitizer tests pass; the full
1019-test host run has 0 new failing names against the recorded baseline.
These are offline results, not a G3 hardware verdict. Next causal target is
one hash-pinned, debug-off/no-KD G3 adapter/VidMm discriminator without render,
then exact package removal and ordinary Code28 recovery. If VidMm rejects the
3/11/13 table projection or supplies unsupported PTE groups, record the first
refusal instead of running GPU jobs. Mesa winsys VA remains after that verdict.

## 2026-09-24 EXP769 debug-off cleanup discriminator and recovery

EXP769 used frozen package765 and the EXP767 debug-off/no-KD staged cold-arrival environment. Windows reached DriverEntry/StartDevice and pinned SSH with exact APPL0002 Code43; no bugcheck. The diagnostic stopped before any GPU job: B1 Stage4, CompletedJobs0, output prefill A5A5A5A5/5A5A5A5A, context0-hash-before status C0000483. Cleanup00 passed and root1 Cleanup01 was BUSY with four tables and 12 owned pages because the earlier abort left the graph intact. Source and m1n1 log identify the first defect: retained QUERY_TABLE_HASH computes page count, then platform response epilogue overwrites `Count` with MappingCount=207; KMD rejects Count>24. Context0 identity and successful cleanup remain unproven. Debug-off/no-observer progression narrows EXP768 KD confounding but does not isolate debug setting from observer timing. Evidence `.local/experiments/EXP769-b1-cleanup/hardware-evidence/state.json` SHA256 `4774202228239dc12dc940ccd58513c0e1b67e5d48a68d129f9fd1ffd92fad99`. Exact disarm, package765 removal and ordinary EXP377/392 Code28 recovery completed; final state SHA256 `f35aa5766f0f251b01899c1c6ace4a99617774d2dc4423538f770158023918c3`, debug No/Local, package/service/module/signer/arm0, CPU8/storage2/USB7/autologon1.

G2 offline after EXP769: m1n1 `386332a4` fixes the retained response Count publication, adds independent process ASID flush and verifies registered 16-KiB guest IPA groups against stage-2 backing. Shared broker ABI Version is 6; context0 and TTBR1 are untouched. Real-C ASan/UBSan host tests pass 3/3, m1n1 ARM64 build/link passes (one preexisting dlmalloc warning), and the full 1017-test run has zero failing names beyond the recorded baseline (13 failures/150 errors, 2 skips in this environment). No G2 artifact is installed or hardware-validated. Next causal target: G3b KMD lifecycle/paging/submit integration offline; a new hardware discriminator remains gated by truthful caps and hash-pinned package/recovery.

## 2026-09-24 EXP768 pre-StartDevice verdict and recovery

EXP768 was one staged cold-arrival B1 run with durable cleanup owner receipts,
EL2 context0 table-page hashes and attached serial KD. KD obtained the Windows
kernel base and sent continue; the full-owner guest did not reach SSH in more
than three minutes. A source-defined m1n1 SIGTERM snapshot/reboot returned to
Running proxy. GPU-hidden inspection found the exact package765 and B1Armed,
but **zero new B1/start/cleanup/hash receipts**; therefore the cleanup owner
and context0 identity remain unproven. No spontaneous bugcheck or new GPU job
verdict was observed. The KD-enabled full-owner boot is a possible confounder,
not an attributed cause. Verdict: `INCONCLUSIVE_PRE_START_FOR_CLEANUP`.

Exact package765 removal, disarm and debug-off/local restore completed in the
GPU-hidden recovery guest. Ordinary EXP377/392 GPU-visible recovery is active
and reachable by pinned SSH: one inert APPL0002 Code28, no AppleAgx package,
service, files, signer or B1Armed, CPU8/storage2/USB7/autologon1. Evidence:
`.local/experiments/EXP768-b1-cleanup/hardware-evidence/`; final state SHA256
`8c61256c720c76ca510b772210b720fba3a965209642ec8691f59a510e57cf15`.
Implementation commits: m1n1 `daedb776`, root `0c138bf1` (plus test
`e44e0577` and KD `e9e0a4bf`). R44 accepts that KD debug-on plus attached
observer was an additional, unqualified environment variable in EXP768;
do not use it in the next GPU cleanup run. Next causal target is a separately
preregistered cleanup discriminator with EXP767 debug-off/no-observer baseline,
after saved CPU0/KD/host evidence is checked. `kd_proclist.py` is excluded
from that playbook because its hardcoded EPROCESS offsets failed. G3a offline
translator/contract is commit `1baaa7f9`; caps and DDIs are not enabled.
G3 remains offline; do not advertise GpuMmu or claim desktop acceleration.

## 2026-09-24 EXP767 hardware verdict and recovery

EXP767 confirms the B1 GPUVA diagnostic **job path**, not full desktop: two
sequential TA/3D jobs used slot1 process roots, both wrote `0xFF112233` at the
same VA to distinct backing, and `CompletedJobs=2`. All 18 durable step receipts
show successful poll with matching TA/D3 stamps and done pointers, drained
events, CPU flush, image release, JOB_END, RELEASE and TLB invalidation ack.
The EXP766 order defect (unbind before flush) is fixed in `2dde761f` and hardware
confirmed by EXP767. Source and Asahi place completion stamp/event objects in
inherited context0 kernel memory, not user TTBR0.

**Remaining boundary:** Stage7 still returned terminal and first-failure
`STATUS_DEVICE_BUSY` with `CleanupStatus=1`; this status comes from final cleanup
after both jobs, not from poll or broker JOB_END. `CompletedJobs=2` excludes a
remaining lease/job in flight. `AdmissionB1Cleanup` next checks `Uncertain`,
then destroys root1/root0 and releases owned pages; the failing substep is not
recorded. Final context0 byte identity is unproven. G3 promotion remains NO-GO.
Next causal target is durable cleanup substeps plus one context0 before/after
byte hash; then one discriminating cleanup hardware run if justified. G3 GpuMmu
DDI work can proceed offline in the next phase using the proven B1 components.

EXP767 evidence: `.local/experiments/EXP767-b1-retirement/hardware-evidence/state.json`
SHA256 `20fd1f7b2094a363c8ae562a2a0fa8637f04252e86081738d02b977d9671a45a`.
Exact disarm, GPU-hidden package764 removal and ordinary EXP377/392 recovery
completed. Final Code28 receipt at 2026-09-24T12:42:24Z SHA256
`2c3447e94af431376ca4a6e08e4e75f767e5a3fdd52e2fe1d37454baa05f2d54`:
one APPL0002, no AppleAgx package/service/SYS/UMD/signer or B1Armed, CPU8,
storage2, USB7, autologon1. Air SSH is reachable using the pinned EXP641
known-host file with strict checking; L41/L43 and ordinary launcher are active.

## Working mode
One executor; routine build/test/SSH/launch/recovery work stays in this task. No
new agents without user direction. Read this compact file first and consult only
experiment evidence named here. Do not load the full historical ledger.

## Objective and fixed architecture
Stable, visibly correct accelerated Windows desktop on Air M1 is UNPROVEN.
Primary path is WDDM GpuMmu/GPUVA by user decision 2026-09-23. This reopens
GPUVA and supersedes the earlier CLOSED/NO verdict. The physical/patch-list
architecture is preserved at tag `milestone/physical-patchlist-dwm-admission` and
documented in `investigation/ARCHITECTURE_PHYSICAL_PATCHLIST.md`; no source or
evidence is deleted. G1 finite model passes 330 checks: aligned 16/64-KiB
segment-generated pages cannot scatter 4-KiB PFNs inside one AGX leaf;
arbitrary raw 4-KiB VidMm updates remain a counterexample. Exact WDDM input
domain and PAGETABLELEVELDESC representation are unproven. G1b pinned-WDK
inventory and held 16-KiB hardware question are in
`docs/superpowers/specs/2026-09-23-gpuva-g1b-wddm32-inventory.md`. G2 broker v5
design and host specification are in
`docs/superpowers/specs/2026-09-23-gpuva-broker-v5-design.md`. User authorized
A/B on 2026-09-23; A broker v5 is implemented in m1n1 commit `138c510a` with
real-C host tests and offline ARM64 link. Correction `1a46a86c` reserves slot63
for active legacy/v4; v5 uses 1..62 until legacy is disabled. No GpuMmu KMD
integration or GPUVA hardware verdict exists yet. B1 precedes G3 and full B2.
EXP758 now proves only WDDM3.2 **physical-mode** adapter admission: exact758
APPL0002 Code0, WDDMDEVICECAPS0x3200 and Apple AGX client CreateDevice S_OK.
ETW allocation descriptor stayed64KiB and does not directly measure slab
placement. The client failed CreateSwapChain0x887A0005/reason0x887A0020; no
render/Present. Raw ETW639494 events lost0. Exact package cleanup and ordinary
Code28 recovery pass with autologon retained. Evidence: `.local/experiments/
EXP758-g1b64-control/causal-result.json` SHA256
`b8802b36ad5fce81d091d5c01639bad0ea6b0d051240c018f5ca3eeb8c9e1015`.
Next discriminator: 16KiB physical slab with per-QAI and allocation-input
receipts; this still cannot prove GpuMmu page tables. B1 firmware-slot run is
unperformed and separate.
EXP758 also exposed a new standard-client CreateSwapChain regression relative
to EXP736. The first failing DDI is not in the captured ETW/last-QAI receipt.
EXP760 isolated the current UMD byte-for-byte on physical WDDM3.0, with the
client and runner both in console Session1. It repeated CreateSwapChain
0x887A0005/reason0x887A0020; the process-local trace names the first refusal:
`CreateResource line467 E_INVALIDARG` after
`AgxD3d10WindowsPresentationCreate`. ETW639741 events lost0; three Event129
storage resets had no established GPU cause. Exact rollback and ordinary Code28
recovery pass, including removal of the user transfer package; autologon1
remains. Evidence `.local/experiments/EXP760-currentumd-wddm30/causal-result.json`
SHA256 `ab5837aae4f0dea620720e4748c3abc11c062174cf26d90c4b794763a3890cf5`.
Current causal target is the UMD RGBA `pPrimaryDesc`/NO_SCANOUT contract;
the UMD correction has a focused RED exit3 on the old guard and GREEN exit0
with the x64 native frontend/runtime suite, plus pinned WDK26100 ARM64 UMD
compile/link PASS. Evidence `.local/experiments/EXP761-rgba-primary-no-scanout/
offline/manifest.json` SHA256
`220185e9295ab6022e059fb43efae9dd0e6abcb9aea17d9fb0d07808456025bc`.
The offline NO_SCANOUT gate did not establish the standard client's runtime
primary inputs. B1 firmware-slot work remains separate and unperformed.
EXP761 ran that exact profile0 UMD once: CreateDevice S_OK, CreateSwapChain
0x887A0005/reason0x887A0020 again. `reject-primary` identifies Format28 RGBA,
**pPrimaryDesc=NULL**, Bind PRESENT|RT, MiscFlags0x8
(`DISCARD_ON_PRESENT`), 2560x1600/sample1. Thus NO_SCANOUT was not exercised;
the actual UMD guard rejects a documented discard-swapchain flag. Raw ETW639168
events lost0, no stop code, CPU8/storage/USB/SSH alive. Exact package and
user transfer copy removed; ordinary Code28/autologon1 recovery passes.
Evidence `.local/experiments/EXP761-rgba-primary-no-scanout/causal-result.json`
SHA256 `34ca160081a16c2a94bec1b831f321f023565c8cd30bb0d562369ed0b2652e75`.
Review R25 also identifies a non-optional-primary companion missing from the
speculative NO_SCANOUT success path; do not retain that path without its full
DXGI contract. Current target is the source-backed MiscFlags Usage/Bind rule.
R25 rollback is now offline verified: speculative unconditional NO_SCANOUT
success and its active test removed; argument-bearing `reject-primary` remains.
Full x64 frontend/native-runtime suite exit0 and pinned-WDK ARM64 UMD link
exit0. The historical a85d1d3e offline proof is superseded, not deleted.
The observed DISCARD_ON_PRESENT branch is now PASS_OFFLINE: pinned-WDK/Microsoft
misc flag contract, unchanged existing frontend RTV/Blt/retirement fixture with
Misc0x8, negative cases, final identical-source x64 RED exit8 / GREEN exit0,
ARM64 UMD compile/link PASS. Evidence `.local/experiments/EXP762-discard-present/
offline/manifest.json` SHA256
`01e0cc163628e1b158953863a734b4c61db93500c18b7ce3aa80fe057b3d62f5`.
No KMD/m1n1/Mu source or hardware changed in this offline gate.
EXP762 physical-mode attempt is INCONCLUSIVE for the DISCARD UMD fix: same SYS/INF as EXP761, but KMD `StartDevice` failed in `AdmissionMemoryRuntimeStart` stage4 with STATUS_INSUFFICIENT_RESOURCES (0xC000009A) before the client or UMD loaded. R28 evidence: no fresh WATCHDOG dump, System 4101/141/117, observed DWM AGX submission, or ETW TDR/reset; DxgKrnl event549 explicitly says StartAdapter_DdiStartDeviceFailed. Specific Reset DDI receipts were not instrumented, and the failing memory allocation substage is unknown. This is an unproven KMD-start nondeterminism/physical-mode debt, not demonstrated DWM progress. Exact package cleanup and ordinary GPU-visible Code28 recovery pass, CPU8/autologon1 retained. Evidence `.local/experiments/EXP762-discard-present/causal-result.json`. By user R27 decision, proceed to B1 without more physical-mode DWM experiments.

B1 offline candidate: m1n1 v5 rejects prepopulated tables, reserves legacy slot63 and accepts explicit shared graph grants with matched allocation generation (capacity1024). The diagnostic KMD profile is WDDM3.0 with GpuMmu caps zero. Asahi/current KMD source proves generated objects0..35 are rebound to retained kernel-half/context0; the source-checked user graph is 284 packed native pages, 17 shader aliases and one distinct output page at VA `0x15001d0000` per root. A checked v5 KMD client, two-root PTE orchestration, slot1 TA/3D work patcher, sequential lease/job/release path and terminal completion callback compile/link under pinned WDK26100 ARM64 with 0 warnings/errors. Real-C broker/wire host tests cover both roots and rollback, and the prepared DMA shadow has RED/GREEN capacity validation. m1n1 ARM64 MACHO link passed offline. EXP762 exact allocation was not retrospectively recoverable; new B1 receipts record Create/ADL/Map/Translate status and Stop outstanding count, with install -> orderly reboot -> test required. **No B1 hardware run or GPUVA go/no-go verdict yet.** Evidence and recovery design are in `docs/superpowers/specs/2026-09-23-gpuva-b1-diagnostic-plan.md`; do not install until exact package/m1n1 hashes, preregistration and rollback scripts are frozen.

EXP763 B1 hardware verdict: **INCONCLUSIVE_PRE_FLIGHT_FOR_GPUVA**. Exact WDDM3.0/GpuMmu0 package761 plus new m1n1 v5 image was installed disarmed, then armed and cold-booted once. Full-owner Windows SSH and KMD PlatformStart succeeded; m1n1 recorded GPUVA v5 init0/epoch1. B1 receipt remained Stage0 with STATUS_DEVICE_HARDWARE_ERROR, CompletedJobs0/outputs0: failure lies before root allocation/PTE/lease/TA+3D in BorrowIo, backend guest-IPA view/alignment, client open or probe. No GPUVA hardware go/no-go can be inferred. MemoryStop returned STATUS_DEVICE_BUSY with five outstanding allocations after platform close, separate teardown debt. Full evidence `.local/experiments/B1-gpuva-diagnostic/causal-result.json` SHA256 `b393ed3d6623ac334d7e425c6e2e0d24e92b02771113e74757178bcf98f7ff12`. Evidence-first rollback required GPU-hidden EXP377/385 because first ordinary recovery guest had no Windows network despite CPU/NVMe/xHCI; exact package/signature/user copy removed there. Final ordinary GPU-visible recovery passes one APPL0002 Code28, zero AppleAgx package/service/module/files/signer, CPU8/storage2/USB7 and autologon1. Next offline discriminator is per-preflight receipts plus MemoryStop owner accounting; no second B1 run without a new experiment.

Post-EXP763 offline source correction: m1n1 `gpuva_execute` returned OK after `verify()` for a mismatched epoch; the KMD epoch0 CREATE probe expected STALE2. Fixed in m1n1 `10feeaed` with real-C host tests and new ARM64 image `84b4d96b...`, not launched. KMD `8e71d9e9` adds separate BorrowIo/BackendView/IPA/ClientOpen/probe status+epoch receipts, also not launched. This bug is sufficient to explain Stage0 **if probe was reached**, but EXP763 did not receipt which preflight check failed. The ordinary recovery hang with an installed disarmed package remains unexplained; B1Armed had been removed before boot, so v5 MMIO access by that driver is not supported by source evidence. Recovery was completed through GPU-hidden exact package removal; final GPU-visible Code28 is proven. No second Air run under the one-launch instruction. Evidence `.local/experiments/B1-gpuva-diagnostic/postverdict-analysis.json` SHA256 `8b883a81dc8a264962b25ee8a8e6d634149b35817780c41b2979715aefcf8172`.

EXP764 attempted corrected m1n1 STALE2 B1 but **aborted before B1**: exact package762 was staged then live `pnputil /install` on ordinary GPU-visible APPL0002 hung/lost Windows network during SetupAPI `{Restarting Devices}`. GPU-hidden inspection found exact oem5 package and System32 hashes, but no disarmed Stage0 receipt, install receipt or B1Armed flag; no full-owner image, PTE or TA/3D run. This is R35's package-present ordinary-boot boundary repeated during installation, not a GPUVA result. Frozen GPU-hidden EXP377/385 exact cleanup and ordinary EXP377/392 recovery restored SSH, one Code28, package/service/files/signer0, CPU8/storage2/USB7/autologon1. Evidence `.local/experiments/EXP764-b1-stale-probe/causal-result.json` SHA256 `d5c1e33ddce5926a58fb97e4572b3a5bdaab59403b6c88bb02c8a1d3b2128fc2`. Next causal target: stage the same package without `/install` while APPL0002 is hidden, then one fresh full-owner device arrival; Microsoft documents staging and live device installation separately. Requires new preregistration; B1 GPUVA go/no-go remains untested.

R39 offline correction 2026-09-24: the 0x133 dump previously named `EXP763-ordinary-recovery-0x133.dmp` is **EXP764**, not EXP763. Its session time is 2026-09-23T21:30:50.725Z, its loaded AppleAgx SYS PE timestamp matches exact package762, and PnP blackbox APPL0002 activity at 21:28:50.753666Z matches the SetupAPI `{Restarting Devices}` boundary. `kd` with Microsoft public `nt` symbols reports P1=1 cumulative high-IRQL/DPC timeout, P2=0x1e00, bucket `0x133_ISR_nt!KeAccumulateTicks`; sampled stack is `nt` clock interrupt/IRQL frames only. AppleAgx and dxgkrnl are loaded but absent from that stack; kernel triage dump cannot identify the offending ISR/DPC or prove KMD causation. EXP764 now has a confirmed Windows bugcheck during the live PnP restart, while R35's earlier EXP763 recovery cause remains unknown. Analysis `.local/experiments/EXP764-b1-stale-probe/hardware-evidence/R39-public-symbols-attribution.json` SHA256 `5c6b75dbd4425132b77a731bcf4fa2175c3e680f059b5e0f04f652f5c6e72851`.

OpenGL and CS1.6 follow accelerated desktop acceptance.

## Proven hardware boundary — EXP753/package748
Package748 passed ARM64 analysis, Universal ApiValidator, Inf2Cat, signer,
catalog membership and native Air CAT verification. Hashes: CAT
3f459c5f2a667ffd870e78e96e5ac4175642e8766bdc29e3f1c7b39325301cab;
INF 64138fe2a75bf7cb0f2540ae88563f5579b582b9078fa5d6892a569a04838d6a;
SYS 306444414d80a6fdd8a3f3a7d7c4741fd7357d490176a0821e58985877ff812d;
UMD 5bf50a9c7fd493a6848f66ad24f3ec54a64bef9828f3d9f1c99d5e1bd781519d.

EXP753 fixed the EXP752 hardware-VS zero-base VertexID assertion. DWM PID2904
loaded exact System32 UMD748 (checksum12390304, timestamp1790151853), and
DXGKRNL HWDEVICE selected adapter `Apple AGX clean WDDM render-admission
experiment`, FL10_0, Interface0xA0006, Version0x177a. Thus hardware-device
selection is PROVEN. No dwm.exe-correlated native AGX graph, submission,
completion or standard Present receipt exists; acceleration is NOT PROVEN.
The old Wom1PresentTransferReceipt fence253 predates PID2904 and is excluded.

DWM then failed 0x8898008D in dwmcore!CD3DDevice::CreateBuffer through
CD3DDynamicAppendBuffer::EnsureByteSpace and CSharedDirect3DResources::Init.
ETW measured dynamic VB widths144,160000,240012 with Usage DYNAMIC, Bind VB,
CPU_WRITE. These are regression cases only, never an admission whitelist.
Operator saw a desktop background with black taskbar and no progress. Without
DWM AGX receipts, background is probably fallback/GDI, not our D3D Present;
black XAML/DComp taskbar is consistent with DWM losing its D3D device.
Evidence: `.local/experiments/EXP753-vertexid-hardware/causal-result.json`,
`hwdevice-submit-events.json`, `debug-dwm0.log`, `etw-buffer-events.json`,
`physical-observation.json`.

Exact748 cleanup completed. Ordinary GPU-visible recovery is active/reachable:
one inert ACPI\\APPL0002 Code28/null INF; no package/service/module/SYS/UMD or
signer; 8 CPUs, storage/USB healthy; trace environment absent. Do not retain an
AppleAgx package between experiments.

## EXP754/EXP755 boundary and hardware verdict
EXP754 implementation commit 4adc9c59 admits contract-wide D3D10 buffers and
arbitrary draw offsets. Package749 proved that DWM passes dynamic
VB144/160000/240012, dynamic IB16000, CBs, the 50x50 BGRA RT|SRV cached visual,
draw type16 and clear. It then recorded UMD E_NOTIMPL during draw type17. No
DWM-correlated AGX submission/completion/Present was proven.

EXP755 implementation commit 098ddedf adds a temporary one-draw-per-native-batch
bridge: Draw/DrawIndexed with an existing actual draw receipt invokes the
existing FlushRetire path before the next normal Mesa draw. Its x64 and ARM64
offline producer/capture/materializer/KMD/retirement gates pass, but the proof is
limited to counters. Before another package it still requires color and depth
LOAD-action plus state-persistence checks across the split; native multi-draw is
post-hardware performance work.

Package750 was installed once after native CAT/hash verification. DWM selected
Apple AGX FL10_0 and exact System32 UMD750 SHA256
50089bf12bce3a8fe790fae3ecd34fe6380336182416fa1acd513feea6ca4177. The
60-second trace contains 632894 events and lost0. First DWM PID6496 finishes
draw type16 and clear, starts type17 at 14:01:26.0797609, then records bad UMD
E_NOTIMPL at 14:01:26.0797925 without a type17 Stop. Retry PID3456 records the
same E_NOTIMPL during type17; a later DWM Stop/SchedulePresent occurs only after
device removal and is not execution proof. `umd-refusals.txt` is empty. The
retained present-transfer receipt is unchanged from login to final capture.
There is no DWM-correlated native graph, KMD Render/Patch/Submit, physical AGX
completion or standard Present receipt. Physical screen behavior was not
observed for EXP755.

Verdict: REJECTED_NO_CAUSAL_ADVANCE. The multi-draw hypothesis is not sufficient,
and the exact rejecting frontend DDI remains unknown because direct SetError
paths are not instrumented. Evidence:
`.local/experiments/EXP755-multidraw-offline/causal-result.json` SHA256
59c03763d81cd5fb67835046ecd8ce6654909c612d35c5a9654e9e3c11d28eb4 and
`dwm-first-failure-window.json`. Exact750 cleanup completed. Ordinary GPU-visible
recovery is restored: one inert ACPI\APPL0002 Code28/null INF; no package,
service, module, SYS/UMD or signer; 8 CPUs and storage/USB healthy.

## Current causal target
The post-EXP755 offline gate passes at source-diff SHA256
`efb35c31b25f510c4da886b55df33a959bd4dac1bba090eeeae1f9f5d668d2cf`.
Every frontend SetError now emits one refusals-only `reject-seterror` record with
function, generated-source line and HRESULT; a successful call stays silent.
The real producer/capture path proves second-batch color and depth LOAD, stable
draw-state roots without rebinding, and both one-draw guards. Basic FL10_0 point,
line-list/strip and triangle-list/strip topologies, arbitrary counts, instancing
and nonzero StartInstance propagate into the typed encoder. CB slots 0..13 and
SRV slots 0..127 are admitted; authored cb1/t1 shaders prove their real captured
relocations. x64 full integrated execution and ARM64 archive/link gates pass.

Adjacency topology is NOT claimed: Asahi routes it through a passthrough GS and
the current mixed-compute capture fails at a separate graph boundary. It is
post-hardware completeness, not part of the reached DWM basic-draw candidate;
the frontend emits `reject-capture reason=adjacency` and fail-closes before the
unsupported graph rather than leaving an unattributed native fault.
The current target is one exact build/sign/hash/preregistered ARM64 package and
one Air discriminator with the new SetError attribution.

Package751 is the preregistered candidate from implementation commit 39a5bc7b.
ARM64 analysis reports 0 warnings/0 errors; Universal ApiValidator, Inf2Cat,
signer thumbprint and catalog membership pass. Hashes: CAT
3ce86663ea375e39225a50d037ac67c7342007b5ca0c79e6e739727d44310a15;
INF 37ff68367989d38f5a56d33dddbaf15926cb3b000cc61f5ba54e573902709fe6;
SYS d00799c143483b8c4385a8ca3c7a624a63706a5509796c9815a6684e814a41f5;
UMD 25b62f5b5b3eea4abd669d9b5091e6e3e4ddae6f7d64613eb802e36456c1c5bb.
Those hashes identify the package later executed in EXP756 below.

## EXP756 hardware verdict
Package751 was installed once with native catalog verification and exact active
SYS/UMD hashes. Autologon entered `J313-WIN\\pavel`; DWM selected the Apple AGX
hardware device but repeatedly failed with 0x889800c0. The 45-second trace has
630464 events and lost0. The new diagnostic records 1890 identical exact
rejections: `reject-seterror fn=CreateResource line=453 hr=0x80004001`.
Generated `Resource.cpp:452-453` is the `bufferResource && !validBufferUsage`
branch. This is causal advance over EXP755: the invisible E_NOTIMPL is now
owned by buffer-usage admission, not inferred from draw type17.

No DWM-correlated native graph, KMD Render/Patch/Submit, physical completion or
standard Present was proven. The retained present-transfer receipt is unchanged
from login to final capture and remains the pre-existing value. Exact oem5 and
package751 were removed; ordinary377/392 recovery is active with Code28, no
package/files/signer, while autologon remains enabled by explicit user request.
Verdict: REJECTED_WITH_CAUSAL_ADVANCE. Evidence:
`.local/experiments/EXP756-dwm-frontend-contract/causal-result.json` and
`hardware-evidence/umd-refusals.txt` SHA256
2d24455119386ca1043da5d8f9e9b16a0b6ac24598c494fcf84a5e9014c85075.

## Buffer-usage offline verdict and next architecture
The EXP756 `Resource.cpp:452-453` boundary is PASS_OFFLINE at source-diff SHA256
`293bd30a6d23da3ff74ae9121b882b491cb56693a1c3a0339d3acb8848f764be`.
Pinned WDK26100 `d3d10umddi.h` (SHA256
`61899403d94840fab282dbb7da6faf234e2954bbdb47e3455f0f0572eb4e723a`),
Microsoft D3D10 DDI documentation and Mesa commit
`9aa1215f878b504f66159dd2ead4c7973142126e` establish typed buffer SRV/RT
binds, buffer view element ranges, and DynamicResourceMapDiscard for dynamic
SRV buffers. The implementation admits contract-wide SRV/RT buffer creation
without size or bind-combination whitelists; constant buffers remain exclusive,
output binds remain DEFAULT-only, and dynamic SRV buffers remain WRITE_DISCARD
without WRITE_NOOVERWRITE.

The real frontend creates a `PIPE_BUFFER` view with byte offset/size, authored
`Buffer<float4>.Load` reaches TGSI SAMPLE_I then NIR `txf`/BUF, and Asahi emits
its native texture-buffer descriptor. Capture records the selected buffer range
as a relocatable texture reference. The same candidate emits
`reject-buffer-usage` with usage/bind/map/misc/logical bytes before the generic
SetError record. x64 integrated execution passes with the typed view range
offset32/bytes64, command v6, three texture-address relocations and two KMD
materializations; ARM64 archive/link and UmdContractTest build pass. Those
numbers are regression evidence only, not admission conditions.

Ownership remains unchanged: the D3D10 frontend owns usage/view/map validation;
TGSI/NIR owns shader lowering; Asahi owns descriptor emission; Windows capture
owns relocation; KMD owns materialization/submission. m1n1/Mu continue to own
the already-proven inherited hardware/ACPI contracts and are unchanged. The
smallest falsifiable checkpoint was one real typed-buffer draw through capture,
two placements and retirement; failure remains fail-closed before hardware.

Later user direction recorded as REVIEW R13 selects GpuMmu/GPUVA for the next
phase. Therefore no package752, EXP757 preregistration or Air run is authorized
from this capture-path candidate. The frontend/shader contract carries into the
new G0/G1 phase; recovery remains the ordinary GPU-visible EXP377/EXP392 pair.

## Fixed experiment procedure
Git `/opt/homebrew/bin/git`; artifacts live under main repo `.local`, not the
worktree. Builder `pauls@192.168.1.24`, key `~/.ssh/windows_builder`. Air
`pavel@192.168.1.37`, key `~/.ssh/air`, pinned known-host file from EXP641.
Preserve TESTSIGNING and existing signer; Smart App Control is separate.
For local host ABI tests use `CC=/tmp/agx-clang-wrapper` (Homebrew LLVM plus the
MacOSX15.5 SDK and `/tmp/agx-ld64-wrapper`); the older admission wrapper is
`CC=/tmp/agx-clang PATH=/tmp/agx-cc:/opt/homebrew/bin:$PATH`. For builder
PowerShell, use the encoded-command helper `/tmp/agx_builder.py` when an inline
SSH command would cross quoting boundaries.

Before asking the operator, probe Windows SSH and both proxy/vUART USB endpoints.
Full owner uses EXP584 m1n1 plus EXP406 Mu; ordinary recovery uses EXP377 m1n1
plus EXP392 Mu. Set LLDDIR=/tmp/agx-lld-dir and the frozen-launch compatibility
environment; full owner additionally needs WOM1_AGX_G2_POWER_BROKER=1. Keep the
launcher foreground with a durable log. Verify native CAT and exact hashes,
install once, collect ETW/dumps/receipts before cleanup, remove exact package and
hash-matched residues/signer, then restore ordinary GPU-visible recovery.

Recovery baseline exception: by the user's explicit 2026-09-23 request,
AutoAdminLogon remains `1` and DefaultPassword is present. Do not record its
value. All GPU-cleanliness checks remain unchanged.

DirectFlip remains required by the advertised WDDM contract; behavior without
CheckDirectFlipSupport is UNKNOWN and observed through reject/ETW. Kernel-mode
command-buffer cap remains clear until coherent aperture exists. TDR ABI remains,
but software ResetFromTimeout does not quiesce AGX firmware; timeout is fatal and
requires reboot. General Blt remains post-first-DWM under NO_REDIRECTION unless
an actual reject-BltDXGI reopens it.

HARDWARE ROADMAP
[PASS] Frozen admission/shared/BGR/DXGI1.1 gates; EXP751/752/753 shader advances;
       exact hardware-device selection and ordinary recovery.
[PASS] EXP754 contract-wide buffers and package749 hardware buffer admission.
[PASS] EXP755 package750 exact build/sign/hash/install/evidence/cleanup cycle.
[PASS] Exact frontend SetError attribution, honest batch-split color/depth/state
       gates, draw guards, basic topology/instancing and cb1/t1 capture. x64 full
       execution and ARM64 link pass at implementation-tree hash efb35c31.
[PASS] Package751 exact ARM64 build/sign/hash/catalog gate; EXP756 evidence-first
       hardware run; exact cleanup and ordinary Code28 recovery.
[PASS] Resource.cpp:452-453 buffer usage, typed SRV load/capture relocation,
       DynamicResourceMapDiscard and argument-bearing rejection pass offline.
[NEXT] EXP761 measured DISCARD_ON_PRESENT with pPrimaryDesc NULL at the UMD
CreateResource rejection. The non-optional NO_SCANOUT success path was
superseded and documented DISCARD flag admission passes offline. Package and
preregister one exact profile0 client discriminator after final source/build/
sign/hash gates; clean its package before another trial.
The 16KiB physical slab trial then requires a measurable VidMm
placement signal before preregistration; GpuMmu caps remain zero. B1 requires a genuine
paging/app process caller and firmware job tied to a v5 lease; an MMIO echo is
insufficient. Full GPUVA B2 requires G3 after B1. Resolve 4-KiB logical
PAGETABLELEVELDESC indices and system-memory scatter before advertising caps.
Do not package or run cancelled EXP757. Keep intentional autologon enabled.
POST-HARDWARE: native multi-draw batching, optional features, performance,
sustained desktop stability, OpenGL and CS1.6 after accelerated-desktop acceptance.
