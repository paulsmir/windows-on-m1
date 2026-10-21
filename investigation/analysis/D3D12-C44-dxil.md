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
