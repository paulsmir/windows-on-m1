# G3 paging operations at the EXP799 boundary

Source contract: pinned WDK 26100 `d3dkmddi.h`, Microsoft
[operation list](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_buildpagingbuffer_operation),
[BuildPagingBuffer](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer),
and current `paging_windows.c`, `gpuva_g3_paging_windows.c`,
`memory_runtime_windows.c`, `allocation_windows.c`, `callbacks.c`.
VidMm owns requests and residency; KMD owns the software aperture, logical
page-table shadow, CPU copies/fills and ordered fence; m1n1 owns validated
AGX table publication and DMA ownership. Mu exposes the frozen profile.

| Operation | Gate and expected call | Current implementation |
| --- | --- | --- |
| Map/UnmapApertureSegment (5/6) | Aperture segment 1 advertised; callable, including DMA buffer mapping | `AdmissionMemoryRuntimeMapAperture/UnmapAperture` validates MDL/PFNs/range and updates software aperture immediately, including dummy mapping |
| Transfer/Fill/DiscardContent (0/1/2) | Physical paging path; callable for local memory | `AdmissionMemoryPlan*` plus CPU packet and completion fence |
| VirtualTransfer/VirtualFill (8/9) | GpuMmu, 16 KiB local profile; observed through EXP799 | Logical VA shadow → CPU packet, multipass, ordered fence |
| UpdatePageTable/FlushTlb (11/12) | GpuMmu; observed | R70 shadow and v5 broker; inactive-root flush no-op |
| SignalMonitoredFence (16) | Paging context; callable | CPU packet writes the monitored value before completion fence |
| NotifyResidency (15) | Requires `ExplicitResidencyNotification` **and** `AccessedPhysically`; only latter is set | Not requested by declared flags; no synthetic success |
| InitContextResource/UpdateContextAllocation (10/13) | Require KMD context-allocation callbacks; no call sites in this KMD | Not requested by current architecture |
| MapApertureSegment2 (17) | Requires `MapAperture2Supported`; unset | Not requested |
| CopyPageTableEntries (14) | WDDM 2.x permits this operation; no current trace establishes a call | Unsupported pending a real input; do not fabricate table-copy semantics |
| Read/WritePhysical, SpecialLockTransfer, later MMU/Residency2 variants | AGP, alternate VA, or caps/version opt-ins absent | Not requested by current profile |

R82 one-pass audit: the EXP798 `Flags.Paging=1`, DMA `0x220`, private `0xff0`
packet is accepted by the current route. Record framing, process/root,
memory ranges, IRQL and ordered fence checks protect executable CPU work.
WDK also defines ContextSwitch and Resubmission bits; the current CPU paging
route accepts only paging-only flags and nonempty records. Those packets have
different completion semantics and need a recorded input before enabling them.
The fence-record offset check allowed unsigned wraparound; replay now rejects
it and the CPU executor checks the bound before reading the eight-byte value.

Smallest hardware checkpoint: EXP800 reaches past level0 local leaf index
`0xbb0` and `CreateCddDevice`, or records the next exact broker/DDI boundary.
If a new stop occurs, collect dump/receipts/ETL before exact package cleanup;
R60 same-profile recovery is conditional on durable arm consumption and known
package identity.
