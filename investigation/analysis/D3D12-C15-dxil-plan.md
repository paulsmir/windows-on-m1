# D3D12 C15 (plan): DXIL shaders (shader model 6)

Status 2026-10-17. Every D3D12 shader path (C5-C14) consumes DXBC through
the repository's dxbc_to_nir. The UMD never answers
D3D12DDICAPS_TYPE_0011_SHADER_MODELS, so the runtime caps applications at
shader model 5.1: pipelines with DXIL bytecode (dxc, SM 6.x -- what current
D3D12 titles and engines ship) are refused before the driver sees them.

## WINDOWS CONTRACT (WDK 26100 d3d12umddi.h)

- D3D12DDICAPS_TYPE_0011_SHADER_MODELS: the driver reports the highest
  D3D12DDI_SHADER_MODEL (5_1_RELEASE_0011 ... 6_x_RELEASE); SM 6.x adds
  wave intrinsics (D3D12DDICAPS shader options), 64-bit types, etc. per
  model; each model's optional features have their own caps.
- With SM 6 admitted the runtime hands DXIL containers (DXBC container
  with a DXIL part: LLVM 3.7 bitcode + metadata) to the shader-creation
  DDIs.

## AGX/ASAHI CONTRACT

- The Asahi compiler consumes NIR (agx_compile_shader_nir); Mesa's
  spirv_to_nir is the supported SPIR-V front end (the Vulkan driver path).
  Mesa's src/microsoft/compiler goes the other way (NIR -> DXIL); there is
  no DXIL -> NIR in Mesa.

## TRANSLATION OPTIONS (decision needed before implementation)

1. DXIL -> SPIR-V -> NIR: an external DXIL to SPIR-V translator (the
   dxil-spirv project used by vkd3d-proton, MIT) feeding Mesa spirv_to_nir
   with the D3D12 binding model mapped to descriptor sets. Smallest new
   code; requires the AGENTS license and compatibility review before any
   external code enters the build.
2. DXIL -> NIR directly: an LLVM 3.7 bitcode reader + DXIL op lowering in
   the repository (dxbc_to_nir's role for SM 6). Largest effort, no
   external dependency.

## WHAT IS STILL UNKNOWN

- Which option the project accepts (license review / effort).
- The 26100 runtime's DXIL creation DDI sequence after the cap -- measured
  by a probe step that creates an SM 6.0 compute PSO (expect a refusal
  receipt naming the DDI).

## Checkpoint (when implemented)

Probe C15: SM 6.0 compute PSO (dxc -T cs_6_0) writing 3i + 7, then an SM
6.0 VS/PS triangle; PASS = readback / C6b image checks.
