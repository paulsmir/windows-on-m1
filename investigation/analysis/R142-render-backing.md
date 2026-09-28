# R142 — EXP855E render access: VDM backing and one architecture decision

2026-09-28. **The rejected root is VdmCtrlStreamBase, not Stencil.CompBase.**
Select **VidMm-managed GPU-only local BOs with separate CPU staging/readback**
for the next design/implementation phase. Do not change allocation flags alone:
CPU map, synchronization, residency and imported-resource semantics must change
with them. This is an offline decision, not an implemented backing fix.

## 1. Primary evidence and inspected sources

Task: `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R142.md`.
Root `a982fb7552acfb894f5ea58f037e88f9d65997e8`, branch
`integration/ad04-windows-compiler`; EXP855E package source `fa8a66a2`.
No Air connection, new ADT/register/IRQ observation, launch or package build.
Saved EXP855E evidence is the machine-state authority for this analysis.
`.local/` paths below are relative to `/Users/pavel/public_windows`.

Inspected in source-first order:

- EXP855E `hardware-evidence/Wom1G4SubmitFailure.bin`, `state.json`,
  `devnode.reg`, `umd.log`, `EXP801DxgBoot.etl`, and saved launch `contract.bin`
  identity from its retained manifest. Receipt SHA256
  `03e795a28e74431b325b2ea93667c5e6fad57daa7ff951b1206f1c81ac8e0a5b`;
  ETL SHA256 `98e07b40d18a00b8505db18f28a201f950335011d5b1bf3596d105d7f572e5a2`.
  All five analyzed guest evidence files match the retained final artifact manifest.
  ETL copied to the authorized builder and hash-checked; only saved ETL decoding
  used Windows. No builder compilation/signing/package operation occurred.
- `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/{mmu.rs,pgtable.rs}`:
  `UAT_PGBIT=14`; `map_node` checks IOVA, physical address, offset and span
  against that granule. `arch/arm64/boot/dts/apple/t8103.dtsi` supplies UAT
  reserved regions and `ps_gfx`; this is not a power/IRQ investigation.
- `.local/reference/mesa/src/gallium/drivers/asahi/{agx_batch.c,agx_pipe.c}`:
  encoder BO creation and render-root emission. Windows transformation in
  `drivers/apple-agx/mesa/scripts/build-native-asahi-state.py` preserves
  the 0x80000-byte encoder size while selecting explicit Encoder intent.
- `m1n1_windows/src/{hv_agx_retained_platform.c,hv_agx_gpuva_v5.c}`:
  full 16-KiB contiguous translation, protected-range exclusions, ownership,
  grant and synchronization. HEAD `8769e5e981730ca5c971ad985e65bd47d005e8c0`.
- Mu `Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c`
  and `Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc`:
  EfiReservedMemoryType R64 and reconstructed 64-bit _CRS range survive EBS.
  HEAD `f0f1c50a040d490f78340b8995917ede24fc4220`. Both submodules had pre-existing
  dirt; neither was changed. Frozen firmware was not rebuilt/decompiled.
- Under `drivers/apple-agx/`: `shared/src/apple_agx_g4_submit.c`,
  `shared/include/{apple_agx_g4_submit.h,apple_agx_gpuva_g3_caps.h,apple_agx_g3_private_pool.h}`;
  `render-admission/src/{allocation_windows.c,memory_windows.c,memory_runtime_windows.c,
  physical_memory_windows.c,gpuva_g3_windows.c,gpuva_g3_paging_windows.c}`;
  `render-admission/umd/src/{umd_win32_screen.c,umd_gpuva_windows.c}`;
  `mesa/winsys/{agx_win32_asahi_bo.c,agx_win32_gpuva_batch.c}`.
- Pinned `.local/reference/wdk26100/d3dkmddi.h`: input page-size union and
  allocation/segment flags. In particular, MinimumPageSize/RecommendedPageSize
  are inputs, not new KMD placement-forcing outputs.
- Last accepted contracts remain full-owner EXP584 m1n1 / EXP406 Mu and ordinary
  EXP377/392, recovered by EXP855E. Relevant historical comparison is limited
  to EXP836's saved `matrix.txt`: its local-only CPU-visible rows failed even
  at 64 KiB, whereas write-set3 rows passed. No broader archaeology pass.

Official Microsoft sources checked on 2026-09-28:
[allocation sets](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo),
[segment preference](https://learn.microsoft.com/en-us/previous-versions/windows/hardware/device-stage/drivers/ff562047%28v%3Dvs.85%29),
[allocation flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags_wddm2_0),
[GpuMmu](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
[MMU caps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps),
[CPU access tracking](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-usage-tracking),
[GPU upload heap specification, VRAM-only lockable surfaces](https://microsoft.github.io/DirectX-Specs/d3d/D3D12GPUUploadHeaps.html#support-for-vram-only-lockable-surfaces).
The last source explicitly states the WDDM 3.0 system-memory-fallback requirement
for CPU-visible allocations. It supports the architectural constraint; it does
not expose the internal VidMm branch that rejected EXP836. No external code copied.

## 2. Observed contract, attribution and placement limits

The receipt is v2/192 bytes: branch9, C000000D, ParseUnmapped3, subsite Access1,
kind Render4, ordinal11, **read one byte**, VA0x2f0000, GraphPresent0.
DMA280, UMD480, capacity331776, DMA VA0x3b0000. Process owner5, root
0x9de570000, process generation1, mapping generation0x192. Receipt PID4 is
execution context, not proof of which user process owns that BO. TotalFailures
1696 is a refreshed aggregate, not 1696 copies of the same packet.

`check_access` increments one counter across the entire envelope. Nine Process
ranges occupy ordinals0..8, CPU envelope9, one attachment10, first nonzero
Render root11 = **VdmCtrlStreamBase**. There is exactly one attachment:
280 = native attachment header8 + attachment24 + render header8 + render240.
VdmCtrlStreamBase is mandatory and read-only. Stencil.CompBase is the twelfth
entry in a *different* array, is optional, and would request write access.
The original EXP855E ledger attribution was therefore incorrect. Sparse zero
fields also make a general ordinal-to-struct-index conversion invalid.

Mesa's root encoder allocation is **Encoder class3, 0x80000 bytes (512 KiB),
alignment0x10000**, created through AgxWin32AsahiEncoderCreate and used by
`agx_build_render` / the Windows batch bridge. This identifies the producer BO
role and source size behind the root; it does **not** recover its Windows
allocation handle from this receipt. UMD logs include successful
`g4-native-allocate-cb ... 00000003 00080000 00010000 00000001` (e.g. line214),
but reserve/map logs expose booleans and page counts rather than VA/token.
Thus an exact receipt-owner/VA → UMD token → ETW global-allocation join is
**not present in the saved diagnostics**. Do not label a same-size ETW allocation
as that exact BO. This limits precise eviction/time/placement attribution.

The sampled logical group is unambiguous:

| VA | Guest IPA | 4-KiB PFN | Logical segment | Flags |
|---|---|---|---|---|
| 0x2f0000 | 0x97627d000 | 0x97627d | 0 | 3 (valid/write) |
| 0x2f1000 | 0x97638a000 | 0x97638a | 0 | 3 |
| 0x2f2000 | 0x976389000 | 0x976389 | 0 | 3 |
| 0x2f3000 | 0x976388000 | 0x976388 | 0 | 3 |

The first IPA is misaligned by0x1000; the rest are neither its next three pages
nor ascending. `AdmissionG3UpdateLeaf` cannot publish that group. It preserves
the logical PTEs and leaves the hardware graph absent; the parser then refuses
GPU use. This is **4-KiB scattered system backing**, already sufficient to explain
the refusal before a protected-range grant check. It is not a write-permission
failure. No rounding down, false contiguous mapping or broader grant is valid.
State.json records 382558 unpublished segment0 group updates and zero in other
counters; devnode.reg was sampled later (404803). These are updates, not unique
BOs or proof of one repeated cause. Segment0 in logical PTEs means system backing;
it is not the segment-number convention of the ETW aperture descriptor.

ETL decoding and counts are preserved in
[R142-render-backing-evidence.json](R142-render-backing-evidence.json).
Select the Apple adapter by its 40-MiB segment2 at CPU PA0x8e0030000, not by
segment number alone (the trace also includes the Microsoft adapter):

- Apple adapter `0xffffbc851aae6000`: aperture1 size256MiB; local2 size and
  CommitLimit40MiB; OS-reported system segment3. Local residency peak
  34,783,232 bytes =33.171875MiB. This includes tables, primary and other owners.
- Pair PageIn start67/stop68 by thread and join the recorded global-allocation
  identity to allocation33/34. Successful segment2 pairs include 67 ×64KiB,
  1 ×192KiB and 1 ×15.625MiB with CpuVisible/AccessedPhysically flags0x8001,
  preferred2, supported write-set3. Thus local is used by that allocation shape;
  ETW does not carry Win32ClassId, so not all those allocations can be labelled
  native Mesa BOs individually. Tables and 4-MiB context allocations also use it.
- All 152 recorded successful page-ins of the 512-KiB CPU-visible/native-shaped
  allocation are to aperture1; none of that shape to local2. There are also
  1192 ×64KiB, 216 ×256KiB and 87 ×1MiB aperture successes. Counts are page-ins,
  not unique/live allocation counts. No failed local 512-KiB attempt is captured.
- Native class allocations already return preferred2, write-set3, eviction-set0,
  normal priority, CpuVisible1 and AccessedPhysically1; UMD uses LockEntire and
  keeps a CPU pointer. Preference is not a guarantee. Eviction-set0 means a
  paged-locked system backing path, not forbidden eviction. AccessedPhysically
  guarantees contiguous placement only in GPU memory segments, not system RAM.
  SysMem64KBPageSupported already equals1; this measured group is still 4-KiB.

The supported fallback and real CPU mapping explain why system placement is
legal. Available evidence cannot distinguish per-BO budget pressure, pin/lock
policy and eviction as the exact VidMm choice at the first failed submit.
512KiB is smaller than40MiB, so it is not simply an oversized allocation.
The observed peak leaves6.828125MiB but is neither free space at that submit nor
a contiguous-hole guarantee. Do not claim the pool was full or never used.

Ownership: Mu reserves RAM and exposes ACPI; m1n1 retains power/firmware, validates
DMA grants and UAT publication, and supplies the existing interrupt contract.
VidMm owns standard allocations, residency and process VA; KMD translates PTEs,
keeps CPU-only shadows, transfers memory and validates submit. UMD/Mesa owns BO
intent, CPU access ordering and command addresses. Completion/IRQ and recovery
remain the existing KMD/broker and immutable-profile contracts. Asahi requires
native-granule backing; Windows can supply4KiB pages. This is a Windows allocation
and data-movement ownership mismatch, not grounds to weaken EL2 exclusions.

## 3. One selected approach and bounded follow-up

**GPU-only local canonical allocations, separate CPU staging/readback, managed
by VidMm.** Every admitted resident native Shader/USC, Encoder/VDM, state,
texture and render-target BO must use a GPU allocation with CpuVisible0 and
local segment2 as its only supported execution placement. UMD CPU pointers must
refer to staging storage, never lock that GPU-only allocation. Preserve
GpuMmu VA, MakeResident/fence ownership and fail on exhaustion rather than
silently executing scattered system backing. Evicted system copies are storage,
not executable residency. Re-residency must republish verified local leaves.

This is a design decision **not a one-line flag patch**. Setting CpuVisible0
while leaving pfnLockCb unchanged would immediately break every CPU producer.
A truthful GpuMmu/physical-use/MmuSet declaration must be validated with pinned
WDK and the new allocation intent; do not blindly copy the old EXP836 class0
shape. CPU visibility of the *segment* may remain, while a GPU-only allocation
must not be UMD-lockable. Acceptance on this Windows build is unproven.

Minimum implementation units/files for the next phase:

1. Introduce explicit GPU-canonical vs CPU-staging allocation intent in
   `render-admission/include/render_win32_transport.h`,
   `render-admission/src/{render_win32_transport.c,allocation_windows.c}` and
   `render-admission/umd/src/umd_win32_screen.c`. Preserve existing class0 CDD
   aperture policy. Test complete classes/rights, not a size/bind whitelist.
2. In `mesa/winsys/{agx_win32_asahi_bo.c,agx_win32_gpuva.c,agx_win32_gpuva_batch.c}`
   and `render-admission/umd/src/umd_gpuva_windows.c`, make CPU maps return
   staging and upload before consuming each BO. GPU-written state remains
   authoritative until completion and explicit readback. Persistent CPU maps,
   partial writes, rename/discard, alias/shared/import and destruction all need
   ownership and fence semantics; whole-BO uploads must not overwrite GPU writes.
3. Specify a bounded, owner-validated CPU copy path using KMD's existing R64 CPU
   view in `render-admission/src/{callbacks.c,gpuva_g3_windows.c,gpuva_g3_paging_windows.c}`
   and `memory_runtime_windows.c`. Existing virtual paging copy machinery is a
   reference, not automatically an application upload API. Pin/validate current
   allocation offsets, residency generations and scatter MDLs before copies;
   use documented callback/handle lookup and locking, never raw user pointers or
   arbitrary PA escapes. Its exact application-copy ABI is an **unresolved design
   gate**, and must precede implementation, not be guessed in this task.
4. Imported CPU-visible resources need owned local GPU storage plus ordered copy
   in/out tied to standard sharing/present ownership. Preserve existing primary
   and Windows synchronization. No GPU address may continue to target the CPU
   staging/import's scattered backing. Review the frontend import/map/copy paths
   in `render-admission/umd/src/umd_win32_screen.c` and
   `mesa/winsys/agx_win32_asahi_bo.c` together before enabling that path.

No Asahi code copy or m1n1/Mu change is selected. No new IRQ, DMA grant bypass,
firmware state or caps advertisement is implied. Private scene16MiB stays with
R137; enlarging it to hold every Mesa BO is rejected for this decision because
it bypasses normal residency/accounting and still needs CPU/shared-resource
semantics. Repeating preferred2 or SysMem64KB is rejected: already present.
CPU-visible local-only is inconsistent with the cited fallback constraint and
EXP836. GPU-only local + staging removes that constraint explicitly.

Budget (all capacities are hard bounds, not DWM sufficiency claims): R64=64MiB,
VidMm-local40MiB, private scenes16MiB, backend8MiB. Local includes page tables
and primary: a2560×1600×4 surface is16,384,000 bytes=15.625MiB; two such surfaces
consume31.25MiB, leaving8.75MiB **before** tables, context buffers, shaders and
other BOs; three require46.875MiB and cannot fit. One VDM root adds0.5MiB.
For illustration, one primary + one same-size RT + one4MiB context allocation +
one0.5MiB encoder leaves4.25MiB before all remaining allocations. Budget must
use actual padded/tiled BO bytes rounded to64KiB, not pixel size alone. Existing
segment2 peak is33.171875MiB; migrating all aperture BOs concurrently can exceed40.
No fixed DWM concurrency can be admitted from these traces. Queue/evict/wait with
bounded failure when the pinned working set exceeds the available budget;
account every resident canonical BO, table and context. Larger R64 would be a
separate firmware-authorized design/experiment, not an implicit step here.

## 4. Verification, next causal gate and recovery

Added a production serializer/parser regression in
`tests/g4_mesa_attachment_replay.c`: real v3 nine-range/one-attachment packets
at two geometries/DMAs reject the first Render read as ordinal11/VDM. Deliberately
substituting the old Stencil.CompBase attribution makes the assertion fail;
restoring VDM passes. This is RED→GREEN of the **analysis assertion**, not a
claim to have fixed GPU backing. No production driver code changed.

Executed with `CC=/tmp/agx-clang-wrapper python3 -m unittest discover -s tests`:
`-p 'test_g4*.py' -v`:31 PASS; full `-v`:1141 tests,15 failures,41 errors,2 skips.
The56 exact failure/error names match the recorded R138 baseline (added0/removed0),
comparison `.local/experiments/R142-offline/failure-comparison.json`.
This is **not a green full suite**. `test_g3_vidmm_replay.py`:25 PASS, including both
page profiles16/64. Independent read-only review found no important findings;
reviewer reran the production attachment test and independently checked all2328
ETL PageIn start/stop pairs (no unmatched/overwritten/pid-mismatched pair).
There is no driver change or Windows build.
Reviewer dispositions: [R142-review-dispositions.md](R142-review-dispositions.md).
Raw decoding script/output: `.local/experiments/R142-offline/{decode.py,evidence.json,
placement-events.xml,event-samples.xml}`. ETL extraction on the builder:
`Get-WinEvent -FilterHashtable @{Path='<copied ETL>';Id=33,34,78,313,274,67,68} -Oldest`
followed by `ToXml()` per event; start/stop paired by thread. The tracked JSON
contains input/decoded XML/script hashes. UMD token/VA and per-BO eviction joins
are absent; no synthetic identifier is presented as hardware evidence.

Required next offline RED→GREEN gates, **not executed/implemented here**:

- `tests/test_g4_segment_placement_replay.py`: real CreateAllocation output
  refuses CPU-lockable/local-only intent; new GPU-canonical intent emits
  local-only/CpuVisible0, CPU staging remains system-capable; class0 unchanged.
- `tests/test_g3_vidmm_replay.py` /
  `tests/g3_vidmm_replay_scenarios.c`: exact four captured PFNs remain unpublished;
  local replacement at same VA becomes representable, eviction removes it,
  re-residency restores it with new generation; both16/64 page profiles.
- `tests/test_g4_mesa_attachment_replay.py` and real BO/submit integration:
  persistent-map writes, partial uploads, GPU-write→readback ordering, shader/USC
  and VDM/state/texture/RT reach only local canonical VA. Poison/mutate staging
  during queued work, share/import aliases, CPU/GPU race, stale handle/generation
  and allocation-destroy-before-completion fail safely. Copies never use GPU
  access to scattered staging pages. No fake success callback can prove VidMm.
- Real budget exhaustion and rollback: pinned set larger than remaining40MiB,
  multi-process pressure, fragmentation, copy failure, pending residency and
  failed teardown retain ownership safely or return bounded failure. No alias
  into private16/backend8; no oversized allocation admitted by trace shape.
- Pinned Windows native callback/ABI compile and offline execution, affected
  tests first, full host suite once before implementation commit. Complete the
  copy-ABI and import design before any production edit; this document is not
  authorization to invent either.

WHY THIS HYPOTHESIS:
1. Receipt proves the first GPU render read targets a fragmented system group;
   removing system execution placement directly removes that necessary cause.
2. Preferred2 and system64 support already failed to prevent it; changing hints
   again has less evidence than separating CPU access from GPU backing.
3. WDDM3.0 CPU-visible fallback requirement plus EXP836 explain why retaining
   CPU-visible local-only cannot be the general guarantee.

Smallest **separately authorized** later checkpoint: one bounded native producer
creates canonical GPU-only local plus CPU staging, uploads a varied buffer, maps
and makes it resident, then records handle/owner/VA/size/segment/PTE/generation
before any AGX execution. Prove all32 native leaves of the normal512KiB VDM BO
and the complete submitted BO set, not merely the first byte. Exercise eviction
and re-residency with unchanged content. Refusal, fallback system execution,
unpublished group, capacity breach or stale copy ends the test. Only after
that gate may the same production render path test acceptance/completion; a
passing VDM access is not a DWM frame. No new hardware run is authorized here.

Recovery artifact contract remains immutable ordinary EXP377/392 and emergency
EXP377/385. Before any physical-action request check both control planes.
After a later Code0 run collect receipts/ETL/dump first, ordered restart into
GPU-hidden Code45 for exact package removal, then ordinary Code28 as demonstrated
by EXP855E. No live Code0 removal and no new firmware/G2 experiment implied.
