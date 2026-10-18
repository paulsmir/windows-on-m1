# EXP850R: deterministic Windows-to-AGX system-memory translation

Offline design, 2026-09-27. No Air access, package, firmware build, or product
change. Scope: explain segment-0 translation and specify independently testable
changes; this document does not authorize their implementation or hardware use.

**First answer: the exact failing VA/subsite is not recoverable from receipt v1.**
`0x840000` is the submitted DMA VA, not a recorded failed-access VA. The receipt
does not contain the private packet, process ranges, attachment addresses,
logical PTEs, root identity, access ordinal, or output-resolution subsite.
Consequently segment-0 backing of the *failing* address is not confirmed.
The source proves a general system-memory publication barrier, and the saved
counters prove that segment-0 groups encountered that barrier. They do not join
that observation to the first rejected packet.

Raw evidence is main-repository `.local/experiments/retry-exp850-r130/`, abbreviated
`E/` below; these paths are not worktree-local. `E/hardware-evidence/
Wom1G4SubmitFailure.bin` is 64 bytes, SHA-256
`b1cdb791ea62c2e759994834a3a8db174ae64005ccd06aa61c04e1e1c1136c8a`.
Independent little-endian decode (`struct.unpack('<6IQ8I', raw)`):

| Byte offset | Field | Value / meaning |
|---|---|---|
| 0, 4 | Version, Bytes | 1, 64 |
| 8 | Branch | 9, `AdmissionG4RejectParse` |
| 12 | Status | `0xC000000D`, `STATUS_INVALID_PARAMETER` |
| 16 | DownstreamStatus | 3, `AppleAgxG4ParseUnmapped` |
| 20 | Alignment/padding word | 0; not a diagnostic field |
| 24 | DmaBufferVirtualAddress | `0x840000` (8650752) |
| 32 | DmaBufferSize | `0x118` (280); interval `[0x840000,0x840118)` |
| 36 | PrivateDataSize | `0x51000` (331776), capacity |
| 40 | UmdPrivateDataSize | `0x1c0` (448), actual UMD bytes |
| 44 | Submit flags | 0, nonpaging submission |
| 48 | ContextFlags | 4, virtual addressing |
| 52 | Pid | 4, current submitting thread's PID; not proof that the owning GPU process is PID 4 |
| 56 | TotalFailures | 1706, adapter-wide count sampled when published, not 1706 identical packets |
| 60 | Tail padding word | 0; not a diagnostic field |

`448 - 280 = 168` implies the accepted header was v2: the v1 header is only
24 bytes and would have returned Invalid before any Unmapped result. The exact
check order in `shared/src/apple_agx_g4_submit.c` is: nine v2 process ranges in array
order (write), DMA envelope range (read), each native attachment (write), then
render addresses in this order: VDM stream, vertex helper binary/data, fragment
helper binary/data, scissor, depth bias, occlusion query, depth base/compression,
stencil base/compression, sampler heap, BG/EOT/partial-BG/partial-EOT USC.
Nonzero render addresses are tested for one byte, not their entire transitive
working set. Finally `AdmissionG4ResolveOutput` can change an otherwise successful
parse to the same Unmapped value. If parsing reached the native payload, 280
bytes are consistent with one 8+24-byte attachment block and one 8+240-byte
render block; an earlier process-range failure does not prove these bytes valid.
Thus neither DMA nor attachment can be selected uniquely by parser order.

The task's live count 520920 is a supplied earlier observation. Independently
decoded saved counters are **1343754** in `state.json` (base64 `<32Q>`) and
**1398112** in UTF-16 `devnode.reg`; all other segment counters are zero in both.
These are different collection snapshots, not a contradictory simultaneous
measurement. The counter counts update/group encounters, including repeats,
and combines incomplete, scattered, and ungranted groups. It is neither a unique
page count nor a DWM memory-footprint measurement.

## WINDOWS CONTRACT

Inspected the pinned local WDK 26100 headers under main-repository
`.local/reference/wdk26100/`: `d3dukmdt.h:315-341` (SHA-256
`d1c43d619589c2eb3f47877bba6d48735c7f26644fed44a5bb682f9e591eb33f`) and
`d3dkmddi.h:2081-2143,4680-4709` (SHA-256
`c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`).
Runtime/compile ABI remains the project's pinned WDDM 3.0 contract. Current
Learn shows newer fields in some structures; do not substitute that layout for
the pinned header. No builder access was needed.

`DXGK_PTE.Segment == 0` identifies system memory. Its PageAddress encodes the
upper 52 physical-address bits; reconstruct the byte address with checked
`PageAddress << 12`, as the existing `AppleAgxGpuvaG3PteAddressBytes` does.
Inside Windows this is a guest physical address (IPA), not CPU VA, GPU VA,
local-reserve offset, or a host PA. For local segment 2 the reconstructed value
is a segment-relative byte offset. Do not add the R64 base to segment 0, nor
double-shift when the paging operation already supplies a byte address.
Valid/read-only/coherency/execute attributes are separate from the address.
[Microsoft DXGK_PTE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_dxgk_pte).

VidMm owns GPU VA mappings and page-table lifetime. System memory has a 4-KiB
physical granularity in the baseline GpuMmu model; sizing an allocation to
64 KiB does not itself make its PFNs contiguous. GPU VA and CPU VA are separate.
[GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model).
Making an allocation resident in system memory pins its physical pages, updates
the process PTEs and orders use behind paging completion. A transfer can create
temporary mappings in the paging process in addition to the application's
mapping. A grant must therefore permit legitimate aliases across those owners.
[GpuMmu sequences](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/examples).

Residency belongs to VidMm/UMD, not to the broker. Nonresident GPU access is
illegal. Residency references and paging-fence waits must precede use; a broker
registration is not an OS pin and cannot extend an expired residency promise.
[Driver residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/driver-residency-in-wddm-2-0),
[MakeResident](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_makeresident).
Backing may change through migration, eviction/re-residency, VA remapping and
allocation destruction/reuse. Capture that change at the corresponding ordered
PTE updates and completion boundaries, rather than caching a PFN until process
exit or assuming `Evict` immediately frees a physical page.

`UpdatePageTable.hProcess` identifies the mapping owner; `hAllocation` and
`AllocationOffsetInBytes` describe the mapped allocation but hAllocation can be
NULL for OS-managed objects. Do not impose a non-NULL-allocation admission rule.
`Repeat`, partial updates and `FirstPteVirtualAddress` require complete logical
shadow state across calls.
[UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable).
Current `lifecycle.c:729-735` advertises explicit invalidation, address-space idle
for updates, GPU_PHYSICAL update mode, and 39-bit VA. Retain those contracts.
CPU_VIRTUAL bootstrap updates are immediate; do not invent a later queue-based
completion for them. For other paging operations, preserve the existing
BuildPagingBuffer/submit/fence ordering and prove that no fence can precede
grant publication, invalidation, or necessary copy completion.
[System paging process](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/system-paging-process).

R71 is a candidate, not a proven allocator guarantee: the pinned header contains
`SysMem64KBPageSupported`; the published cap description does not promise that
every internal, imported, or aperture-backed mapping becomes an aligned 64-KiB
run. `OpportunisticSysMem64KBPageSupported` is not a no-fallback guarantee.
Current caps leave these bits clear and `AdmissionG3UpdateLeaf` explicitly
requires local segment 2 for Use64KBPages. Support segment-0 64-KiB decoding and
fallback before proposing a separate cap experiment. A 64-KiB physical page
would yield four AGX leaves; arbitrary 4-KiB mappings would still need a policy.
[GpuMmu caps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps),
[64-KiB page tables](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages).

## AGX/ASAHI CONTRACT

Source basis: Asahi checkout `.local/reference/asahi-linux-asahi`, commit
`77cb8f24c2381a8abb7272d7bbdec548d6426a8a`; inspected
`drivers/gpu/drm/asahi/pgtable.rs:1-170`, `mmu.rs:475-540,643-805,1080-1120`
and `arch/arm64/boot/dts/apple/t8103.dtsi:477-517,610-691`.
UAT has 16-KiB leaves, three levels and 39-bit user VA. Asahi maps owned GEM
scatter/gather runs after requiring address, offset, length and VA alignment
to 16 KiB. It can map arbitrary *allowed 16-KiB RAM frames*; that does not mean
four unrelated 4-KiB Windows PFNs fit one leaf. The GPU node uses UAT and retained
memory regions; DCP/display nodes have separate DARTs. Do not insert a display
DART IOVA into an AGX UAT PA or assume DART adds 4-KiB subpage remapping to AGX.

Asahi distinguishes GPU shared/coherent mappings (`PROT_GPU_SHARED_RO/RW`, UAT
MEMATTR_UNCACHED) from firmware private cached mappings, whose unmap path needs
firmware cache maintenance. The UAT attribute name does not mean the Windows
CPU mapping must be Device memory. Existing `AppleAgxUatGpuSharedReadOnly` and
`AppleAgxUatGpuSharedReadWrite` descriptors are the appropriate
starting point for shared GPU RAM. Keep
read/write rights, ordering, and CPU cache attributes explicit; neither a TLB
flush nor `_CCA=1` is proof of completed GPU data writes. Do not map arbitrary
OS RAM with a new uncached `MmMapIoSpace` alias, or treat `CacheCoherent=0` as
evidence that a cached alias is safe. New CPU views require a supported locked
page/MDL path and compatible cache type; the direct UAT-grant path needs no CPU
mapping of the payload at all.

m1n1 checkout commit `8769e5e981730ca5c971ad985e65bd47d005e8c0`; inspected
`src/hv_agx_gpuva_v5.c:101-157,213-286,335-404,500-557`, its header and
`hv_agx_retained_platform.c:48-135,149-225`, `hv_agx_retained_backing.c`,
`hv_vm.c:528-538`, `hv_autonomous_runtime.c:306-322`,
`hv_launch_j313.c:1-120`, and assisted `proxyclient/m1n1/hv/__init__.py:2124-2141`.

`REGISTER_BACKING` already accepts eligible guest RAM outside R64. Its
`translate_page` calls `translate_guest`: verify the whole aligned 16-KiB span
against stage-2 translation, including both ends of each 4-KiB subpage; reject
SW/MMIO mappings, noncontiguity, host addresses outside the admitted RAM map,
retained roots, ramdisk and protected launch regions. `hv_ipa_to_pa` resolves
hardware mappings; autonomous normal RAM is explicitly identity-mapped, while
the low-memory window is relocated. Assisted launch likewise establishes RAM
mapping and removes carveouts. Therefore use the translator even where the
last launch proved IPA=PA; never replace it with an identity assumption.

The broker also excludes registered UAT table pages, validates epoch and process
generation, and requires a live same-owner/same-allocation-generation grant
before publishing a leaf. Shared registration permits a repeated IPA/PA across
different processes only if all grants are shared and carry the same backing
generation. It does **not** determine Windows allocation ownership or pin RAM:
the trusted KMD attests that from VidMm. Exclusive and shared records cannot be
mixed opportunistically. A table/backing collision must remain rejected.

`publish_entry` refuses an owner with in-flight jobs, orders stores through
`sync_tables`, invalidates occupied slots, and increments map generation.
Rollback failure taints the broker. `revoke_backing` is BUSY while leaves refer
to the frame. Existing invalidation uses outer-shareable barriers and ASID TLB
invalidation; a new grant does not justify changing the power, IRQ or RTKit path.
The global 8192 per-process/page grant slots represent at most 128 MiB before
alias duplication, **not** 128 MiB available to each process. R86's range-grant
work is a separate scalability prerequisite, not permission to raise an array
limit or register all RAM permanently.

Mu commit `f0f1c50a040d490f78340b8995917ede24fc4220`: inspected
`Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c:407-442`
and generated `Platform/MacBookAirMid2020Pkg/AcpiTables/
J313AppleAgxAbiAdmission.asl.inc`. Mu reserves the broker-supplied 64 MiB as
EfiReservedMemoryType and exposes it dynamically in APPL0002 `_CRS`; `_CCA=1`,
the four fixed resources and IRQ 889 remain unchanged. That reserve survives
ExitBootServices outside general VidMm RAM. System pages come from Windows'
ordinary RAM, so this design needs no new ACPI resource or Mu reservation.

Reference contract is EXP850R on unchanged R110 full-owner, G1b16/package850
root `7058f145d8ae6ef5825712283758f71ebcbd2aed`, with Code0/SSH and the rejection
above; ordinary EXP377/392 is the last proven clean recovery. Saved manifest
firmware SHA-256: m1n1 `14872dba0a6ecab9d4e49e42237298b44909126f0d5f7267cc5deaed587372b1`,
Mu `3a76857cb650cae6b8d7c5d22250debeec437cd28c2dfd032ebecb1cbd0f6eca`.
The launch-contract comparison preserves the identity/relocated-memory
distinction, protected ranges and existing assisted/standalone differences in
software MMIO traps. No new live ADT/register/IRQ snapshot was obtained under
the offline restriction; this is not a claim that current Air state was checked.

## TRANSLATION

**Recommendation: first separate CPU envelope validation from GPU memory
validation, then implement residency-scoped grants for representable system
frames. Do not call that complete support for arbitrary system-memory PTEs.**
The general 4-KiB scatter case is not directly representable in 16-KiB UAT.
Failure attribution and a real-C offline replay precede any new hardware.

WHY THIS HYPOTHESIS:

1. The current logical shadow accepts segment-0 addresses, but
   `gpuva_g3_paging_windows.c:207-211` deliberately suppresses even complete
   system groups; the saved segment-0 counters are nonzero. This makes missing
   publication more likely than a GPU execution fault at this boundary.
2. Branch9/downstream3 occurs before native queue submission, and both
   `AdmissionG4GraphAccess` and output resolution can produce it. Power, IRQ,
   RTKit and completion are downstream; none explains this synchronous reject.
3. `render_backend_image.c:342-348` copies native metadata from private data;
   `backend_platform_windows.c:2189-2199` resolves that owned copy. Nevertheless
   parser and BeginJob require the DMA envelope VA in the UAT graph. This is a
   concrete CPU/GPU-consumer mismatch, not proof that it was the first failed
   check in EXP850R.

**Ownership and translation invariant.**

| Layer | Owns | Must not substitute for |
|---|---|---|
| VidMm / UMD | allocation residency, pinning, mapping updates, paging dependencies | broker lifetime |
| KMD logical shadow | 4-KiB PTEs, owner/root, allocation identity when supplied, permissions and mapping epoch | host physical validation |
| KMD backing registry | live frame generations, alias references, pending revocation and execution references | process PID or a constant R64 token |
| m1n1 broker | protected-memory validation, IPA→PA, grants, UAT publication, leases and TLB completion | Windows ownership knowledge |
| Mu | boot memory map and R64/ACPI exposure | runtime residency |
| Existing KMD scheduler/platform | submission fences, firmware completion and IRQ handling | successful Parse as execution proof |
| Operator/experiment controller | approved launch, evidence and exact rollback | silent reset/recovery inside the mapper |

For a direct system leaf at GPU VA `V`, the four logical PTEs must be valid,
have compatible supported permissions/cache/execute semantics, and map
`IPA[i] = IPA[0] + 4096*i`, with `IPA[0] % 16384 == 0`. The broker must prove
the entire span translates to one admissible aligned host frame. The CPU-stage-2
mapping is not traversed by GPU DMA; the UAT leaf contains the translated PA.
Do not round down a PFN and expose unowned neighbours, or combine RO and RW into
one RW leaf. A 64-KiB system PTE expands to four such independently validated
leaves, after implementing its flags/address/lifetime path.

Use a KMD-wide backing identity registry rather than the current per-graph
`SharedBackingGeneration = local_view.GuestIpaAddress`. Preserve that existing
R64 policy only for the immutable reserve. System entries are keyed by backing
kind, guest frame and a monotonically increasing lifetime generation within the
broker epoch; track all participating processes/roots, logical aliases, rights,
resident mapping references and submitted-job references. Use allocation
identity/offset as additional provenance when present. For NULL hAllocation,
the trusted UpdatePageTable plus the live physical-frame registry is the
authority; do not invent a user allocation handle. A new PFN lifetime cannot
inherit a revoked token, even at the same physical address.

Use shared grants consistently for VidMm frames that can be visible in the
application and paging processes. Join an existing lifetime only while a live
VidMm mapping reference proves continuity. Within one process, reuse its grant
and increment alias references; the current broker rejects duplicate
registrations by the same owner. Process generation remains independent of
backing generation. Registration failure must leave no published leaf or leaked
reference; inability to prove rollback poisons the graph and blocks jobs.

The ordered lifetime is:

1. Validate all incoming PTEs and the affected merged logical groups; obtain
   ownership and generation references before changing the published graph.
2. For representable groups, register each missing owner/grant, then publish
   UAT leaves. Only report successful publication after broker synchronization
   and slot invalidation have succeeded. CPU-only logical mappings may exist
   without UAT leaves, with an explicit classification.
3. At submission, validate all GPU-consumed ranges against published leaves and
   rights, hold backing/root references through BeginJob and firmware completion.
   Revalidate generations when a queued packet becomes runnable; a parse-time
   lookup alone cannot cover eviction between parse and execution.
4. On replacement, invalidation, detach or teardown, prevent new jobs using the
   old view; honor the existing address-space-idle contract and broker BUSY.
   Remove affected leaves, complete TLB invalidation, then drop alias references
   and revoke each zero-reference owner grant. Keep other live aliases intact.
   Invalidating one of four PTEs invalidates the whole AGX leaf; it cannot retain
   the old mapping for the three surviving subpages.
5. Complete the paging operation/fence only after this ordering is satisfied;
   then VidMm may release/reuse backing. On uncertain TLB state retain ownership
   records, fail closed and use the established fatal/reboot recovery contract.
   A late NotifyResidency callback cannot repair an already stale GPU mapping.

**Exact implementation surface and offline RED→GREEN gates (future work).**
Paths below are relative to `drivers/apple-agx/` except tests and m1n1 paths.

| Step | Files / change | Required falsifiable offline test |
|---|---|---|
| 1. Discriminate failure | `shared/include/apple_agx_g4_submit.h`, `shared/src/apple_agx_g4_submit.c`; `render-admission/src/gpuva_g3_windows.c`, receipt structure/header and `receipts.c`: versioned first-failure subsite, access kind, ordinal, VA/bytes/write, owner/root/generation, four logical PTEs/segments and graph-present state; output reject has a separate subsite | Extend `tests/apple_agx_g4_submit_test.c` and `tests/g4_submit_virtual_replay.c`: inject a failure at every ordered access and output resolution, preserve first record across >128 submissions, prove all formerly ambiguous cases distinguishable without retaining raw pointers |
| 2. Fix CPU envelope contract | Same parser and both call sites in `gpuva_g3_windows.c` (`AdmissionG4SubmitVirtualEnvelope`, `AdmissionGpuvaG3BeginJob`): typed CPU-envelope versus GPU-read/write access; maintain header/VA/length binding and bounded immutable private copy; validate logical envelope range without requiring native UAT publication | A scattered, resident logical DMA range with valid private metadata reaches later GPU checks; missing logical mapping, stale root, bad lengths or bytes reject. GPU stream/USC/attachment with the same unmapped VA still rejects. Cover BeginJob's additional `GpuvaG3DmaBufferVa` graph check, not just the first parser invocation |
| 3. Implement per-frame lifetime | `render-admission/src/gpuva_g3_private.h`, `gpuva_g3_paging_windows.c`; `shared/include/apple_agx_gpuva_g3_graph.h`, `shared/src/apple_agx_gpuva_g3_graph.c` and translation helpers: retain segment/attributes/provenance, typed backing provider and generation, remove local-only publication veto only after validation | Real production paging→graph→v5 client→broker replay: segment-0 aligned four-page run succeeds; local path unchanged; partial updates in every order, Repeat, 4K/64K transition, mixed rights, scattered PFNs, null allocation, overlapping aliases, overflow and out-of-range values |
| 4. Verify broker boundary | `m1n1_windows/src/hv_agx_gpuva_v5.c`, `hv_agx_retained_platform.c`, `hv_agx_retained_backing.c`: **no ownership-check weakening is required for the bounded direct path**; add tests in their existing host suites. If range grants are needed, extend broker header/dispatch and shared v5 ABI/client together, negotiate a new feature/version | Real translator model: nonidentity IPA→PA success, one bad subpage failure, protected RAM/MMIO/table conflicts, wrong epoch/generation, shared/exclusive collision, two processes plus paging alias, same-PFN reuse, revoke BUSY and rollback/TLB fault injection. Exhaust capacity without publishing partial success; range implementation must split/merge/revoke without covering gaps |
| 5. Remove output's reserve dependency | `gpuva_g3_windows.c` (`AdmissionG4ResolveOutput`, `AdmissionG3OutputMatchesLocal`); `render_backend_image.c`, `backend_platform_windows.c` and packet/output view declarations: replace contiguous local CPU/PA proof with a generation-bound GPU output view and explicit optional CPU scatter view | Real backend bind/BeginJob/completion test for a VA-contiguous output made of noncontiguous 16-KiB frames; no fabricated contiguous `DestinationPhysical`, no dangling CPU token, no copied-back fence before completion. Keep local scanout transport as a separate destination |

The receipt declaration is `render-admission/include/render_admission.h`; output
declarations are `include/render_submission.h`, `include/render_backend_image.h`
and `include/render_completed_output.h`. Do not infer full resource access
coverage from the parser's one-byte render-root checks: allocation/range
provenance must also bound the GPU's referenced buffers in the producer/builder
contract. That obligation exists independently of the segment identifier.

Step 5 is required for claiming system-backed render targets, even if steps 2–4
allow the first parser call. A direct GPU target need not have one contiguous
host PA. Existing backend output/debug/presentation consumers must either use
an explicitly pinned compatible CPU mapping/scatter iterator or decline that
optional CPU operation; setting a fake local token is not a solution. Preserve
the final scanout/DCP ownership and copy ordering. The exact CPU mapping API and
all output consumers must be resolved in the implementation review before that
step is executable; no unsupported RAM mapping is prescribed here.

Steps 1–3 also need tests for map→queue→invalidate→BeginJob, shared-page access
from two roots, process destruction while referenced, reverse teardown order,
and failed synchronization after a descriptor write. The broker cannot protect
against premature VidMm page reuse if the KMD reports paging completion early.
Keep unsupported PTE attributes fail-closed unless their implementation and
advertised capability are added together. Trace sizes and PFNs are regression
inputs only; generate additional valid sizes, offsets and page permutations.

**Alternatives and scope limits.** Steering all allocations to local segment 2
does not satisfy this contract. EXP836 rejected local-only write sets; accepted
Mesa shapes include aperture+local. EXP839's section-backed CPU-visible CDD
surface failed in local memory, and EXP840 keeps that class in aperture.
`callbacks.c:410-412` puts virtual-context DMA in the aperture. Microsoft's
context contract permits aperture segment choices for this field, not an
arbitrary local-memory replacement.
[DXGK_CONTEXTINFO](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_contextinfo).
Moreover R64 has only 64 MiB shared among tables, firmware support and surfaces:
four 2560×1600×4 surfaces alone use 62.5 MiB. That is a capacity example, not
evidence that DWM held exactly four. The counters do not measure DWM demand.
Local preference remains useful for controllable GPU BOs but is not a universal
placement/lifetime solution; increasing the reserve is a separate firmware
experiment and does not remove imported/system mappings.

For truly scattered 4-KiB GPU data, direct mapping is impossible under the
inspected UAT contract. A 16-KiB staging leaf could gather four pages and later
scatter writes, but it must preserve physical aliases across VAs/processes,
CPU/GPU visibility, partial-page rights, atomics and all completion fences.
Two independent staging copies of one shared PFN are not coherent. Therefore
do not include a transparent general bounce mapper in this minimal design.
For a controlled copy-only resource, an explicit transfer with established
ownership and completion can be a separate design. Until a supported placement
contract or a fully specified emulation exists, actual GPU use of an
unrepresentable group remains a named rejection. Direct-group support is a
bounded bring-up step, not complete WDDM system-memory compatibility.

**Smallest future hardware discriminator, not a preregistration or launch.**
After offline gates, a separately authorized diagnostic candidate changes only
failure attribution and records the exact failed access and its four PTEs.
Success is a joined subsite/VA/segment/generation receipt; failure is another
ambiguous receipt or loss of the established Code0/SSH checkpoint. A subsequent
separate mapping checkpoint uses one resident representable system frame,
proves GPU read/write plus completion, invalidates it and verifies stale access
rejection. No DWM-frame claim follows from Parse success. Preserve hash-bound
R110/recovery artifacts; collect evidence before exact package cleanup. Follow
the EXP850R Code0 recovery contract (ordered restart, hidden cleanup where
required, ordinary EXP377/392 durable Code28) and both control-plane checks
before requesting physical action. Each run requires its own AGENTS ledger
before/after entry and explicit permission for any G2/m1n1 hardware work.

**Offline provenance and checks.** Inspected root
`47323f1e42764184c46996ffc6a3c90a7390435d`, branch
`integration/ad04-windows-compiler`. Pre-existing root/m1n1/Mu binary diff
SHA-256 values respectively:
`e7ee1c10085074c2ba1bee7334f1f3dad3068af898a01e322950916575817fe2`,
`68387a2e4a778333004ae9e8e035304dfd70c5d83c8e1ec4dcff4c949427f0e7`,
`2e654da05fbcd83288511c5161cc749c8b87d69e222cfc3f6fed3fceaea79a7d`;
all match EXP850R's manifest. Existing submodule dirt was not changed.
Read the current boundary, EXP850R receipt/manifest and only directly relevant
source/contract material; no old-reference archaeology loop. External code was
read for behavior, not copied into implementation.

Executed on the host:
`CC=/tmp/agx-clang-wrapper python3 -m unittest tests.test_apple_agx_g4_submit tests.test_g4_submit_virtual_replay tests.test_gpuva_g3_contract tests.test_agx_retained_backing tests.test_change_ledger`
— **7 tests PASS**, including production-C parser/virtual-submit/graph/planner
and retained-backing tests. These establish the unchanged baseline, not the
proposed system-memory implementation. Independent raw receipt/counter decode
and WDK/source identity checks were also completed. No full driver build or
hardware experiment was performed. The new RED→GREEN cases above are future
acceptance criteria, not tests claimed to have passed today.

WHY CONTINUE COMPARISON: this was one current-source/EXP850R pass to distinguish
logical mappings, native leaves and CPU-only metadata. No historical comparison
is proposed. WDDM admission already reaches Code0; clean reconstruction of
DriverEntry/AddDevice/StartDevice would test the wrong lifecycle boundary.

**Tandem review dispositions for this design.** Read main-repository
`.local/tandem/REVIEW.md`, SHA-256
`753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5`.
OPEN labels include historical entries whose later dispositions are recorded in
the current state. These responses do not rewrite those results or reopen them:

- REVIEW R113: ACCEPT — preserve direct cold full-owner transition; no launch here.
- REVIEW R111: ACCEPT — preserve the measured R64 CPU memory-type fix; no new uncached OS-RAM alias.
- REVIEW R110: ACCEPT — preserve the completed 64-bit dynamic ACPI range fix.
- REVIEW R109: REJECT — header already records the firmware hypothesis rejected; no sysmem implication.
- REVIEW R108: DEFER — historical firmware issues are outside the proven Submit boundary.
- REVIEW R107: DEFER — historical 0x101 attribution is outside this offline mapping design.
- REVIEW R106: ACCEPT — keep ordered transition and package gates for any later run.
- REVIEW R105: ACCEPT — use the recorded EXP836 matrix result; do not rerun it here.
- REVIEW R104: REJECT — already rejected by EXP821, not evidence for this mapping failure.
- REVIEW R103: DEFER — AllocateCb boundary is crossed; no live harness in this task.
- REVIEW R102: DEFER — AllocateCb failure is not the current boundary.
- REVIEW R100: REJECT — preserve its recorded rejection; no re-investigation.
- REVIEW R99: DEFER — old CreateDevice failure is superseded by the current boundary.
- REVIEW R98: ACCEPT — retain full-size output cases in future scatter-output tests.
- REVIEW R97: ACCEPT — one current-source pass, no EXP208 archaeology.
- REVIEW R96: ACCEPT — preserve the native TA/3D builder and change only its backing contract.
- REVIEW R95: ACCEPT — include ownership, generation, paging and failure-attribution gates.
- REVIEW R94: ACCEPT — keep per-process GPU objects distinct from firmware objects.
- REVIEW R91: DEFER — prior screen observation is unchanged; no presentation claim here.
- REVIEW R90: DEFER — panel/color observations are outside this address-translation boundary.
- REVIEW R88: ACCEPT — submission, completion and actual presentation remain separate checkpoints.
- REVIEW R86: ACCEPT — bounded direct grants first; versioned range grants before capacity claims.
- REVIEW R85: ACCEPT — future package must bind source, ABI, profile and artifact hashes.
- REVIEW R74: ACCEPT — read the context contract; retain aperture DMA placement.
- REVIEW R71: REJECT — the assertion that a cap guarantees all mappings contiguous is not established; retain the supported 64-KiB path as a separately tested candidate.
- REVIEW R69: ACCEPT — no live bind or Air access here.
- REVIEW R65: ACCEPT — preserve AGX4 v2, root/rights/lease/fence ordering; compute remains separate.
- REVIEW R64: ACCEPT — both OPEN entries: preserve the implemented R64 carveout and current G4 ABI; neither grants new hardware permission in this task.
- REVIEW R63: ACCEPT — keep firmware-owned reserve; no new contiguous allocation at StartDevice.
- REVIEW R57: ACCEPT — preserve full-span bounds and meaningful paging failure status in new tests.
- REVIEW R55: DEFER — repeat StartDevice is a separate recovery defect, not a mapping experiment.
- REVIEW R54: DEFER — no series is started; later hardware requires its own authorization and ledger.
- REVIEW R49: ACCEPT — preserve pinned WDK caps/table ABI; no opportunistic cap change.
- REVIEW R48: ACCEPT — no ports opened; future control-plane/launcher exclusion remains required.
- REVIEW R47: ACCEPT — preserve build-profile/ABI checks; no firmware build here.
- REVIEW R45: ACCEPT — test real broker dispatch and leaf/grant/table/root teardown order.
- REVIEW R40: DEFER — timer/watchdog attribution is outside the synchronous Parse rejection.
- REVIEW R37: DEFER — historical disarmed-start failure does not justify changing sysmem or launching Air.

## WHAT IS STILL UNKNOWN

- Which EXP850R first access actually failed and its mapping at that instant.
  The saved receipt lacks that information; only a new live capture can recover
  an equivalent future failure, not retroactively reconstruct the old packet.
- The real distribution of contiguous, aligned, mixed-rights and scattered
  system groups used by GPU work, separate from CPU-only envelope/paging VAs;
  and actual active grant capacity/alias pressure under DWM. Aggregate counters
  cannot answer these questions.
- Whether the supported 64-KiB system-page capability path, after implementation,
  changes placement of the relevant allocations on this Windows build. Its
  effect on internal/imported mappings must be measured, not assumed.
- Actual coherent CPU↔GPU data visibility, UAT/TLB retirement and isolation on
  eligible non-R64 frames under the preserved launch profile. Host replay proves
  ordering decisions but cannot prove device/cache execution.
- Whether the resulting native submission completes and reaches a DWM-correlated
  rendered/presented frame without a new downstream rejection. Current evidence
  stops at virtual-submit admission; no accelerated frame is proven.
