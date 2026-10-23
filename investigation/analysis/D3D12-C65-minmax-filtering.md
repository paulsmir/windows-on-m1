# D3D12 C65: min/max reduction filtering (Tier 2, FL 12_0)

Status: design, 2026-10-23, offline. Part of the FL 12_0 path
(D3D12-FL-program.md step 4; D3D12-C53-tiled-textures.md "Tier 2").

## WINDOWS CONTRACT

Sources:
- Microsoft Learn: D3D12_FILTER_REDUCTION_TYPE and D3D12_FILTER. The
  reduction lives in the filter bits:
  `D3D12_FILTER_REDUCTION_TYPE_SHIFT` 7, MINIMUM 2, MAXIMUM 3.
- D3D11.3 functional spec, "Minimum/Maximum filtering" (tiled resources
  chapter; microsoft.github.io/DirectX-Specs, D3D11_3_FunctionalSpec):
  - "fetches the same set of texels that a normal texture filter would
    fetch", and returns their component-wise min or max;
  - texels with zero weight "still do not contribute to the result";
  - "Support for this feature depends on Tier 2 support for Tiled
    Resources".

So MINIMUM_* and MAXIMUM_* samplers become valid once TiledResourcesTier
>= 2. Every filter combination (point/linear min, mag, mip; anisotropic)
has a MIN and a MAX variant.

## AGX/ASAHI CONTRACT

- The AGX sampler descriptor (genxml `Sampler`) has no reduction field.
  Bits 43-54 are unknown. Metal exposes no min/max sampler, and Honeykrisp
  reports `samplerFilterMinmax` only through the `fake_minmax` driconf
  workaround, which plain-filters. Hardware reduction is therefore absent;
  emulation is needed.
- Gather: `nir_texop_tg4` lowers to the hardware gather for one component
  (agx_compile.c `agx_gather_for_nir`). It returns the 2x2 bilinear
  footprint with the sampler's wrap and border modes applied, which is
  exactly the texel set of a bilinear fetch. agx_compile uses only
  `AGX_LOD_MODE_AUTO_LOD` for tg4, so gather at an explicit mip level is
  not known to work.
- `nir_texop_lod` gives the hardware LOD (bias and clamps of the sampler);
  `nir_texop_txs` gives a level's size.

## TRANSLATION

1. **Sampler heap form.** `AgxD3d12OwnerSamplerDescriptor` packs a
   MIN/MAX sampler as the same filter with STANDARD reduction, so the LOD
   and footprint are the hardware's. The 2-bit reduction mode goes into
   the heap form (`AGX_D3D12_HEAP_SAMPLER`) and into the dword the shader
   reads for that sampler (the C47 heap sampler map in
   `DXBC_HEAP_TABLE_CB`). Slot-bound samplers get the same bits through
   their constant.
2. **Shader lowering** (NIR, after the frontends; a new pass shared by
   DXIL and DXBC). For each sample (`tex`, `txb`, `txl`, `txd`) that does
   not compare, read the sampler's reduction bits. When they say MIN/MAX:
   - Mip levels and weights: from `nir_texop_lod` with the mip filter. A
     point mip uses one level; a linear mip uses floor and floor+1, and a
     level of weight 0 is dropped.
   - Per level: the bilinear footprint position `p = uv * size(level) - 0.5`,
     `f = fract(p)`. An axis with `f == 0` keeps only its first texel
     (zero weight). Point min/mag uses the one nearest texel, which is the
     regular sample itself.
   - Texels: 4 gathers (R, G, B, A) per level, or `txf` of the computed
     integer coordinates with wrap modes emulated, if explicit-level gather
     is unavailable (WHAT IS STILL UNKNOWN 1).
   - Result: component-wise min or max over the participating texels; then
     the view's swizzle, as a regular sample.
   - The lowering branches on the reduction bits, so STANDARD samplers keep
     the hardware sample: one extra uniform load and branch per sample.
3. **Anisotropic MIN/MAX:** the probes the hardware's anisotropic footprint
   would take are not exposed. Plan: emulate it as the linear-mip footprint
   over N probes along the major axis of the derivatives (N =
   min(MaxAnisotropy, ratio)), each probe a bilinear footprint as above.
   Exact agreement with a reference rasterizer is not promised by D3D for
   anisotropic filtering either; verify against WARP (WHAT IS STILL
   UNKNOWN 3).
4. **Caps:** nothing new. Min/max becomes valid with the Tier 2 flip.

## WHAT IS STILL UNKNOWN

1. Whether the AGX gather accepts an explicit LOD (lod-min mode with the
   gather field). Probe: a 2-level texture, gather at level 1 through a
   hand-encoded instruction, or a NIR tg4 with a `lod` source if Asahi
   lowers it. If not, the lowering uses `txf` with emulated addressing
   (border colour from the sampler).
2. ~~Where the heap sampler dword is read~~ RESOLVED from source:
   - Both frontends read the map word at `DXBC_HEAP_SAMPLER_MAP_OFFSET` of
     `DXBC_HEAP_TABLE_CB` (dxbc_to_nir.c `heap_sampler`, agx_dxil_to_nir.c
     heap deref lowering): bits 0-15 are the AGX sampler handle and bits
     16-31 the fp16 MipLODBias.
   - A D3D12 sampler heap holds at most 2048 samplers, so handles fit in
     11 bits, and bits 14-15 of the handle field can carry the reduction
     mode (0 standard or comparison, 2 minimum, 3 maximum).
   - Both frontends then mask the handle (`word & 0x3fff`) and pass the
     reduction bits to the lowering. These are the frontend session's
     files: one hunk each.
3. How closely anisotropic MIN/MAX agrees with WARP on the D3D12 conformance
   images.

Checkpoint: a probe step C65A that samples a 4x4 texture with known texels
through MINIMUM and MAXIMUM point, linear and trilinear samplers. It
compares against the CPU reference, including zero-weight edges.

## Status 2026-10-23

- Lowering `dxbc_minmax.c` (c3fb25d2), heap-sampler wiring in both frontends
  (1089c2aa), API probe step C65A (31dfc197): PASS on WARP, J313 pending
  (handoff `.local/handoff/C53-HW2-for-EXP1889.md`).
- Map word as implemented: handle bits 0-10, reduction bits 11-12, min/mag/mip
  linear bits 13-15, fp16 MipLODBias bits 16-31 (`dxbc_minmax_bits.h`).
  Item 2 above is superseded by this layout. Item 1 is moot: the lowering
  uses one explicit-LOD `txl` per texel at its centre, not gather.

## Cost and gating (C65G)

Measured on host libnir (scratch harness around `test_c65_minmax_nir`,
after algebraic/CSE/DCE): one wrapped 2D sample grows from 16 to 129-136
NIR instructions (2D array: 163), including 11 extra texture instructions.
1089c2aa put this on every heap sample: every SM 5.1 unbounded and SM 6
heap sampler, whether or not a min/max sampler is ever bound. The
temporaries in the never-taken branch also count towards the shader's
register allocation, which on AGX lowers occupancy for the whole shader.

Honeykrisp's answer to the same hardware gap (`00-hk-defaults.conf`,
`hk_fake_minmax`): advertise min/max for vkd3d, sample with the standard
filter, and accept the conformance failure. It is a reference, not this
driver's contract: the wrap remains the exact path.

Decision: the frontends emit the wrap only when the sampler binding limit
carries `DXBC_BINDING_MINMAX`. The owner sets it only with
`APPLE_AGX_D3D12_MINMAX=1` (`shader_binding_limits`). Tier 2, which permits
min/max samplers, is not advertised, so the default loses no contract.

## Next: a min/max shader variant (needed before the Tier 2 flip)

- The owner compiles a second CSO with `DXBC_BINDING_MINMAX` lazily. It
  selects that CSO for a draw or dispatch when a bound sampler has reduction
  2 or 3:
  - heap map entries: `bind_heaps` already walks them;
  - slot samplers: known at bind time.
  Shaders that never meet a min/max sampler keep their code size, register
  count and compile time.
- Slot and static samplers in the variant: their 5 bits are compile-time
  constants of the variant key (16 slots x 5 bits). A run-time constant
  buffer read would need `DXBC_HEAP_TABLE_CB` to be bound for every
  slot-sampled stage. Asahi does not bounds-check UBO loads
  (`agx_nir_lower_sysvals.c` `load_ubo` is base + offset; `ubo_base` is only
  refreshed for bound buffers in `agx_set_cbuf_uniforms`), so an unbound
  table would fault.
- RESOLVED from source: the PSO keeps every stage's bytecode
  (`AgxD3d12PipelineState::cs`, agx_d3d12_ddi.cpp copies vs/ps/gs/hs/ds
  after cs). It already compiles lazily under `compile_lock`
  (`pso->compiled`), so a min/max variant can be a second compiled object
  translated from the same tokens on first use.
- WHAT IS STILL UNKNOWN: nothing offline. The C65A run on J313 tells whether
  txs/query_levels/texel-centre txl on heap handles match D3D values on AGX.

## C65V: the min/max variant (heap samplers)

- Owner: `AGX_D3D12_SHADER_MINMAX` (`AGX_D3D12_GRAPHICS_DESC::Flags`,
  `AgxD3d12OwnerCreateComputeFlags`) sets a scoped thread_local
  (`ShaderMinmaxScope`), which `shader_binding_limits()` turns into
  `DXBC_BINDING_MINMAX`. The `APPLE_AGX_D3D12_MINMAX` env gate of C65G is gone.
- DDI: the PSO keeps its owner description (`owner_desc`, `owner_so`).
  `MinmaxVariant` compiles the variant once, under `compile_lock`, on the first
  replay whose heap walk (`SetStageHeapWalk`) finds a sampler-map entry with
  reduction 2 or 3. It keeps the variant only if every binding map and
  SRV/UAV dimension equals the plain compile's. Receipt:
  `measure-d3d12-minmax-variant {compute, same}`.
- Correction: the variant was first inline in the PSO (60724af7). That cost
  ~28 KB per PSO (five binding maps), so it is now allocated on first use.

## C65S: slot and static samplers

- The key of a variant (`AgxD3d12MinmaxKey`):
  - the heap flag;
  - per stage (`AGX_D3D12_HEAP_VS..CS`) and slot, the bound slot or static
    sampler's `dxbc_minmax_bits` (`SlotMinmax`, from
    `AgxD3d12OwnerBindings::samplers[].Filter`), with reduction 2/3 only.
- The PSO caches up to 4 variants (`kMinmaxVariants`). A fifth key draws
  with the plain pipeline (`reject-d3d12-minmax-variants`).
- The owner (`wrap_slot_minmax`) runs `dxbc_minmax_wrap_slots` on each
  translated stage with that stage's row. Each sample through slot s with
  reduction 2/3 is wrapped with its bits as a constant, so the branch folds to
  the emulation alone. Samples through heap handles, sampler derefs or a dynamic
  sampler offset are left alone; a dynamically indexed bounded slot range
  still filters normally.
- Checkpoint: C65A has three modes (heap table, one-sampler table, static
  sampler), all PASS on WARP.
