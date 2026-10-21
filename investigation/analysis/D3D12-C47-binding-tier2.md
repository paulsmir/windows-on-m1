# D3D12 C47: resource binding Tier 2 (bindless descriptor heaps)

## Why

GetCaps reports `D3D12DDI_RESOURCE_BINDING_TIER_1` (agx_d3d12_ddi.cpp). The
runtime then refuses root signatures beyond Tier 1: MiniEngine's g_CommonRS
(u0-u9 at FL 11_0) needed a test-only patch, and most D3D12 titles require
Tier 2 or 3. The AGENTS capability rule forbids reporting Tier 2 before every
Tier 2 limit is implemented.

## WINDOWS CONTRACT (learn.microsoft.com, direct3d12/hardware-support)

| Limit | Tier 1 | Tier 2 | Tier 3 |
| --- | --- | --- | --- |
| CBVs in all tables per stage | 14 | 14 | full heap |
| SRVs in all tables per stage | 128 | full heap | full heap |
| UAVs in all tables, all stages | 8 (FL 11_0) / 64 | 64 | full heap |
| Samplers in all tables per stage | 16 | 2048 | 2048 |

Tier 1 (all heaps) and Tier 2 (CBV and UAV heaps) require every descriptor a
table covers to be populated when the shader runs; Tier 2 SRV and sampler
tables need not be. Shader-visible CBV/SRV/UAV heaps hold up to 1,000,000
descriptors, sampler heaps 2048. SM 5.1 shaders index register ranges
dynamically (`T0[r0.x + n]`, unbounded ranges); WDK 26100
`D3D12DDI_RESOURCE_BINDING_TIER_2 = 2` in `D3D12DDI_D3D12_OPTIONS_DATA`.

## CURRENT TRANSLATION (Tier 1)

Descriptor tables are resolved on the CPU at replay (`BindTable`,
`OwnerBindings`) into Gallium slots: CBV 14, SRV 128 sampler views, UAV 8
(shader buffers u#, counters at 8..15, typed UAVs as images), samplers 16.
Dynamic SM 5.1 indexing inside those slots works (C34: `texture_offset`,
Asahi's per-stage binding table, `agx_nir_lower_bindings`).

## AGX/ASAHI CONTRACT

- Texture/PBE descriptors are 24-byte records in memory; a bindless handle is
  (uniform register holding a 64-bit table base, byte offset)
  (`nir_bindless_image_agx`, `agx_translate_bindless_handle`); Asahi's
  gallium driver uses it for its per-stage table (`texture_base`,
  `agx_nir_lower_sysvals.c` `load_texture_handle`). A tex/image instruction
  that already carries `nir_tex_src_texture_handle` is left as is
  ("real bindless").
- Samplers beyond the 16 sampler state registers use the batch's sampler heap
  (`nir_tex_src_sampler_handle`, `uses_sampler_heap`).
- Asahi lowers SSBOs to global memory with base/size uniforms; Gallium caps
  shader buffers at 32 and images at 64 per stage.
- Honeykrisp (Asahi Vulkan) implements descriptor indexing with the same
  handle model (descriptor sets in memory, base in a uniform).

## TRANSLATION (proposed)

1. **Heap mirror (UMD).** Every shader-visible CBV/SRV/UAV heap owns a GPU
   buffer of 24-byte AGX descriptors per D3D12 descriptor (texture descriptor
   for SRVs; PBE descriptor plus texture descriptor for typed UAV images;
   (address, size) for raw/structured buffers), written by Create*View into a
   shader-visible heap and by CopyDescriptors(Simple). Sampler heaps mirror
   AGX sampler descriptors. Descriptors are produced by Asahi itself
   (`create_sampler_view`, image/PBE packing) through a winsys accessor, so
   formats and swizzles match the slot path.
2. **Heap base sysval (Asahi patch, build-native-asahi-state.py).** A
   per-context `d3d12_heap_base[2]` in `agx_draw_uniforms`, set by a winsys
   call when SetDescriptorHeaps replays, and a NIR load lowered to its
   uniform register (as `texture_base`).
3. **Shader translation (dxbc_to_nir).** A range declared beyond the slot
   limits (or unbounded) translates its accesses into bindless handles
   (heap base, (table start + index) * 24); the table start comes from the
   root signature (a per-draw root value the owner uploads). Ranges inside
   the Tier 1 limits keep the slot path.
4. **Limits.** SRVs and samplers by heap; UAVs 64 through images (typed) and
   global-memory buffers (raw/structured) by heap; CBVs stay 14 slots.
5. **Caps.** Report Tier 2 only after 1-4 pass offline and on hardware.

## Handle emission (offline study of Asahi)

`agx_nir_lower_sysvals` step 4 writes the uniform register of a
`load_sysval_agx(.flags = ~0)` into the `bindless_image_agx` that *immediately
follows* it; Asahi emits that pair itself inside the pass
(`load_texture_handle`). Emitting the pair from the translator is fragile
(passes before the sysval lowering may separate them), and
`load_texture_handle_agx` has no index to select a table. Proposed: the
translator emits `nir_bindless_image_agx(offset, .desc_set = AGX_D3D12_HEAP_MARK)`
and the native-state patch adds, in the sysval pass, a
`load_sysval_agx(stage table, &ubo_base[15], .flags = ~0)` directly before each
marked handle; the heap mirror is bound as constant buffer 15 (Asahi exposes
16, D3D12 uses 14), so `ubo_base[15]` is the mirror's GPU address.

## Heap layout (refined)

Shader-visible CBV/SRV/UAV heaps are already GPU buffers of the owner (CPU
mapped since C45) and CopyDescriptors copies whole descriptors. A descriptor
grows from 64 to 128 bytes: the CPU record (<= 64 bytes) at +0, the packed
AGX texture descriptor at +64 and the PBE descriptor at +88 (typed UAVs), a
layout key at +112. The heap itself is the mirror (constant buffer 15 = the
heap buffer); handle offset = index * 128 + 64. Asahi re-packs bound texture
descriptors before every draw (`agx_upload_textures`: "the layout might have
changed from under us": decompression, BO shadowing), so a heap descriptor
records the resource's layout key when packed and the replay re-packs a
descriptor whose resource key changed.

## Residency and hazards (consequences of bindless)

- The winsys submits the BOs a batch references (bound views, framebuffer,
  pools). Resources reached only through the heap mirror are not in that
  list: every resource with a descriptor in a bound shader-visible heap must
  join the submission's residency set (or all D3D12 heaps stay resident, the
  D3D12 default until Evict).
- Asahi's writer tracking (which C46/EXP1852 relies on to order batches) sees
  only bound SSBOs/images. Bindless UAV writes are invisible to it, so
  ResourceBarrier (UAV and transition barriers), today a no-op, must become a
  real ordering point for heap-accessed resources (flush the writing batch).

## WHAT IS STILL UNKNOWN

- Whether the Asahi sampler heap can hold 2048 D3D12 samplers per batch, or
  whether a second mirror plus heap offset is needed.
- The 24-byte layout Asahi's `agx_texture_packed` uses for buffer textures
  versus a raw buffer's (address, size) record (bounds checking for
  robustness).
- How null descriptors and unpopulated SRV entries must read (Tier 2:
  unpopulated SRVs are allowed; reads of them are undefined but must not
  fault -- use Asahi's null descriptor).

## Checkpoints

1. Offline: NIR of an SM 5.1 shader indexing `Texture2D t[] : register(t0)`
   beyond 128 carries bindless handles from the heap base sysval.
2. Offline: the heap mirror bytes equal Asahi's slot descriptors for the same
   views (host fixture).
3. Hardware: probe C47A (SRV index 1000 of a 2048-entry table), C47B (sampler
   1500), C47C (UAV 40); then MiniEngine without the UAV-range patch.

## Caps (implemented)

GetCaps reports `D3D12DDI_RESOURCE_BINDING_TIER_2` once every Tier 2 limit
has a path: SRV and sampler tables over the heap (heap ranges, sampler map of
2048 entries), 64 UAVs (8 slots plus heap UAVs, counters through their
counter PBE) and 14 CBV slots. A shader may declare its UAVs one register at
a time, each past the slots a heap range of its own, so a shader holds up to
`DXBC_HEAP_RANGE_MAX` = 64 heap ranges (was 8: a 17th separate UAV was
refused), `heap_sampler_mask` is 64 bits wide and `DXBC_BINDING_MAP_MAX`
covers the 166 slots plus the ranges (fixture cs51uav64).
Hardware checkpoint: probe C47B (SRV 300 of a 512-entry table, sampler 20 of
32, 12 UAVs with u8-u11 in the heap, an append counter).
