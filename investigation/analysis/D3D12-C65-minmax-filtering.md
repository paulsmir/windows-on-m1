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
