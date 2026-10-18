# D3D12 C15: integrate pinned DXIL -> SPIR-V -> NIR -> Asahi

Updated2026-10-18. **Selected direction: integrate the reviewed pinned dxil-spirv
C API into the user-mode compiler owner, then use actual Mesa spirv_to_nir and
the existing Asahi shader-state path.** This is a staged implementation plan,
not an SM6 support declaration. No capability, DDI version or production source
is changed by this document. Classification: RENDER_ONLY shader compilation.

The consolidated [license decision](D3D12-C15-dxil-license-review.md) is **yes with
conditions** for dxil-spirv `8434b0639fb2a48ce13aac08a420ecce5d23df0e` and its exact
minimal runtime graph. Use the selected nine static archives, builtin LLVM parser,
CLI/native LLVM/misc CLI off, fixed parent SPIRV-Headers and complete notices.
Do not copy vkd3d-proton integration glue or replace the local SM4/5 DXBC frontend.

## Current evidence and ownership

The local DXBC path already owns SM4/5 translation and C17 static SM5.1 register
spaces. C18 component mapping and C19 shader-derived view dimensions are runtime
contracts to preserve. The DDI currently advertises only `D3D12DDI_SUPPORTED_0003`
(GetVersions in drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:73-81); GetCaps does not
answer `D3D12DDICAPS_TYPE_0011_SHADER_MODELS`. Core shader callbacks/table layout
are0003. An SM6 model-list response is not sufficient proof that this negotiated
ABI can accept every required DXIL shader-creation/companion contract.

Offline C15 evidence now exists:

- [Runtime/license inventory](D3D12-C15-runtime-license-inventory.md): exact83 TUs,
  103 reached project headers; selected union187 files, with complete N01–N14 notices.
- [Host/Windows build inventory](D3D12-C15-host-build-inventory.md): host C API build
  and parent-reported EXP1388 Windows ARM64 BUILD_ONLY PASS, nine archives/C caller
  link/PE and /MD. **No Windows ARM64 runtime execution** is implied.
- [EXP1389 standalone checkpoint](D3D12-C15-standalone-sm6-checkpoint.md): original
  cs_6_0 raw-u0 fixture -> reviewed host C API ->980-byte validated SPIR-V1.3 ->
  actual Mesa NIR -> CPU dataflow/guard oracle -> actual AGX backend68-byte binary.
  Its SSBO0_BASE preamble parameter is a **compiler-only symbolic fixture ABI**,
  not the production owner binding convention or GPU execution.

These observations select a concrete path and reduce compiler risk. They do not
establish general descriptors, Windows runtime ownership, shader-model conformance,
wave semantics, format support or execution correctness. The owner remains in
user mode; no translator enters KMD, broker, m1n1 or Mu. KMD/firmware power, DMA,
interrupt and recovery ownership does not change for this compiler integration.

## WINDOWS CONTRACT

Pinned source is WDK26100 `d3d12umddi.h`:

- `D3D12DDICAPS_TYPE_0011_SHADER_MODELS` is type1012; the data is
  `D3D12DDI_D3D12_SHADER_MODELS_DATA_0011` (:3503-3507), a count pointer and optional
  output array of supported model enums. It **enumerates models**; it is not the
  application-facing highest-model scalar query. Implement its sizing/capacity
  rules from the matching negotiated DDI, not a guessed structure or bool.
- Release model enums include `D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011=0x00060005` and
  later distinct release revisions (:3478-3500). Use a reviewed release enum,
  not an experimental value or the largest constant present in a new SDK.
- Options structures evolve by DDI revision, including WaveOps/lane limits,
  Int64Ops and later features. Derive the required device/table/callback/capability
  set from the exact supported interface before changing GetVersions or model list.
  Do not reinterpret a later structure through0003-sized storage.

Official [shader model6 overview](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-6-0)
and [wave features](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/hlsl-shader-model-6-0-features-for-direct3d-12)
introduce additional semantics; [OPTIONS1](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options1)
separately reports wave and Int64 availability. [Shader requirement flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/nf-d3d12shader-id3d12shaderreflection-getrequiresflags)
identify optional features required by a particular module. Model advertisement,
feature level and optional feature bits are distinct contracts. A module requiring
an unsupported separately queried feature must be refused with a precise reason;
passing one scalar32-bit compute shader does not justify a broader claim.

The runtime supplies owned-by-caller shader container bytes to shader creation.
The compiler must validate container/lengths, identify DXIL versus SHDR/SHEX by
bounded container parsing, copy/consume bytes during creation, and return only
owned shader metadata/state. Keep the existing public DXBC APIs and failure
behavior intact; no accidental fallback from a malformed DXIL part into DXBC.

## AGX/ASAHI CONTRACT

Actual Asahi consumes NIR; Mesa's microsoft/compiler emits DXIL in the other
direction and supplies no DXIL frontend. The chosen bridge is:

```
DXIL container
  -> pinned dxil-spirv public C API + explicit resource remappers
  -> owned SPIR-V + admitted capabilities/environment
  -> actual Mesa spirv_to_nir using AGX compiler options
  -> explicit Vulkan-descriptor-to-Gallium indexed-resource lowering
  -> owned NIR + resource map/dimensions
  -> existing pipe create_compute_state/create_vs_state/create_fs_state
  -> actual Gallium Asahi lowering/sysval tables + AGX compiler
```

The production path must use the existing native Gallium owner to upload actual
CBV/SRV/UAV/sampler state. Do **not** transplant EXP1389's symbolic preamble parameter
into production. That parameter deliberately bypassed a runtime binding question;
existing Asahi binding/SSBO/sysval/uniform passes own the real tables.

Inspected primary implementations: pinned Mesa compiler/spirv/nir_spirv.h and
vtn_variables.c, Vulkan vk_nir.c common finalization, lavapipe indexed-buffer
lowering, Asahi hk_shader.c for descriptor/explicit-IO sequencing, and Gallium
Asahi agx_state.c shader initialize/compile stages. Use actual passes and exact
supported address formats; preserve writes and validate NIR after each boundary.
Do not copy external algorithms into the repository or erase unsupported intrinsics
to force compilation. Review any additional Mesa source/license/build graph before
adding it to the Windows runtime.

## TRANSLATION: logical keys, SPIR-V sets and physical slots

Use the same semantic resource key as C17: **(binding kind, register space,
register index)**, per shader stage/module. DXIL resource IDs are parser-local
identifiers, not a substitute for this key. Keep register spaces even when every
resource uses register0. Dense physical slots are assigned per kind within actual
owner limits (currently CBV14/SRV128/UAV8/sampler16), after collecting/checking
resources; use deterministic ordering and reject duplicate/ambiguous declarations,
overflow, unsupported dynamic/unbounded selection or malformed ranges.

Proposed fixed adapter descriptor namespaces:

| Logical kind | SPIR-V descriptor set | Binding | Production NIR/owner destination |
| --- | ---: | --- | --- |
| CBV bN, spaceS | 0 | that CBV's dense physical slot | UBO/constant-buffer slot; root constants resolve through their CBV key |
| SRV tN, spaceS | 1 | that SRV's dense physical slot | sampler-view/texture slot; raw/structured SRV buffer accesses match the existing buffer-texture model |
| UAV uN, spaceS | 2 | that UAV's dense physical slot | raw/structured SSBO slot or typed image slot, according to declared kind/format |
| sampler sN, spaceS | 3 | that sampler's dense physical slot | sampler-state slot, including resolved static samplers |

Example: `u0/space1` and `u0/space2` become set2/binding0 and set2/binding1,
respectively; `b0/space3` becomes set0/binding0. The compiled map retains both
logical UAV keys and their distinct slots. Root resolution still matches the
logical key and visibility before touching that stage's physical arrays. SPIR-V
set numbers are internal adapter conventions; they are not D3D12 register spaces.

Resource **kind** also controls the representation. The current owner binds SRVs
as sampler views and raw UAVs as shader buffers. Existing DXBC mem_load uses a
buffer-texture fetch for raw SRV but load_ssbo for raw UAV. Therefore do not remap
all raw buffers into one SSBO namespace merely because the C API permits it:
SRVslot0 and UAVslot0 would otherwise alias in one backend table. Prefer the C API's
texel-buffer representation for raw/structured SRVs, SSBO for raw/structured UAVs,
and typed texture/image representation for typed resources, with format/alignment
proof and explicit NIR lowering. Initially refuse a representation not yet proved.

Collect resource metadata before conversion and validate each callback against the
precomputed map; do not assign semantic identity by callback arrival order. Start
with singleton static resources to validate the bridge, while leaving SM6 caps
unchanged. Extend bounded arrays only with complete range/index/limit proofs before
claiming any model whose required contract includes them. Keep root descriptor,
table, static sampler, visibility, format/component mapping and range checks from
the production resolver; a translator callback cannot authorize missing runtime state.

Gather actual NIR texture dimensions and cross-check DXIL resource kinds before
shader-state creation destroys NIR. Store map/dimension metadata by value in the
compute/PSO object, separately for VS/PS, like C17/C19. Do not borrow translator,
SPIR-V or NIR pointers. One-element cube-array metadata must stay distinguishable
from cube; raw/structured component-mapping restrictions and exact typed extents
remain in force.

## Implementation sequence and deterministic gates

1. **Immutable dependency/package integration.** Adopt the reviewed lock and
   explicit selected targets in the Windows build; verify source/include hashes,
   nine archives, C API symbols, same ARM64 toolchain and CRT policy (including actual MSVCP140/VCRUNTIME140/UCRT deployment obligations). Package
   component notices before signing. No default recursive import/install, native
   LLVM or optional CLI dependency expansion.
2. **Owned frontend adapter with capabilities unchanged.** Detect DXIL containers,
   begin per-thread allocator context, parse/create/configure converter, copy
   output before converter/blob free, reset/end context, then feed owned words to
   Mesa. Keep all cleanup/error paths and threading/reentrancy explicit. Add exact
   module-feature refusals and bounded diagnostics; no borrowed allocations cross
   public allocator/free boundaries.
3. **Binding/NIR bridge and owner integration.** Implement the table above, prove
   each admitted resource representation and descriptor address format, retain
   logical maps and dimension metadata, then hand owned NIR to the existing pipe
   state path. Test actual production resolver/owner code, not a duplicate mapper.
4. **Broaden offline semantics.** Original fixtures cover compute and VS/PS, multiple
   spaces/classes, root/table/static samplers, required buffer/image operations,
   control flow, memory ordering and every mandatory model feature. Validate
   SPIR-V for the explicitly chosen environment, NIR before/after lowering, actual
   AGX nonempty binaries, negative unsupported features and allocation lifetimes.
   Establish wave/quad and precision/Int64 semantics according to actual advertised
   support; do not map Vulkan defaults blindly to D3D requirements.
5. **Truthful DDI/model rollout.** Review exact negotiated Windows interface/table
   upgrade and model-list/count ABI plus all companion mandatory fields/DDIs.
   Only after the required model contract is implemented and offline gates pass,
   advertise the reviewed release model and separately justified optional bits.
   Preserve production/diagnostic behavior equivalence. Never flip an SM6 bit to
   discover an already documented ABI or to deliver one test shader to a partial
   compiler. Record an atomic contract only where WDK requires the fields together.
6. **Preregistered hardware verification and packaging.** Fresh-device SM6 compute
   writes64 words `3*i+7` with untouched tail sentinels; then independent VS/PS
   triangle and multi-space resource checkpoints. Use existing hash/sign/profile
   gates, known-good recovery and experiment-local package cleanup. Parent owns
   experiment IDs, BEFORE/ACTUAL, CHANGES and hardware coordination.

Deterministic regressions must catch actual namespace aliasing, stale ownership,
malformed containers/ranges, typed representation mismatches and changed shader
semantics. Reuse EXP1389's original fixture/dataflow oracle, but test the production
adapter and real owner tables once present. Do not treat a fixture-only symbolic
ABI as that runtime regression.

## WHAT IS STILL UNKNOWN / acceptance conditions

The route is selected; unresolved work is concrete implementation and proof:
production descriptor/pointer/owner mapping, Windows runtime C API/CRT/lifetime,
complete required feature semantics and exact DDI version/model-capability contract,
and signed artifact delivery of notices. Optional native LLVM/CLI/test graphs stay
excluded under the consolidated review. Host/ARM64 build and EXP1389 compiler-only
success narrow risk but do not close those contracts.

Acceptance is a notice-complete immutable Windows runtime build, deterministic
logical-key-to-physical-state mapping with owned metadata, validated SPIR-V/NIR/AGX
for the required feature surface, truthful model/optional capability reporting,
and exact fresh-device GPU oracles with recovery evidence. Until then preserve the
current DXBC baseline and unchanged shader-model admission.

## Subsequent primary-source gates for the selected route

The bounded [DDI/model contract review](D3D12-C15-ddi-model-contract.md) identifies
R1/0011 as the smallest explicitly versioned candidate in the pinned WDK, without
claiming a proved universal cross-version minimum. Its typed CORE_0010 adapters,
PSO destination-handle argument move, model count/list, and shader program-body
versus application-container distinction must be addressed together. FillDDITable's
last UINT is a table index, not a negotiated interface version.

The [pipeline-library gate](D3D12-C15-pipeline-library-gate.md) found no normative
early-R1 absence field authorizing six generic failing callbacks. Preserve0003
until a coherent owned CPU library/reference/serialization implementation or an
explicit documented gate exists. Public API optionality and later driver-managed
shader cache controls do not establish that early DDI mapping.

The [owner threading contract](D3D12-C15-owner-threading-contract.md) establishes
free-threaded device resource creation. Before production adapter integration,
prove one-owner publication, heap-registry lifetime and actual shared Gallium
context usage under that call model. Correct initial zeroing remains a separate
already-fixed invariant; none of this attributes EXP1381's poison value to a race.
These are implementation and offline regression gates, not reasons to probe caps
or reinterpret the compiler-only EXP1389 result as runtime readiness.

The [owned converter contract](D3D12-C15-owned-converter-contract.md) derives the
actual C API scan, frozen binding map, reported-error cleanup and same-thread TLS
lifetime. Resource scan success does not prove complete traversal: its adapter
needs sticky rejection. Resource representation must survive the SPIR-V/NIR bridge;
descriptor sets alone cannot prevent SRV/UAV aliasing after lowering.

The reviewed pin has no supported recoverable internal OOM contract. In
particular, failed arena backing allocation and unchecked custom allocator
returns are not repaired by an outer C facade or enabling exceptions alone.
Production UMD integration is gated on a separately reviewed upstream fix/new pin
with checked allocation, safe partial-object ownership and actual-library fault
injection. Preserve MIT notices and pin/hash every external delta; do not copy
external implementation into this repository. A bounded original offline adapter
may prove normal success and reported errors while explicitly retaining this
production blocker. The Windows MSVC exception policy must be measured separately
from the existing host build's confirmed -fno-exceptions flag.

A positive [owned pipeline recipe/library design](D3D12-C15-owned-pipeline-library-contract.md)
now separates pointer-free CPU components, deduplication and bounded private wire
encoding from the typed runtime resolver. Its resource limits are proposed CPU
policies. One bounded primary-source follow-up did not establish conditional
LibraryReference field elision, repeated-index rules, runtime opaque-payload
framing or DDI-specific mismatch propagation. Internal CPU storage tests alone
cannot close those typed boundaries or justify a negotiation/capability change;
retain the existing0003 path until the complete selected contract is established.
