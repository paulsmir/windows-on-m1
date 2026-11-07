# D3D12 C44: DXIL (Shader Model 6.x) shaders

## Why now

Every shader the D3D12 path accepts today is DXBC (SM 4.0-5.1, translated by
`mesa/dxbc/dxbc_to_nir.c`). Modern D3D12 titles ship DXIL (SM 6.0+, compiled
by DXC); the runtime hands such bytecode to the UMD only when the driver
reports SM 6.x through `D3D12DDICAPS_TYPE_0011_SHADER_MODELS`, which GetCaps
currently answers E_NOTIMPL (the `d3d12-caps 0x80004001` receipts of every
sample), so the runtime caps applications at SM 5.1. The existing
`mesa/dxil/agx_dxil_program.c` only frames DXIL containers (711 lines); no
DXIL instruction translation exists.

## WINDOWS CONTRACT

- WDK 26100 `d3d12umddi.h`: `D3D12DDI_D3D12_SHADER_MODELS_DATA_0011
  {pNumShaderModelsSupported, pShaderModelsSupported}` with
  `D3D12DDI_SHADER_MODEL_*_RELEASE_*` values (6.0 = 0x00060005 ...); the
  runtime then accepts DXIL containers ("DXBC" magic, `DXIL` chunk, LLVM 3.7
  bitcode with dx.* intrinsics and metadata) in the same create-shader DDIs
  and PSO subobjects as DXBC.
- SM 6.0 adds wave intrinsics (WaveReadLaneFirst etc.), 64-bit integers;
  6.1 SV_ViewID/barycentrics; 6.2 fp16/denorm modes; 6.3 DXR (not planned);
  6.4 dot4/packed; 6.5 mesh/amplification (not planned), wave match; 6.6
  dynamic resources (ResourceDescriptorHeap, needs binding tier 3), 64-bit
  atomics, IsHelperLane; 6.7/6.8 more. Report only the highest model whose
  every required feature is translated (AGENTS capability rule); 6.0 first.

## AGX/ASAHI CONTRACT

- Asahi consumes NIR (agx_nir_options) through Gallium; Mesa's
  `spirv_to_nir` (src/compiler/spirv) produces NIR from SPIR-V with
  descriptor-set/binding variables, lowered by `nir_lower_io`,
  `nir_lower_explicit_io`, etc. Wave ops map to NIR subgroup intrinsics,
  which Asahi supports (32-wide SIMD groups) for the subset AGX implements.
- The D3D12 owner binds Gallium slots exactly as the DXBC path produces them
  (CBV/SRV/UAV/sampler slot maps from `dxbc_binding_map`, root-signature
  resolution in `agx_d3d12_ddi.cpp`).

## TRANSLATION (proposed)

DXIL container -> dxil-spirv (HansKristian-Work/dxil-spirv, MIT, Valve;
its own LLVM bitcode reader, no LLVM dependency, stable C API
`dxil_spirv_c.h`) -> SPIR-V -> `spirv_to_nir` -> a lowering pass that maps
the SPIR-V (set, binding) of every D3D register (space, register) to the
same Gallium slots and binding map the DXBC path builds -> existing owner
create-shader path (`AgxD3d12OwnerCreateCompute` / graphics). dxil-spirv's
remapping callbacks let the UMD choose the (set, binding) per D3D resource,
so the root-signature resolution stays unchanged.

## WHAT IS STILL UNKNOWN

- dxil-spirv licence/compatibility review for inclusion (AGENTS.md: no code
  copied before an explicit review) and whether it builds with the pinned
  MSVC for ARM64/x64 UMDs.
- The exact SPIR-V capability set dxil-spirv emits for SM 6.0 and which
  `spirv_to_nir` options Asahi's NIR needs (subgroup size, int64, fp16).
- Whether the runtime validates anything beyond the reported shader model
  (DXIL signing/validation hash is checked by the runtime, not the driver).

## Checkpoints

1. Offline: host fixture translating DXC-compiled SM 6.0 VS/PS/CS (built
   on the builder) to NIR through dxil-spirv + spirv_to_nir + Asahi
   preprocessing, binding map equal to the DXBC fixture of the same HLSL.
2. Hardware: probe steps compiled with DXC -T *_6_0 (compute, graphics,
   wave intrinsics) with the SHADER_MODELS cap reporting 6.0.

## Current state (2026-10-22 review)

- `mesa/dxil/agx_dxil_owned.c` already converts SM 6.0 compute DXIL through the
  dxil-spirv C API (2.75.0, lock `mesa/scripts/dxil_spirv_dependency_lock.json`,
  licence review `D3D12-C15-dxil-license-review.md`: YES with notice
  conditions); `agx_dxil_nir_map.c` maps its Vulkan buffer indices to the
  binding-map slots. Both are compute/raw-buffer only and linked only by the
  optional prepared-NIR-owner build; the shipped UMD reports no SHADER_MODELS
  cap and StoreShader accepts DXBC tokens only.
- Mesa's `spirv_to_nir` (libvtn) is not part of the native runtime closure
  (`build-asahi-runtime-closure.py` LIBRARIES); the host Mesa build has
  `src/compiler/spirv/libvtn.a` and `.local/dxil-work` has host dxil-spirv
  archives, so the frontend can be developed against host fixtures first.

## Frontend parity checklist (what the owner consumes from dxbc_to_nir)

- Entry: `dxbc_translate_tokens_bindings_xfb` -> NIR + `dxbc_binding_map`
  (dense slots per kind in declaration order; ranges one slot per register;
  limits {14, 128|HEAP, 8|HEAP, 16|HEAP}); then `dxbc_nir_srv_dimensions`,
  `dxbc_nir_uav_dimensions` (scan the NIR), `dxbc_nir_compute_info`, and for
  fragment shaders `dxbc_nir_lower_fragment_position`.
- CBV: `load_ubo(imm slot)`; cb14/cb15 reserved for heap tables.
- SRV texture: `nir_tex_instr` with `texture_index`/`sampler_index` (no
  derefs); typed buffer SRV `txf` DIM_BUF; raw/structured SRV one `txf` per
  dword on an R32_UINT buffer view (coord = byte address >> 2).
- UAV: typed -> `nir_var_image` (binding u) + indexed `image_*` (never
  `image_deref_*`), placeholder images up to the highest typed slot; raw and
  structured -> `load/store_ssbo`, `ssbo_atomic` with imm u; append/consume
  counters at SSBO `DXBC_NIR_UAV_COUNTER_SLOT + u`.
- Dynamic ranges: `texture_offset`/`sampler_offset`; heap ranges (C47):
  `bindless_image_agx(index*160+64[+24|+48], desc_set 0xd312)` from cb14.
- I/O: VS inputs `VERT_ATTRIB_GENERIC0 + input register`; varyings
  `VARYING_SLOT_VAR0 + register` both sides; SV_Position POS; PS outputs
  `FRAG_RESULT_DATA0 + register`; system values as intrinsics
  (vertex_id_zero_base, instance_id, front_face, sample_id, primitive_id...).
- Finalisation (`dxbc_nir_finalize`): returns/vars_to_ssa/system values, outputs
  to temporaries (non-CS/HS), shared memory explicit IO (CS), opt loop,
  gather_info; no nir_lower_io/nir_lower_samplers (Asahi's create_*_state does
  them).

## Plan

1. Host frontend `mesa/dxil/agx_dxil_to_nir.c`: dxil-spirv with remapper
   callbacks building the same binding map (set = kind, binding = slot;
   counters, typed/raw classes), stage I/O remapped to register locations,
   spirv_to_nir (Vulkan environment, subgroup size 32), then a lowering pass to
   the checklist above (Vulkan indices -> slots, image/texture derefs ->
   indexed forms) and the same finalisation. Host fixtures (DXC on the
   builder) compared against the DXBC translation of the same HLSL.
2. Windows: build libvtn and dxil-spirv into the native runtime closure for
   ARM64 and x64 UMDs; THIRD-PARTY-NOTICES in the package.
3. DDI: DXIL containers in StoreShader/CreateShader and PSO paths; report SM
   6.0 through D3D12DDICAPS_TYPE_0011_SHADER_MODELS only after offline parity
   and hardware probes (cs/vs/ps 6.0, waves).

## Finding: SM 6 needs the R1 DDI interface (2026-10-22)

The samples' traces (EXP1855 HelloTriangle) show the runtime asks the 0003
driver caps types 1000-1010 and 1069-1074 only; D3D12DDICAPS_TYPE_0011_
SHADER_MODELS (1012) is never queried while GetVersions offers
D3D12DDI_SUPPORTED_0003 alone, so the runtime keeps applications at SM 5.1
and never hands DXIL to the driver. SM 6.0 therefore needs the R1 interface
(D3D12DDI_SUPPORTED_0011). D3D12DDI_DEVICE_FUNCS_CORE_0010 differs from
CORE_0003 in 20 signatures (element layout, blend/depth/rasterizer states
-- adapters already exist, CPU-only --, every CreateShader as
PFND3D12DDI_CREATE_SHADER_0010, the stream-output GS, CalcPrivate/Create
PipelineState_0010), adds six pipeline-library entries and drops the four
object-serialization entries; the other tables' versions under 0011 still
need the same audit.

## Build integration (2026-10-22)

`build-asahi-runtime-closure.py` adds, for the ARM64 and x64 UMDs only,
`agx_dxil_to_nir.c`, `agx_dxil_program.c` and `agx_dxil_checked.cpp` to the
native runtime archive and links libvtn (`nir-<arch>` build of the same
configuration) and the nine dxil-spirv libraries (`c44-build-dxspv.ps1`,
EXP1388 configuration, /MD) with their hashes in the closure manifest.

## Geometry, hull and domain shaders (2026-10-22)

dxil-spirv + spirv_to_nir produce the DXBC frontend's layout for these
stages without extra lowering (fixtures `par_gs`, `par_hs`, `par_ds`):
GS array inputs at the signature rows, emit/cut on stream 0 and the same
primitive/vertex-count/invocation info; HS control points at VAR0 + row
(SV_Position included, the C37/EXP1834 contract), the control point
function in every invocation, a barrier, then invocation 0 writing the tess
factors and patch constants. Differences that remain:

- Generic patch constants sit at PATCH0 + signature row, and DXC places
  them after fixed tess-factor rows (row 6 where FXC uses register 4). HS
  and DS of one DXIL pipeline agree; a pipeline mixing a DXBC hull shader
  with a DXIL domain shader (or the reverse) would not link. Unknown
  whether the runtime admits such a mix.
- The DS reads tess factors as inputs (VARYING_SLOT_TESS_LEVEL_*), the DXBC
  frontend as system values; Asahi takes both (`glsl_tess_levels_as_inputs`,
  `poly_nir_lower_tes`).
- Both frontends set the output topology's winding (`info.tess.ccw`) on the
  hull shader only, while Asahi reads it from the TES alone
  (`agx_state.c`: `params.ccw = !tes->tess.ccw`), so a `triangle_ccw` hull
  shader is tessellated with clockwise winding on both paths. Owner fix
  pending (copy the HS winding into the DS at pipeline creation).
- Stream output from a DXIL GS still refuses (translate_shader).

## The R1 runtime converts every DXBC shader (EXP1858)

Under R1 the D3D12 runtime hands an SM 6 driver every FXC shader as DXIL produced
by its DXBC converter (dxilconv.dll): measure-d3d12-graphics-shader receipts of
the SM6 probe and sample runs carry the DXIL status (0x10000) for every stage, so
the SM6 profile runs all existing applications through the DXIL frontend. Three
dxilconv-only forms failed and are fixed with fixtures from the system
dxilconv.dll (tests/fixtures/dxil/c44/dxbc2dxil.cpp, conv-probe/):

- a hull shader without a control point phase names no entry function
  (5612c9d5: let dxil-spirv build its pass-through function);
- stream output reaches the DXIL frontend (de5b61b9: agx_dxil_nir_xfb with the
  SV_Position registers of the output signature);
- dx.op UDiv with two outputs made dxil-spirv 8434b063 type its helper with the
  result struct (invalid SPIR-V, vtn assertion, dead ARM64 process in C41A):
  patches/dxil-spirv-0001-dxbc-udiv-operand-type.patch, applied to the builder
  source (c49-dxspv-source.ps1, c49-build-dxspv.ps1) and the host test build.
