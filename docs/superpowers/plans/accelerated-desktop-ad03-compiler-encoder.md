# Accelerated Desktop AD03 Compiler/Encoder Implementation Plan

> Execute inline in the main process. Do not create subagents. Use
> systematic-debugging, TDD for deterministic contracts, and verification
> before each hardware claim.

**Goal:** Reuse the pinned Mesa D3D10 frontend, NIR/AGX compiler and Asahi
encoder to produce non-replay Apple AGX draw workloads through the AD02 WDDM
transport, while replacing Mesa's software Windows target and DRM/fd winsys
with one Windows-owned allocation/submission adapter.

**Current hardware base:** EXP651 proves two immutable allocation-relative
commands, two physical AGX jobs/fences, dynamic full/band output, two latches,
HOLD and retirement. AD03 must not change the retained-root broker, RTKit,
firmware, physical queue/completion or DCP owners.

**Pinned source:** Mesa commit `9aa1215f878b504f66159dd2ead4c7973142126e`,
repository `https://gitlab.freedesktop.org/mesa/mesa.git`, MIT license hash
`323c587d0ccf10e376f8bf9a7f31fb4ca6078105194b42e0b1e0ee2bc9bde71f`.

## Primary-source contract inspected

- `src/gallium/frontends/d3d10umd/Shader.cpp`: D3D10 vertex/pixel DXBC is
  translated to TGSI and passed to `pipe_context::create_vs_state` /
  `create_fs_state`.
- `src/gallium/frontends/d3d10umd/Draw.cpp`: Draw resolves state and calls the
  Gallium draw path; it does not own hardware submission.
- `src/gallium/targets/d3d10umd/d3d10_gdi.c` and Mesa top-level Meson: the
  shipped Windows target is explicitly softpipe/llvmpipe-only. It is a
  software control, not an AGX target.
- `src/gallium/drivers/asahi/agx_state.c`: TGSI is lowered with `tgsi_to_nir`,
  variants are compiled with `agx_compile_shader_nir`, and draw state builds
  vertex/fragment USC pipelines and the VDM control stream.
- `src/asahi/compiler/agx_compile.h/.c`: compiled shader output contains binary,
  main/preamble offsets, register counts, rodata and scratch requirements.
- `src/gallium/drivers/asahi/agx_pipe.c`: render finalization emits VDM,
  scissor/depth-bias, background/EOT USC, attachment and render-pass state.
- `src/gallium/drivers/asahi/agx_batch.c` and
  `include/drm-uapi/asahi_drm.h`: Linux submission attaches BOs, sync objects
  and a versioned render-pass description; raw firmware work-item ownership is
  not delegated to userspace.
- `src/gallium/drivers/asahi/agx_pipe.c::agx_screen_create`,
  `src/asahi/lib/agx_device.c`, `agx_bo.c`, and the Asahi DRM winsys: current
  screen/device/BO/VM/fence paths require DRM fd, ioctls and syncobj. None is a
  ready Windows hardware path.
- Current AppleAgx `apple_agx_backend_runtime`, `render_backend_image`,
  `apple_agx_g13_queue_provider`, retained-root broker and AD02 Win32 transport:
  Windows already owns WDDM allocations, validation, physical queue submission
  and exact fence completion. These remain the only backend owners.

## Ownership and translation

`D3D10 FRONTEND OWNED`: API objects, DXBC parsing, TGSI and Gallium state.

`MESA ASAHI OWNED`: TGSI→NIR lowering, AGX shader compiler, USC/VDM/PBE/texture/
sampler/scissor encoding and dependency discovery. Reused under its MIT license.

`WINDOWS UMD WINSYS OWNED`: runtime resources, WDDM callbacks, CPU mapping,
allocation-relative command construction, fence waits and reset generation.
It does not open DRM, submit ioctls, invent physical addresses or emulate AGX in
software.

`KMD/BROKER OWNED`: copy-once validation, allocation ownership/residency,
allowed class VA assignment, typed relocation resolution, retained-root
mapping, existing TA/3D queue publication, completion and Windows fence.

`FIRMWARE/DCP OWNED`: unchanged AGX execution and scanout.

The translation boundary is:

```text
DXBC → TGSI → NIR → AGX shader/encoder objects
     → immutable typed allocation-relative graph
     → KMD owner/range/relocation validation
     → existing broker mappings and TA/3D queue provider
     → physical completion/fence/output
```

No Linux DRM UAPI struct becomes the Windows ABI verbatim. It is a primary
reference for the information that must be represented; the Windows ABI stays
pointer-free, versioned, bounded and expressed in WDDM allocation references.

## Task 1 — lock the reusable Mesa surface and reject software/DRM shortcuts

**Files:** create
`drivers/apple-agx/mesa/mesa-ad03-source-contract.json`,
`tools/verify_apple_agx_ad03_source_contract.py`,
`tests/test_apple_agx_ad03_source_contract.py`.

- [x] Record every upstream file compiled or adapted, its exact path/license,
      classification (`FRONTEND`, `COMPILER`, `ENCODER`, `DRM_ONLY`,
      `SOFTWARE_ONLY`) and intended Windows replacement.
- [x] Machine-check the pinned commit, clean checkout and MIT hash.
- [x] Assert source signatures for DXBC→TGSI, TGSI→NIR, AGX compile, VDM/render
      finalization, software-only d3d target and DRM-only screen/BO/submit.
- [x] Fail if a proposed target links softpipe/llvmpipe, `gdi_create_sw_winsys`,
      DRM fd/ioctl/syncobj or reports software output as AGX.
- [x] Record generated-source commands and hashes before compiling Mesa files.

**Gate:** exact source inventory is complete and the verifier distinguishes
reusable compiler/encoder code from the two owners that must be replaced.

## Task 2 — deterministic AGX compiler fixture

**Files:** create under `drivers/apple-agx/mesa/compiler-host/` only build glue,
fixture inputs and project-owned serializers; use pinned Mesa sources in place.

- [x] Build the pinned NIR/AGX compiler and generated opcode/algebraic sources
      in a project-local reproducible environment; do not edit the reference
      checkout.
- [x] Compile two source-distinct compute NIR fixtures through the compiler core.
      Vertex/fragment output lowering is intentionally deferred to the Asahi
      driver/encoder adapter: calling the backend compiler directly on generic
      `store_output` bypasses required tilebuffer/UVS lowering and is invalid.
- [x] Export project-owned metadata: stage, binary bytes, main/preamble offsets,
      register counts, rodata, scratch and content hash. Reject unknown fields,
      overflows and nondeterministic uninitialized bytes.
- [x] Compile at least two source variants and require different shader binary
      or metadata hashes. Rebuild identical input twice and require equality.
- [x] Disassemble/validate with the same pinned compiler tooling; software pixel
      output is not a hardware result.

**Intermediate gate:** `AGX_COMPILER_CORE_OFFLINE_PROVEN=YES` after deterministic
compute fixtures. Full compiler readiness remained NO until the driver lowering
required for vertex/fragment programs was executed below. No KMD/capability/
hardware change.

Compiler update: commit 0aa10b4d35cbdb88554873434d318f4c35aaa632
executes the pinned Asahi VS prolog/UVS and FS epilog/sample-mask lowerings,
then compiles and disassembles deterministic source-distinct graphics-stage
binaries. `AGX_COMPILER_OFFLINE_PROVEN=YES`; this is still offline shader
evidence, not USC/VDM encoder or hardware Draw proof.

## Task 3 — Windows Asahi screen/BO/fence adapter

**Files:** extend `drivers/apple-agx/mesa/winsys/agx_win32_transport.[ch]`;
create `agx_win32_screen.[ch]`, `agx_win32_bo.[ch]` and portable tests.

- [x] Construct the Asahi device key/params from an exact read-only KMD query,
      not DRM GET_PARAMS and not guessed J313 constants.
- [ ] Replace BO allocate/map/bind/unbind with WDDM resource callbacks and AD02
      generation/ownership. Keep separate CPU mapping, residency and internal
      object reference counts.
- [ ] Replace Linux VM allocation with broker-approved per-class VA arenas;
      UMD sees opaque allocation-relative handles, KMD resolves actual VAs.
- [ ] Replace syncobj/fd with exact Windows context fence values and callback
      waits. Cross-process sharing remains explicit Windows handles.
- [ ] Make reset/teardown invalidate the generation and reject stale compiler,
      BO, graph or fence objects.
- [x] Build a `pipe_screen`/`pipe_context` without linking the software GDI
      winsys or calling DRM/fd functions.

Task3 progress: `pfnQueryAdapterInfoCb` now retrieves a versioned G13G/16K
device/class contract before GetCaps/CreateDevice; boot generation and UMD
context generation remain distinct. Portable class-buffer state validates
alignment/access/reset with opaque tokens. Internal logical class buffers now
use real `Allocate(hResource=NULL)` plus paired Lock/Unlock and exact handle-list
Deallocate callbacks over the existing KMD CPU-visible staging allocation path;
bounded tokens and teardown are tested with the real WDK callback structures.
Commit f396856ce4af0bd612b793a6b283ee4776a5ab9b replaces Linux
syncobj with bounded WDDM1.2+ `EnqueueCpuEvent` completion tokens ordered after
the same Windows context's Render work; timeout, failed insertion, retirement
and teardown are executable tests. The token is not misreported as the hidden
Dxgk scheduler fence ID. Commit
c574d40520befed878050a2f20a4131b758f936d constructs actual Mesa
`pipe_screen`/`pipe_context` types with conservative zero caps, bounded buffer
and BGRA8 resources and checked transfers over `AGX_WIN32_SCREEN`; clang
ASan/UBSan and MSVC14.44 analysis fixtures pass with no DRM/fd/software target.
It is not yet linked into the production ARM64 UMD; broker VA/typed relocation,
draw/encoder callbacks and typed graph submission remain the next boundary,
and the generic screen submit callback stays fail-closed.

Commit 29aa0281c7f0d3c93203bfc9e3ce9e9e61f13dc4 adds the prerequisite
versioned internal-allocation descriptor and retains General/Shader/Encoder
class and access through UMD Allocate and KMD Create/Open/Render facts.
Ordinary surface descriptors remain compatible. This is offline-proven only;
no class is mapped into a broker arena until the typed Draw graph passes the
Task4 policy.

**Gate:** fake-runtime BO/map/fence/reset tests and ARM64 analysis build GREEN;
pipeline mask remains0.

## Task 4 — typed draw graph ABI and KMD validation

**Files:** extend `apple_agx_win32_abi.[ch]`, `render_win32_transport.[ch]` and
their tests; create `apple_agx_dynamic_job.[ch]` and tests.

- [x] Add one versioned Draw opcode with bounded references for render target,
      vertex/index/constants, shader code/rodata, USC pipelines, descriptors,
      scissor/depth-bias and encoder stream.
- [x] Every pointer-like field is an allocation index plus checked offset/size;
      typed relocations have a finite allow-list for destination structure,
      width, alignment, access and target role.
- [x] Validate non-overlap, executable/read/write policy, device generation,
      active-display exclusion, attachment geometry/format and exact graph
      reachability before publishing state.
- [x] Copy command and graph bytes once. Mutation, duplicate/missing object,
      stale VA, relocation into opcode bytes, arithmetic overflow and rollback
      failures must be executable negative tests.
- [ ] KMD resolves VAs only after validation and reuses the existing retained-
      root broker and WDDM allocation lifetime. No UMD-supplied physical VA is
      accepted.

**Gate:** `DYNAMIC_JOB_ABI_OFFLINE_PROVEN=YES`; malformed graphs cannot reach
queue publication.

Task4 progress: commit c9c20e507fa84440a31c59f9315b4373a2549035
implements the versioned Draw graph, allocation-role/class policy, alias-aware
relocation validation, immutable command builder and rollback-safe copy-once
materializer. `DYNAMIC_JOB_ABI_OFFLINE_PROVEN=YES`. Production physical-range
reader/resolver callbacks and backend publication are intentionally absent;
validated Draw currently returns `STATUS_NOT_SUPPORTED`, so the final KMD
resolve/publication item remains open and no queue can observe the new graph.

Source review of the generated pack format found that raw64 relocation is not a
universal AGX pointer encoding. Commit
cbac2d60d629e71f1a241860536c6c17dc8715f3 adds exact USC shader32,
USC buffer40 and VDM pipeline32 encodings with control-bit preservation and
shader-base-relative validation. Commit
9c121992a768ab03119ca1a04ebed3bc6f850135 then serializes deterministic
Mesa USC pipelines and partial VDM/fragment state into88-byte pipeline and
68-byte encoder objects and verifies the five placeholder locations with the
same generated unpack code. Full PPP/render-pass/PBE/EOT and production
publication remain open.

## Task 5 — one encoder graph through the production backend

**Files:** create `render_dynamic_windows.c` and minimally generalize the
existing backend image/job builder. Do not create another RTKit/UAT/queue owner.

- [ ] Translate the validated graph into the current broker mapping inventory
      and existing `APPLE_AGX_BACKEND_JOB_IMAGE`/queue provider.
- [ ] Replace EXP208 captured-object assumptions only where a typed Mesa object
      supplies the same role; preserve firmware-private objects and runtime
      queue identity.
- [ ] Require exact VS/FS code, USC pipeline, VDM stream, BG/EOT store,
      attachment and completion-event reachability.
- [ ] Reverse cleanup owns only Windows-created mappings/allocations; failed
      relocation or partial publication rolls back without touching firmware
      mappings.
- [ ] Compare the serialized first triangle graph against Mesa's own encoder
      decode and the hardware-proven queue contract before hardware.

**Gate:** graph materialization, relocation, lifecycle and completion tests plus
WDK analysis/Universal/sign/version gates GREEN.

## Task 6 — staged hardware checkpoints

Each checkpoint gets its own preregistered EXP and one new invariant. Preserve
EXP651 as the dynamic-clear control; do not repeat it without contradictory
evidence.

- [ ] Triangle: two vertex-position or fragment-colour variants produce two
      distinct command/shader hashes, physical TA/3D, exact fences and predicted
      pixel regions. This is not merely clear/EOT replay.
- [ ] Textured draw: upload a small texture through a Windows allocation;
      descriptor/sampler/texture references and sampled output must correlate.
- [ ] Scissor plus alpha blend: destination load, blend state and guard regions
      match a deterministic oracle.
- [ ] Repeated varying frames: monotonic fences, no stale relocation/object,
      exact output, safe ping-pong/HOLD/retirement and healthy device.

Before every run record `WHY THIS HYPOTHESIS`, the four architecture sections,
exact source/overlay/build/artifact/platform/recovery hashes and the one changed
invariant. No pipeline cap is enabled in AD03. Hardware PASS requires physical
AGX counters/completion and exact output; a Mesa compiler artifact or software
rendered image is OFFLINE evidence only.

## Exit criteria

AD03 is `HW_PROVEN` only when triangle, texture, blend/scissor and repeated
varying frames execute through the shared production backend with exact fences
and output. Then AD04 completes all121 mandatory UMD rows and only that coherent
contract may publish feature-level10_0. DWM/standard Present remains AD05/AD06.

## First executable action

Implement Task1 source-contract JSON and validator. It must fail on the current
software-only `d3d10_gdi.c` as an AGX target while accepting frontend/compiler/
encoder reuse from the pinned checkout. Then build the Task2 compiler fixture;
do not start a hardware EXP or set a capability bit.
