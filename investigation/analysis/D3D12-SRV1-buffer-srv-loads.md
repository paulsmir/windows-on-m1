# SRV-1: raw and structured buffer SRVs as device loads

Status: design (2026-11-13). Fact 1 (`.local/night-20261110/srv1-fact/REPORT.md`): GravityMark declares no buffer SRVs, so SRV-1 is a general defect and not a GravityMark lever. The OPTWIN G1 rows measure it: random gathers run 1.45-3.98x macOS, with the same request rate on both sides and 4x the requests on ours.

## WINDOWS CONTRACT

Sources: D3D11.3 functional spec 7.5 and the SM5 assembly reference for `ld_raw` / `ld_structured`, as already applied for UAVs in D3D-H6 (`winsys/agx_win32_d3d_robustness.h`).

- A raw or structured SRV read returns each in-bounds 32-bit component.
- An out-of-bounds component returns 0, judged per component, as for a UAV.
- D3D12 root SRVs carry an address and no size; reading outside the resource is undefined. This is the same rule RC75 already uses for root UAVs (`ACCESS_IN_BOUNDS`).

## AGX/ASAHI CONTRACT

Sources:
- `mesa/src/gallium/drivers/asahi/agx_nir_lower_sysvals.c`: `load_ssbo_address` and `get_ssbo_size` sysvals.
- `agx_state.h`: `ssbo_base` / `ssbo_size` for `PIPE_MAX_SHADER_BUFFERS` = 32 slots.
- Honeykrisp `hk_shader.c`: robust SSBO = `nir_address_format_64bit_bounded_global` and `lower_load_global_bounded`, one check per load.

A shader-buffer read is a device load of up to 16 bytes from the slot's base address. The buffer-texture path, by contrast, costs a texel fetch per dword plus the 1024-wide coordinate split, the size compare and the format branch, all wrapped by agx_nir_lower_texture.

## TRANSLATION

1. **Owner** (`winsys/agx_d3d12_windows.cpp` bind_stage):
   - Each raw/structured SRV at register t < 16 is also bound as a read-only `pipe_shader_buffer` at slot 16 + t, with the same offset and the same size, rounded down to dwords as today. Writable mask bit clear.
   - The sampler view stays bound, so `bufinfo`/`resinfo` and any fallback keep working.
   - Slots 0..15 stay as they are: UAVs 0..7, counters 8..15.
2. **DXBC front end** (`dxbc/dxbc_to_nir.c` mem_load):
   - A non-heap SRV operand of a raw/structured declaration with index < 16 emits one `load_ssbo` of n dwords at slot 16 + t, with `ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER`.
   - Heap SRVs, t >= 16 and TGSM keep their current forms.
3. **DXIL front end** (`dxil/agx_dxil_to_nir.c` remap_srv): the same SRVs map to `DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_SSBO` at the shader-buffer slot 16 + t.
   - dxil-spirv then vectorizes the raw/structured access (`raw_access_*_vectorize`), as it does for GravityMark's UAVs.
   - lower_buffer_slot accepts set SRV storage buffers.
4. **Robustness:**
   - The existing D3D-H6/RC74 pass covers the new loads unchanged: a per-component select, re-vectorized by `agx_win32_d3d_vectorize_cb`. It reads the size from `get_ssbo_size` (slot 16 + t).
   - Root SRVs are marked `ACCESS_IN_BOUNDS` through the RC75 root-view mask, extended to slots 16..31. They then stay one vector load with no select.
5. **Switch:** opt-in `APPLE_AGX_D3D12_SRV_SSBO=1` selects the compiler form. The owner's extra binding is unconditional and inert when no shader reads slot 16+.

## Bounds check per load or per component (architect's request)

- Honeykrisp checks a vector load once, against its last byte. That is allowed by Vulkan robustness, which permits any in-bounds value or 0 for a partially out-of-bounds access.
- D3D requires each in-bounds component of a partially out-of-bounds read, so one check per load is not D3D-correct for a straddling vector.
- The RC74 form (per-component offset clamp + select, then the vectorizer) is the existing D3D-correct form. A branched whole-vector form serialised every UAV read (RC74).
- Proposed: keep RC74 for table SRVs and measure its cost on G1. Root SRVs need no check (point 4).
- If RC74 does not re-vectorize in G1, the candidate is: one vector load at a clamped base, valid when the whole vector is in bounds, plus the per-component fallback only in the straddling case. That needs a branch-free formulation, still to be designed.

## WHAT IS STILL UNKNOWN

- Whether RC74's per-component select re-vectorizes for the G1 gather (offline: NIR/AGX counts with the r20 harness).
- Whether 16 shader-buffer slots for SRVs cover the corpus (offline: census of raw/structured SRV registers in the dxbc-corpus and the DXIL captures).
- Hardware: the gather ratio against the macOS OPTWIN table.

## Checkpoint

1. Offline: G1 NIR/AGX counts before and after (cost gate), the corpus census, and the existing suites green.
2. Hardware: the OPTWIN ours-side rerun with the switch on vs off in one boot. That is one variable, since the switch is read per process and the probe can launch with and without it. Expected: G1 gather ratios toward 1.0-1.5, and every other row unchanged.
