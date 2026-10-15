# D3D-H6: D3D10/11 out-of-bounds buffer semantics (2026-10-15, design record)

Goal: the D3D10/11 UMD (Mesa d3d10umd over projected Asahi Gallium) gives
the out-of-bounds behaviour D3D specifies for constant buffers, buffer
SRVs/UAVs and vertex fetch. Before this change an application's
out-of-bounds index read or wrote whatever GPU memory followed the buffer,
or faulted the GPU.

Status: implemented offline (test_d3d_buffer_robustness), not yet run on
hardware. Proposed checkpoint at the end.

## Sources inspected

- Microsoft D3D11.3 functional spec
  (microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm):
  5.2.1 (out-of-range behaviour is relative to the view), 5.3.4.3.2 (a
  ranged CB binding past the resource is out of bounds), 7.5 (CB reads),
  7.11.2 (out-of-range slot index = unbound slot), 7.13 (each memory
  instruction defines its own out-of-bounds rule), 8.19.2 and 8.20 (IA).
- Shader Model 4/5 assembly reference (learn.microsoft.com/windows/win32/
  direct3dhlsl): ld, ld_raw, ld_structured, store_raw, store_structured,
  ld_uav_typed, store_uav_typed, atomic_iadd, imm_atomic_iadd.
- Reference Mesa 9aa1215f: gallium/drivers/asahi/agx_state.c
  (should_lower_robustness 1705, agx_shader_initialize 1737, agx_update_vs
  2158), agx_pipe.c 1858 (`ctx->robust = flags & ROBUST_BUFFER_ACCESS`),
  agx_uniforms.c (agx_set_cbuf_uniforms 106, agx_set_ssbo_uniforms 118,
  agx_upload_vbos), agx_nir_lower_sysvals.c (load_ubo -> global, ubo/ssbo
  size sysvals), asahi/lib/agx_nir_lower_vbo.{c,h} (robustness levels),
  agx_helpers.h agx_calculate_vbo_clamp, asahi/compiler/agx_nir_lower_texture.c
  lower_buffer_texture, compiler/nir/nir_lower_robust_access.c,
  asahi/vulkan/hk_shader.c lower_load_global_bounded (robustness2).
- This repository: winsys/agx_d3d10_windows.cpp AgxD3d10WindowsCreateDevice,
  winsys/agx_win32_asahi_scene.c (AgxWin32AsahiContextCreate passes flags 0;
  `params.features=0`, soft fault off), winsys/agx_win32_asahi_bo.h,
  scripts/native-asahi-batch-lifecycle.py (zero/scratch page BOs, EXP1069),
  dxbc/dxbc_to_nir.c (load_cb, mem_load, mem_store, atomics), the projected
  frontend (Shader.cpp CB binding: size = logical_bytes, multiple of 16;
  buffer SRV views sized FirstElement/NumElements), d3d11/agx_d3d11_ddi.cpp
  (UAV buffers bound with the view's offset/size, counters 4 bytes),
  windows/icd/agx_wgl_icd.cpp (GL backend, zero-allocated).

## Required semantics and what Asahi gave

| Access (DXBC -> NIR here) | D3D rule | Before (flags 0) | Asahi GL robust | This change |
|---|---|---|---|---|
| cb#[i] (load_ubo) | OOB or unbound: 0 in all components (7.5) | unchecked read | offset 0: buffer's first register | 0 |
| unbound CB slot | 0 (7.5) | stale base/size of the slot's last binding in the batch, or VA 0 | same base, fault once clamped | zero page, size 0 |
| ld t# buffer, ld_raw/ld_structured t# (txf BUF) | 0 for format components, defaults for missing; unbound 0 | robust in hardware: Asahi maps an OOB index to an OOB coordinate | same | unchanged |
| ld_raw/ld_structured u# (load_ssbo) | 0 per OOB 32-bit component, in-bounds read | unchecked | whole vector from offset 0 | per component |
| store_raw/store_structured u# (store_ssbo) | nothing written OOB, in-bounds part written | unchecked | whole store dropped if last component OOB | per component |
| atomic_*/imm_atomic_* u# (ssbo_atomic) | nothing written; result undefined | unchecked | predicated, result 0 | GL lowering kept |
| typed u# buffer (image BUF) | ld: as ld; store: nothing | always lowered (Asahi) | same | unchanged |
| typed u# texture | as ld / nothing | texture/PBE hardware (Asahi's claim) | same | unchanged |
| vertex fetch | OOB element: 0, format defaults for missing (8.19.2) | GL level forced (no soft fault): element 0 | element 0 | AGX_ROBUSTNESS_D3D: 0 + defaults |
| g# (TGSM) | undefined (writes: all TGSM undefined) | none | none | unchanged |

Asahi vertex fetch: without soft fault agx_nir_lower_vbo raises the level to
at least GL (index clamped to element 0, an element that does not fit
entirely counts as out of bounds; an empty binding reads the zero page). At
D3D level without soft fault it keeps the clamp (a safe address) and then
selects 0 before the format swizzle, so missing components get their
defaults, as 8.19.2 requires.

## Ownership

- Policy: the D3D UMD (agx_d3d10_windows.cpp sets Backend.D3dRobustness
  before creating its per-device screen). The OpenGL ICD's backend is zero
  allocated and keeps the robustness its contexts ask for (no GL change).
- Implementation: the projected Asahi Gallium driver (ctx->windows_d3d_robust
  set in agx_create_context; implies ctx->robust), the NIR pass in
  winsys/agx_win32_d3d_robustness.h run in agx_shader_initialize before
  nir_lower_robust_access (which keeps buffer images and atomics), the
  vertex-fetch level in agx_update_vs, unbound CB slots on the device zero
  page in agx_set_cbuf_uniforms (read from the device, not the per-thread
  published VA, so a compute dispatch on a reused batch cannot pick another
  device's page).
- Not involved: KMD, m1n1, Mu. AGX soft fault stays disabled
  (agx_win32_asahi_scene.c params.features=0), so no access may depend on a
  fault returning zeros: every clamped access reads a mapped address.

## Design notes

- CB loads keep one vector load: D3D binds and reads CB registers of 16
  bytes and sizes are multiples of 16, so an access is wholly in or out; the
  offset clamp and the select are ALU on uniform values (ubo_size sysval,
  constant offsets), which the AGX preamble still hoists with the load.
- UAV loads/stores keep the original vector access when wholly in bounds
  (one branch); only a partially out-of-bounds access takes the per-component
  path. Bounds tests do not wrap (offset <= size - bytes, size >= bytes).
- Host evidence (tests/d3d_buffer_robustness_fixture.c, host libnir of the
  same Mesa): with constant sizes the lowered NIR folds to exactly the D3D
  results. The same fixture with nir_lower_robust_access in place of the pass
  returns load_ubo@0.. for every OOB CB read, load_ssbo@0..12 for a partial
  UAV read, drops a partial store, and at offset 0xfffffffc lets a 2-dword
  store through (its `offset + size - 1` check wraps).

## Residual gaps (not fixed here)

- Unbound vertex buffer (8.20): D3D wants 0 in every component with no
  defaults; Asahi reads the zero page and applies the format's defaults
  (W=1 for formats without alpha).
- Index buffer out-of-bounds fetch: not examined.
- TGSM: D3D leaves it undefined but data outside the process must stay
  invisible; whether AGX local memory accesses outside the allocation stay
  inside the threadgroup's allocation is unmeasured (hypothesis).
- DXBC CB register indices >= 2^28 wrap in the translator's `index << 4`.
- Upstream nir_lower_robust_access offset wrap (above) still applies to GL
  robust contexts of the OpenGL ICD.
- Performance of the D3D lowering is unmeasured.

## Hypothesis to keep in mind (EXP1296)

EXP1296 (translator default) produced a DWM GPU-fault storm about 1 ms
after submission. A translator binding/slot mismatch that reads an unbound
CB slot (base 0 or stale) or past a buffer is one fault class this change
neutralizes. Not established: no fault address/unit was recorded.

## Proposed hardware checkpoint (one variable: this change)

A D3D11 smoke (FL 10_0 device, cs_4_0/vs_4_0), results read back by
staging copies; each OOB view is smaller than its resource so an
out-of-bounds access lands in mapped memory holding sentinels:
1. CB: 32-byte CB bound at b0, shader reads cb0[k] for k = 0..3 from a
   dynamic index: expect cb0[2] = cb0[3] = 0; cb1[0] with b1 unbound = 0.
2. Raw UAV (16-byte view of a 64-byte buffer, sentinels after the view):
   Load4 at 8 -> (v2, v3, 0, 0); Store4 at 8 -> bytes 8..15 written,
   sentinels unchanged; InterlockedAdd at 16 -> sentinels unchanged.
3. Raw SRV (16-byte view): Load4 at 8 -> (v2, v3, 0, 0) (hardware path).
4. Typed RWBuffer<uint> store at NumElements -> sentinel unchanged.
5. Vertex fetch: a 3-vertex VB drawn with 6 vertices, VS outputs the
   attribute to stream output: vertices 3..5 capture (0, 0, 0, 1) for an
   R32G32B32_FLOAT attribute read as float4.
Pass: every value exact on x86 and ARM64, no new Wom1FirmwareRecovery
recoveries, no device removal; GL smoke, CS 1.6 bench and DWM faults 0
unchanged (the GL ICD path is unchanged by construction). Failure: a
nonzero OOB read, a changed sentinel, a GPU fault receipt or a removed
device. Recovery: the pipeline's standard recN rollback.
