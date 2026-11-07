# RC9 offline device-removal audit

Base: 26612114, worktree codex/rc9. No hardware, guest, builder, network,
package or other-worktree access. Saved EXP1947/1948 evidence only.
The handoff REPORT.md carries the binary decode, timing and test artifacts.

## Findings

**PROVEN:** the KMD can return STATUS_INVALID_PARAMETER from a render
SubmitCommandVirtual for a logically valid but natively unrepresentable mapping.
`drivers/apple-agx/render-admission/src/gpuva_g3_windows.c:4561` returns the
parse failure; `:149` records the first failure and returns its status (`:186`).
The wrapper at `:4775` propagates it. This is a calling-device error route;
it does not by itself establish an adapter-wide error or a reset request.
The pinned WDK declares the DDI; the repository's previously checked Microsoft
SubmitCommandVirtual contract is recorded in
`investigation/analysis/EXP911-paging-resubmission-verdict.md:7`.
No fresh online documentation lookup was performed under the offline restriction.

**PROVEN:** EXP1948's saved failure is not EXP1947's record. EXP1948 is v3,
392 bytes, branch 9, C000000D, downstream Unmapped, Access/Render, ordinal 11,
read one byte at 0x1105ca0000, owner OS PID 5112, graph process 23.
Its logical pages are 0x99e747000, 0x892948000, 0x38449000, 0xa04a000,
all valid/writeable segment 0, the same allocation at offsets 0/4K/8K/12K.
TrackFound=0: allocation class/size/CPU visibility are unavailable, not zero.
Do not copy the Encoder classification or PID 1220 from EXP1947 onto it.

**PROVEN observation / strong timing exclusion:** EXP1948 GM 2k receives
DEVICEREMOVED at QPC 5115461922 (`evidence/EXP1948/gm1948a-umd.log:15504`).
PID 5112 first opens the adapter at QPC 5574605776
(`evidence/guest-extra/umd.log:24786`), 19.130994 seconds later at 24 MHz.
Thus the saved refusal in that newly opened owner cannot explain the first
arm's removal. The first-failure record itself has no failure QPC. It cannot
be placed precisely relative to the second arm, whose failure is QPC
8640231206 (`gm1948b-umd.log:15346`). This excludes the specific recorded
submit as the first-arm cause; it does not exclude other paging/DDI failures.

**PROVEN:** native guard failures occur earlier and cause UMD SetError calls.
EXP1948 system trace :12952 first records PID 1236's native refusal at QPC
2367909022; :14479-14483 show another refusal followed by ResourceCopyRegion
SetError(E_FAIL) at QPC 2955957536. More refusals precede GM's failure.
`umd_gpuva_windows.c:1724` emits the refusal; the generated D3D11 copy path
in `mesa/scripts/build-native-asahi-state.py:5462` propagates flush failure
through SetErrorCb (`:1480`). DWM and GM failures during the same pressure
window do not prove one adapter-wide Windows removal decision.

## Backing contract and ownership

VidMm owns placement, budgeting and eviction. The KMD owns the logical/native
page-table translation; the UMD owns residency holds and CPU/GPU coherence.
`render-admission/src/allocation_windows.c:511` documents the admitted
CPU-visible placement contract: local-only CPU-visible allocations failed
EXP1027/1266; `:534` admits aperture fallback. GPU-only/local canonical BOs
exclude it. `umd/src/umd_win32_screen.c:784` selects C75c direct storage;
`:787` selects native canonical plus private CPU storage for the D3D12 owner.
`:907` enables persistent-map dirty synchronization for that storage.

Asahi `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39`
sets UAT_PGBIT=14. Four arbitrary 4-KiB pages cannot become one native leaf.
The current native guard must remain: returning success without a coherent
mapping would submit invalid GPU work. No 4-KiB native-leaf register mode was
found or invented. m1n1/Mu initialization, interrupts, power and recovery remain
outside this offline change; no new firmware or broker contract is proposed.
Saved launch contract is EXP1948, m1n1 0d3a75b3, visible exact-package recovery.

## Failure-route audit

All paths below are source possibilities, not claims that each occurred.
The handoff DDI_FAILURE_SITES.txt indexes return/callback sites for inspection.

| Route | Source evidence | Interpretation |
|---|---|---|
| DxgkCbSetDeviceError | No call in render-admission | Not an explicit KMD removal source here. |
| Render DMA_PAGE_FAULTED | backend_platform_windows.c:3142-3189, :3454 | Only Dropped completion; timeout/fault recovery can report device error without reset flags. Saved run reports no firmware event. |
| Paging/present DMA_PAGE_FAULTED | paging_windows.c:280-293, :360 | Failed CPU present with a fault VA; no adapter/reset/invalid-fence flags. A CPU present fault need not be a firmware fault. |
| DMA_FAULTED | paging_windows.c:294-300 | Failed CPU paging with no fault VA reports this reserved interrupt. Source concern, but no saved execution failure proves it ran here. Do not replace it with false completion. |
| SubmitCommandVirtual | gpuva_g3_windows.c:4665-4799, :4024, :4561 | Envelope, parse, legacy, present and system paging failures propagate. Specific saved G4 refusal is later than GM arm A. |
| BuildPagingBuffer | paging_windows.c:36-140; gpuva_g3_paging_windows.c:1015-1182 | Shape, address resolution, unsupported endpoints, queue capacity and backing failures can return. Insufficient DMA buffer is multipass flow control, not inherently device removal. |
| UpdatePageTable / FlushTlb | gpuva_g3_paging_windows.c:1389-1413 | Illegal statuses are normalized only for these operations; owning process is poisoned. Later work can fail even though this DDI returned success. This requires no GPU fault. |
| MapCpuHostAperture | driver.c:34-126; memory_windows.c:21-58 | DDI not registered, CPU host-aperture support not set. Software aperture operations are BuildPagingBuffer operations, a different contract. |
| QueryAdapterInfo | lifecycle.c:654-1089; memory_windows.c:62-199 | Invalid input, insufficient output buffer, declaration/local-view failures, unsupported query. No explicit all-device removal callback. No failed query correlated with this run. |
| Present / source address | callbacks.c:129-299; present_windows.c:53-187; display.c:759-893 | Validation, insufficient DMA space, scanout queue/mapping failures. CPU present execution can fault through the notification route above. No causal timing evidence here. |
| STATUS_GRAPHICS_* | paging, present, GDI, display and lifecycle source | Mostly DMA capacity or display topology/status handling; not a blanket device-removal API. A complete source match list is included in the handoff. |
| Commitment / residency | allocation_windows.c:525; umd_gpuva_windows.c:397-437 | Pool reserve failure returns NO_MEMORY. Saved pool counters show zero reserve/commit/floor failures. MustSucceed is never set; CantTrimFurther can still fail above maximum budget (WDK d3dukmdt.h:1466-1469). Internal VidMm unrecoverable reason remains unknown. |

## Proposed behavior change; not authorized for commit

The handoff README protects C75c and explicitly requires proposed changes
that weaken it to be delivered separately without committing. The existing
safe storage path can be extended to unshared internal Encoder/Shader BOs:
CpuVisible=0 canonical allocation plus private CPU staging and dirty-page
uploads. General resource buffers keep direct storage; borrowed/shared paths
keep their policies. This prevents those internal allocations from acquiring
scattered GPU-resident backing. It costs command-buffer copies and does not
solve scattered General/Rodata backing or establish the first removal cause.

`PROPOSED-internal-command-staging.patch` in the handoff contains the exact
source and regression-test proposal. Its new policy expectation fails against
current production source and passes against a temporary candidate source
under ASan/UBSan. It has not been applied to this worktree or hardware.
The existing heap-coherence and private-map residency tests pass separately.
A user decision on the protected-behavior exception is pending. No driver fix
is claimed by this analysis commit.

## Next causal checkpoint

First inspect EXP1949's first device-error ETW event: QPC, process/device/context,
reason and preceding paging/submit status. Preserve per-device identities.
The current evidence cannot establish every internal dxgkrnl removal path:
no Windows implementation or kernel dump was inspected, and WDK declarations
are not an exhaustive implementation specification.

If the internal-staging exception is accepted, apply/test the proposal on
EXP1949 base, compile with the pinned WDK/native toolchains, then run the same
registry switches (Wom1GpuFaultInfoRead=1, Wom1PoolHalfSize=1), trim enabled,
descriptor guard enabled and validator record-only: 2k/240 s, then 10k/240 s
only after actual 2k frames. Require DWM survival and correct frames, not just
Ready or the absence of one refusal. Keep ETW around the first removal and
exact-package visible recovery. This experiment belongs to the integration
owner; no hardware action was performed here.
