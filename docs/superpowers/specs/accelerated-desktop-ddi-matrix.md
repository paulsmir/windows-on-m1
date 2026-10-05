# Accelerated desktop DDI obligation matrix

Status: AD01 planning inventory, not implementation or hardware proof.

Selected contract: **D3D10_0_DDI_INTERFACE_VERSION**, **D3D_FEATURE_LEVEL_10_0**, pipeline mask `1 << D3D11DDI_3DPIPELINELEVEL_10_0 == 1`. The advertised mask remains **0** until every mandatory row is implemented and tested.

Frontend decision: **mesa-d3d-reuse**. Pinned Mesa already supplies the full D3D10 device-function table and DXBC→TGSI state tracker; Asahi's vertex/fragment path explicitly accepts TGSI and converts it with `tgsi_to_nir`. We reject `thin-d3d-over-mesa` because it would duplicate that entire DDI/state surface. This decision does not reuse Mesa's software target: Windows must replace DRM/fd BO, sync, submit and fence ownership with the AD02 transport. Existing Mesa `OpenResource` failure and `DXGI_STATUS_NO_REDIRECTION` are gaps, not acceptable desktop behavior.

Primary sources inspected: pinned WDK 26100 `d3d10umddi.h` and `dxgiddi.h`; Microsoft feature-level, pipeline, required-format, resource-lifetime and initialization contracts; pinned Mesa `d3d10umd/{README,Adapter,Device,Resource,Shader}`; Mesa Asahi `agx_state.c`, `agx_pipe.c` and `agx_device.c`.

The 101 D3D10 device callbacks below are exactly the non-PSGP fields of WDK 26100 `D3D10DDI_DEVICEFUNCS`. Every row is mandatory for this chosen full table. Semantic limits/formats, the adapter/DXGI surfaces, frontend translation, Windows winsys and desktop sharing are listed separately. “Planned owner” may not exist yet; all such rows remain NOT IMPLEMENTED.

| ID | Class | Planned owner | Test | Status |
|---|---|---|---|---|
| adapter-open10 | UMD adapter | `drivers/apple-agx/mesa/d3d10umd/Adapter.cpp` | `ad01-adapter-open10` | NOT IMPLEMENTED |
| adapter-open10-2 | UMD adapter | `drivers/apple-agx/mesa/d3d10umd/Adapter.cpp` | `ad01-adapter-open10-2` | NOT IMPLEMENTED |
| adapter-calc-device | UMD adapter | `drivers/apple-agx/mesa/d3d10umd/Adapter.cpp` | `ad01-adapter-calc-device` | NOT IMPLEMENTED |
| adapter-create-device | UMD adapter | `drivers/apple-agx/mesa/d3d10umd/Adapter.cpp` | `ad01-adapter-create-device` | NOT IMPLEMENTED |
| adapter-close | UMD adapter | `drivers/apple-agx/mesa/d3d10umd/Adapter.cpp` | `ad01-adapter-close` | NOT IMPLEMENTED |
| adapter-version-caps | UMD adapter | `drivers/apple-agx/mesa/d3d10umd/Adapter.cpp` | `ad01-adapter-version-caps` | NOT IMPLEMENTED |
| ddi-default-constant-buffer-update-subresource-up | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-default-constant-buffer-update-subresource-up` | NOT IMPLEMENTED |
| ddi-vs-set-constant-buffers | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-vs-set-constant-buffers` | NOT IMPLEMENTED |
| ddi-ps-set-shader-resources | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-ps-set-shader-resources` | NOT IMPLEMENTED |
| ddi-ps-set-shader | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-ps-set-shader` | NOT IMPLEMENTED |
| ddi-ps-set-samplers | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-ps-set-samplers` | NOT IMPLEMENTED |
| ddi-vs-set-shader | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-vs-set-shader` | NOT IMPLEMENTED |
| ddi-draw-indexed | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-draw-indexed` | NOT IMPLEMENTED |
| ddi-draw | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-draw` | NOT IMPLEMENTED |
| ddi-dynamic-iabuffer-map-no-overwrite | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-dynamic-iabuffer-map-no-overwrite` | NOT IMPLEMENTED |
| ddi-dynamic-iabuffer-unmap | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-dynamic-iabuffer-unmap` | NOT IMPLEMENTED |
| ddi-dynamic-constant-buffer-map-discard | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-dynamic-constant-buffer-map-discard` | NOT IMPLEMENTED |
| ddi-dynamic-iabuffer-map-discard | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-dynamic-iabuffer-map-discard` | NOT IMPLEMENTED |
| ddi-dynamic-constant-buffer-unmap | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-dynamic-constant-buffer-unmap` | NOT IMPLEMENTED |
| ddi-ps-set-constant-buffers | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-ps-set-constant-buffers` | NOT IMPLEMENTED |
| ddi-ia-set-input-layout | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-ia-set-input-layout` | NOT IMPLEMENTED |
| ddi-ia-set-vertex-buffers | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-ia-set-vertex-buffers` | NOT IMPLEMENTED |
| ddi-ia-set-index-buffer | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-ia-set-index-buffer` | NOT IMPLEMENTED |
| ddi-draw-indexed-instanced | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-draw-indexed-instanced` | NOT IMPLEMENTED |
| ddi-draw-instanced | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-draw-instanced` | NOT IMPLEMENTED |
| ddi-dynamic-resource-map-discard | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-dynamic-resource-map-discard` | NOT IMPLEMENTED |
| ddi-dynamic-resource-unmap | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-dynamic-resource-unmap` | NOT IMPLEMENTED |
| ddi-gs-set-constant-buffers | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-gs-set-constant-buffers` | NOT IMPLEMENTED |
| ddi-gs-set-shader | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-gs-set-shader` | NOT IMPLEMENTED |
| ddi-ia-set-topology | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-ia-set-topology` | NOT IMPLEMENTED |
| ddi-staging-resource-map | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-staging-resource-map` | NOT IMPLEMENTED |
| ddi-staging-resource-unmap | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-staging-resource-unmap` | NOT IMPLEMENTED |
| ddi-vs-set-shader-resources | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-vs-set-shader-resources` | NOT IMPLEMENTED |
| ddi-vs-set-samplers | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-vs-set-samplers` | NOT IMPLEMENTED |
| ddi-gs-set-shader-resources | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-gs-set-shader-resources` | NOT IMPLEMENTED |
| ddi-gs-set-samplers | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-gs-set-samplers` | NOT IMPLEMENTED |
| ddi-set-render-targets | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-set-render-targets` | NOT IMPLEMENTED |
| ddi-shader-resource-view-read-after-write-hazard | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-shader-resource-view-read-after-write-hazard` | NOT IMPLEMENTED |
| ddi-resource-read-after-write-hazard | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-read-after-write-hazard` | NOT IMPLEMENTED |
| ddi-set-blend-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-set-blend-state` | NOT IMPLEMENTED |
| ddi-set-depth-stencil-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-set-depth-stencil-state` | NOT IMPLEMENTED |
| ddi-set-rasterizer-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-set-rasterizer-state` | NOT IMPLEMENTED |
| ddi-query-end | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-query-end` | NOT IMPLEMENTED |
| ddi-query-begin | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-query-begin` | NOT IMPLEMENTED |
| ddi-resource-copy-region | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-copy-region` | NOT IMPLEMENTED |
| ddi-resource-update-subresource-up | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-update-subresource-up` | NOT IMPLEMENTED |
| ddi-so-set-targets | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-so-set-targets` | NOT IMPLEMENTED |
| ddi-draw-auto | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Draw.cpp` | `ad04-draw-auto` | NOT IMPLEMENTED |
| ddi-set-viewports | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-set-viewports` | NOT IMPLEMENTED |
| ddi-set-scissor-rects | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-set-scissor-rects` | NOT IMPLEMENTED |
| ddi-clear-render-target-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-clear-render-target-view` | NOT IMPLEMENTED |
| ddi-clear-depth-stencil-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-clear-depth-stencil-view` | NOT IMPLEMENTED |
| ddi-set-predication | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-set-predication` | NOT IMPLEMENTED |
| ddi-query-get-data | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-query-get-data` | NOT IMPLEMENTED |
| ddi-flush | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Device.cpp` | `ad04-flush` | NOT IMPLEMENTED |
| ddi-gen-mips | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Device.cpp` | `ad04-gen-mips` | NOT IMPLEMENTED |
| ddi-resource-copy | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-copy` | NOT IMPLEMENTED |
| ddi-resource-resolve-subresource | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-resolve-subresource` | NOT IMPLEMENTED |
| ddi-resource-map | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-map` | NOT IMPLEMENTED |
| ddi-resource-unmap | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-unmap` | NOT IMPLEMENTED |
| ddi-resource-is-staging-busy | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-resource-is-staging-busy` | NOT IMPLEMENTED |
| ddi-relocate-device-funcs | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Device.cpp` | `ad04-relocate-device-funcs` | NOT IMPLEMENTED |
| ddi-calc-private-resource-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-calc-private-resource-size` | NOT IMPLEMENTED |
| ddi-calc-private-opened-resource-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-calc-private-opened-resource-size` | NOT IMPLEMENTED |
| ddi-create-resource | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-create-resource` | NOT IMPLEMENTED |
| ddi-open-resource | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-open-resource` | NOT IMPLEMENTED |
| ddi-destroy-resource | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-destroy-resource` | NOT IMPLEMENTED |
| ddi-calc-private-shader-resource-view-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-calc-private-shader-resource-view-size` | NOT IMPLEMENTED |
| ddi-create-shader-resource-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-create-shader-resource-view` | NOT IMPLEMENTED |
| ddi-destroy-shader-resource-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-destroy-shader-resource-view` | NOT IMPLEMENTED |
| ddi-calc-private-render-target-view-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-calc-private-render-target-view-size` | NOT IMPLEMENTED |
| ddi-create-render-target-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-create-render-target-view` | NOT IMPLEMENTED |
| ddi-destroy-render-target-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-destroy-render-target-view` | NOT IMPLEMENTED |
| ddi-calc-private-depth-stencil-view-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-calc-private-depth-stencil-view-size` | NOT IMPLEMENTED |
| ddi-create-depth-stencil-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-create-depth-stencil-view` | NOT IMPLEMENTED |
| ddi-destroy-depth-stencil-view | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-destroy-depth-stencil-view` | NOT IMPLEMENTED |
| ddi-calc-private-element-layout-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Device.cpp` | `ad04-calc-private-element-layout-size` | NOT IMPLEMENTED |
| ddi-create-element-layout | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Device.cpp` | `ad04-create-element-layout` | NOT IMPLEMENTED |
| ddi-destroy-element-layout | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Device.cpp` | `ad04-destroy-element-layout` | NOT IMPLEMENTED |
| ddi-calc-private-blend-state-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-calc-private-blend-state-size` | NOT IMPLEMENTED |
| ddi-create-blend-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-create-blend-state` | NOT IMPLEMENTED |
| ddi-destroy-blend-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-destroy-blend-state` | NOT IMPLEMENTED |
| ddi-calc-private-depth-stencil-state-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-calc-private-depth-stencil-state-size` | NOT IMPLEMENTED |
| ddi-create-depth-stencil-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-create-depth-stencil-state` | NOT IMPLEMENTED |
| ddi-destroy-depth-stencil-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-destroy-depth-stencil-state` | NOT IMPLEMENTED |
| ddi-calc-private-rasterizer-state-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-calc-private-rasterizer-state-size` | NOT IMPLEMENTED |
| ddi-create-rasterizer-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-create-rasterizer-state` | NOT IMPLEMENTED |
| ddi-destroy-rasterizer-state | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-destroy-rasterizer-state` | NOT IMPLEMENTED |
| ddi-calc-private-shader-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-calc-private-shader-size` | NOT IMPLEMENTED |
| ddi-create-vertex-shader | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-create-vertex-shader` | NOT IMPLEMENTED |
| ddi-create-geometry-shader | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-create-geometry-shader` | NOT IMPLEMENTED |
| ddi-create-pixel-shader | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-create-pixel-shader` | NOT IMPLEMENTED |
| ddi-calc-private-geometry-shader-with-stream-output | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-calc-private-geometry-shader-with-stream-output` | NOT IMPLEMENTED |
| ddi-create-geometry-shader-with-stream-output | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-create-geometry-shader-with-stream-output` | NOT IMPLEMENTED |
| ddi-destroy-shader | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-destroy-shader` | NOT IMPLEMENTED |
| ddi-calc-private-sampler-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-calc-private-sampler-size` | NOT IMPLEMENTED |
| ddi-create-sampler | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-create-sampler` | NOT IMPLEMENTED |
| ddi-destroy-sampler | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad04-destroy-sampler` | NOT IMPLEMENTED |
| ddi-calc-private-query-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-calc-private-query-size` | NOT IMPLEMENTED |
| ddi-create-query | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-create-query` | NOT IMPLEMENTED |
| ddi-destroy-query | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-destroy-query` | NOT IMPLEMENTED |
| ddi-check-format-support | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-check-format-support` | NOT IMPLEMENTED |
| ddi-check-multisample-quality-levels | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-check-multisample-quality-levels` | NOT IMPLEMENTED |
| ddi-check-counter-info | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-check-counter-info` | NOT IMPLEMENTED |
| ddi-check-counter | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Query.cpp` | `ad04-check-counter` | NOT IMPLEMENTED |
| ddi-destroy-device | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/Device.cpp` | `ad04-destroy-device` | NOT IMPLEMENTED |
| ddi-set-text-filter-size | D3D10 UMD DDI | `drivers/apple-agx/mesa/d3d10umd/State.cpp` | `ad04-set-text-filter-size` | NOT IMPLEMENTED |
| dxgi-present | DXGI DDI | `drivers/apple-agx/mesa/d3d10umd/DxgiFns.cpp` | `ad06-present` | NOT IMPLEMENTED |
| dxgi-get-gamma-caps | DXGI DDI | `drivers/apple-agx/mesa/d3d10umd/DxgiFns.cpp` | `ad06-get-gamma-caps` | NOT IMPLEMENTED |
| dxgi-set-display-mode | DXGI DDI | `drivers/apple-agx/mesa/d3d10umd/DxgiFns.cpp` | `ad06-set-display-mode` | NOT IMPLEMENTED |
| dxgi-set-resource-priority | DXGI DDI | `drivers/apple-agx/mesa/d3d10umd/DxgiFns.cpp` | `ad06-set-resource-priority` | NOT IMPLEMENTED |
| dxgi-query-resource-residency | DXGI DDI | `drivers/apple-agx/mesa/d3d10umd/DxgiFns.cpp` | `ad06-query-resource-residency` | NOT IMPLEMENTED |
| dxgi-rotate-resource-identities | DXGI DDI | `drivers/apple-agx/mesa/d3d10umd/DxgiFns.cpp` | `ad06-rotate-resource-identities` | NOT IMPLEMENTED |
| dxgi-blt | DXGI DDI | `drivers/apple-agx/mesa/d3d10umd/DxgiFns.cpp` | `ad06-blt` | NOT IMPLEMENTED |
| feature-sm4-vs-ps-gs | Feature semantics | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad03-shader-model-4` | NOT IMPLEMENTED |
| feature-resource-limits | Feature semantics | `drivers/apple-agx/mesa/d3d10umd/Resource.cpp` | `ad04-d3d10-required-limits` | NOT IMPLEMENTED |
| feature-required-formats | Feature semantics | `drivers/apple-agx/mesa/d3d10umd/Format.cpp` | `ad04-required-dxgi-formats` | NOT IMPLEMENTED |
| frontend-dxbc-tgsi | Frontend translation | `drivers/apple-agx/mesa/d3d10umd/Shader.cpp` | `ad03-dxbc-to-tgsi` | NOT IMPLEMENTED |
| backend-tgsi-nir-agx | Backend translation | `drivers/apple-agx/mesa/winsys/agx_win32_screen.c` | `ad03-tgsi-nir-agx` | NOT IMPLEMENTED |
| backend-windows-winsys | Windows winsys | `drivers/apple-agx/mesa/winsys/agx_win32_transport.c` | `ad02-windows-winsys` | NOT IMPLEMENTED |
| desktop-cross-process-sharing | Desktop resource sharing | `drivers/apple-agx/mesa/winsys/agx_win32_transport.c` | `ad06-cross-process-sharing` | NOT IMPLEMENTED |

Inventory total: 121 mandatory rows (101 exact D3D10 device callbacks). The JSON contract is authoritative for machine validation. A synthetic complete test validates the guard only and never changes real readiness.

