# Windows GpuMmu over retained-root AGX — design only

## Verdict and scope

GPUVA_MIGRATION_FEASIBLE: CONDITIONAL. Existing ABI-v4 broker is insufficient.
The single retained root plus one shared Windows subtree is NOT a per-process
address space. A source-backed alternative is separate process TTBR0 roots,
selected by authenticated firmware/UAT context, while firmware context0 retains
its original TTBR1 identity. This is a design hypothesis, not hardware proof or
permission to enable GpuMmu. No production/caps/DDIs/firmware/hardware changed.

Scope is FULL GRAPHICS, WDDM3.0 with pinned WDK26100; no KMDOD assumptions,
hardware scheduling requirement, feature-level increase or GPU-PV interface
claim. Existing compiler PASS and hardware evidence remain unchanged.

## Sources and exact versions

- Integration input05954bcc52cf419d8f95be60b25ae902bf1ed27e.
- Pinned WDK: FRYZZING Windows Kits10/Include/10.0.26100.0/shared/d3dkmddi.h,
  SHA256 c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e.
  Reviewed context caps1556, page-table queries1821/2097, GPU-MMU caps2114,
  physical-adapter flags2168, VidMm flags2269, paging operations4604–4615,
  BuildPagingBuffer5080 and SetRootPageTable5114–5131. UMD callbacks from the
  same SDK um/d3dumddi.h4535–4551. Installed NuGet WDK28000 is NOT the authority.
- Mesa9aa1215f878b504f66159dd2ead4c7973142126e: asahi/lib/agx_bo.h,
  agx_device.h/c; gallium/drivers/asahi/agx_state.c, agx_uniforms.c, agx_batch.c.
- Asahi Linux77cb8f24c2381a8abb7272d7bbdec548d6426a8a: asahi/mmu.rs92–120,
  1470–1539,1580–1645; pgtable.rs39–63; gpu.rs665–669; queue/render.rs VM binding.
- m1n1 c6d10e04afdad5314e8ac1e67bc3919b094ab000 (matches gitlink):
  hv_agx_retained_root.c138–174,205–282; proxyclient/m1n1/hw/uat.py245,560;
  proxyclient/m1n1/agx/render.py492,962. Native reference bind_context shares
  TTBR1; this is not accepted as isolation proof for untrusted Windows shaders.
- Mu f1ef718e08db0e4c30fdb5d8555973513ad9a004 (matches gitlink): J313 AGX SSDT
  wrappers expose reviewed resources; they do not implement process GPUVA.
- Current shared retained-root ABI/client, context0 broker, UAT publication,
  render provider context63, and dynamic allocation-list address resolver.

Primary Microsoft references:

1. [GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model)
2. [Per-process spaces](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/per-process-gpu-virtual-address-spaces)
3. [GPUVA and table hierarchy](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-address)
4. [Page-table level description](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_page_table_level_desc)
5. [UpdatePageTable including16KiB GPU pages](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable)
6. [GpuMmu caps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps)
7. [Paging operation ordering](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/examples)
8. [Driver residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/driver-residency-in-wddm-2-0)
9. [Virtual submission](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual)
10. [64KiB page transitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages)

## Required contract inventory — not an enablement patch

| Area | Required for selected GpuMmu mode | Conditional or retained obligations |
|---|---|---|
| Adapter capabilities | DRIVERCAPS.MemoryManagementCaps VirtualAddressingSupported + GpuMmuSupported; coherent PHYSICALADAPTERCAPS GpuMmuSupported, paging node and page-table placement | IoMmu is not the selected model; keep unsupported bits zero; WDDM3.0 version alone is insufficient |
| MMU description | QueryAdapterInfo GPUMMUCAPS and PAGETABLELEVELDESC for every level/physical adapter; VA width, level count, actual table sizes/alignment/segments and update mode | RO/NX/zero/coherency/large pages/dual PTE/idle-before-update/explicit invalidation must reflect implementation; CachedPageTables is3.1, not this3.0 design |
| Process | CreateProcess/DestroyProcess with unique generation-tagged KMD object, system paging process distinguished from application process; CreateDevice associates owner | Privileged companion address space for tiled-resource paths only if supported; not firmware context0 |
| Context | CreateContext/DestroyContext, hKmdProcess ownership, NoPatchingRequired and driver-managed residency contract, correct paging companion association | Context/root identity retained independent of temporary UAT slot; do not just zero patch-list sizes |
| Root | SetRootPageTable stores context's current VidMm root address and entry count; honor relocation/replacement | GetRootPageTableSize is required only for two-level resizable-root model; proposed native hierarchy has three levels |
| Paging/PTE | BuildPagingBuffer UPDATE_PAGE_TABLE, FLUSH_TLB; initialization, protection, invalidate, partial writes and root/child table transitions | COPY_PAGE_TABLE_ENTRIES, UPDATE_CONTEXT_ALLOCATION, NOTIFY_RESIDENCY and virtual transfer/fill according to selected caps/paths; not a universal no-op success |
| Execution | SubmitCommandVirtual restores the correct process root/slot before dispatch; validate DMA GPUVA/private data and exact submission fence | Retain completion/DPC, context monitoring, fault attribution, preemption/reset contracts; no claim current implementation already meets the virtual-mode variants |
| UMD mapping | Allocate/Deallocate + MapGpuVirtualAddressCb/FreeGpuVirtualAddressCb; process-scoped VA reservation managed through VidMm | ReserveGpuVirtualAddressCb for constrained arenas; UpdateGpuVirtualAddressCb/tile companion if that feature is supported; never native Asahi independently allocating conflicting process VA |
| UMD residency/sync | MakeResidentCb/EvictCb, budget/trim and allocation-usage tracking; synchronize paging fences and in-flight rendering | Paging queue lifetime and monitored synchronization objects/CPU or GPU waits use supported runtime services; no private resident boolean |
| UMD submit | SubmitCommandCb rather than assuming legacy RenderCb semantics establish GPUVA submission | CPU map/unmap, rename, destruction, sharing and device removal retain their independent lifetime contracts |

This is the migration-specific dependency closure. All already-required full
graphics DDIs remain mandatory; optional feature paths are not silently enabled.
Final callback/cap values await the proofs below, not guessed bit settings.

WDK annotations: SetRootPageTable is VOID/PASSIVE_LEVEL; BuildPagingBuffer is
PASSIVE_LEVEL. A root callback is not necessarily a context-switch event: retain
its state and restore it at submission. Do not retrofit an arbitrary NTSTATUS
return into SetRootPageTable. SubmitCommandVirtual has its documented success/
invalid-parameter/device-error behavior; arbitrary errors there can bugcheck.

## Ownership matrix

| Object / authority | VidMm | KMD | Broker / EL2 | Firmware | UMD / native Asahi |
|---|---|---|---|---|---|
| Process VA reservations | Authoritative allocator/mapper | Tracks process and root generation | Enforces granted process namespace | None | Requests address/range via VidMm, stores returned VA |
| Process page-table allocations | Allocates, grows/moves, makes resident, updates and frees | Translates hardware layout; registers implicit tables, including hAllocation=NULL updates | Validates Windows backing; writes/publishes translated PTEs and root selection | Uses selected context, no ownership transfer | No page-table/PA access |
| Allocation backing/residency | Authoritative placement, eviction and residency services | Tracks valid ownership and required transitions | Guest-to-host translation/range exclusions only; cannot override eviction | Uses backing while authorized work is active | Usage/lifetime and residency obligations, not ownership of physical placement |
| Firmware retained root/private tables | Never adopts, moves or frees | Never exposes to UMD or describes as a process table | Preserves exact identity/prefix; only approved owned mutations | Retained allocation and private contents | No read/write mapping |
| Crashlog/system firmware mappings | Not a process allocation | Receives only needed protocol handles | Holds designated system backing/mappings and cleanup | Firmware protocol owner | None |
| UAT slots/ASIDs and TLB | Supplies process/root operations, not slot numbers | Process/context/fence association | Slot lease, publication, barriers, invalidation, rollback | Work commands refer to correct slot | No raw slot/table-control authority |
| Render/page-fence retirement | Schedules/coordinates Windows fence domains | Reports actual completion and teardown | Retains in-flight mapping/root grants until safe retirement | Actual execution/completion | May reuse/free only under the appropriate contract |

Broker must NOT take over VidMm's allocation/page-table lifetime policy. The
preferred process tables are the actual VidMm-owned backing translated into UAT
format. A separate authoritative shadow allocator is not assumed. If direct
representation fails, shadowing requires a separate, explicit proof of every
VidMm update/relocation/lifetime operation; it is not an accepted shortcut.

## VA topology and root compatibility

Native source:39-bit low VA,16KiB GPU pages, three hardware table levels; the
address decomposition is3 root bits +11 +11 +14 page offset.64 UAT slots exist,
slot0 is firmware/kernel; current Windows production has fixed render context63.

Proposed semantic topology (not new literal addresses/caps):

    firmware context0: existing lower firmware mappings + TTBR1 = R_fw
                      R_fw identity/private prefix unchanged for firmware life

    Windows paging process S: TTBR0 = R_S (VidMm-owned)
    Windows process P:        TTBR0 = R_P (VidMm-owned)
    Windows process Q:        TTBR0 = R_Q (VidMm-owned)
                             R_P != R_Q; VA v may map different backing
    broker-selected slot -> corresponding TTBR0/root-generation
                         -> authorized firmware work's context selector

Native lower application span is [0,2^39), with null/guards and any shader/USC
arena constraints reserved through VidMm, not an independent Asahi VMA allocator.
Native firmware upper half starts at0xffffff8000000000; broker class bands are
firmware/kernel mappings, NOT per-process user heaps. They are excluded from the
Windows advertised application range.

For G13, pinned Asahi gpu.rs sets map_kernel_to_user=false; its user slots have
TTBR1=0 while context0 retains the kernel root. This is the preferred isolation
reference. Sharing R_fw in user slots, as native m1n1 debugging does, is not
automatically safe. If some required firmware access needs a shared upper root,
GPU privilege/protection must reject user shader access independently of whether
UMD can read page-table bytes. No-PA-leakage alone is not isolation.

Rejected: partition P and Q into different ranges under one undifferentiated root.
It cannot honor the same numeric VA in independent processes, nor stop arbitrary
shader pointers reaching another process. Swapping a shared subtree would itself
require fully serialized, acknowledged per-process root selection and TLB rules;
separate lower roots are the source-backed option.

SetRootPageTable identifies R_P, never R_fw. VidMm relocation of R_P updates the
broker's translated root identity/version; it does not replace the retained
firmware root. Root0/63/current helper behavior is not edited in this stage.
Broker v4 has only epoch/VA/IPA/range/handle and narrow context63 publication;
process/table/slot grants and safe slot reuse require a new versioned contract.
Physical slot count must not become a63-process limit: inactive roots persist,
slots are virtualized with outstanding-job references and ASID/TLB retirement.

## Page-table geometry / bootstrap conditions

Microsoft UpdatePageTable explicitly discusses16KiB GPU pages with4KiB logical
entries. Therefore16KiB is NOT by itself evidence of incompatibility. Translate
logical index/count, validity and protections to the proper hardware entry;
prove partial/group-boundary updates and addresses rather than skipping three
entries blindly. Sequential allocation pages are not necessarily physically
contiguous.64KiB-only allocation alignment does not prove generic system-memory,
eviction or tiled4KiB updates safe.

Table-segment placement is a separate constraint: Microsoft limits system-memory
page-table allocation size to4KiB. Native tables are16KiB. A candidate uses a
proper VidMm-managed CPU-accessible GPU memory segment with truthful size/alignment,
not arbitrary system pages hidden from VidMm. Exact PAGETABLELEVELDESC values,
logical-to-physical entry storage and allocation granularity are unresolved;
do not publish numerical caps before proving this representation end-to-end.

For normal updates in a local segment, CPU_VIRTUAL mode is not permitted by the
GpuMmu caps contract. GPU_PHYSICAL update mode is a candidate; KMD/broker translate
the provided segment/table address to validated Windows backing. No unvalidated
guest PA or PTE is accepted as host PA.

Paging-process bootstrap is a required special case: UpdateMode=CPU_VIRTUAL,
pDmaBuffer=NULL requires immediate table initialization. It cannot be deferred
to a paging job which requires the very root being initialized. Broker ordering,
bounded synchronous operation and lifetime must handle this without altering
firmware tables. hAllocation may be NULL for implicit VidMm tables/directories;
absence of a KMD allocation handle is not permission to skip ownership validation.

## Mapping / residency / completion lifecycle

Correct order (mapping fence cannot precede the mapping work it certifies):

1. Create process/context and ordinary allocation; create a new allocation
   identity distinct from any recycled handle. CPU mapping remains independent.
2. UMD requests MapGpuVirtualAddress through VidMm. Returned VA is reserved;
   returned paging fence may still be pending. Binding is NOT usable yet.
3. VidMm supplies PTE updates; KMD translates; broker validates process/table/
   ranges and backing, then applies update + required publication/TLB work.
4. Required paging work completes. Only then can its paging fence release the
   mapping dependency. Immediate bootstrap follows its separate contract above.
5. Independently MakeResident the transitive allocation set and satisfy that
   residency operation's paging dependency; keep residency through execution.
   Mapping and residency work may overlap, but neither proves the other.
6. SubmitCommandVirtual checks process/context/root/allocation generations,
   VA/range/protection, all paging dependencies and valid residency obligation.
   Select/retain the correct UAT slot/root before dispatch; propagate its identity
   through TA/3D/firmware work. Report the exact render fence after real completion.
7. Eviction removes physical residency after required usage synchronization; VA
   reservation can remain. Remap to new physical backing updates PTEs/TLB and
   waits on paging completion; VA and logical allocation identity stay stable.
8. Close/GPU unmap first prevents new binding use; drain all render and other
   consumers; invalidate mapping and complete required TLB retirement before
   releasing backing/tables/address reservation. DestroyProcess cannot recycle
   an in-flight slot/generation. Failure retains ownership, not success fiction.

Let B=live owner/allocation-generation/VA reservation, M=completed mapping and
current root version, R=valid independent residency obligation. Dispatch requires
B AND M AND R plus context identity/range/protection. B does not imply M or R.
An evicted but still reserved mapping is the constructive counterexample to B=>R.
Completed mapping with no render submitted disproves mapping-fence=render-fence.
Same token reused with new allocation generation rejects an old B. CPU unmap only
changes CPU-map state; GPU unmap invalidates B. Rename/discard is a distinct
allocation-lifetime operation, not an innocuous CPU unmap.

These are logical invariants and proof obligations, NOT newly executed tests.
PA/table information stays in VidMm/KMD/broker; exported binding contains only
opaque identities, process/device generation, GPUVA/size and mapping readiness.
Native agx_bo borrows this binding and tracks usage; it does not own Windows
allocation/residency policy. No broker response containing PA/root is forwarded
wholesale into UMD. Native shader accesses to other process/private VA must fault.

## Comparison with physical/patch-list integration

| | GpuMmu migration | Retain physical/patch-list |
|---|---|---|
| Main changes | Process/page-table lifecycle, paging bootstrap, broker process namespaces, root selection, virtual submit, UMD VA/residency, faults/reset | Complete native pointer provenance/relocations and lifetime/residency reconciliation; keep proven Windows scheduling entry mode |
| Native Asahi pointer use | Stable VA fits BO/pools/shader-relative addressing after supported binding | Every address-bearing command/descriptor/uniform/table and affected GPU-generated pointer must be covered |
| Existing evidence reuse | Compiler, firmware bring-up and execution mechanisms remain evidence; virtual process isolation is new | Existing physical Render/Patch/Submit path reused; general native relocation closure is new |
| Primary risk | Fake VidMm ownership or process isolation through a global/shadow root | Untracked pointer remains stale after move, or apparent numeric value is patched as a pointer |

Current fixed template declares207 relocations, not the old159. Windows dynamic
ABI has7 relocation kinds and16 maximum references. Neither describes general
native Asahi. A narrow source scan found35 candidate address-producing/use lines
in agx_state.c and5 in agx_uniforms.c/agx_batch.c; these are lexical observations,
NOT unique pointer counts or an exhaustive audit.

At least shader/USC offsets, texture/image/PBE descriptors, sampler/table bases,
uniform/SSBO/vertex/index pointers, PPP/VDM/CDM links, scratch/heap/tessellation/
indirect buffers and GPU-generated command data need provenance. Runtime count
depends on resources/batches/descriptors; no defensible fixed total follows from
the triangle template. Fixing seven relocation encodings is not a complete port.

## Stop conditions / recommended next design proof

Do not start production implementation until a bounded offline model using the
existing shared UAT functions and pinned WDK structures demonstrates:

- P and Q map identical VA to different backing; switching/root relocation/
  slot reuse cannot expose stale translations or firmware/private tables.
- VidMm table geometry, allocation/movement/free,4KiB logical16KiB physical
  updates, partial invalidation and64KiB/system-memory transitions are representable.
- Paging process bootstraps without self-dependency; no mapping fence is signaled
  before broker publication/TLB completion. No lost progress on rollback/reset.
- Binding/residency/render-fence and CPU/GPU-unmap domains remain separate;
  destruction/recreated handles and stale generations are rejected.
- Firmware WorkCommand/queue context selection uses the same retained process-root
  lease through completion; G13 private upper-half protection is enforceable.

No such executable model was built in this DESIGN ONLY stage. If the geometry,
hardware isolation or firmware-context contract cannot be reconciled, verdict
becomes NO for this migration and work stops. Do not substitute a global VA
partition, guessed stable PA, invisible page-table allocator or blanket caps.
