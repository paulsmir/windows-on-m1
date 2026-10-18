# R58: one offline pass over G3 DDI inputs

Boundary: EXP780 died in `BuildPagingBuffer(UpdatePageTable)` before G3 page-table installation. Pinned WDK is `.local/reference/wdk26100/d3dkmddi.h` in the root repository; line numbers below refer to that copy. The declared adapter has one node/engine, no VPR, hardware queue, dual PTE, native fence, or IOMMU. This audit covers the G3 callback set, not a promise that every optional WDDM feature is implemented. The m1n1 broker owns AGX/UAT execution, KMD owns process/table graph and bounds, VidMm owns the input PTEs and process metadata. The next falsifiable checkpoint is acceptance of EXP780's first 8192-entry CPU_VIRTUAL UpdatePageTable, with flushed input/result receipt; GPU-hidden is the recovery image.

| DDI / input | WDK 26100 possible input | Decision / current guard |
| --- | --- | --- |
| CreateProcess / `Flags`, `NumPasid`, `pPasid`, `pProcessName` | `DXGKARG_CREATEPROCESS`, lines 5150–5190, explicitly carries all; `pProcessName` may be NULL. | Accept and receipt PASID count; require live adapter/PASSIVE and allocate a separate graph. No OS metadata veto. |
| DestroyProcess / handle | `DXGKDDI_DESTROYPROCESS`, line 5206; process handle previously returned by KMD. | Keep handle ownership, active device/context reference, uncertain graph and broker-release guards. |
| CreateDevice / `Pasid`, `hKmdProcess`, flags, runtime handle | `DXGKARG_CREATEDEVICE`, lines 1470–1497: PASID is input, process handle is input, only SystemDevice/GdiDevice flag bits are defined. | **Changed:** G3 accepts and receipts nonzero PASID; attach process by handle. Keep undefined flag and non-system NULL-handle guards. |
| DestroyDevice / handle | KMD handle returned by CreateDevice. | Keep magic, context/allocation reference and ownership checks. |
| CreateContext / `Flags`, private data, node/engine | `DXGK_CREATECONTEXTFLAGS`, lines 1512–1544: System/Gdi/VirtualAddressing/SystemProtected/HwQueue/Test; `DXGKARG_CREATECONTEXT`, lines 1583–1592. | **Changed:** G3 accepts TestContext as a kernel test context with no UMD private blob. Existing System/Gdi/VA accepted. VPR and hardware queues remain unsupported because corresponding features are not declared. Node0/engine mask1 and private-data generation checks protect the single advertised engine and UMD transport. |
| DestroyContext / handle | KMD context handle. | Keep scheduler, outstanding fence, object and process-ref guards. |
| SetRootPageTable / address, `NumEntries` | `DXGKARG_SETROOTPAGETABLE`, lines 5114–5119, size is input. | Eight entries exactly matches our advertised 128-byte root table. Keep table segment/range/alignment, process ownership and PASSIVE checks; poison context on void-DDI failure. |
| BuildPagingBuffer / operation | `DXGK_BUILDPAGINGBUFFER_OPERATION`, lines 4595–4628, includes legacy transfer/fill/discard, aperture map/unmap, VA transfer/fill, context/fence notifications, UpdatePageTable/FlushTlb, and WDDM3.2 MMU/alloc notifications. | Legacy physical/aperture cases use `paging_windows.c`; G3 UpdatePageTable/FlushTlb use `gpuva_g3_paging_windows.c`. Other opcodes remain explicit unsupported operations, not silently successful. They are a separate missing-feature risk if VidMm requests them; this audit does not mislabel them as malformed inputs. |
| UpdatePageTable / DMA pointers, update mode | `DXGKARG_BUILDPAGINGBUFFER`, lines 4880–5075, has optional DMA pointers; `DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE`, lines 4691–4710, has CPU/GPU update addresses. | **Changed after EXP780:** CPU_VIRTUAL accepts non-NULL DMA/private pointers, updates synchronously, leaves supplied pointers untouched. Keep PASSIVE, process/table range/alignment and page-count checks. |
| UpdatePageTable / Repeat, InitialUpdate, NotifyEviction, Use64KBPages, NativeFence | `DXGK_UPDATEPAGETABLEFLAGS`, lines 2081–2095. Repeat uses one input PTE, InitialUpdate denotes first residency, NotifyEviction announces pending eviction. | Repeat/InitialUpdate supported. **Changed:** NotifyEviction no longer vetoes a well-formed update; graph applies the supplied entries. 64-KB leaf translates into four 16-KB AGX entries. NativeFence remains unsupported because no native-fence capability is advertised; reserved bits remain invalid. |
| UpdatePageTable / `DriverProtection`, PTEs, allocation offset, dual PTE | Update struct lines 4691–4710; DriverProtection is UMD metadata. Dual array is used only with DualPteSupported. | **Changed:** receipt DriverProtection and ignore metadata for this G3 translation. Keep PTE segment, physical range, table-level, page granularity and dual-array guards; these prevent corruption or require an unadvertised capability. |
| FlushTlb / process/root/range | `DXGK_BUILDPAGINGBUFFER_FLUSHTLB`, lines 4680–4688. | Keep matching process root, valid range and graph flush ownership checks. |
| SubmitCommandVirtual / flags, UMD private bytes, fence, VA | `DXGKARG_SUBMITCOMMANDVIRTUAL`, lines 5218–5232; flags are `DXGK_SUBMITCOMMANDFLAGS`; private size can reflect SubmitCommandCb data. | Keep the current prepared-packet correlation, VA range, node/engine, exact KMD private format and fence-state guards. Additional UMD private bytes or submission flags require a transport change; accepting them without preserving packet ownership would be unsafe. |
| CreateAllocation / resource, count, private data | `DXGKARG_CREATEALLOCATION`, lines 3977–3998, permits resource grouping and multiple allocation records. | Current UMD transport emits one allocation with its own private description and no resource. Resource/multi-allocation calls remain a known unsupported feature, not an input-memory guard. They require transactional allocation/cleanup, so are not silently accepted in this paging bootstrap correction. |
| DestroyAllocation / handles, resource | `DXGKARG_DESTROYALLOCATION`, lines 4215–4221, permits a resource handle. | Keep current owned-handle/open-count checks. Non-NULL resource needs the paired resource-creation implementation above. |

Microsoft Learn cross-checks: [CreateDevice](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createdevice), [CreateContext flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_createcontextflags), [UpdatePageTable fields](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable), [update flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_updatepagetableflags), [SubmitCommandVirtual](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_submitcommandvirtual), [DestroyAllocation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_destroyallocation).

Only five unconditional metadata vetoes were removable without changing a declared capability or implementing a new lifetime: CPU_VIRTUAL DMA pointer presence, G3 device PASID, TestContext flag, DriverProtection, and NotifyEviction. Tests cover each removed veto; other rejected cases map to ownership, memory safety, a declared page-table shape, or an unimplemented WDDM operation. In particular, replacing a real failure with `STATUS_SUCCESS` is forbidden by the fail-closed paging contract in `2026-09-24-g3-paging-bootstrap.md`.

## EXP782 level-1 addendum (R57.2 / R59)

Inspected: EXP781's last input/status/dump analysis, `gpuva_g3_paging_windows.c`, `gpuva_g3_windows.c`, `apple_agx_gpuva_g3_graph.c`, `apple_agx_gpuva_g3_caps.h`, WDK26100 `d3dukmdt.h` (DXGK_PTE and page-size enum), `d3dkmddi.h` (UpdatePageTable), and Microsoft's [BuildPagingBuffer return contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer) and [64-KB page table contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages). EXP781 proves only that level0 advanced and level1 Count2048 returned C0000141; it does not reveal the first PTE.

VidMm owns the page-table allocation and DXGK_PTE input. KMD owns the declared size, CPU-VA-to-local-IPA bounds, the G3 graph, and the immediate CPU_VIRTUAL update. m1n1 owns AGX/UAT broker execution; Mu exposes the same frozen full-owner/ACPI profile. Neither firmware layer changes here. The G3 graph writes 2048 native 8-byte leaf descriptors in a 16-KB table and requires 16-KB IPA alignment. The old `Leaf64KBytes=8192` describes less memory than that native table and can permit an 8-KB allocation/alignment, so the source contract itself is false. Declare 0x4000 and validate `Leaf64KBytes >= 0x4000` as well as enough room for 512 logical WDK PTEs; the normal level descriptor continues to require 16-KB alignment. Whether this mismatch caused EXP781 remains a hypothesis until EXP782's first-failure receipt or progress past level1.

All level1 INVALID_ADDRESS paths are: current table CPU VA translation/range/alignment/physical continuity; current table graph registration; child PTE segment offset translation/range/alignment, in the preflight or commit pass; child graph registration; and parent link. Parent PTE flags and an unlink failure return different non-success codes. The new flushed receipt records branch, index, original PageTableAddress, resolved table IPA, PTE Flags/PageTablePageSize/PageTableAddress, resolved child IPA, graph last status/uncertainty, and return status. It is written after releasing the fast mutex, at PASSIVE_LEVEL, before BuildPagingBuffer returns. Existing input/result receipts are also flushed. A graph failure remains distinguished from address failure; a later graph-specific probe is justified only if this branch is observed.

R57.2 decision: Microsoft's documented return values are STATUS_SUCCESS, STATUS_GRAPHICS_ALLOCATION_BUSY, and STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER. The latter is only for insufficient paging-buffer space; allocation-busy is only for an allocation actually in use. An internal invalid address, invalid graph state, or broker failure cannot truthfully be mapped to either. Keep the existing fail-closed internal status and expected VidMm 0x10E/0xB, with durable failure/result receipts before return. This is an intentional fatal contract, not a retry signal. If IRQL is not PASSIVE_LEVEL, registry flushing is unavailable; the early guard remains fail-closed.

Smallest hardware checkpoint: one EXP782 cold boot with only the KMD cap/receipt change, frozen package/signing route, m1n1, Mu, and recovery. If level1 advances, test the next named DDI; if it fails, use the first-failure branch/PTE to discriminate. On SSH loss or bugcheck, collect the dump/receipt from immutable GPU-hidden, delete the exact package, then restore ordinary GPU-visible Code28.

## R61 follow-up after EXP782

EXP782's flushed receipt isolates level1 child PTE index1: Valid/Segment2 (`Flags=0x41`), 4K leaf (`PageTablePageSize=0`), raw `PageTableAddress=0xC`. The old KMD passed `0xC` directly into `D3DGPU_PHYSICAL_ADDRESS.SegmentOffset`, whose byte-alignment guard returned C0000141 before graph registration. Pinned WDK26100 `d3dukmdt.h:337-338` calls the DXGK_PTE field the high 52 address bits; the observed value identifies the 4-KB page number, hence byte offset `0xC000`. This is an inference from the WDK field definition and the reproducible receipt. The official [DXGK_PTE reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_dxgk_pte) describes the same high 52 bits and a zero low 12-bit physical address.

The Windows KMD owns conversion at the DXGK_PTE boundary. `AppleAgxGpuvaG3ResolvePageAddress` and `AdmissionGpuvaG3ResolveTable` already consume byte offsets; CPU_VIRTUAL table resolution and `SetRootPageTable` do not use DXGK_PTE and stay unchanged. Convert each valid parent `PageTableAddress` in both preflight and commit passes, and each valid leaf `PageAddress`, using a checked left shift by 12. Use the PTE's Segment for the parent physical address. Apply 64-KB alignment and segment-end checks to the converted leaf byte offset. Retain raw PTE numbers in failure receipts so a later refusal remains interpretable.

The offline RED→GREEN checkpoint is raw EXP782 parent `0xC` in Segment2 resolving to local IPA base plus `0xC000`, and a 64-KB leaf page number `0x10` resolving to base plus `0x10000`; overflow must fail before a truncated offset reaches the graph. Only one hash-verified G3 package may then test level1 acceptance or expose the next named DDI, with R60 recovery rules and the same frozen firmware/signing route.

## EXP783 hardware follow-up — 2026-09-24

Package786 RED→GREEN page-number decoding advanced beyond EXP782's level1
`ChildAddress` rejection. The next flushed failure is level1 index2
`ParentFlags`, `PageTablePageSize=1` / 64K, flags `0x20041`, raw child page
number `0x2C`, status `C00000BB`. The corresponding update has `Flags=0`,
including `Use64KBPages=0`. Pinned WDK26100 `d3dukmdt.h` defines
`DXGK_PTE.PageTablePageSize` per PTE, while `d3dkmddi.h` defines
`DXGK_UPDATEPAGETABLEFLAGS.Use64KBPages` for the update. The current parent
validator ties them together. Its exact-flags rejection is the next offline
causal target; determine supported combinations and table alignment before
changing it. Dump `0x10E/0xB` parameter3 `C00000BB` confirms the R57.2
fail-closed internal status and durable receipt before the return. R60 same
full-owner disarmed recovery returned pinned SSH and preserved exact package786.

## R62 host replay and EXP783 correction

Inspected EXP776–783 receipts and `GPU_CURRENT_STATE.md`, pinned WDK26100
`d3dukmdt.h` (`DXGK_PTE`, `DXGK_PTE_PAGE_SIZE`), `d3dkmddi.h`
(`DXGK_UPDATEPAGETABLEFLAGS`), Microsoft [64 KB page support](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages),
the current G3 KMD process/context/root/paging functions, Asahi and m1n1
ownership recorded above, and the frozen Mu APPL0002 profile. WDK and Learn
separate the per-parent-PTE child leaf type from the update's `Use64KBPages`
selection for the table being written. A level1 update can therefore contain
both 4K and 64K child PTEs while its own `Use64KBPages` is clear, exactly as
EXP783 observed. Level2 still requires `PageTablePageSize=0`; all other
unsupported PTE bits remain fail-closed.

`tests/g3_vidmm_replay.py` compiles selected actual KMD C function bodies with
minimal WDK, memory-view and broker mocks. Its sequence includes system
PASID1, paging-context flags5, CPU_VIRTUAL level0 Repeat|InitialUpdate 8192
with both DMA pointers, level1 2048 with mixed `0x41` and `0x20041` parent
PTEs at page numbers `0xC` and `0x2C`, then a 64K leaf, root binding, flush,
destroy and ordinary-process creation. Historical pre-fix function bodies
fail at PASID1 (`C000000D`), flags5 (`C00000BB`), and Repeat or DMA-pointer
admission (`C000000D`). Revision 077fad3e~ fails at the child address
(`C0000141`); package786 source fails at the parent flags (`C00000BB`);
the candidate passes in less than one second. Mocked broker
success does not prove hardware graph or VidMm behavior after these inputs.

Owner split: VidMm owns PTE values and update order; KMD decodes/validates
them and maintains the process graph; m1n1 executes graph/broker operations;
Mu exposes the frozen platform. The next hardware checkpoint is one
hash-verified package under unchanged firmware/caps/signing: level1 index2
advances and the next durable receipt names the following boundary. On
bugcheck, apply R60 only after durable disarm proof, known exact package and
one bounded same-profile SSH recovery; otherwise GPU-hidden dump-first exact
cleanup to ordinary Code28.

## EXP784C leaf boundary and R62 replay correction

Package787 proved the mixed parent-size change on hardware: after 624
successful retained operations, VidMm reached `BuildPagingBuffer` level0,
`CPU_VIRTUAL=0`, `Count=32`, `Flags=0`, `FirstPteVirtualAddress=0x2000000`,
with both DMA pointers. KMD returned `STATUS_DEVICE_HARDWARE_ERROR`
(`C0000483`) and Windows bugchecked `0x10E/0xB` inside
`CompleteBuildPagingBufferIteration`. The input receipt did not include the
PTE array; no first leaf failure receipt was flushed. This excludes the old
level1 `ParentFlags` refusal but does not distinguish graph precheck from
broker response. The same full-owner image rebooted disarmed under R60 and
returned pinned SSH/CPU8 with exact package787.

Inspected `gpuva_g3_paging_windows.c`, shared
`apple_agx_gpuva_g3_graph.c`/`apple_agx_gpuva_broker_v5_client.c`,
`m1n1_windows/src/hv_agx_gpuva_v5.c`, pinned WDK26100 update-mode enum,
EXP784C receipt/dump/ETL, and Microsoft GPUVA and 64K-page documentation.
The original host replay had mocked `GraphUpdateLeaf` itself; this could not
reproduce a graph return failure. It now compiles the real graph and client,
mocking only broker I/O and Windows platform services. The recorded Count32
geometry is included with explicitly synthetic, contiguous PTEs because the
exact PTE contents were not receipted. A broker-injected leaf failure is RED
without a receipt and GREEN with branch7, logical PTE index, raw PTE address,
resolved guest IPA and graph status flushed before return.

VidMm owns the missing PTE values; KMD owns translation and graph state;
m1n1 owns actual page-table/backing registration and GPU access; Mu remains
frozen. The smallest next checkpoint is a diagnostic-only package with the
new leaf failure receipt. If graph status is zero with no uncertainty, test
local graph conditions offline. If graph status is nonzero, compare the exact
request with m1n1 ownership/slot validation before any broker policy change.
Recovery is the same R60 disarmed full-owner path or GPU-hidden dump-first
exact cleanup.
