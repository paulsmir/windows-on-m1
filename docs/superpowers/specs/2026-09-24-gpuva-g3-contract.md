# G3 offline contract: VidMm GPUVA to AGX UAT

Status: offline contract and implementation gates. EXP767 proves two diagnostic
process-root TA/3D jobs and lease/job/TLB retirement. EXP768 did not reach a
new StartDevice receipt; it adds no cleanup/context0 hardware verdict. No
GpuMmu-capable package is approved for Air.

G3a implementation checkpoint: the pure translator and its Windows KMD project
wiring compile on pinned WDK26100 ARM64 (package build 766, zero warnings and
errors; `.local/tandem/g3a-build.log` SHA256
`d5e9d2f60e2c069bfcd13d74002819713b13715ba459c6ac280c38448fcf4655`).
Host ASan/UBSan group tests pass. The 1016-test suite has the same 251 failing
names as `.local/tandem/test-baseline.txt`, no new failures; log SHA256
`6de7fc4e5209e6e966562cd4f78860e99d533f44484d8a337169eea6a95dbdae`.
No G3 caps or DDI is enabled by this checkpoint: the missing VidMm IPA resolver
and independent EL2 flush are requirements before truthful advertisement.

## Sources and observed contract

- Hardware: EXP767 `state.json` and 18 retirement receipts; EXP768 full-owner
  host/KD logs and zero B1 receipts in GPU-hidden registry. Live J313 full-owner
  contract has one APPL0002, synthetic scanout interrupt and a v5 broker window;
  no new register/IRQ/memory value is inferred from EXP768.
- Asahi `drivers/gpu/drm/asahi/{mmu,pgtable}.rs`: 39-bit AGX input VA,
  16-KiB pages, three levels with 3/11/11 indices, user slots bound with
  reference-counted users, and release after last user. This is behavioral
  evidence only; no Asahi code is copied.
- m1n1 `src/hv_agx_gpuva_v5.c`, `hv_agx_retained_root.c`,
  `hv_agx_retained_platform.c`: EL2 owns context0, process roots, slot TTBRs,
  shared grants, table/leaf publication, TLB invalidation and recovery.
  v5 `UPDATE_LEAF` accepts four logical IPAs only when they form one aligned,
  contiguous 16-KiB AGX leaf. `RELEASE` invalidates a slot; v5 has no
  independent FLUSH_TLB command.
- Mu `J313AppleAgxAbiAdmission.asl.inc`: exposes resources and the APPL0002
  devnode, but owns no page table, DMA, interrupt runtime or recovery.
- Current KMD `lifecycle.c`, `callbacks.c`, `paging_windows.c`,
  `memory_runtime_windows.c`, `gpuva_b1_windows.c`, shared B1 roots/v5 client:
  WDDM 3.0 physical mode is active. B1 owns two fixed diagnostic roots and
  test pages, outside VidMm. The 64-MiB local object has a CPU/IPA/PA view;
  VidMm page-table allocations are not yet tied to that owner.
- Pinned WDK26100 `shared/d3dkmddi.h` SHA256
  `c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`
  and `shared/d3dukmdt.h` SHA256
  `d1c43d619589c2eb3f47877bba6d48735c7f26644fed44a5bb682f9e591eb33f`.
  Microsoft primary documentation:
  [GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
  [GPU virtual address](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-address),
  [GPUMMUCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps),
  [page-table levels](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_page_table_level_desc),
  [UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable),
  [FlushTlb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_flushtlb),
  [DXGK_PTE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_dxgk_pte),
  [CreateProcess](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_createprocess).

## WINDOWS CONTRACT

| Boundary | Pinned WDK and Microsoft meaning | Owner and gate |
|---|---|---|
| `DXGK_VIDMMCAPS` | `VirtualAddressingSupported=1` and `GpuMmuSupported=1` opt in. `DXGKQAITYPE_GPUMMUCAPS` supplies update mode, VA bits and level count. | KMD advertises only after every required DDI and bootstrap path is live; physical B1 profile stays zero. |
| `PAGETABLELEVELDESC` | Level 0 is leaf. Each logical entry covers 4 KiB; `PageTableSegmentId` and `PagingProcessPageTableSegmentId` must name truthful storage. System-memory tables cannot exceed 4 KiB. | Candidate 3-level projection: leaf 13 logical index bits, middle 11, root 3, all actual AGX tables 16 KiB in local segment. Whether VidMm accepts four logical entries per native PTE in this descriptor is unresolved. |
| `CreateProcess/DestroyProcess` | `DXGKARG_CREATEPROCESS.hKmdProcess` is KMD output; `DXGKARG_CREATEDEVICE.hKmdProcess` carries it to device creation. Destroy receives that handle. | KMD owns process object, generation, broker graph and outstanding context/lease counts; destroy fails closed until no job/slot/mapping remains. |
| `SetRootPageTable` | VOID callback receives `hContext`, physical root address and entry count at PASSIVE_LEVEL before context execution. | KMD associates context through its device/process and can only bind a root already registered with EL2; failure must poison later submission because this DDI cannot return NTSTATUS. |
| `BuildPagingBuffer UPDATE_PAGE_TABLE` | Level, process handle, table address/update mode, logical `DXGK_PTE[]`, start/count, allocation offset and first PTE VA. Paging-process initial updates always use CPU_VIRTUAL mode with NULL DMA buffer and must execute immediately. `AllocationOffsetInBytes` guarantees sequential allocation pages, not physical contiguity. | KMD resolves table/backing to guest IPA, validates every four 4-KiB PTEs as one 16-KiB native leaf, calls shared v5 table/grant/parent/leaf operations and records first refusal. Unsupported partial/scattered groups fail closed; no silent rounding. |
| `BuildPagingBuffer FLUSH_TLB` | Process/root and affected VA range; zero/zero means whole address space. | EL2 owns actual ASID invalidation. Existing v5 update and release invalidations are insufficient for an independent flush request until a source-backed v5 flush operation or equivalent proof exists. |
| `SubmitCommandVirtual` | `hContext`, DMA GPUVA/size, private data, flags and `SubmissionFenceId`; completion fence follows real hardware completion. | KMD validates process root/slot and residency, leases the slot, uses shared JOB_BEGIN/END/RELEASE and existing TA/3D/fence/DPC paths. Diagnostic B1 command bytes are not a general UMD submission format. |

## TRANSLATION and offline gates

1. **G3a, pure shared translator:** a 4-KiB logical PTE group represents one
   AGX leaf iff all four are invalid, or all four are valid with the same
   segment/protection, an aligned first IPA, and contiguous 4-KiB addresses.
   The input must already have a validated guest IPA; `DXGK_PTE.PageAddress`
   alone is insufficient. The current v5 wire carries only validity and write
   permission, so other DXGK PTE attributes require a separate source-backed
   mapping or an explicit refusal. Reject mixed validity, scattered PFNs,
   differing protection, overflow,
   unaligned start/VA and range crossing. Test groups at table and VA
   boundaries, partial updates, eviction and 16/64-KiB segment profiles.
   This can use the existing B1 v5 logical-IPA wire and host broker tests.
2. **G3b, Windows lifecycle:** process/device/context ownership and a truthful
   caps response compiled against pinned WDK26100. `gpuva_g1b_profile.h` is
   the single 16/64-KiB page selection; 16 KiB remains experimental, 64 KiB
   is the fallback. Keep GpuMmu bits zero in B1/production while the full
   paging/submit contract is incomplete.
3. **G3c, page-table/submit integration:** resolve VidMm local/system pages
   to guest IPAs, handle paging-process CPU_VIRTUAL bootstrap, and implement
   explicit EL2 flush plus root/slot/job ownership. Reuse B1 client and
   broker verbs, not B1's fixed two-root graph. Compile and host-test all
   callback paths before a separate G3 hardware candidate.

Initialization and recovery ownership: Mu describes the device; m1n1 creates
and protects context0 and power/slot state; KMD creates per-process table
owners, translates VidMm updates, tracks jobs and fences, and requests EL2
mapping/TLB work; VidMm owns GPUVA allocation, paging, residency and eviction.
DART/physical memory backing remains under m1n1 stage-2 and Windows physical
owner validation. Interrupt and DPC completion remain with the existing KMD
platform runtime. A failed v5 exchange preserves all potentially referenced
pages and marks the process uncertain until reset/recovery.

## WHAT IS STILL UNKNOWN

- Whether pinned Windows 26100 accepts the proposed 3/11/13 logical
  `PAGETABLELEVELDESC` with 16-KiB actual table storage, and what exact
  `InitialUpdate`, `Repeat`, eviction and cross-boundary inputs it sends.
- Whether selected 16-KiB or 64-KiB local segment slabs prevent all scattered
  4-KiB groups in practice; system-memory 4-KiB pages remain a counterexample
  unless explicitly excluded or copied into contiguous local backing.
- How to recover a guest IPA for paging-process CPU_VIRTUAL tables and every
  VidMm page-table/allocation location; `DXGK_PTE.PageAddress` is a 4-KiB unit,
  not a guest IPA or host PA.
- The independent `FLUSH_TLB` broker verb and failure/ack semantics; the v5
  protocol currently has none. This is a G2 m1n1 change and requires the
  project's explicit permission gate before implementation/hardware use.
- Which existing UMD private command format can be submitted by
  `SubmitCommandVirtual` without the physical patch-list and how that maps to
  current TA/3D completion, Present and TDR paths.

Smallest falsifiable hardware checkpoint after these offline gates: a separate
hash-pinned G3 profile reaches natural StartDevice/Code0 with VidMm issuing one
process root and one bounded UPDATE_PAGE_TABLE group; KMD and broker report
identical IPA/PA/VA/protection and TLB ack. Failure or partial/scattered input
stops before GPU work. Preserve the known-good ordinary Code28 artifact and
exact package rollback. No G3 package is installed in EXP768.

## EXP773 G1b 64-KiB admission discriminator

Sources checked: EXP771/EXP772 armed and recovery entries in
`investigation/EXPERIMENTS.md`; pinned WDK26100 `d3dkmddi.h` and `d3dukmdt.h`;
Microsoft GPU segments, GpuMmu model, `DXGK_GPUMMUCAPS`,
`DXGK_UPDATEPAGETABLEFLAGS`, `DXGK_PTE`, `DXGK_PAGE_TABLE_LEVEL_DESC`, and
`DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE`; the Asahi/m1n1/Mu ownership sources
above; current `apple_agx_physical_topology.c`, `memory_windows.c`,
`lifecycle.c`, and `gpuva_g3_paging_windows.c`. EXP772 proved natural
StartDevice and successful QAI13/14 with 128/32/16-KiB table sizes, followed
by AddAdapter `STATUS_INVALID_PARAMETER`. VidMm documents 4- or 64-KiB memory
segment pages under GpuMmu, while EXP772 advertised 16 KiB. This is a
candidate cause, not an established Windows rejection rule.

The one profile change is G1b local memory pages from 16 to 64 KiB. QUERYSEGMENT4
and QUERYSEGMENT5 already derive local `Use64KBPages` and slab size from that
selection; topology exposes exactly one aperture (segment 1) and one local
segment (segment 2). The three 39-bit page-table levels stay 128/32/16 KiB;
the 64-KiB leaf view has 512 `DXGK_PTE` entries and 8192 bytes. Dual-PTE and
64-KiB system-memory support remain zero. A 64-KiB leaf entry resolves only
inside the contiguous local object and expands into four 16-KiB AGX leaves;
the 4-KiB logical path remains for system memory. Unsupported, scattered or
misaligned input fails before graph mutation. The WDK 26100 `DXGK_PTE` is 16
bytes and its level-1 page-size enum has 4- and 64-KiB values.

Ownership is unchanged: VidMm allocates and updates GPUVA/table storage;
the KMD validates PTEs and process graph updates; m1n1 owns AGX publication,
TLB, power and recovery; Mu exposes APPL0002 and resources. `DRIVERCAPS`
advertises VirtualAddressingSupported/GpuMmuSupported only in armed G3, one
execution node makes paging node 0 valid, and QAI13/14 use local segment 2.
QUERYMMUCOUNT/QUERYMMUS still report zero/invalid; the available Microsoft
documentation does not establish whether that WDDM 3.2 topology is an
AddAdapter gate, so this discriminator leaves it unchanged.

Smallest hardware checkpoint: one debug-off/no-KD EXP773 boot with the frozen
EXP772 full-owner m1n1/Mu contract and only the hash-verified package773
profile changed. Observe first QAI/VidMm receipts and AddAdapter. A Code0
result opens G4 offline Mesa winsys work; another invalid-parameter result
rejects the 16-KiB-only hypothesis and retains QUERYMMUCOUNT, other segment
fields, and PnP state as separate candidates. Export GraphicsDrivers and
AppleAgx service registry without changing boot. WPR boot trace is omitted:
its boot instrumentation would be an additional experimental variable. Disarm
and remove the exact package via the proven GPU-hidden recovery image, then
verify ordinary GPU-visible Code28. No m1n1 or Mu modification is part of
EXP773.
