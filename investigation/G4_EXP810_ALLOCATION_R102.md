# EXP810 R102 — one-pass allocation output audit

2026-09-25. Boundary: package817 DWM native screen `pfnAllocateCb`
returns `0x80070057` for ClassId1/64 KiB after KMD CreateAllocation and
OpenAllocation both return success. The values below are **source-derived**
from `render-admission/src/allocation_windows.c` and the package817 build
profile; the KMD receipt records the request shape and DDI status, not every
output field. Do not treat the table as a byte-for-byte hardware capture.

| DXGK_ALLOCATIONINFO field | Native ClassId1 / 64 KiB | CDD / G3 path and WDK contract |
| --- | --- | --- |
| Size, Alignment | 65536, 65536 | Same KMD alignment policy; both multiples of the local 16 KiB slab. |
| MinimumPageSize, RecommendedPageSize | Not set by package817; Alignment union used | Package uses G1b page profile 16 and allocation hint 0. |
| PitchAlignedSize, HintedBank | 0, 0 | Neither advertised segment has PitchAlignment; zero required. |
| PreferredSegment | SegmentId0=2, all other bits 0 | Local segment 2 exists; CDD/G3 uses the same preferred segment. |
| SupportedReadSegmentSet / MmuSet, SupportedWriteSegmentSet | 2, 2 | Local segment only. CDD non-CPU-visible=2; CPU-visible class0 may use 3. WDDM2 VidMm uses the write set; WDK gives no MmuSet semantics sufficient to change this union. |
| EvictionSegmentSet | 0 | Same source path for CDD/G3; WDK permits direct transfer without an aperture eviction segment. |
| MaximumRenamingListLength / PhysicalAdapterIndex | 0 | WDDM2 adapter index 0; single physical adapter. |
| hAllocation | Non-null KMD object | KMD Create/Open success is recorded; exact value is not relevant here. |
| FlagsWddm2 | CpuVisible=1, AccessedPhysically=0, all other bits 0 | Native BO uses process GPUVA; local segment is CPU-visible in the G3 qualification profile. CDD/G3 class0 keeps AccessedPhysically=1. |
| pAllocationUsageHint, AllocationPriority, Flags2 | NULL, NORMAL (0x78000000), 0 | Same source path for class0; priority zero would be invalid, but package817 uses NORMAL. |

Sources: package817 `build-receipt.json` and `EXP817-evidence/state.json` in
`.local/experiments/EXP810-g4-package817/`; `allocation_windows.c`,
`memory_windows.c`, `gpuva_g1b_profile.h`, WDK26100 `d3dkmddi.h`; Microsoft
[DXGK_ALLOCATIONINFO](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo),
[GPU segments](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments),
and [allocation priorities](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_setallocationpriority).

Verdict: no one output-field difference has a documented rejection rule that
explains this `E_INVALIDARG`. Do not change another field by guess.

EXP818 ran the exact unchanged package817 with DxgKrnl all-keywords/verbose
ETW and Admin/Operational export. The byte-verified ETL contains 127040
events, with no lost buffers. DWM PID1224 has 87 VidMm global allocation
events (ID33), but none for the 64-KiB native BO; 262 UMD native Allocate
attempts returned `0x80070057`. There is no named invalid-parameter payload
or warning/error level among 32762 events for that PID. This places the
refusal before the global allocation event without exposing its exact check.
The ETL is `a9493a03`, UMD log `04288ee7`, targeted audit `c838753a` under
`.local/experiments/EXP818-r102/evidence/`. Exact package817 was removed and
the guest returned to ordinary GPU-visible Code28. Next discriminator is a
single D3DKMT user-mode harness with an exact ClassId1/64-KiB request and a
known-good allocation control, collecting NTSTATUS in one boot (REVIEW R103).
