# R136 — deterministic process backing: offline architecture decision

2026-09-27. **Select option 2: KMD-owned process/queue render storage in an
exclusive R64 subpool, mapped only in a documented KMD-reserved GPUVA range.**
This is a proposed bounded architecture, not an implemented or hardware-validated
fix. Its decisive Windows interface is `DxgkCbReserveGpuVirtualAddressRange`.
A raw mapping into an ordinary VidMm-owned VA, or taking bytes out of the
currently advertised local segment behind VidMm's back, is not this design.

Scope: the task in `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_PROCESS_BACKING.md`.
Only this document is changed. No Air access, live measurement, builder SSH,
package, driver/firmware change, hardware run or experiment preregistration.
No implementation/workflow change is made, so no CHANGES.csv row is claimed;
no hardware activity requires a new EXPERIMENTS.md entry. The accepted compact
GPU state remains unchanged. Tests below distinguish executed arithmetic checks
from the proposed RED→GREEN implementation gates.

## 1. Inspected primary sources and evidence

Paths beginning `.local/` refer to `/Users/pavel/public_windows`, not this
worktree. Source-first inspection used saved machine evidence, then Asahi,
m1n1, Mu, pinned WDK and official Microsoft documentation. Air was prohibited;
no new ADT, register, interrupt-route or memory-map observation is asserted.

Current root is `c01fa6d5260a2eb792bbc8dec6a1d37980642a20`, branch
`integration/ad04-windows-compiler`. m1n1 HEAD is
`8769e5e981730ca5c971ad985e65bd47d005e8c0`, Mu HEAD is
`f0f1c50a040d490f78340b8995917ede24fc4220`. Their pre-existing dirty diff
SHA-256 values (`git diff --binary HEAD --`) are respectively
`68387a2e4a778333004ae9e8e035304dfd70c5d83c8e1ec4dcff4c949427f0e7` and
`2e654da05fbcd83288511c5161cc749c8b87d69e222cfc3f6fed3fceaea79a7d`.
They identify inspected source, not replacement launch artifacts.

- Saved boundary: the leading R136/R135/EXP854B entries in
  `investigation/GPU_CURRENT_STATE.md`; analyses `R136-process-bo-pages.md`,
  `R135-root-reuse.md`, and the relevant contract/source portions of
  `EXP853-granule-decision.md`. Only the relevant EXP836/837–839 ledger
  entries were consulted, not an old-working-reference archaeology pass.
- EXP854B primary receipt:
  `.local/experiments/EXP854B-r134-provenance/hardware-evidence/g4-submit-failure-decoded.json`,
  SHA-256 `5fce284fb808955bb4e1473094a0c6447d6fa8371659e06576ebe3670949f671`;
  its `hardware-manifest.json` SHA-256
  `fa9ee005c516091b20d9504f516a1adb53c911ab3e66eed12968203f9bdaca64`.
  EXP836 `.local/experiments/EXP836-r116-open/matrix.txt` SHA-256
  `d0a9951f84e4212466281ed953c5da26405521de78ed45c4d18e954a49cf86f1`.
- Asahi Linux checkout `.local/reference/asahi-linux-asahi`, commit
  `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`: exact relevant files
  `drivers/gpu/drm/asahi/buffer.rs` (`new`, `ensure_blocks`, `new_scene`),
  `queue/render.rs` (tile geometry, scene and aux FB allocation), `alloc.rs`
  (`array_empty_tagged`, `array_gpuonly`), `object.rs:657` (`GpuArray::empty`),
  `mmu.rs:486` (`map_node`), `pgtable.rs` (14-bit page offset), `Kconfig`,
  and `arch/arm64/boot/dts/apple/{t8103.dtsi,t8103-j313.dts}`. The GPU node
  names inherited UAT regions and `ps_gfx`; this task proposes no power,
  mailbox, DART or interrupt-route change.
- Mesa checkout `.local/reference/mesa`, commit
  `9aa1215f878b504f66159dd2ead4c7973142126e`,
  `src/gallium/drivers/asahi/agx_batch.c`, compared with the actual Windows
  producer `drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c:188–233`.
  The latter's SHA-256 is
  `ea4e3b16385c0b9c639a6473f911f7ca91d696cab61c85a95a9a1a133a512d94`.
- m1n1: `m1n1_windows/src/hv_agx_local_reserve.{c,h}` (reserve and receipt),
  `hv_agx_retained_platform.c:48–83` (complete native-leaf translation and
  synchronization), `hv_agx_gpuva_v5.c:213–282` (exclusive/shared backing
  grants and revoke). Existing v5 has exclusive grants; no new EL2 ABI is
  assumed. Complete-leaf checks and protected exclusions remain mandatory.
- Mu: `mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c:402–442`
  and generated `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc`.
  These reserve RAM as `EfiReservedMemoryType`, preserve it past EBS and
  expose the receipt-derived 64-bit range through APPL0002. Generated ASL
  was inspected; the frozen FD was not rebuilt or newly decompiled.
- Windows KMD, relative to `drivers/apple-agx/render-admission/`:
  `src/allocation_windows.c:404–439`, `src/render_win32_transport.c:22–74`,
  `src/memory_runtime_windows.c` (56/8 MiB split and backend/scanout views),
  `src/memory_windows.c` (segment Size/CommitLimit/SlabSize),
  `src/physical_memory_windows.c:430` (borrowed Normal/WC CPU mapping),
  `src/gpuva_g3_windows.c` (Create/DestroyProcess, bootstrap, table shadows,
  root/submit validation), `src/gpuva_g3_paging_windows.c` (logical paging,
  native publication, table residency), `src/callbacks.c:247` (existing escape),
  and `umd/src/umd_win32_screen.c:468–699` (Allocate and CPU Lock).
  Shared files: `drivers/apple-agx/shared/include/apple_agx_g4_submit.h:109–158`,
  `apple_agx_gpuva_g3_caps.h`; `drivers/apple-agx/shared/src/apple_agx_g4_{submit,builder}.c`
  and `apple_agx_gpuva_g3_graph.c`. Existing replay sources inspected include
  `tests/g4_mesa_process_buffers_replay.c` and
  `tests/apple_agx_g4_process_layout_test.c`.
- Pinned `.local/reference/wdk26100/d3dkmddi.h`, SHA-256
  `c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`:
  allocation union at 3921 and reserved-GPUVA callback/arguments at 8708–8734.
  The page-size members are inputs, overlaying output `Alignment`.
  Local headers sufficed; builder SSH was unnecessary.

Official references checked on 2026-09-27:
[reserved-GPUVA callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_reservegpuvirtualaddressrange),
[reservation arguments](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_reservegpuvirtualaddressrange),
[escape identity](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_escape),
[allocation flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags_wddm2_0),
[allocation segment sets](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo),
[GpuMmu ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0),
[GPU segments](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments),
[residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/driver-residency-in-wddm-2-0).
No inspected sample proves acceptance of the proposed private path on this
Windows build. External code was used for behavioral understanding, not copied.

## 2. Observed contract, CPU needs, ownership and budget

EXP854B first rejects Process[0], the TVB page-list BO, at VA `0x3b0000`,
write 65,536 bytes, GraphPresent=0. Its four sampled logical PTEs are valid
and writable, segment0, with IPAs `0x97292e000`, `0x97292f000`,
`0x972931000`, `0x972932000`. The first is misaligned to 16 KiB and the
third skips a 4-KiB page. This group cannot form a direct native UAT leaf.
This measures only Process[0]'s sampled backing, not all nine allocations.
64-KiB allocation/VA rounding and SysMem64KB advertisement already existed.
R135's separate root-reuse fix is offline only and must remain in future gates.

The exact EXP836 result is narrower than “local-only can never work.” All
recorded ROWs use `cpu=1`; their KMD echo has CpuVisible set. The minimal
class1 pair is token11, bits05 (read3/write2, AccessedPhysically1, alignment64K),
which fails with `C000000D`, versus token20, clone-class0 (read3/write3 with
the same CPU/physical/alignment properties), which succeeds. KMD Create/Open
succeed in both cases. Thus local-only write placement is the observed
rejecting difference **within that CPU-visible contract**. There is no
CpuVisible=0 control and no identified internal VidMm rejection instruction
in this evidence. Calling CPU visibility itself the proven internal cause
would overstate it. WDDM2 uses the write set for placement, consistent with
this discriminator. [Segment-set contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo)

GPU-only also cannot mean “skip initialization.” In current Windows code all
nine buffers are CPU-mapped, new buffers are zeroed, and lists 0/1 are written
with `Process[2].Va >> 15` and successive 32-KiB page numbers. Buffer-manager
BOs 0–2 persist in `G4BufferManager`; 3–8 are batch-owned. The Process name
in AGX4 is an address-space role, not proof that all nine have process lifetime.
The inspected producer has no application data upload or CPU result readback
for these nine beyond initialization/list writes. This is distinct from color,
depth, encoder, shader and imported resources, whose backing is not solved here.

| i | Role | CPU operation in current Windows / Asahi | Raw bytes at 2560×1600, 16×16 utile | Current allocation bytes |
|---|---|---|---:|---:|
| 0 | TVB page list | zero + page-number writes / kernel writes in `ensure_blocks` | 512 | 65,536 |
| 1 | TVB block list | zero + first word of each pair / kernel writes, second word initially zero | 256 | 65,536 |
| 2 | TVB block heap | Windows zero / Asahi `array_gpuonly`, no mandatory CPU map in this allocation API | 4,194,304 | 4,194,304 |
| 3 | user buffer | zero / `array_empty_tagged`, kernel CPU initialization | 65,664 | 131,072 |
| 4 | tilemap | zero / kernel CPU initialization | 102,400 | 131,072 |
| 5 | heap metadata | zero / kernel CPU initialization | 512 | 65,536 |
| 6 | tail-pointer cache | zero / kernel CPU initialization, cached across scenes; firmware writable | 1,310,720 | 1,310,720 |
| 7 | preemption scratch | zero / kernel CPU initialization | 18,144 | 65,536 |
| 8 | auxiliary FB | zero / `queue/render.rs` kernel CPU initialization | 32,768 | 65,536 |

Asahi `GpuArray::empty` actually writes `Default::default()` into each element;
“empty” is not an uninitialized-allocation promise. The heap's lack of a typed
CPU view does not justify leaking previous owners' memory: the new KMD design
must zero all nine, including heap and padding, before exposing them to a GPU.
Asahi allocates lists with its private per-VM allocator, heap blocks with its
user-VM allocator, scene scratch from kernel-managed per-VM storage; firmware
control/work objects remain in the kernel half. Mesa does not need to perform
the list construction in user mode. Our Windows producer moved that duty to
the UMD, making CPU Lock part of the failing backing contract.

Budget arithmetic comes from `AppleAgxG4ProcessRequiredBytes`, not the trace's
VA or a resource-size whitelist. At these dimensions tiles=80×50,
macro-tile factors=20×16, blocks=32. Every extent is rounded to 65,536 bytes.
TPC deliberately reserves eight clusters and preemption nine adjusted clusters;
these are current code upper bounds, not a new live measurement of J313.

- One manager (0–2): 4,325,376 bytes = 4.125 MiB, 264 native 16-KiB leaves.
- One scene (3–8), 16×16 utile: 1,769,472 bytes = 1.6875 MiB, 108 leaves.
- One complete set: 6,094,848 bytes = 5.8125 MiB, 372 leaves.
- Same dimensions, 32×32 utile: 5,046,272 bytes = 4.8125 MiB; scene 720,896 bytes.
- Manager + two 16×16 scenes: 7,864,320 bytes = 7.5 MiB, 480 leaves.
  Three scenes require 9,633,792 bytes = 9.1875 MiB before table overhead.
- Current R64 is 67,108,864 bytes, but `memory_runtime_windows.c` already
  assigns 58,720,256 (56 MiB) to local allocations and 8,388,608 (8 MiB)
  to the backend tail. Neither is a free process allocator. Current ordinary
  BO maximum is 16 MiB. The builder additionally supports only its existing
  32-block heap, one layer, one sample; allocating larger backing alone does
  not extend those builder capabilities.

| Layer | Current ownership / proposed change |
|---|---|
| Asahi reference | Kernel owns per-VM TVB/scene allocation and initialization; native 16-KiB backing. Its Linux allocator is not Windows VidMm. |
| UMD | Today allocates/maps/initializes all nine and retains BO references through fences. Proposal requests opaque manager/scene leases and uses KMD-returned VAs; ordinary resources still use VidMm residency and paging fences. |
| VidMm | Owns ordinary allocation backing, placement and logical page-table residency. Proposal asks it to reserve a disjoint VA subtree; never overrides ordinary mappings. |
| KMD | Owns new private pool, zero/list initialization, budget, private tables, generation/rights checks and lifetime. Existing submission, completion, interrupt notification and DMA validation remain here. |
| m1n1 | Preserves inherited hardware/platform state; owns power broker, physical authorization, UAT/TLB ordering, stage-2 translation and hardware IRQ routing. Exclusive v5 grants already exist. |
| Mu | Owns R64 firmware exclusion and ACPI exposure across EBS; 64-MiB physical reservation remains unchanged. |
| Recovery | Existing experiment procedure owns evidence-first cleanup and ordinary Code28 recovery; neither the proposed pool nor a new UMD token authorizes a launch. |

Reference launch remains saved R110 cold full-owner with v5/ARM_CONSUMED and
R64 IPA=PA `0x8e0000000`. Manifest firmware hashes are m1n1
`14872dba0a6ecab9d4e49e42237298b44909126f0d5f7267cc5deaed587372b1`, Mu FD
`3a76857cb650cae6b8d7c5d22250debeec437cd28c2dfd032ebecb1cbd0f6eca`.
EXP854B ends in documented ordinary EXP377/392 Code28 recovery. This analysis
neither replaces those assisted/full-owner/recovery contracts nor proves a new
standalone launch contract. No current free-memory or power-state claim is made.

## 3. Alternatives and the selected design

**Option 1 — GPU-only VidMm class: possible but not selected.** Removing
CpuVisible also removes legal UMD Lock access. It needs a new class/parser path
(the current transport requires CPU flags and STAGING_CPUVISIBLE), zeroing and
list initialization through a staging transfer or KMD operation, plus complete
residency/eviction handling. EXP836 does not reject this untested class, but
neither proves its acceptance. Local-only resident storage could avoid system
scatter; allocation success, local placement, paging initialization and migration
must all be demonstrated. Changing one flag would instead break the current
producer. No official source inspected explains the exact internal EXP836
refusal or guarantees a non-CPU-visible local-only allocation on this build.
[CpuVisible/Lock and AccessedPhysically semantics](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags_wddm2_0)

**Option 2 — selected.** The KMD-reserved VA exception supplies the missing
supported ownership contract. Reserve during `DxgkDdiCreateProcess`, including
system-process handling, at PASSIVE_LEVEL; later CreateContext is too late to
make this reservation. The pinned callback explicitly requires leaf-table
coverage multiples. Current 4K logical leaves cover 8192×4096 = 32 MiB;
64K format covers 512×65536 = the same 32 MiB. Reserve a 32-MiB aligned,
32-MiB VA window, `AllowUserModeMapping=0`, and use the returned base. Respect
the OS's reserved root-index rules; never hard-code the old `0x3b0000` or
assume the returned base belongs in root entry zero. This reserves VA, not
32 MiB of physical storage. [Callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_reservegpuvirtualaddressrange),
[arguments](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_reservegpuvirtualaddressrange)

The following is the proposed implementation contract, still requiring replay:

1. **Separate physical budgets before exposure.** At adapter start divide the
   existing reserve into `[0,40 MiB)` VidMm, `[40,56 MiB)` private process pool,
   `[56,64 MiB)` unchanged backend tail, relative to the validated R64 base.
   Report 40 MiB through all local segment Size/CommitLimit/scanout-bound views;
   decouple backend offset from the smaller VidMm extent. Derive physical
   addresses from the receipt, never from the historical base constant.
   This is an intentional KMD segment-capacity change, not a firmware change
   or a drop-in package855. It needs its own reviewed candidate. One 2560×1600
   BGRA surface consumes 16,384,000 bytes = 15.625 MiB, leaving only 24.375 MiB
   in that segment before other resources/tables. DWM capacity is not proven.
2. **Bound private storage.** Proposed adapter quota is 16 MiB; process quota
   is 8 MiB, charged for all manager/scene data, private tables and alignment
   overhead. A process can have multiple contexts/managers only within that
   one quota. Do not eagerly allocate a manager for every CDD/system process:
   CreateProcess reserves VA/metadata, first native render context/request
   lazily commits manager backing. At the measured geometry one manager and
   two scenes leave 0.5 MiB within quota for private tables/overhead; charge
   actual use, not an assumed constant. A third scene must wait for retirement
   or fail cleanly. Two full-quota processes exhaust the pool; this bounded
   milestone is not a production DWM multi-process capacity claim.
3. **Initialize in KMD.** Allocate 64-KiB-aligned extents to preserve current
   ABI and 32-KiB TVB numbering; every physical leaf is 16-KiB aligned and
   contiguous internally. Zero full extents through the existing Normal/WC
   CPU mapping. Derive list words from the assigned heap GPUVA using the
   source protocol, check arithmetic overflow, keep unused words zero, then
   perform the existing required memory/publication ordering. CPU writes stay
   in KMD. Lists are initialized once per manager generation, not overwritten
   while firmware owns an active scene. Scratch 3–8 is per scene; no silent
   alias across simultaneous batches. Growth or reuse requires idle ownership.
4. **Use the reserved subtree only.** Register private table and backing
   grants through current v5 and graft the private subtree into the active
   broker shadow root. Private data uses exclusive owner/generation grants;
   current `Graph.SharedBackingGeneration` for VidMm's R64 local pages must
   not automatically make private allocations shared. The graph currently
   selects shared grants whenever that field is nonzero: explicit backing
   ownership is required, not temporarily toggling a process-wide field.
   VidMm clears its reserved entries when their parent table becomes resident;
   the KMD must restore its own links at that lifecycle boundary before job
   admission. Ordinary updates must never write into private tables. Root
   switch, invalidation, re-residency and R135 root-page reuse all need combined
   coverage; attach to the actual current root, not only the bootstrap root.
   Retiring a VidMm root must detach private links safely without losing live
   private data or bypassing nonempty-root/job/lease checks. This lifecycle
   rule follows the [reserved-entry contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_reservegpuvirtualaddressrange).
5. **Versioned UMD handoff.** Add a narrowly typed production escape:
   acquire/prepare/release, dimensions/utile/layers and context identity in;
   opaque manager/scene ID, generation, nine VAs and extents out. Validate
   `hKmdProcessHandle`, `hDevice` and `hContext` against actual attached
   objects; a user-supplied PID or pointer is not authorization. All length and
   reserved fields are checked, input is copied before use. The existing
   qualification-only `AdmissionDdiEscape` needs a production path in both
   profiles. The [escape DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_escape)
   supplies kernel identities and a bounded shared payload; it does not supply
   this proposed private protocol automatically.
6. **Bind each submission.** Version AGX4 for the owner/scene generation;
   UMD uses returned ranges in the native builder envelope and removes these
   nine from ordinary Allocate/Lock/MakeResident BO references. KMD compares
   them against its own lease, geometry-derived capacity, rights and current
   mapping generation, then performs the normal graph checks at submit and
   again at BeginJob. A stale token cannot access reused bytes; a valid VA
   alone cannot select another context's scratch. The ordinary resources and
   CPU command envelope retain their existing checks. No general user-write
   escape, CPU mapping, arbitrary IPA, executable permission or access to
   context0 work objects is exposed.
7. **Retire and recover.** Queue acceptance holds the scene/manager; release
   is only a retirement request until exact TA/3D completion and render fence.
   Process exit/cancellation first stops new work, drains or quarantines
   in-flight access, unlinks leaves, completes TLB invalidation, revokes grants,
   then zeroes and returns storage. DestroyContext drops its manager reference;
   DestroyProcess releases remaining private tables and state only after all
   contexts/leases are gone. A broker timeout/uncertain revoke poisons and
   quarantines storage until a proven reset; never hand it to a second owner.
   No new power or TDR-success claim is introduced. OOM/VA-reservation failure
   before publication rolls back transactionally and returns a normal allocation
   failure, not success with absent leaves or a fatal paging surprise.

Files implicated by a future implementation are exactly the producer/header,
UMD screen transport, callbacks, GPUVA process/private structures, paging/graph,
memory runtime/segment views and G4 parser/builder listed in section 1, plus
`render-admission/src/gpuva_g3_private.h` and the versioned private ABI
header. Tests should live beside the existing G3/G4 replay families. Mu and
m1n1 need verification against their current interfaces, not changes. If existing
v5 cannot meet exclusive ownership or lifecycle requirements, stop for a new
source-based decision; G2 edits are not authorized by this document.

**Option 3 — not selected.** Large-page hints already failed to establish
representability; page-size input fields are not a KMD output control. User
preallocation/ExistingSysMem would require another supported allocation,
locking and migration contract and does not remove all nine initialization
obligations. No inspected source supplies a simpler proven route. General
4-KiB scatter repacking would also require copy/coherency semantics far beyond
this process-scratch boundary. No third historical comparison pass is useful.

## 4. Falsifiable gates, validation limits and review dispositions

**WHY THIS HYPOTHESIS:**

1. EXP854B directly records nonrepresentable backing for our own page-list BO,
   despite the size/VA policy; moving ownership of that backing addresses the
   measured failure more directly than repeating allocation hints.
2. Asahi constructs these buffers in the kernel and the inspected Windows
   producer needs only zero/list initialization for them, so KMD can own their
   CPU writes without an application-facing CPU map.
3. Pinned WDK and Microsoft's reservation callback explicitly support private
   KMD tables in a disjoint process VA range; R64 already supplies validated
   contiguous RAM. The remaining issue is bounded ownership/lifetime, not a
   guessed register sequence or larger firmware reserve.

Proposed offline RED→GREEN gates (not implemented or run in this task):

| Gate and existing test family | RED discriminator on current implementation | GREEN acceptance after implementation |
|---|---|---|
| WDK callback, `test_gpuva_g3_paging_bootstrap.py` / G3 Windows replay | CreateProcess never reserves a driver VA subtree | Actual callback receives hDxgkProcess, 32-MiB coverage/alignment, no user mapping; failure cleans up; no callback during later context creation |
| R64 partition, `test_g3_kmd_local_reserve.py` and memory tests | All first 56 MiB remain advertised to VidMm; no exclusive pool | All views agree on 40/16/8 split, backend stays at +56 MiB, boundary/overflow/overlap rejected, no page can belong to two allocators |
| CPU initialization, `test_g4_mesa_process_buffers_replay.py` | Fake runtime that rejects CPU Lock/ordinary Process BO creation fails the old producer | Real new UMD/KMD handoff constructs all nine; poisoned allocation contents become zero including padding; list words follow returned heap VA; no ordinary Allocate/Lock for these ranges |
| Reserved table lifecycle, `g3_r135_root_reuse_cases.c` / G3 graph replay | No private subtree survives parent InitialUpdate/root switch | Real outer DDI plus real v5 broker retains correct private ownership across 4K/64K formats, root parking/reuse and re-residency; old root/generation cannot BeginJob |
| Security and pool pressure, G3 graph/broker tests | No process-private pool quota/lease admission exists | Two owners isolated; private-grant sharing, forged/stale lease, UMD mapping collision, table/data overlap, wraparound and third-scene overbudget refused; allocation failure injected at every construction step leaves no leak |
| End-to-end G4, `test_g4_submit_virtual_replay.py`, `test_g4_real_broker_builder_replay.py` | Saved fragmented Process[0] trace fails native access | Real parser/builder/BeginJob sees nine owned native ranges, preserves CPU-envelope rules and refuses unrelated fragmented resources; cancellation/fence/teardown cannot free live backing |

Use trace values only as regression inputs. Include varied valid dimensions,
utile choices, reused/parallel contexts and sizes around quota/alignment edges;
no equality gate on the recorded VA, 64-KiB first access or DWM bind signature.
Run affected tests first, pinned ARM64 compile/link for the new callback/escape
ABI, and the full host suite once before an implementation commit. Record
failures honestly; fake callback success cannot prove Windows admission.

**Executed here:** a temporary host C program included the unmodified shared
header and called the real `AppleAgxG4ProcessRequiredBytes` for 2560×1600,
one layer, utiles16/32. Built with `clang -x c -std=c11 -Wall -Wextra -Werror
-I drivers/apple-agx/shared/include`; both returned success. Results:

```text
utile16: 65536 65536 4194304 131072 131072 65536 1310720 65536 65536
         total=6094848 manager=4325376 scene=1769472
utile32: 65536 65536 4194304 131072 65536 65536 327680 65536 65536
         total=5046272 manager=4325376 scene=720896
```

Header SHA-256:
`0981e4a0892112f8623d06f18b2b092c834c5f248c3a4549f4671382aa13c9df`.
This checks budget arithmetic only; it is not a new backing-path test or a
RED→GREEN claim. No full suite/build is warranted for this document-only change.

**Smallest later hardware checkpoint, not authorized here:** after all offline
and package gates, one separately approved, hash-bound cold R110 full-owner
candidate changing only this process-backing contract (including its truthful
capacity partition), with existing caps bits, firmware, signer and recovery.
A bounded native-render client acquires one scene through the same production
handoff. Before GPU submission, evidence must show callback reservation success,
actual root attachment, exact private ranges/owner generations, R64-relative
physical bounds and all nine native graph accesses. Then submit one ordinary
native render and capture the first G4 outcome plus completion/fence if reached.
Passing the nine Process access checks is the backing checkpoint; a later
resource rejection is a distinct boundary, not proof of rendering. Completion
and visible DWM remain separate claims. CPU zero/read checks alone are not GPU
execution proof. Releasing the scene must make its old token unusable.

Failure: reservation/admission refusal, private mapping loss, owner mismatch,
new paging bugcheck, no bounded SSH or no evidence of the nine checks. Record
actual refusal rather than trying another hint in the same run. Capture raw and
decoded receipts, root/PTE ownership, pool accounting, ETL, events and dump if
present before exact package cleanup. Use the already documented evidence-first
Code0 recovery route (ordered restart, emergency hidden cleanup when required,
then ordinary EXP377/392), ending at one inert APPL0002 Code28 and no experiment
package/service/files/signer. Preserve immutable recovery hashes. Preregister
BEFORE and append AFTER with the real result; no GO or launch command is issued
by this analysis.

Tandem OPEN-item dispositions (review file is in the main repository's `.local`;
its statuses are not edited here):

REVIEW R113: ACCEPT — armed candidates require the established cold full-owner route; no hardware here.
REVIEW R111: ACCEPT — reuse Normal/WC R64 mapping and preserve cache-type consistency.
REVIEW R110: ACCEPT — retain measured 64-bit ACPI resource reconstruction.
REVIEW R109: REJECT — retain recorded firmware hypothesis rejection; no new hole-punch theory.
REVIEW R108: DEFER — historical firmware build/selector analysis cannot resolve this backing boundary.
REVIEW R107: DEFER — old intermittent 0x101 comparison is outside this offline decision.
REVIEW R106: ACCEPT — durable transitions and firmware-owned reserve remain required.
REVIEW R105: ACCEPT — use the valid EXP836 matrix, narrowly scoped to its CPU-visible rows.
REVIEW R104: REJECT — page-size inputs overlay Alignment and cannot be rewritten as a force-backing output.
REVIEW R103: DEFER — no live matrix; saved CPU-visible results do not measure the GPU-only class.
REVIEW R102: ACCEPT — exact internal VidMm rejecting predicate remains unproven; do not invent one.
REVIEW R100: REJECT — preserve prior rejection; existing rounding does not repair physical scatter.
REVIEW R99: DEFER — old CreateDevice callback refusal is past the current boundary.
REVIEW R98: ACCEPT — budget and proposed replay cover full primary geometry without whitelists.
REVIEW R97: ACCEPT — derive initialization from current Asahi source; no template-byte archaeology.
REVIEW R96: ACCEPT — retain current TA/3D builder and its explicit capability limits.
REVIEW R95: ACCEPT — preserve graph, residency, fence and ownership validation for ordinary resources.
REVIEW R94: REJECT — blanket prohibition on KMD process mappings is contradicted by DxgkCbReserveGpuVirtualAddressRange; select its explicit disjoint-subtree exception and kernel initialization.
REVIEW R91: DEFER — previous panel/primary observation does not establish the present backing contract.
REVIEW R90: DEFER — retain previous physical display evidence without a new panel claim.
REVIEW R88: ACCEPT — distinguish mapping, native completion and displayed frame checkpoints.
REVIEW R86: ACCEPT — count grants and enforce capacity; no silent array increase or new G2 work.
REVIEW R85: ACCEPT — future package must bind source/profile/ABI and artifact hashes.
REVIEW R74: DEFER — ordinary system/CDD DMA contract is not changed by private process scratch.
REVIEW R71: REJECT — reject a guaranteed-system64 backing inference; EXP854B disproves it for this path and newer fields absent from pinned WDK are unusable.
REVIEW R69: ACCEPT — no live bind experiment; cold-start/recovery remains separately gated.
REVIEW R65: ACCEPT — version the handoff and preserve context/root/fence validation; compute is not opened.
REVIEW R64: ACCEPT — both entries: retain R64 firmware ownership and explicit G4 ABI; this offline task does not authorize prior hardware permissions to be exercised.
REVIEW R63: ACCEPT — use existing carveout; no large contiguous StartDevice allocation fallback.
REVIEW R57: ACCEPT — preserve bounds and fail-closed paging; private-root reattachment must not hide real conflicts or acknowledge incomplete updates.
REVIEW R55: DEFER — general repeat-StartDevice recovery remains a separate issue.
REVIEW R54: DEFER — no series or package retention starts here.
REVIEW R49: ACCEPT — pinned ABI and logical/native table coverage are explicit gates.
REVIEW R48: ACCEPT — no USB/proxy ports opened; launcher exclusion unchanged.
REVIEW R47: ACCEPT — preserve frozen profile/ABI identification; no firmware build here.
REVIEW R45: ACCEPT — revoke/unlink/table/root teardown order is required in real-broker replay.
REVIEW R40: DEFER — EL2 timing does not explain the recorded synchronous missing native leaf.
REVIEW R37: DEFER — old disarmed-start losses are outside this decision; recovery remains unchanged.


## 5. R137 implementation stop — missing retirement owner in the exact scope

2026-09-27. This is an appended correction to the proposed implementation
contract above, not a reinterpretation of EXP854B or a rejection of option 2.
User task NEXT_TASK_R137.md requires stopping and documenting a design problem
instead of improvising. **Stop after the committed steps 1–3 prerequisites.**
The private backing path is incomplete and EXP855 is not ready.

Inspection while preparing the combined lifecycle gate found a conflict between
step 7 and the exact implementation file list in section 3:

- `render-admission/src/backend_platform_windows.c:2384` calls
  `AdmissionGpuvaG3CompleteJob` before the completion transaction and Windows
  notification. The G3 function ends the v5 job and advances LastCompletedFence;
  that is a broker/hardware retirement observation, not a reported render fence.
- The same backend clears `CompletionContext->Object.FenceOutstanding` at
  line 2502, before DxgkCbSynchronizeExecution and the successful Reported phase
  around lines 2540–2554. Notification can still fail. QueryCurrentFence reads
  the scheduler's CompletedFence, which also advances before notification.
- `scheduler_windows.c:326,449` and `submission_windows.c:281` clear an
  outstanding fence on preemption/reset/prepared-cancellation paths too. Zero
  is therefore not evidence of exact native completion and fence notification.

These completion/cancellation/reset owners are outside the decision's literal
"exactly" list. The planned GPUVA, callbacks and graph interfaces provide no
existing scene-generation-bound post-notification retirement event. Using their
earlier fields would violate step 7; trusting an UMD release claim would not
repair it. Retaining every scene forever would avoid premature reuse but would
not implement the bounded reusable-storage contract. This is a missing owner
interface in this plan, not evidence that v5 exclusive grants are insufficient.

**Required design revision before continuing:** explicitly include the existing
backend completion, scheduler preemption/reset and prepared-cancellation owners.
Specify a submission identity tied to process/context, manager/scene generation
and exact fence. Distinguish TA/3D completion, successful OS notification,
proven never-started cancellation and uncertain access requiring quarantine.
Define deferred reclamation for elevated-IRQL notifications; do not acquire the
passive G3 mutex from an interrupt/spin-lock callback. Add a real-code failure
replay where notification fails after broker EndJob and local fence clear:
private bytes must remain charged and unavailable to a second owner. Also test
notification retry, queued cancellation, reset uncertainty and destruction.
This paragraph describes the missing contract; it does not silently authorize
or implement a new architecture. No Mu/m1n1 change is indicated by the evidence.

Committed prerequisites: `0658e80f` (40/16/8-MiB partition), `a4f918d7`
(reservation and quotas, plus pinned-WDK R135 BOOLEAN normalization), `fe9ce325`
(kernel construction primitives). Their CHANGES rows are implemented only.
The first two change reachable behavior (segment capacity and CreateProcess
reservation prerequisite); they are not an inert or deployable placeholder.
The step-3 producer/handoff acceptance gate is still pending: production UMD
continues to create/Lock its ordinary nine BOs. No full fix is claimed.

For this R137 correction only, .local evidence paths refer to the worktree
/Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler,
not the main repository paths used by the original analysis.

The uncommitted step-4 prototype passed 19 G3 replay tests and ARM64 compile/link,
but independent read-only review found a transactional defect: detaching the
current private root before checking an ordinary bootstrap mapping can lose the
private mapping on rejection. The conflict replay checked root identity only.
**The entire step-4 diff was withdrawn, not fixed or accepted.** Its diagnostic
patch is `.local/experiments/R137-offline/step4-withdrawn.patch`, SHA256
`13ad61ce02659553f27b6e28f1d9cef3289a490404af763bb75cea2665b70cdc`.
Future replay must assert retained private access on every rejected reuse path,
including the bootstrap conflict and allocation failures, before reusing code.

Independent review found no additional must-fix issue in the currently reachable
committed prerequisites. It did not independently rerun tests/builds or validate
Windows acceptance, DWM capacity, production escape/security, complete private
publication/teardown or hardware. Execution evidence and exact full-suite
failure names are in `R137-process-backing-implementation.md` and its local logs.
No Air, package, install, preregistration or GO_EXP855. The saved ordinary
EXP377/392 Code28 recovery remains the last accepted recovery contract.
