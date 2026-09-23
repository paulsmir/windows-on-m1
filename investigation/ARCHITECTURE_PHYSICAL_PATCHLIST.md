# Physical/patch-list DWM admission milestone — 2026-09-23

Frozen source: clean `05646147b9787045d9860a097fbc854cddd93d27`, tag
`milestone/physical-patchlist-dwm-admission`. This is a preserved implementation
milestone, not a working accelerated DWM desktop.

## Built contract

The real Asahi producer feeds typed graph capture, immutable request
materialization, a UMD composer and `pfnRenderCb`, then a physical/patch-list
KMD `Render/Patch/Submit` path to AGX. Capture records address-bearing native
resources, and KMD resolves allocation-list placements. The temporary bridge
splits consecutive draws into one native batch each. Fixed render context 63,
per-feature relocations, and capture materialization belong to this milestone.

## Hardware evidence and last DWM verdict

| Experiment | Measured result | Limit |
|---|---|---|
| EXP730 | Standard-client AGX submission/completion path reached under the physical contract; retain its exact ledger receipt as the qualification baseline. | It is not DWM acceptance. |
| EXP736 | Coherent installed UMD accepted the observed Blt Present flag in the standard client. | DWM device creation still failed. |
| EXP751 | Exact System32 UMD reached native shader creation; indexable TEMP assertion in `tgsi_to_nir` identified. | No accepted desktop or post-login observation. |
| EXP752 | Indexable TEMP assertion cleared; next `load_vertex_id_zero_base` Asahi assertion identified. | No accepted desktop. |
| EXP753 | DWM selected Apple AGX HWDEVICE at FL10_0 and loaded exact System32 UMD; next failure was `CreateBuffer`. | No DWM-correlated graph, AGX completion, or standard Present. |
| EXP754 | DWM passed dynamic buffers, first draw and clear; second draw led to bad-UMD `E_NOTIMPL`. | No DWM-correlated AGX completion or Present. |
| EXP755 | One-draw split did not clear draw-time `E_NOTIMPL`; exact frontend reject remained unknown. | Rejected without causal advance. |
| EXP756 | Exact reject was `CreateResource` buffer-usage admission (`E_NOTIMPL`); offline buffer SRV/RT/typed-load fix later passed. | No DWM-correlated AGX completion or Present. |
| EXP757 | Frontend/typed-buffer fix passed offline and was committed. | Package preregistration and Air run were cancelled by user decision; no hardware verdict. |

Last hardware verdict is EXP756 `REJECTED_WITH_CAUSAL_ADVANCE`. Stable, visibly
correct accelerated DWM remains unproven. Do not interpret background/fallback
pixels or a retained pre-DWM Present receipt as DWM AGX execution. The exact
experiment records in `investigation/EXPERIMENTS.md` and the compact
`GPU_CURRENT_STATE.md` govern details.

## GPUVA disposition

Carry forward unchanged: m1n1/Mu boot and recovery, signing/package workflow,
KMD lifecycle, VidPn/scanout/VSync, interrupts/DPC, fence/completion,
TA/3D/RTKit/TDR infrastructure and GDI RenderKm; D3D10.0_x/DXGI1.1 admission,
BGR/shared/NO_REDIRECTION paths, shader fixes, DDI buffer/Map/topology/slot
contracts, R5 diagnostics, Asahi compiler Windows builds and `UmdContractTest`.
Each path still requires its own GPUVA integration proof.

Freeze as historical physical-path code: graph/typed capture, materializer,
per-feature relocation, patch-list contexts, one-draw-per-batch bridge, and
fixed render context 63. Keep source and evidence intact. GPUVA process roots,
VidMm-owned mappings, virtual submission and broker slot leasing replace their
runtime roles only after their separate offline and hardware gates pass.
