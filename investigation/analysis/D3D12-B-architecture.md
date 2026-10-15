# D3D12 B: native AGX execution architecture

Scope: source comparison only. `HYPOTHESIS` marks a proposed mapping or an unverified contract. Evidence paths below use `MESA= /Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan` and `WDK=/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h`; citations retain the full path to be mechanically checkable.

## Memory: heaps, binding, and GPU VA

The WDK device table exposes heap/resource creation and residency/eviction (`/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:13529`, `:7195`, `:7196`). The existing D3D11 implementation keeps `pipe_resource` objects (`drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:232`, `:442`). Honeykrisp allocates device memory and binds buffers/images separately (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_device_memory.c:161`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_buffer.c:254`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_image.c:1342`); buffer binding reaches `agx_bo_bind` (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_buffer.c:266`). KMD registers GPUVA submission and root page table callbacks (`drivers/apple-agx/render-admission/src/driver.c:100`, `:106`) and processes PTE addresses (`drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:529`).

| Option | Fit |
|---|---|
| 1 Gallium | HYPOTHESIS: pipe resources obscure placed heap aliasing and runtime VA decisions; requires a parallel heap/VA model. |
| 2 honeykrisp | HYPOTHESIS: separate memory and bind objects are a useful model, but Linux BO/VA allocation must be replaced by a Windows runtime/KMD mapping. |
| 3 hybrid | HYPOTHESIS: retain Asahi layout and BO encoding, own D3D12 heap, placed/reserved resource, residency and VA records. |

HYPOTHESIS: the D3D12 runtime callbacks and GpuMmu path must be the source of GPU VA truth; the precise UMD VA callback contract still needs WDK callback-structure review. Do not let `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_buffer.c:266` independently bind a conflicting VA.

## Descriptors and root signatures

The WDK table has descriptor heap and root signature creation (`/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:13510`, `:13522`) and root descriptor table commands (`:3038`). Honeykrisp allocates an AGX descriptor-table BO (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_descriptor_table.c:25`, `:26`) and binds Vulkan sets via set addresses (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_buffer.c:366`, `:396`). D3D11 uses pipe views and a pipe context (`drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:516`, `:572`).

| Option | Fit |
|---|---|
| 1 Gallium | HYPOTHESIS: translate root tables into per-draw Gallium bindings; descriptor heap identity and update timing need extra tracking. |
| 2 honeykrisp | HYPOTHESIS: reuse physical descriptor encoding/table allocation, but Vulkan set layout and update semantics cannot be the D3D12 root signature ABI. |
| 3 hybrid | HYPOTHESIS: own D3D12 heap/root model and reuse AGX descriptor encoders where their lifetime and relocation rules pass tests. |

## Command lists, allocators, and barriers

The DDI exposes command allocators, command lists, close/reset and resource barriers (`/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:3102`, `:3111`, `:3001`, `:3002`, `:3019`). Honeykrisp records beginning/ending command buffers and pipeline barriers (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_buffer.c:258`, `:298`, `:328`); D3D11 holds a `pipe_context` (`drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:572`).

| Option | Fit |
|---|---|
| 1 Gallium | HYPOTHESIS: serial replay into an immediate context can prototype a list, but allocator reset, parallel recording and barrier visibility become emulation. |
| 2 honeykrisp | HYPOTHESIS: command buffer recording is the closest starting structure, with D3D12 barrier/state logic adapted before AGX encoding. |
| 3 hybrid | HYPOTHESIS: own allocator/list lifecycle and barrier tracker, reuse low-level AGX command emission from honeykrisp when isolatable. |

## Queues and fences

The DDI has command queue and execute-list entries plus synchronization object callbacks (`/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:13507`, `:2731`, `:18307`, `:18308`, `:18309`). The KMD registers virtual submission and current-fence query (`drivers/apple-agx/render-admission/src/driver.c:100`, `:88`); its submit path validates `SubmissionFenceId` and queues that fence (`drivers/apple-agx/render-admission/src/submission_windows.c:66`, `:215`). Its paging path handles a monitored-fence signal record (`drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:915`, `:969`). Honeykrisp queue uses DRM syncobj transfers and timeline waits (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_queue.c:58`, `:901`).

| Option | Fit |
|---|---|
| 1 Gallium | HYPOTHESIS: pipe flush fences need translation to queue-ordered monitored fence semantics. |
| 2 honeykrisp | HYPOTHESIS: retain command stream batching, replace DRM queue/syncobj transport entirely. |
| 3 hybrid | HYPOTHESIS: own Windows queue and fence bridge, reuse command stream encoding only. |

The searched KMD registration block (`drivers/apple-agx/render-admission/src/driver.c:66`, `:106`) contains no `DxgkDdiCreateSynchronizationObject`/signal/wait registration; synchronization-object callbacks shown above are UMD-to-runtime callbacks. HYPOTHESIS: KMD scheduler fence completion is sufficient for the first copy checkpoint, while the full monitored-fence contract needs proof.

## Shaders: DXBC now, DXIL later

The project exports `dxbc_to_nir` (`drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c:2845`), and D3D11 includes the translator (`drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:41`). Honeykrisp compiles NIR through `agx_compile_shader_nir` (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_shader.c:1066`, `:1194`).

| Option | Fit |
|---|---|
| 1 Gallium | HYPOTHESIS: reuse existing DXBC-NIR integration and Gallium shader state; D3D12 binding semantics still need new lowering. |
| 2 honeykrisp | HYPOTHESIS: feed project NIR into honeykrisp compile path, adapting descriptor/root lowering. |
| 3 hybrid | HYPOTHESIS: share DXBC parser, NIR and libagx compiler while owning D3D12-specific NIR binding lowering. |

HYPOTHESIS: SM 5.1 coverage and DXIL ingestion are separate gates; this source pass found no DXIL frontend in the named project translator (`drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c:2845`).

## D3D11 sharing and future Apple GPUs

D3D11 currently uses `pipe_resource` and `pipe_context` (`drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:232`, `:572`); its projection builds a Windows winsys source set (`drivers/apple-agx/mesa/scripts/build-native-asahi-state.py:5436`, `:5521`). Honeykrisp draws through chip-parameterized AGX emission (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_draw.c:3631`); Mesa's AGX decode table includes generation 14 (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/lib/decode.c:937`).

| Option | D3D11 sharing and portability |
|---|---|
| 1 Gallium | Shares the whole present D3D11 stack; HYPOTHESIS: explicit API emulation cost persists across chips. |
| 2 honeykrisp | HYPOTHESIS: duplicates most D3D11 frontend logic, shares compiler/libagx and possibly winsys adapters. |
| 3 hybrid | HYPOTHESIS: share compiler, layout, format conversion, BO transport and diagnostics; retain separate API frontends. |

G14 has concrete source handling above. HYPOTHESIS: G15 reuse depends on pinned Mesa support and Windows KMD capability; this pass found no evidence that this driver supports G15 hardware.

## Licensing

The proposed honeykrisp files checked here carry MIT SPDX headers: `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_buffer.c:5`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_draw.c:5`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_buffer.c:5`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_image.c:5`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_device_memory.c:5`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_descriptor_table.c:5`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_descriptor_set.c:5`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_shader.c:7`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_queue.c:13` under `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/`. HYPOTHESIS: Mesa as a whole is not safely characterized by one license; inspect every dependency and generated file before copying or linking code. No non-MIT header was observed in this checked set; broader dependency closure remains unreviewed. The project rule requires explicit compatibility review before copying external code (AGENTS.md, Source-first engineering).

## Recommendation

Choose **3, hybrid**: own the D3D12 ABI, memory/VA, root descriptors, list/allocator/barrier and Windows queue/fence layers; share DXBC-to-NIR, libagx compiler/layout and selectively isolated honeykrisp AGX descriptor/command encoding. HYPOTHESIS: this minimizes semantic translation compared with Gallium while avoiding a Vulkan/DRM runtime inside the Windows UMD. Evidence for the split: DDI explicit object surface (`/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:3102`, `:13529`), honeykrisp recording/binding (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_buffer.c:258`, `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_buffer.c:254`), DRM-specific queue (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_queue.c:58`), and KMD virtual submit (`drivers/apple-agx/render-admission/src/driver.c:100`).

## Phased plan with falsifiable checkpoints and failure criteria

1. **Device + copy:** implement only device creation, one copy queue submission and one completed fence. Fail if creation rejects, submit is rejected, bytes differ, fence stalls, or recovery cannot return to the known GPU-visible Code28 state. HYPOTHESIS: adapt existing KMD virtual submission (`drivers/apple-agx/render-admission/src/driver.c:100`) and fence path (`drivers/apple-agx/render-admission/src/submission_windows.c:215`). Preserve a recovery artifact and preregister hashes before hardware per AGENTS.md.
2. **Memory:** prove committed and placed buffer copies, aliasing, reserved mappings and residency/eviction separately. Fail on mismatched VA, wrong bytes or stale mapping. HYPOTHESIS: implement D3D12 ownership atop KMD G3 PTE updates (`drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:529`).
3. **Descriptors/compute:** one root constant, one table entry, one SM 5.1 DXBC compute shader, then descriptor update/rebind. Fail on stale descriptor or incorrect output. HYPOTHESIS: reuse encoding from `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_descriptor_table.c:25` with the project translator (`drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c:2845`).
4. **Graphics/barriers:** one draw, copy/render transitions, two command lists and allocator reuse. Fail on ordering, visibility or fence mismatch. HYPOTHESIS: compare AGX emission with `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_draw.c:3631` and barrier logic with `/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_buffer.c:328`.
5. **Coverage:** DXIL, full fence cross-queue waits, sparse resources and later GPUs only after separate source and hardware gates. HYPOTHESIS: each needs its own contract proof.

Each hardware step requires the AGENTS.md preregistration, one changed variable, evidence collection, exact package rollback and recovery rules; none is authorized by this document.

## Risks

- HYPOTHESIS: D3D12 runtime VA and KMD G3 page tables may disagree over allocation/bind lifetime; check against `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:529`.
- HYPOTHESIS: a DRM-derived queue may accidentally retain syncobj ordering assumptions (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_queue.c:58`).
- HYPOTHESIS: NIR descriptor lowering and D3D12 root signature semantics may diverge (`/Volumes/pwdev/public_windows/.local/reference/mesa/src/asahi/vulkan/hk_cmd_buffer.c:396`; `/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:13522`).
- HYPOTHESIS: sharing only libagx/compiler means two API state stacks to maintain (`drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp:572`).

## WHAT IS STILL UNKNOWN

- Exact D3D12 UMD/runtime VA mapping and residency callback sequence, including reserved resources; WDK table shows residency (`/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:7195`) but this pass did not establish a MapGpuVirtualAddress callback from this header. HYPOTHESIS: its contract is defined in another WDK interface/header.
- Whether the current KMD virtual submit accepts a copy-only command stream and whether its fence completion maps to a D3D12 monitored fence; source only proves registration/queueing (`drivers/apple-agx/render-admission/src/driver.c:100`; `drivers/apple-agx/render-admission/src/submission_windows.c:215`).
- Exact interface version and minimum D3D12 device callbacks accepted by the runtime; `/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d12umddi.h:13507` establishes one table entry, not runtime admission.
- License closure of honeykrisp dependencies beyond the checked MIT files, and G15 support in the pinned libagx/compiler/winsys.

## Architect decision (2026-10-15)

Accepted: option 3 (hybrid). The D3D12 UMD owns the D3D12 semantics
(ABI, heaps/placed/reserved resources and GPU VA, descriptor heaps and root
signatures, command allocators/lists and barriers, the Windows queue and
monitored-fence layer on the KMD's virtual submission); it shares the DXBC
-> NIR translator, the AGX compiler and libagx (layouts, lowering) with the
D3D10/11 UMD, and reuses honeykrisp's AGX descriptor and command encoding
(MIT) only through isolated, DRM-free pieces copied with their licence
headers after the per-file licence check AGENTS.md requires. Reasons: the
explicit D3D12 model maps onto honeykrisp's explicit encoding with less
semantic translation than Gallium's immediate context; honeykrisp/libagx
already target G13-G15, which keeps the stack reusable for later Apple
GPUs; the Vulkan/DRM queue and sync layers are not portable to WDDM and
are replaced, not wrapped. First checkpoint: phase 1 of the plan (device
creation + one copy-queue submission + one completed fence), after the
ABI-gate probe (D3D12-A1) has shown which interface version the 26100
runtime accepts.
