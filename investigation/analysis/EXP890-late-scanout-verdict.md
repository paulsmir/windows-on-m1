# EXP890 late scanout measurement verdict

## Original boot and pixel evidence

EXP890 used the byte-exact signed EXP889 package30.0.889.0 (KMD SHA-256 `61b3c640d85d53b22b96a0d0965f39dd7dea1b61b195231132920681f45a804b`, UMD `0d51d967a1939ad6431f44ce01e32b40d8671acb579b004b8082b92e2a451888`) and unchanged Mu R143. The only runtime change was m1n1 commit `9320da31e72af2b64bb1f17c74ec6ecc2fab5919`, profile `EXP890_SCANOUT_WINDOW`, image SHA-256 `d3e0f999fe23bffa58f2343cbd0ee1f696da0c6fae08e1510f0ca58d7ca7db69`. Snapshot callbacks read the current mapped surface; they do not fill, cache clean, remap, flip, or submit.

Original boot `2026-09-30T01:59:52.6715290Z` stayed Code0/Start12/status0 with eight CPUs, pinned SSH, and no real kernel bugcheck through the monitor's `778.323584`-second checkpoint. A bounded SSH sample after the last snapshot confirmed the same boot and Code0. Explorer PID4192 remained; no DWM process appeared in any of the 20 monitor samples (first at boot+170.458 seconds).

The diagnostic records four source/snapshot pairs. Each source was the **current DCP latched** swap and in-pool surface at the time of sampling, not merely a retained seq2 address:

| Latch elapsed | Broker sequence / DCP swap | DCP surface IOVA | Sampled PA | CPU-view nonzero pixels |
|---:|---:|---:|---:|---:|
| 120 s | seq2 / swap9 | `0x102a0000` | `0x8e0110000` | 0 / 4,096,000 |
| 120 s | seq3 / swap10 | `0x102a0000` | `0x8e0110000` | 0 / 4,096,000 |
| 300 s | seq3 / swap10 | `0x102a0000` | `0x8e0110000` | 0 / 4,096,000 |
| 600 s | seq3 / swap10 | `0x102a0000` | `0x8e0110000` | 0 / 4,096,000 |

All pairs reported `same_surface=1`, `in_pool=1`, hash `ff4a55d94eaa2325` and `cache_clean=0`. A later seq4/swap11 first-swap read occurred after the ordered recovery request, so it is not used to judge the original measurement window. **Confirmed:** the CPU-readable view of the currently latched DCP surface remained zero late in the original boot. **Not established:** whether DWM ever successfully presented, or whether another cache-visibility issue could hide writes from this CPU view. There is no nonzero standard Present proof.

## Earlier lifecycle evidence

The original boot produced a dxgkrnl `VIDEO_DXGKRNL_LIVEDUMP` 0x193 at uptime90.196 seconds, reason `0x810`, stack `dxgkrnl!ProcessDeadlockThread`. This is a *live dump*, not a kernel stop. The pinned symbol debugger does not by itself identify the blocked resource owner. At uptime142.489 seconds DWM PID1224 crashed with access violation `c0000005` in `dwmcore!CComposition::PreRender+0x2e4`, reading `0x3fdf3990`; the writer of that invalid pointer is unknown. DWM was absent before all monitored late samples. Do not attribute either event to the diagnostic firmware or assume the live dump caused the user-mode crash without a causal trace.

The retained first `Wom1PresentReceipt` in EXP890 is Version1/160B, Branch4, status `STATUS_INVALID_PARAMETER`, `Flags=1` (BLT), `SubrectCount=1`, `DmaSize=0x50000`, `DmaPrivateSize=0x51000`, but `PatchListSize=0`; the receipt records a non-null DMA buffer and private data. Source `callbacks.c:AdmissionDdiPresent` routes BLT flag1 to `AdmissionPresentBlt`, which records Branch4 on failure. `present_windows.c:AdmissionPresentBlt` requires a non-null output patch list and at least two slots before encoding the BLT. Current `callbacks.c:AdmissionDdiCreateContext` explicitly advertises `NoPatchingRequired=1` and `PatchLocationListSize=0` for virtual-addressed non-system/non-GDI contexts. The pinned WDK and Microsoft's FULL GRAPHICS [Residency Overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview) describe zero patch lists for GPUVA no-patching contexts; [DxgkDdiPresent](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_present) defines source/primary presentation semantics. This is a deterministic **driver-internal incompatibility** for any BLT Present delivered with that context contract. The receipt does not identify its caller process, so do not claim it caused DWM's earlier pointer fault. EXP889 had no retained first Present failure receipt; absence there does not disprove this mismatch.

## Ownership and next offline checkpoint

WINDOWS CONTRACT: FULL GRAPHICS UMD Present/Flush calls a runtime callback; KMD receives a source-to-primary BLT or a flip with the documented GPUVA allocation-list and no-patching rules. A successful present must actually write or select the primary, not merely acknowledge an allocation. The existing physical BLT patch-list requirement cannot be applied unchanged to a GPUVA no-patching context.

AGX/ASAHI CONTRACT: Asahi's per-process GPUVA/UAT source and m1n1's verified scanout service distinguish a render allocation from the DCP pool surface. The DCP latched swap/surface is proven by m1n1; its sampled physical bytes stay zero. Mu exposes the same 1 GiB reserve and APPL0002 ACPI device, unchanged across EXP889/890. m1n1 owns DCP power, DART mapping, latch, interrupts and recovery; it does not produce the Windows primary's pixels. Windows KMD/UMD own rendering, BLT/flip/present, allocation synchronization and recovery on DDI failure.

TRANSLATION: first resolve the no-patching BLT contract in the Windows KMD/UMD layer, including source/destination allocation handles, GPU virtual addresses, actual copy/flip execution, fences and primary ownership. Do not treat returning `STATUS_SUCCESS` or merely deleting the patch-list check as a fix. The crash/deadlock evidence may reveal an earlier independent admission or object-lifetime defect; analyze that before any new hardware candidate.

WHAT IS STILL UNKNOWN: the initiating process and exact source/destination of the first failed BLT; whether the live dxgkrnl deadlock and DWM invalid pointer share a writer; whether valid GPU writes are visible to the CPU snapshot without cache maintenance; and which producer should populate the latched pool after a successful FULL GRAPHICS Present. Smallest deterministic RED test: a virtual-addressing context with `NoPatchingRequired=1`, zero patch slots and a valid full-size source/destination pair must not fail solely because the physical BLT encoder expects patches; GREEN requires a real GPUVA presentation transaction and primary synchronization, not a status-only bypass. No next hardware hypothesis until this mapping and the DWM fault boundary are narrowed offline.

## Evidence and recovery

Original evidence was copied and independently host size/SHA verified **before recovery**: 17 files/864,165,831 bytes, manifest SHA-256 `ee0f341186ae4f7729d88ccfcc426e3d63cd767b57ca8043c33db86af4bdd806`, including a 512 MiB capped original ETL and the WER DWM dump SHA-256 `36d393486106027eb6cb6629de1e88418dc5176cbb1e8069d09bb54ecf21b566`. The ETL session had stopped at its size cap; guarded collection preserved the original boot receipt without modifying the packaged collector. The included `093026-13265-01.dmp` is historical EXP888 evidence.

Ordered original restart stalled on Code0; only after host verification did owning-launcher SIGTERM capture a diagnostic snapshot and reset Air. Immutable hidden boot reached Code45/exact oem5/package889/stopped service. Supplemental live-dump/WER evidence was copied and all 25 files/909,536,994 bytes were independently host size/SHA verified, manifest `3c676ef5c62784072c18fe5ebed6b1b15a13b84afd201224b0f1b2873197a8c1`, **before** diagnostic and exact-package cleanup. No live Code0 package removal. Cleanup ordered restart returned to immutable ordinary Code28, verified at `02:26:57.6290453Z`: package0/arms0/SYS0/UMD0/service0/signer0/diagnostics0, eight CPUs, two OK disks, five OK USB devices, RDP service Running, autologon1/password present. Raw serial/heartbeat/dumps/PDB debugger output and recovery receipts are under `.local/experiments/EXP890-late-scanout/`.
