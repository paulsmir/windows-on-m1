# R143 — 1-GiB GPU reserve: offline design

2026-09-28. Task: `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R143.md`.
**Selected:** one physically contiguous 1-GiB reserve, identity IPA=PA,
with **1000 MiB VidMm-local / 16 MiB private scenes / 8 MiB backend**.
The candidate is `[0x8e0000000, 0x920000000)`; it is conditional on validation,
not a newly measured free range. Keep the DCP DMA window at 56 MiB and separate
it from the local segment. Reserve admission and scalable GPU execution are
different gates. Enlarging RAM alone does not fix EXP855E's system-backed VDM.

Scope is the requested design only: no production/test code, firmware, package,
Air connection, builder operation or hardware experiment. No new hardware
verdict. All `.local/` references mean the main `/Users/pavel/public_windows`
repository. Only this document and its CHANGES row are to be committed.

## WINDOWS/UEFI CONTRACT

### Evidence and source provenance

The source-first investigation used saved machine evidence, Asahi, m1n1,
Mu/ACPI, then pinned WDK and official specifications; reference launch comparison
preceded this design. Early file discovery is not evidence of a new measurement.
The user explicitly excludes live Air capture. Consequently ADT, registers and
interrupt routes below are **archived observations**, to be refreshed before
any later hardware run.

- Current boundary: `investigation/analysis/R142-render-backing.md` and its
  evidence JSON; EXP855E `full.log`, `hardware-manifest.json`, `contract.bin`.
  R142 identifies Render ordinal11 as VDM, backed by scattered 4-KiB system
  pages. Local40MiB was used, with peak33.171875MiB; this does not prove the
  precise failed BO's eviction reason or that the pool was full.
- Saved raw ADT: `.local/experiments/EXP476-initdata-inputs/j313-live.adt`,
  SHA256 `7e2a944d7b2d0900209cfe11be94e2b8012ef497c8c2951111216018a734dbfe`.
  Decoded node properties directly from its binary format, including
  `/chosen/{memory-map,carveout-memory-map}` and `/arm-io/sgx`.
- EXP855E framed launch contract, decoded with `tools/launch_contract.py`,
  passes framing/CRC checks: SHA256
  `8568b46284a3dda6198edcd1b45f9a54c9585700bf99609f44af83998c44ac1a`.
  Four checkpoints, eight CPUs, guest RAM base0x850000000, size0x18f708000,
  entry0x8510b4000, bootargs0x8533e8000. Its guest ADT digest is
  `ae255e1125476acd5bfccd5e11a95eddefb9e7cc0f97528fb988a716f2d17b4e`;
  do not equate it with the older raw ADT above. Routes include physical
  563..566→880..883,579→884,576→885,575→886,578→887,577→888;
  ACPI scanout interrupt889 is synthetic. No routing change is selected.
  CPU register snapshots and mapping attributes remain in that exact contract.
- Asahi Linux reference HEAD `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`:
  `arch/arm64/boot/dts/apple/t8103.dtsi`,
  `drivers/gpu/drm/asahi/{mmu.rs,pgtable.rs}`.
- m1n1 HEAD `8769e5e981730ca5c971ad985e65bd47d005e8c0`:
  `src/hv_agx_local_reserve.{c,h}`, `hv_agx_power_mmio.c`,
  `hv_agx_retained_{platform,backing}.c`, `hv_agx_gpuva_v5.{c,h}`,
  `hv_agx_scanout_{broker.h,broker.c,service.c}`, `display.c`, `dcp.c`,
  `heapblock.c`, `hv_vm.c`, `hv_launch_j313.c`,
  `hv_autonomous_layout.generated.h`, `proxyclient/m1n1/hv/__init__.py`.
- Mu HEAD `f0f1c50a040d490f78340b8995917ede24fc4220`:
  `Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c`,
  `Silicon/Apple/T810XFamilyPkg/Include/Library/AgxLocalReserveValidation.h`,
  `Silicon/Apple/AppleSiliconPkg/PrePi/PrePi.c`,
  `Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc`;
  its generator `tools/generate_j313_agx_abi_admission.py` and retained R110 AML
  receipt in `EXP823-r64-reserve/exp830/r110-offline.json`.
- KMD/shared sources under `drivers/apple-agx/`:
  `shared/include/{apple_agx_local_reserve_abi.h,apple_agx_gpuva_g3_translation.h,
  apple_agx_g3_private_pool.h}`, `shared/src/apple_agx_scanout.c`,
  `render-admission/src/{physical_memory_windows.c,memory_runtime_windows.c,
  memory_windows.c,render_memory.c,lifecycle.c,scanout_windows.c,
  gpuva_g3_windows.c,gpuva_g3_paging_windows.c}`.
  WDK26100 `d3dkmddi.h`: SEGMENTDESCRIPTOR4/5 and segment flags.

Root at inspection: `4ff9cb5541da7135120f0f5af012fde8a6654d65`, branch
`integration/ad04-windows-compiler`. Pre-existing submodule dirt is preserved.
`git diff --binary HEAD` SHA256: root
`e7ee1c10085074c2ba1bee7334f1f3dad3068af898a01e322950916575817fe2`, m1n1
`68387a2e4a778333004ae9e8e035304dfd70c5d83c8e1ec4dcff4c949427f0e7`, Mu
`2e654da05fbcd83288511c5161cc749c8b87d69e222cfc3f6fed3fceaea79a7d`.
These hashes identify the inspected working trees, not the frozen binaries.
No external implementation was copied.

### Reservation, CPU access and WDDM exposure

Mu owns exclusion from the OS allocator before DXE allocations, through the
resource HOB split plus a memory-allocation HOB of EfiReservedMemoryType. This
must reach GetMemoryMap and survive ExitBootServices. Merely adding `_CRS` does
not reserve RAM. Reserved memory is unavailable to the general OS allocator
before and after EBS. [UEFI 2.10 §7.2](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html)

KMD borrows exactly the firmware receipt/resource, maps it once and never frees
it into the OS page allocator. No 1-GiB contiguous StartDevice allocation.
`MmMapIoSpaceEx(..., PAGE_READWRITE | PAGE_WRITECOMBINE)` remains the CPU mapping
contract; preserve R111's Normal/WC RAM behavior, not Device-memory zeroing.
The API can fail for lack of mapping space and this must unwind cleanly.
[Microsoft mapping API](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmmapiospaceex)

UEFI's memory *type*, ACPI's resource cacheability, ARM stage-1/stage-2 attributes
and UAT permissions are separate contracts. Mu currently maps system DRAM with
its normal RAM attributes and the broker as Device. m1n1 stage2 uses
`PTE_MEMATTR_UNCHANGED`; do not substitute Device attributes for the reserve.
KMD must initialize the complete slab before exposing any bytes; retain ordering
and cache maintenance already required by the shared-memory/UAT paths. No new
cacheable CPU alias, blanket `_CCA` change or inferred hardware coherence.

Mu changes: size0x40000000; validation alignment independent of size; validate
identity IPA/PA, overflow and RAM/HOB containment. Exclude FD, framebuffer,
ramdisk, low backing, active PHIT/free-memory interval, stack, HOB and FV
allocations before punching the hole. Current explicit exclusions cover only
four layout ranges; `AgxLocalReserveHobContains` alone does not prove absence of
allocation HOBs. PrePi constructs the HOB list and calls MemoryPeim before
BuildStackHob, so pass/check its live stack and allocation arena explicitly.
Do not rely on a stack HOB that does not yet exist. The existing UINT32 reserve
size argument fits1GiB; test arithmetic before any narrowing. Keep the named
FVMAIN_COMPACT fix and the virtual-map terminator/count guard.

Generate `_CRS` with one fifth QWordMemory resource: base from the receipt,
length0x40000000, inclusive maximum base+0x3fffffff. Preserve DWordAcc low/high
reconstruction from R110, updating both length and maximum, not just the guard.
`_DSD agx-local-reserve-version` follows receipt v2. Preserve APPL0002, other
resources and IRQs. Bad/missing receipt exposes BAS0 and KMD refuses before GPU
access. A new Mu image must reject a valid but unsupported old reserve receipt
before allowing its new AML to run; mixed firmware is not a supported launch.
[ACPI QWord address-space resources](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/06_Device_Configuration/Device_Configuration.html)

Segment2 reports only1000MiB. Descriptor4 Size/CommitLimit=0x3e800000;
Descriptor5 Size=0x3e800000 (**no CommitLimit member** in the pinned header).
Keep GPU logical base0x1500000000; CpuTranslatedAddress is the actual borrowed
**guest IPA**, not an arbitrary host PA or a trace-derived0x30000 offset.
Keep aperture1 at256MiB, page-profile16/64 declarations, existing fully
CpuVisible segment, SupportsCpuHostAperture=0, PopulatedFromSystemMemory=0,
PopulatedByReservedDDRByFirmware=1. No new standby/hibernate-preservation claims.
The private/backend tails are never reported in segment2.
[Segment descriptor](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentdescriptor),
[segment flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentflags)

The R142 follow-up can make individual canonical allocations GPU-only while the
segment remains CPU-accessible to KMD. VidMm continues to own residency and
process VA. CPU staging/copy/import synchronization is a separate design gate;
keep existing class0 aperture policy and do not force today's CPU Lock BOs into
local-only placement. WDDM3.0 CPU-visible fallback is still required.
[GpuMmu ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
[Microsoft VRAM-only lockable-surface constraint](https://microsoft.github.io/DirectX-Specs/d3d/D3D12GPUUploadHeaps.html#support-for-vram-only-lockable-surfaces)

## M1N1/ASAHI CONTRACT

### Where the contiguous range comes from

This is a reservation of already present guest RAM, not an allocation from the
m1n1 heap and not reclamation of firmware carveouts. Archived bootargs show
actual RAM8GiB, normal-RAM top0x9df708000. Raw ADT region24 covers
0x802078000+0x1dd690000: it describes normal RAM and is **not** a flat exclusion.
`/memory/reg` in that saved blob is zero; do not invent a populated Linux-style
memory node. The bootargs and chosen carveout semantics supply the useful map.

| Range (half-open) | Existing owner / implication |
|---|---|
| below0x850000000 | m1n1 image/heap; saved EXP855E heap_top0x83f5f4000 |
| 0x851000000 onward | relocated ADT, FD at0x8510b4000, bootargs0x8533e8000; excluded |
| 0x85f000000..0x85ffa0000 | inherited framebuffer; excluded |
| 0x860000000..0x8a0000000 | maximum preloaded ramdisk; excluded even if absent |
| 0x8a0100000..0x8e0000000 | backing of low IPA0x100000..0x40000000; excluded |
| **0x8e0000000..0x920000000** | proposed1GiB, inside normal guest RAM |
| 0x9df708000..0x9e1338000 | observed TZ-unmapped span, including region5 and4 |
| 0x9e6e20000..0x9ffe20000 | second observed TZ-unmapped span |
| 0x9fff78000+0x40000;0x9fffb8000+0x4000 | retained gfx-shared/UAT root and GPU region; excluded |

Asahi's DT reserves `ttbs`, `pagetables`, `handoff`, `hw-cal-a/b`, `globals`;
several initial addresses are zero and are patched by the boot chain. Their
reserved-region names do not imply available memory. `mmu.rs::map_node` checks
physical address, IOVA, offset and length against16KiB; `pgtable.rs` has
UAT_PGBIT14 and distinct cached, uncached/shared and device protections.
Linux may use scattered memory at its supported granule. Windows4KiB scatter
is not thereby executable on this UAT. Physical contiguity of the reserve is a
stronger, useful local-allocation guarantee, not a new UAT page format.

Select `align_up(low_backing_end, 64KiB)` and validate exactly1GiB there.
64KiB satisfies both UAT16KiB and existing segment/scanout alignment. Do not
align the base to1GiB: current `HV_AGX_LOCAL_BYTES` doubles as alignment and
would move the first candidate to0x900000000. Separate size/alignment in
`hv_agx_local_reserve.{c,h}`, its power-mmio caller, Mu validation, shared ABI
and KMD lifecycle checks. Reject rather than scan toward unknown upper holes.
This policy is based on the launch layout, not hardcoded admission of one PA.

Before publication verify all65536 native leaves and their complete spans,
including 4KiB interior/end translations, have PA=IPA, normal-RAM permissions,
no holes and no alias into protected PA ranges. Existing selector checks only
one translation per16KiB and two explicit exclusions; strengthen it using
launch ownership, bootargs/MCC carveouts and the retained-backing rules.
Classify ADT ownership instead of rejecting every overlapping region-id.
An altered layout/heap/FV/low alias or unknown carveout fails closed.

The assisted path first logs unavailable before Python installs RAM stage2,
then succeeds after `pt_update()`; EXP855E records both. Preserve that retry
and make validity final only after mappings/ownership are stable. Standalone
must perform the same selection after native RAM maps and before guest entry.
Do not stage2-unmap the reserve: KMD must access its IPA. Mu's reservation
removes it from general OS RAM. No translated IPA alias is selected; supporting
IPA!=PA would add another mapping/attribute/ownership contract unnecessarily.

### Grant and DART limits exposed by enlargement

**Correction to the task shorthand:** v5 GPU grants and scanout56MiB are
independent. `hv_agx_retained_platform.c::translate_guest` validates normal
RAM, stage2, root exclusion and launch protected ranges; it does not restrict
all v5 grants to the scanout pool. Preserve table-versus-backing, owner,
generation, shared identity, busy-revoke, context0/63 and TLBI checks.

v5 has512 table records and8192 **per-page** backing records. At16KiB that is
at most128MiB unique backing, less with shared registrations.1GiB has65536
leaves;1000MiB local has64000. Do not silently enlarge the array and claim
scalability; linear duplicate/revoke scans would also grow. The initial
reserve-admission checkpoint may retain v5 with explicit capacity failures;
it cannot certify execution of the full local segment.

Before R142's full working-set use, select a separately versioned **range-grant
extension** (proposed v6, exact wire layout reviewed before implementation):
reserve-relative offset and length, process identity/generation, allocation
generation, access rights and explicit shared identity; validate the complete
span and each translated leaf before atomic grant publication. Use bounded
range metadata, disallow table overlap and cross-owner overlap except exact
shared identity, and check containment/rights at each leaf publication.
Partial unmap/revoke requires interval splitting with pre-reserved metadata;
if split space is unavailable, fail without mutation. Keep grants until job,
leaf and table references are retired; stale generation cannot resurrect them.
No single grant for the entire reserve and no raw user PA authorization.
Existing v5 remains a bounded compatibility path; a new consumer must negotiate
v6 and never reinterpret a v5 request. Range ABI realization is not needed to
measure1GiB firmware reservation, but is a blocker to claiming full capacity.

DCP is separate from AGX UAT. `dcp.c` initializes its shared IOVA allocator at
`vm_base+0x10000000..vm_base+0x20000000` (256MiB), also used by RTKit.
`display_scanout_map` maps both DCP and DISP DARTs. Mapping1016MiB or1GiB into
that allocator cannot work. Preserve its domain and fixed56MiB DMA window.
A future scanout at an arbitrary local offset requires safe window movement
(section below), not a DART address-width guess or broad mapping of tails.

## TRANSLATION

### Selected partition and address domains

All offsets are from the **validated receipt base B**, initially candidate
0x8e0000000; sizes are binary MiB. No additional memory is taken for scanout.

| Purpose | Offset | Bytes / MiB | Candidate PA/IPA range |
|---|---:|---:|---|
| VidMm-local, including primary/tables/context/BOs | 0 | 0x3e800000 /1000 | 0x8e0000000..0x91e800000 |
| KMD private scenes | 0x3e800000 | 0x01000000 /16 | 0x91e800000..0x91f800000 |
| KMD backend | 0x3f800000 | 0x00800000 /8 | 0x91f800000..0x920000000 |
| DCP DMA view, not a fourth allocation | movable W inside local | 0x03800000 /56 | B+W..B+W+56MiB |

This preserves private/backend budgets and gives all960MiB of growth to
VidMm. Three unpadded2560×1600×4 surfaces require46.875MiB, which exceeds the
old40MiB local pool.1000MiB leaves substantial room but is not proof of DWM's
maximum live/pinned demand; use padded BO sizes and real residency accounting.
Private16MiB remains a quota, not a bypass for every Mesa BO. Reserve growth
reduces OS memory by960MiB relative to the old64MiB reserve: if5.22GiB is the
same measured starting quantity, arithmetic gives about4.28GiB, not an exact
promise of4.2GiB. Capture actual Windows usable/available figures separately.

An alternative1GiB-aligned base wastes a known adjacent gap and changes address
placement unnecessarily. Splitting the reserve into a new scanout segment and
a second local segment would add segment IDs/placement policy to this capacity
task. Neither is selected. Expanding private scenes is unsupported by current
capacity evidence.

Address translations stay explicit:

- CPU VA → reserved guest IPA B+offset → identity stage2 PA B+offset.
- VidMm segment offset → KMD local logical base0x1500000000+offset;
  process GPUVA → validated UAT16KiB leaves → physical backing.
- Primary local offset S → DCP window-relative S-W → allocated DCP IOVA+S-W.
  A GPUVA, host PA or segment-relative address is not itself a DART IOVA.

### Scanout window versus surface limit

Decouple `AdmissionMemoryRuntimeScanoutView` from QuerySegment's CPU-local
view. Current `BackendOffset == POOL_SIZE` and `Bytes == LocalAllocationBytes`
assumptions are invalid at1GiB. Create a full-local view for memory descriptors,
paging and CPU copies; a scanout view reports its own window base, PoolBytes56,
and surface-accessible Bytes bounded to that window and the local allocation.
Backend offset becomes1016MiB, never a scanout-size equality.

For the initial Code0 checkpoint register W=0,56MiB, retaining scanout wirev2
and its proven latch semantics. KMD must validate every primary range against
the actual registered window. A high placement must return a precise bounded
refusal, never access past56MiB or claim a successful flip. This is an explicit
admission-checkpoint limitation, not a production scanout solution.

For general presentation after that checkpoint, choose a64KiB-aligned window
containing the entire primary: `W=min(align_down(S,64KiB), localBytes-56MiB)`;
require S>=W and surfaceBytes<=56MiB-(S-W). Wait for the old producer and
scanout consumer, quiesce/release the old mapping, register the new56MiB range
with both DARTs, then submit the window-relative offset and await the latch.
Serialize window movement with Stop, paging/eviction and Present. Old backing
stays pinned until quiescence; failure preserves ownership or quarantines it.
The existing RELEASE/REGISTER sequence must prove re-registration and error
unwind offline and later on hardware before this is enabled. If it cannot,
a transactional replacement ABI needs a separate design; do not remap an
actively scanned IOVA. DirectFlip support cannot be claimed for high offsets
until that gate passes; keep current caps for the initial comparison and
report actual refusal rather than fake success. No dedicated scanout copy or
extra reserve is introduced here.

### Per-layer work and version contract

| Layer / files | Exact design change and preserved owner |
|---|---|
| m1n1 local reserve + power-mmio caller |1GiB size,64KiB alignment, complete identity/protection validation, publication after stage2; selector owns placement |
| m1n1 retained platform/backing + v5 |audit full-span arithmetic and protected ranges; retain v5 capacity fail-closed for admission; later range ABI as specified above, not unchecked array growth |
| m1n1 scanout broker/service + display |retain56MiB wirev2/DART mappings and bounded stepping; verify REGISTER span lies in validated local area for the new profile; no pool crossing private/backend |
| Mu validation/MemoryInitPeiLib/PrePi |reserve exact1GiB before allocator use, explicit arena/stack/FV conflict checks, no silent old-version skip; preserve normal RAM attributes |
| generator + generated admission ASL |v2 reserve receipt,1GiB length/max,64-bit halves,_DSD version; identical other resources |
| shared local-reserve ABI; KMD lifecycle/physical owner |v2 identity/length/alignment matching, exactly one resource, WC borrowed mapping, complete translation check, release mapping only |
| KMD memory_runtime/render_memory/memory_windows |1000/16/8 partition; full-local and scanout-window views; descriptor4/5 consistency, upper-bound paging/copies, backend/private offsets |
| KMD scanout/shared client |surface offset relative to actual56MiB window; fail safely outside initial window; later serialized relocation gate |
| launch/build manifests and host tests |profile explicitly requires reservev2 and matching1GiB Mu/KMD; old profiles stay64MiB; record source and artifact hashes |

Reserve receipt **v1→v2** is required because the fixed-size and alignment
contract changes. Keep magic, broker offset0xd00 and0x28-byte register window;
v2 defines size1GiB and64KiB alignment with identity mapping and this fixed
partition. No new numeric MMIO address. Unknown version, invalid flag or size
fails closed. G4 packet/parser, power/IRQ ABI, launch framing and scanoutv2
remain unchanged. If a receipt structure gains fields later, version it then;
existing64-bit size fields suffice here. Range grants require their own v6,
not an unrelated bump to the reserve receipt. Log all three identities.

m1n1 owns inherited platform initialization, power and DMA/UAT validation;
Mu owns OS reservation/ACPI and EBS handoff; VidMm owns standard allocations,
residency and process VA; KMD owns partition/lifetime, page translation, copies,
submit/IRQ servicing and cleanup; UMD owns CPU producer intent. DCP broker
owns display mapping/latch. IRQ routing, timer, PSCI, power sequences and
RTKit initialization are preserved. R143 adds no Windows workaround for an
EL2 or firmware defect.

### Ordered offline gates and smallest later hardware checkpoint

1. Freeze the inspected source/profile and immutable recovery hashes. Add
   size-independent geometry tests first: preserve legacy64MiB profile, new
   1GiB identity candidate, alternative aligned layout, misalignment, overflow,
   shortened RAM, protected-range overlap, interior4KiB hole, final leaf and
   PA alias. Extend `tests/test_g3_local_reserve_selector.py` and its real-C
   fixture; `test_agx_local_reserve_launch.py` proves post-stage2 publication.
2. Mu/ABI RED→GREEN: `test_g3_{local_reserve_abi,mu_local_reserve,
   local_reserve_aml,kmd_local_reserve}.py`, real PEI HOB arena/stack/FV
   exclusion cases and `acpiexec` checks of min0x8e0000000/max0x91fffffff/
   length0x40000000. Old/new mixed versions, missing receipt and duplicated
   resource must reject. Run generated-file check, build Mu with the R110
   CLANGPDB RELEASE admission profile, inspect the actual emitted AML/FV and
   final memory-map descriptors. A successful compile is not an EBS proof.
3. Real KMD borrow/Start/Stop and QuerySegment4/5 replay with1000/16/8;
   first/last byte and boundary-crossing rejection for every view. Exercise
   upper offsets beyond64MiB and128MiB, map failure and partial initialization
   rollback. Preserve WC and zero-before-publication. The existing fixed-UAT
   allocator has64 page slots:1GiB requires32 leaf tables (32MiB per table)
   plus parents and existing aliases; count the actual complete graph in the
   test, not a guessed margin. Verify no overlap with aperture VA0x1600000000.
4. Scanout real broker/service replay:56MiB pool independent of1000MiB local;
   Start atW=0, bound primary end, refuse high offset safely, cross-tail
   registration rejection, both-DART partial-map unwind, in-flight release.
   General window relocation is a separate follow-up gate above. Keep the
   full32MiB private-process VA contract and private16MiB physical quota.
5. `tests/test_g3_vidmm_replay.py` profiles16/64 and G4 attachment/placement
   replay retain EXP855E fragmented4KiB rejection. v5 tests must demonstrate
   8192-record exhaustion atomically and no false1GiB execution promise.
   For later v6, test >128MiB coverage, last1GiB leaf, interval split exhaustion,
   shared aliases, table collision, stale generation, busy revoke and reset.
   Do not enlarge8192-entry *logical leaf* arrays: those describe VA geometry,
   not the total reserve.
6. Before an implementation commit, affected tests first then full host suite
   once, compare exact failure names against the known R142 baseline
   (1141 tests;15 failures/41 errors/2 skips). Pinned WDK26100 ARM64 KMD/UMD
   build and ABI checks; preserve native archive provenance/signer. No claim
   that this full suite is currently green. Hash final source tree and new
   firmware/package artifacts; read tandem review again before preregistration.

These are **future tests/builds**, not results of this documentation task.
Future firmware builds must be preregistered in EXPERIMENTS before execution,
including exact commands and after-results. Reference build parameters are
R110 `IOMFB_FULL_OWNER=1 EXP808_SCANOUT_DELAYED=1`, Mu `TOOL_CHAIN_TAG=CLANGPDB
TARGET=RELEASE BLD_*_AIC_BUILD=FALSE BLD_*_J313_AGX_G2_PROFILE=FALSE
BLD_*_J313_AGX_ABI_ADMISSION_PROFILE=TRUE`; derive complete commands from its
saved scripts and register new output paths, never overwrite their artifacts.

WHY THIS HYPOTHESIS:
1. Current40MiB local cannot hold even three raw panel-sized surfaces; the
   requested1GiB directly changes that capacity while keeping private owners.
2. Saved R110/EXP855E RAM map places a contiguous candidate immediately above
   low backing and below TZ/firmware exclusions; it avoids uncertain reclaim.
3. Existing R110 PEI/_CRS/KMD borrowing reached Code0 in EXP855E, so extending
   that contract has more evidence than runtime contiguous allocation.

After separate implementation/build and explicit hardware authorization, the
smallest checkpoint is **one normal Windows boot on the new cold full-owner
profile with its exact GPU package**, reaching reservev2/1GiB in m1n1, Mu HOB,
ACPI and KMD receipt; APPL0002 Code0, StartStage12/Status0, arm consumed, pinned
SSH and no bugcheck during a preregistered600-second window from Windows boot.
Here “normal Windows boot” does **not** mean the immutable ordinary Code28
recovery profile: that lacks the broker/reserve and must not boot an armed
package. Keep allocation intent/UMD behavior unchanged for this discriminator.
Record usable OS memory, segment1000MiB, CPU8, disks, USB, RDP, display/input
observations or explicitly unmeasured; capture ETL, serial, receipts and dumps.
No StartDevice/Code0 success proves TA+3D,1GiB residency, DWM frame or scanout.
Missing reservation, overlap, version mismatch, Code12/43, kernel fault or lost
SSH rejects admission. A later G4 refusal is separately attributed, not hidden.

### Frozen reference and recovery

Hash-checked R110 files in `EXP855E-r141-attachment-envelope/firmware/`:

- `m1n1-r110.macho`: `14872dba0a6ecab9d4e49e42237298b44909126f0d5f7267cc5deaed587372b1`.
- `J313_EFI-r110.fd`: `3a76857cb650cae6b8d7c5d22250debeec437cd28c2dfd032ebecb1cbd0f6eca`.

EXP855E actually launches that pair; the older EXP584/406 lineage named in the
compact state is not permission to substitute different firmware. Its launcher
uses assisted physical display/debug-off/low-mem with the full-owner broker.
EXP392's archived framed `launch-contract.json` also decodes four records,
SHA256 `ed4cb98c28ff4f23ccda330374d953011bfac6c82ed96f7acf47d1c604dccc49`;
its RAM/entry/bootargs match, but it is an assisted recovery reference, not
proof of standalone1GiB behavior. `hv_launch_j313.c` explicitly separates
standalone native hooks from assisted Python traps. Preserve its portable
CPU/IRQ/region invariants and test both launch paths; no bitwise mapping-graph
parity or new standalone validation is claimed. An exact last hardware-validated
standalone capture was not established from the bounded task references; it is
a mandatory artifact-selection gate before any standalone build/launch.

Build new files under a new experiment directory, with an explicit reservev2
profile and final SHA256 manifest for macho, FD, AML, package members, launcher,
source trees and launch contract. Preserve R110 and both recovery pairs byte
for byte. Mixed-version launch is rejected before installation/arm; package
checks must also reject missing reserve without GPU MMIO. Production and
diagnostic builds share reservation/window behavior.

Recovery artifacts rehashed locally:
`EXP810-g4-package817/recovery/m1n1-exp377.macho`
`fae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a`;
ordinary `J313_EFI-exp392.fd`
`16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06`;
hidden `EXP-20260903-385-hvc-single-page/recovery/J313_EFI-no-agx-autoboot.fd`
`279bd36ad3bbb1ee5e2393fa965343ea856b4c2b0dd4df2b2add6a8010e3f32c`.

Use EXP855E's latest demonstrated Code0 recovery: evidence/dump first, remove
experiment diagnostic keys, ordered restart to immutable GPU-hidden Code45,
remove exact package and hash-matched residues, then ordinary GPU-visible
Code28 with one inert APPL0002 and no package/service/module/signer/arm.
No live Code0 removal. Hidden is a cleanup bridge for the known Code0 hazard,
not an extra boot between ordinary experiments. Preserve intentional autologon.
If SSH is lost, use dump-first recovery, not repeated armed starts. Before any
operator action request inspect bounded SSH and proxy/vUART/launcher planes.
Every later build/run/recovery needs before/after EXPERIMENTS entries.

## WHAT IS STILL UNKNOWN

- The candidate is consistent with archived RAM/ADT/source; current full-span
  physical accessibility, firmware allocator state and EBS memory map at1GiB
  are unmeasured. A changed boot layout can invalidate it. No test may replace
  validation with the candidate address as an allowlist.
- 1GiB WC mapping, complete zeroing, translation receipts and additional UAT
  work may change StartDevice latency. Measure bounded phases; do not remove
  zeroing, run giant high-IRQL loops or suppress a timeout to gain Code0.
- Private16/backend8 and v5 capacity can still limit jobs despite larger
  local. Full-range v6 wire layout, bounded metadata capacity and KMD graph
  budgeting need a reviewed follow-up before scalable execution. Admission
  retains the old capacity and honest failures.
- Safe high-offset scanout window replacement has not been proven. Until its
  lifecycle gate passes, only the initial window is admitted.1GiB DCP mapping
  is rejected by the inspected allocator geometry.
- R142 canonical/staging copy ABI, imports, synchronization and working-set
  admission remain separate prerequisites; extra capacity does not guarantee
  native16KiB system backing or fix CPU-visible fallback. No DWM success claim.
- Exact last standalone hardware contract must be located/verified before
  standalone work; this task does not invent one or launch to reconstruct it.

Design verification performed here: archived contract CRC decoding, ADT
property extraction, source/firmware/recovery hashing, partition/alignment and
capacity arithmetic, four-section/scope review and CHANGES schema test.
No new hardware tests or full driver suite were run for a document-only change.

Tandem review dispositions (canonical `.local/tandem/REVIEW.md`, read at start
and again before commit; SHA256
`753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5`;
both R64 headings included):

REVIEW R113: ACCEPT — arm transitions only to cold full-owner; mismatch refuses before GPU access.
REVIEW R111: ACCEPT — preserve WC/Normal RAM mapping and complete initialization.
REVIEW R110: ACCEPT — retain DWORD halves and test new64-bit maximum/length.
REVIEW R109: REJECT — old double-PcdDxe hypothesis remains rejected; overlap validation is an independent reservation invariant.
REVIEW R108: ACCEPT — use source/manifest-pinned R110 build profile and measured selector inputs.
REVIEW R107: DEFER — no new0x101 experiment or package comparison here.
REVIEW R106: ACCEPT — firmware carveout and durable ordered transitions remain required.
REVIEW R105: DEFER — allocation matrix already analyzed in R142; no new allocation-intent change.
REVIEW R104: REJECT — do not repurpose the page-size input union as placement control.
REVIEW R103: DEFER — no live allocation probe in this offline design.
REVIEW R102: DEFER — historical AllocateCb refusal is not reserve capacity.
REVIEW R100: REJECT — retain recorded rejection; no reopened hypothesis.
REVIEW R99: DEFER — CreateDevice refusal is outside reservation design.
REVIEW R98: ACCEPT — include full panel surface arithmetic without shape allowlists.
REVIEW R97: ACCEPT — bounded current-source pass, no older-image archaeology loop.
REVIEW R96: ACCEPT — preserve existing TA/3D builder.
REVIEW R95: ACCEPT — preserve lifetime, owner and first-failure validation.
REVIEW R94: ACCEPT — private scene16MiB stays separate from VidMm BOs.
REVIEW R91: DEFER — preserve old physical observation; no new pixel result.
REVIEW R90: DEFER — preserve old color evidence; no display test here.
REVIEW R88: ACCEPT — Code0, execution, completion and frame remain separate gates.
REVIEW R86: ACCEPT — range grants are the scaling path; v5 page-array capacity remains explicit for admission.
REVIEW R85: ACCEPT — exact source/profile/ABI/artifact manifest gates.
REVIEW R74: DEFER — context DMA contract unchanged.
REVIEW R71: REJECT — system64 support cannot guarantee every GPU-accessed leaf, as R142 demonstrated.
REVIEW R69: ACCEPT — no live bind; cold full-owner only after authorization.
REVIEW R65: ACCEPT — preserve G4 VA/rights/lease/fence ownership.
REVIEW R64: ACCEPT — both entries: firmware-owned reserve and G4 ownership preserved; this task authorizes design only.
REVIEW R63: ACCEPT — no runtime1GiB contiguous allocation workaround.
REVIEW R57: ACCEPT — retain full-span table checks and truthful paging failures.
REVIEW R55: DEFER — repeated StartDevice recovery remains separate.
REVIEW R54: DEFER — no series started or package retained.
REVIEW R49: ACCEPT — pinned WDK segment semantics and coherent declarations.
REVIEW R48: ACCEPT — no ports opened; future serial exclusion remains mandatory.
REVIEW R47: ACCEPT — explicit build profile and version checks.
REVIEW R45: ACCEPT — grant/leaf/table teardown ordering remains required.
REVIEW R40: DEFER — no timer hypothesis is supported by reserve sizing.
REVIEW R37: DEFER — no new historical recovery experiment.
