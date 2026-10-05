# EXP853 — system-memory granule decision (offline)

2026-09-27. Recommendation: **A, a bounded SysMem64KB capability path**, with
physical-PTE evidence as its acceptance gate. This is the smallest next causal
test, **not a claim that Windows guarantees 64-KiB backing for every allocation**.
Do not enlarge R64 merely to make the present failure disappear. Neither A nor B
is presently proven to provide a complete DWM memory contract.

Scope: the requested offline decision only; no code, package, build, Air access,
installation, hardware experiment or change to the accepted GPU state. The only
deliverable is this document. Proposed implementation/tests below are not run.
No feature/correction/workflow is implemented, so no CHANGES.csv row or new
EXPERIMENTS.md entry is asserted. Existing experimental results remain intact.

## 1. Sources inspected and evidence identity

Paths starting `.local/` below are relative to `/Users/pavel/public_windows`,
not this worktree. Current source root is `b2887a51dde30c362ad2c2b7472039a3c7e4cff1`,
branch `integration/ad04-windows-compiler`. Current m1n1 HEAD is
`8769e5e981730ca5c971ad985e65bd47d005e8c0`; Mu HEAD is
`f0f1c50a040d490f78340b8995917ede24fc4220`. Both submodules were dirty before
this task and were not edited. Their current `git diff --binary HEAD --` hashes
are respectively `68387a2e4a778333004ae9e8e035304dfd70c5d83c8e1ec4dcff4c949427f0e7`
and `2e654da05fbcd83288511c5161cc749c8b87d69e222cfc3f6fed3fceaea79a7d`.
These are source observations, not identities of the frozen running firmware.

- Saved machine evidence: `investigation/GPU_CURRENT_STATE.md`, only the
  EXP853 BEFORE/AFTER/RECOVERY ledger entries, and
  `.local/experiments/EXP853-r133-unpublished/{hardware-manifest.json,build-kmd.ps1,hardware-evidence/g4-submit-failure-decoded.json}`.
  Receipt SHA256: `2c70c3327386cf0dcc926a5c69ccaab2e4c352e6c734ab8548be3e7f43eeb212`;
  manifest SHA256: `e19e07f719c7f079572e15fdf22e200d81150162ba0c5ff8cf46bdac7d516cf9`.
  No new live ADT/register/IRQ/memory-map measurement was made: Air access is
  forbidden for this task. Saved measurements support the present translation
  boundary; they do not establish free RAM for a larger reserve.
- Asahi primary implementation, local checkout
  `.local/reference/asahi-linux-asahi`, HEAD
  `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`: `drivers/gpu/drm/asahi/pgtable.rs`
  (`UAT_PGBIT`, `map_pages`), `mmu.rs` (`map_node`), `gem.rs`, `Kconfig`, and
  `arch/arm64/boot/dts/apple/{t8103.dtsi,t8103-j313.dts}`.
  `pgtable.rs` SHA256 is
  `a25a13fbcb41119ce77ed1f87e0002b8d92d4324ccd91a8005f3ba2d1b5ae230`.
  The stale module comment says `pt`; actual implementation is `pgtable.rs`.
- m1n1: `proxyclient/m1n1/hw/uat.py:245`,
  `src/hv_agx_local_reserve.{h,c}`, `src/hv_agx_power_mmio.c:51`, and
  `src/hv_agx_retained_platform.c:48`. These establish native granule,
  reserve selection/receipt and complete-leaf IPA-to-PA validation.
- Mu: `Silicon/Apple/T810XFamilyPkg/Include/Library/AgxLocalReserveValidation.h`,
  `Library/MemoryInitPeiLib/MemoryInitPeiLib.c:402` under the same package, and
  generated `Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc`.
  The generated ASL was inspected; this task did not decompile the frozen FD's
  AML. The EXP853 manifest/StartDevice evidence binds the deployed contract.
- Windows source: `drivers/apple-agx/render-admission/src/` files
  `lifecycle.c`, `allocation_windows.c`, `memory_windows.c`,
  `memory_runtime_windows.c`, `physical_memory_windows.c`,
  `gpuva_g3_paging_windows.c`; `include/gpuva_g1b_profile.h`;
  `drivers/apple-agx/shared/include/{apple_agx_gpuva_g3_caps.h,apple_agx_local_reserve_abi.h}`;
  `investigation/analysis/R132-frame-lifetime.md`.
- Pinned WDK snapshot `.local/reference/wdk26100/d3dkmddi.h`, SHA256
  `c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`:
  GPUMMUCAPS at 2113, allocation union at 3913, PAGESIZE enum at 11082.
  The local pinned header sufficed; no builder SSH was required.
- Official Microsoft sources consulted on 2026-09-27:
  [GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
  [64KB pages](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages),
  [GPUMMUCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps),
  [SEGMENTFLAGS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentflags),
  [ALLOCATIONINFO](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo),
  [UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable),
  [driver residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/driver-residency-in-wddm-2-0).
  No inspected sample establishes a stronger all-allocation physical-backing
  guarantee than these documents. No external implementation was copied.

## 2. Observed contract, ownership and launch boundary

EXP853 held Code0/pinned SSH/CPU8 for 618 seconds without a new System1001 or
G3 paging-failure receipt. The first G4 rejection remained branch9,
Parse/Unmapped, Process write 64 KiB at VA `0x3b0000`, GraphPresent=0.
Four sampled logical 4-KiB entries have segment0 and flags3:

| VA-relative subpage | Guest IPA | Offset within a native 16-KiB frame |
|---|---|---|
| 0 | `0x9424db000` | `0x3000` |
| 1 | `0x9424dc000` | `0` |
| 2 | `0x9424dd000` | `0x1000` |
| 3 | `0x94259e000` | `0x2000` |

The first IPA is misaligned; the last delta is `0xc1000`, not `0x1000`.
Thus even the first 16 KiB cannot become one direct UAT leaf. The receipt
samples four PTEs, not all sixteen PTEs of the requested 64-KiB access.
The segment0 unpublished counter (338909) is an update/group event count,
not unique resident pages or a DWM working-set size. The measured range does
not justify relaxing protected-firmware exclusions from EXP852/R133.

Asahi `pgtable.rs` fixes the offset to 14 bits, maps one physical base per
16 KiB and advances physical addresses by 16 KiB. `mmu.rs:map_node` rejects
unaligned SG address/length/offset/IOVA; Kconfig requires PAGE_SIZE_16KB.
Its success with scattered *16-KiB* frames does not establish support for
scattered *4-KiB* subpages. The J313 DT selects AGX G13G and names inherited
UAT regions; it exposes no second, 4-KiB GPU translation stage. m1n1's UAT
implementation agrees. This establishes no supported per-4K mapping path
in the inspected M1 implementations, rather than a claim about every
undocumented hardware mode.

| Layer | Ownership and constraint |
|---|---|
| Windows VidMm / OS memory manager | Own allocation backing, residency, logical GPUVA and paging updates. A sequential allocation offset is not proof of sequential physical pages. |
| UMD | Own GPUVA assignment, declared GPU uses, residency requirements and CPU/GPU synchronization. Imported/section-backed and internal allocations cannot be assumed equivalent to private Mesa BOs. |
| KMD | Own logical-to-native translation, rights, frame lifetimes, queue generation checks, DMA publication and completion/interrupt handling. Acknowledge CPU-only logical mappings, but never execute GPU accesses to missing graph leaves. |
| m1n1 | Own retained platform initialization/power broker, stage-2 translation, protected ranges, root/grant authorization and hardware interrupt routing. `translate_guest` checks all four 4-KiB subpages and endpoints within each 16-KiB leaf. CPU stage-2 does not reassemble scattered pages for GPU DMA. |
| Mu | Own firmware memory-map exclusion and ACPI resources. R64 is EfiReservedMemoryType and survives ExitBootServices as memory unavailable to the Windows allocator. |
| Recovery | Existing experiment orchestration owns evidence-first exact package cleanup and immutable ordinary Code28 recovery; this analysis changes neither power nor recovery contracts. |

The measured full-owner contract is R110 m1n1/Mu, v5 broker plus ARM_CONSUMED,
R64 at IPA=PA `0x8e0000000` +64 MiB, package853 G1b16. Frozen m1n1 SHA256
`14872dba0a6ecab9d4e49e42237298b44909126f0d5f7267cc5deaed587372b1`, Mu FD
`3a76857cb650cae6b8d7c5d22250debeec437cd28c2dfd032ebecb1cbd0f6eca`.
Ordinary EXP377/392 Code28 recovery was recorded at 14:50:22Z. These saved
assisted/full-owner and ordinary contracts are the reference; current dirty
firmware source is not a replacement artifact. No new standalone contract or
standalone equivalence is established here. A future A experiment can keep
the exact frozen firmware/launch route; B necessarily cannot.

## 3. A/B/C comparison and decision

### A — selected as the next bounded path, not a universal backing guarantee

`SysMem64KBPageSupported` exists in the pinned header from WDDM2.1 onward;
current `lifecycle.c` leaves it zero. Microsoft describes support but does
not specify which allocation classes must receive contiguous backing, nor
an exhaustion/fallback guarantee. `OpportunisticSysMem64KBPageSupported` and
`PerPtePageSize` appear on current Learn, but **neither exists in the pinned
26100 header**. Do not write their newer bit positions into reserved bits.
The documented fields do not license a claim that every system allocation
becomes 64 KiB. [GPUMMUCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps)

For supported memory-segment residency, 64-KiB-aligned and sized allocations
qualify for 64-KiB PTEs; single-PTE mode also depends on the other mappings
covered by that leaf table. VidMm can change the table to 4-KiB format when
the conditions cease to hold. These conditions describe mapping selection,
not a blanket rule for all OS backing allocations.
[64KB pages](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages)

Exactly what can be concluded about backing:

- A supplied valid 64-KiB system PTE represents one contiguous 64-KiB span
  in the guest physical address space; this follows from a single physical
  base plus page offset. Four independently validated host 16-KiB frames can
  implement it. Host physical continuity across the full 64 KiB is unnecessary.
- Private, appropriately aligned/sized VidMm BOs are the candidate population
  for the capability experiment. The sources do not enumerate which of them
  will receive that PTE format on this Windows build.
- Existing imported/section-backed CPU storage, shared primaries, subrange
  aliases, OS/context/fence/table allocations and memory-pressure transitions
  have **no demonstrated all-64K guarantee**. Even ordinary BO backing must
  be checked at the paging boundary. Support is usable opportunistically from
  this driver's correctness perspective; a formal best-effort allocation policy
  for the named cap is also not documented in the inspected sources.

`DXGK_SEGMENTFLAGS.Use64KBPages` describes a segment's page support; it is not
a request to physically repack system RAM. `ReservedSysMem` is OS-only. Do
not change aperture flags or local SlabSize merely to assert global physical
contiguity. [SEGMENTFLAGS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentflags)

The allocation union is another trap: pinned `MinimumPageSize` and
`RecommendedPageSize` are UINT16 enum fields (64 KiB enum value **4**, not
65536); `Alignment` overlays both. The header labels the two fields `in`,
while Learn leaves their semantics blank. Current allocation code returns
`Alignment=ADMISSION_ALLOCATION_ALIGNMENT`, preserving the EXP836-admitted
shape. Do not set both interpretations, or casually turn them into a KMD
force-contiguous output. Resolving a versioned minimum-page-size contract
would be a separate gate; the recommended first candidate does not need it.

R132 already expands a `Use64KBPages` leaf input into sixteen logical entries
and four native groups, checks base alignment, rights and segment identity,
acquires generations, publishes via the broker and invalidates safely. It
handles local segment2 and system segment0. But the **outer production DDI**
in `gpuva_g3_paging_windows.c:850` rejects that flag unless the *local* profile
is 64, and `AppleAgxGpuvaG3Caps(0)` reports Leaf64KBytes=0 for G1b16. Therefore
enabling the cap alone is internally inconsistent. R132 support in the inner
replay is not proof that package853 can consume system 64-KiB tables.

### B — larger local reserve: deterministic storage, incomplete placement policy

A larger firmware-owned pool has known aligned physical backing. It costs
real Windows RAM: total reservation S removes S from Windows; incremental
cost versus R64 is S−64 MiB (256 MiB: +192 MiB; 512 MiB: +448 MiB; 1 GiB:
+960 MiB). These are arithmetic examples, **not measured required sizes**.
EXP853 contains no reliable DWM+shell resident-byte high-water mark. Choose
no purportedly sufficient size from its unpublished counter. Required size
would include measured simultaneous GPU residency, KMD/firmware/table/scanout
overhead, allocation fragmentation and an explicit budget for growth.

B changes more than one constant:

| Owner / exact file | Required change if B is later selected |
|---|---|
| m1n1 `src/hv_agx_local_reserve.{h,c}`, `src/hv_agx_power_mmio.c` | HV_AGX_LOCAL_BYTES, selection alignment/end checks, full stage-2 walk and receipt validity. Recheck live RAM/exclusions; do not assume the old base plus a larger size is free. |
| Mu `AgxLocalReserveValidation.h`, `MemoryInitPeiLib.c` above | Exact size validation, HOB containment/splitting and EfiReservedMemoryType length; preserve all PEI/FD/ramdisk/framebuffer exclusions. |
| `tools/generate_j313_agx_abi_admission.py` and generated Mu ASL | Change LBYL size gate and resource length/max consistently; preserve 64-bit base assembly. Regenerate/compile/decompile ACPI, not a manual generated-file edit. |
| KMD `shared/include/apple_agx_local_reserve_abi.h`; admission `lifecycle.c`, `physical_memory_windows.c`, `memory_runtime_windows.c`, `memory_windows.c` | Size/resource equality, borrowed length, ADMISSION_LOCAL_BYTES, initialization and full-range mappings; expose usable LocalAllocationBytes after internal reservations, not blindly the whole S. |
| KMD `allocation_windows.c` plus UMD residency policy | Establish which allocations can actually execute only while local-resident, including imported/CPU-visible surfaces, and pressure behavior. Capacity alone does not establish this. |

Current PreferredSegment already requests local segment2 for Mesa BOs.
SupportedWriteSegmentSet permits aperture+local for the admitted CPU-visible
shape; EXP836 rejected local-only writes. EXP839/840 required section-backed
CPU-visible class0 to stay aperture-only after Rotate failure. A larger pool
does not remove either contract. Do not repeat a blind local-only change.

In WDDM2, the write set determines eligible placement; a preference is not
pinning. `EvictionSegmentSet=0` means direct transfer to page-locked system
memory, **not eviction disabled**. An aperture eviction mask still maps OS
backing; it does not repair the granule.
[ALLOCATIONINFO](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo)

Under local pressure, B must either evict inactive content and page it back
into representable local storage before execution, or return a bounded
residency/allocation failure. Silent execution from scattered system pages is
not a fallback. UMD owns usage/residency synchronization; changing segment
capacity cannot replace it.
[Driver residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/driver-residency-in-wddm-2-0)

### C — alternatives not selected

There is no implemented 4-KiB UAT mode to enable. Rounding a physical address
down exposes unrelated bytes and maps the wrong data. A DART or CPU stage-2
mapping is not a demonstrated second GPU translator. Allocating contiguous
memory privately cannot change the physical identity of a VidMm-owned page.

Explicit UMD staging into representable GPU BOs could eventually handle
selected CPU/imported resources; it requires copy-in/out, alias coherence,
CPU lock/rename synchronization, shared-resource semantics and completion
fences. KMD bounce pages silently substituted for writable system PTEs do
not satisfy that contract. This is a larger architecture change, not an
R132 publication fix, and is not recommended as the next experiment.

**Decision:** implement/test the bounded A contract first if separately
authorized. It directly changes the backing opportunity at the failing
boundary while preserving firmware, reserve, placement and recovery. It is
falsified for this boundary if the actual GPU-consumed range remains scattered.
Do not interpret successful cap negotiation, Code0 or another ten minutes
without a bugcheck as proof of this memory contract.

## 4. Proposed changes, offline gates and smallest hardware checkpoint

### Exact A implementation boundary (not executed)

1. `drivers/apple-agx/shared/include/apple_agx_gpuva_g3_caps.h`: represent
   system64 support separately from LocalUse64KBPages. Compute table64 support
   from their OR, preserving local profile16. When table64 is enabled, keep
   Leaf64KBytes=`0x4000`: 512 logical DXGK_PTEs need 8192 bytes, but the native
   leaf needs 16384. Preserve 39 bits, three levels and existing logical
   8192-entry 4K tables. Update CapsValid and AdmissionContractValid together.
2. Admission `src/lifecycle.c`: source SysMem64KBPageSupported and
   LeafPageTableSizeFor64KPagesInBytes from that one model at declaration,
   query and readiness checks. Keep DualPteSupported, LargePageSupported,
   SysMemLargePageSupported and newer/unsupported bits zero. Retain current
   update mode, idle requirement, invalidation and protection contract.
3. Admission `src/gpuva_g3_paging_windows.c`: replace the local-profile-only
   Use64KBPages gate with the tested table64 capability. Preserve root-level
   refusal, level1 child page-size interpretation, Repeat semantics, input
   bounds, R133 protected/unavailable unpublication and lifetime ordering.
   Prove 4K↔64K table conversion through the outer DDI, not just UpdateLeaf.
4. `allocation_windows.c`, `memory_windows.c`, `gpuva_g1b_profile.h` and
   firmware: **no initial policy change**. Preserve allocation size/alignment,
   EXP836 segment sets, class0 aperture exception, local SlabSize16 and the
   pinned WDDM interface. No MinimumPageSize/RecommendedPageSize override.
   No UMD BO whitelist by VA, size, bind combination or trace content.
5. Admission `src/receipts.c` and `src/gpuva_g3_private.h` if needed:
   bounded evidence tying a GPU access to allocation identity/class and
   allocation offset, table format, all covered backing frames, rights,
   graph generation and actual publication. Existing four-PTE first-failure
   data cannot prove the entire 64-KiB range or identify its resource class.
   A success receipt must be generation-bound; never log raw payload bytes.

### Offline gates before any package

- `tests/test_gpuva_g3_caps_contract.py`: RED on current local16+system64
  declaration; GREEN with nonzero native-sized Leaf64KBytes and cap agreement.
  Assert local16 stays unchanged and unsupported bits stay zero. Cover local64,
  system64-only, both, neither and contradictory declarations.
- `tests/test_g3_vidmm_replay.py`, `tests/g3_vidmm_replay_scenarios.c` and
  `tests/g3_system_lifetime_cases.c`: use the production outer paging DDI
  compiled as G1b16. A valid segment0 64K update currently fails at the outer
  gate (deterministic RED); it must reach four correct native leaves (GREEN).
  Include start511/end-of-table, nonzero allocation offset, Repeat, RO/RW,
  sparse multiple 64K pages, parent-first/leaf-first and 4K↔64K conversion.
- Replay EXP853's four IPAs as a regression, plus independently generated
  misalignment/scatter/mixed-rights/partial/unmap cases. They must remain
  unpublished and GPU access must fail closed. This negative result is an
  invariant, not a failure to be turned GREEN by admitting the trace.
- Reuse real broker/BeginJob tests: nonidentity IPA→PA per 16K frame,
  discontinuity inside a frame, protected range, stale generation, alias,
  grant exhaustion, parent detach, PFN reuse, invalidation/TLB failure and
  in-flight mutation. No capability may weaken these exclusions.
- Build with the actual pinned WDK only after targeted tests. Check exact
  field presence/enum widths and absence of fabricated newer bits. Run the
  required full host suite once before an implementation commit; R132's
  recorded non-green full suite is not a pass. Record any remaining failures
  honestly. Preserve output-locality limitations: passing a mapping test
  does not implement R132 step5 scatter output/completion/copy-fence support.

### Hypothesis gate and hardware checkpoint (proposal only)

WHY THIS HYPOTHESIS:

1. EXP853 identifies actual scattered/misaligned segment0 backing at the
   first GPU access; its unrepresentability is arithmetic, closer to the
   failure than IRQ, RTKit or render-state hypotheses.
2. The current driver advertises no system64 support and blocks 64K updates
   in profile16, while the pinned WDK offers the capability and the inner
   R132 translator already implements the required expansion.
3. Local preference already exists; EXP836 local-only refusal and the
   section-backed exception make reserve growth alone a weaker discriminator.

The one behavioral variable is the coherent system64 declaration/consumer
contract. Prerequisite consumer/receipt changes must have offline RED→GREEN;
do not combine this caps experiment with local64 profile, allocation union,
reserve, firmware, signer or recovery changes. A cap-off control can use the
same prepared consumer and receipts if a matched control is needed.

After separate authorization and full preregistration, the smallest machine
checkpoint is **backing and publication at the first actual GPU-consumed
system range**, before allowing its submission: capture allocation identity,
complete range PTE format/IPAs, per-frame host translation and graph presence.
Use VA `0x3b0000` only as an EXP853 regression locator, never as admission policy.
If a receipt proves representable backing and all leaves are published, the
placement hypothesis has advanced; then a bounded read/write job with known
output, completion, invalidation and stale-access rejection is the separate
execution checkpoint. Neither checkpoint alone proves DWM presentation.

Failure: same scattered backing, missing graph, declaration/AddAdapter failure,
paging failure, reset/bugcheck, lost SSH or uncertain package identity. A 4K
format containing complete aligned 16K groups is also representable: judge
the addresses/rights, not the flag alone. If system64 leaves some required
GPU-accessed allocations unrepresentable, reject A as a sufficient general
solution and record their resource class. Do not cycle through undocumented
bits; that evidence must select explicit staging or an independently proven
local residency design.

Any future run must record new package/source/diff/profile hashes and preserve
the exact EXP853 frozen R110 artifacts plus immutable ordinary EXP377/392 and
emergency hidden recovery. Check both control planes before requesting physical
action; stage/arm only for direct cold full-owner entry. Collect receipts,
ETL/events/dumps before exact candidate cleanup; restore ordinary GPU-visible
Code28 with no package/service/module/signer/arm. Follow the measured Code0
ordered-restart/hidden-cleanup route only when required for recovery, not as
an assumed ordinary inter-experiment step. No action in this paragraph is
authorized or performed by this document.

### Tandem dispositions

All headings containing OPEN in `.local/tandem/REVIEW.md` were considered,
including stale OPEN text in already rejected/resolved headings and both R64s.

- REVIEW R113: ACCEPT — preserve cold full-owner after arm; no launch here.
- REVIEW R111: ACCEPT — preserve Normal/WC R64 CPU mapping; no new RAM alias.
- REVIEW R110: ACCEPT — preserve measured 64-bit ACPI range fix.
- REVIEW R109: REJECT — preserve its recorded rejection; no new firmware hypothesis.
- REVIEW R108: DEFER — historical firmware build/selector work is outside this boundary.
- REVIEW R107: DEFER — prior 0x101 attribution cannot distinguish the current PTE failure.
- REVIEW R106: ACCEPT — ordered transitions and firmware reserve ownership remain required.
- REVIEW R105: ACCEPT — use EXP836 matrix verdict; no repeated matrix here.
- REVIEW R104: REJECT — prior hypothesis was rejected; allocation union is not a blanket forcing knob.
- REVIEW R103: DEFER — AllocateCb boundary is crossed; no live probe requested.
- REVIEW R102: DEFER — old allocation rejection is not the present access failure.
- REVIEW R100: REJECT — retain its recorded rejection, no archaeology pass.
- REVIEW R99: DEFER — old CreateDevice issue is outside the measured boundary.
- REVIEW R98: ACCEPT — future output tests include full primary dimensions, without shape whitelists.
- REVIEW R97: ACCEPT — one current-source comparison; no old-working-image search.
- REVIEW R96: ACCEPT — preserve existing TA/3D builder; no reconstruction.
- REVIEW R95: ACCEPT — retain ownership, lifetime and first-failure gates.
- REVIEW R94: ACCEPT — preserve per-process GPU versus firmware-object ownership.
- REVIEW R91: DEFER — panel observation remains recorded; no new display claim.
- REVIEW R90: DEFER — color/panel evidence is outside this translation decision.
- REVIEW R88: ACCEPT — publication, execution/completion and DWM frame remain distinct.
- REVIEW R86: ACCEPT — retain bounded grant capacity; cap support cannot imply unlimited residency.
- REVIEW R85: ACCEPT — any later build/run must bind source, profile, ABI and artifact hashes.
- REVIEW R74: DEFER — preserve existing context DMA contract; no context-placement change in A.
- REVIEW R71: ACCEPT — test the pinned SysMem64KB path; reject the stronger all-backing guarantee and omit the unavailable opportunistic field.
- REVIEW R69: ACCEPT — no live bind; future cold start remains separately gated.
- REVIEW R65: ACCEPT — preserve current G4 ABI/root/rights/lease/fence semantics.
- REVIEW R64: ACCEPT — both entries: preserve firmware-owned reserve and current G4 ABI; prior permissions do not authorize hardware in this offline task.
- REVIEW R63: ACCEPT — retain firmware carveout ownership; no StartDevice contiguous-allocation workaround.
- REVIEW R57: ACCEPT — preserve full table-span checks and meaningful paging errors.
- REVIEW R55: DEFER — repeated StartDevice recovery is a separate defect.
- REVIEW R54: DEFER — no experiment series starts in this task.
- REVIEW R49: ACCEPT — pinned WDK and coherent caps/table declarations are explicit gates.
- REVIEW R48: ACCEPT — no ports opened; launcher/port exclusion preserved.
- REVIEW R47: ACCEPT — preserve exact firmware profile/ABI checks.
- REVIEW R45: ACCEPT — retain grant/leaf/table/root teardown ordering in offline coverage.
- REVIEW R40: DEFER — EL2 timing is not a cause of synchronous Unmapped with these PTEs.
- REVIEW R37: DEFER — historical disarmed-start recovery does not justify a new run.
