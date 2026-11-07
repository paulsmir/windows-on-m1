# Forced sample count and target-independent rasterization (FL 11_1)

Status: design, 2026-10-23. This is part of D3D12-FL-program.md (D3D11 FL 11_1,
step 1). It is the last FL 11_1 gap after logic ops (eac91e7b) and the 64 UAV
registers (5ecb6ff8). The same machinery later gives D3D12 12_1 conservative
rasterization Tier 1.

## WINDOWS CONTRACT

Source: D3D11.3 functional spec, section 3.5.6 "Target Independent
Rasterization" (microsoft.github.io/DirectX-Specs, D3D11_3_FunctionalSpec).
The DDI field is `D3D11_1_DDI_RASTERIZER_DESC.ForcedSampleCount`, also in
`D3DWDDM2_0DDI_RASTERIZER_DESC`.

- **Valid values**: 0 (off), 1, 4, 8, 16.
  - FL 11_1 requires full TIR up to 16.
  - FL 10_x/11_0 allow only 1 with RTVs.
  - UAV-only rendering allows 0/1/4/8 on 11.0 hardware, plus 16 on 11.1.
  - The standard sample patterns are required up to 16, even where render
    targets have fewer samples.
- **Bindings with ForcedSampleCount >= 1**:
  - no DSV may be bound;
  - depth testing is off;
  - the shader writes no depth;
  - with a count > 1, bound RTVs are single-sampled (otherwise undefined).
- **Candidate pixel**: the primitive covers any sample of the forced
  pattern. The shader runs once per candidate pixel when any output sample
  is in the SampleMask. Sample-frequency shading is undefined.
- **Input coverage** (`SV_Coverage`): the primitive's coverage of the forced
  pattern.
- **Interpolation**:
  - Pull-model evaluation (EvaluateAttributeAtSample) uses the forced
    pattern.
  - Centroid uses the forced pattern and ignores the SampleMask.
- **UAV-only**: the viewport and scissor give the pixel grid. The SampleMask
  does not affect rasterization; count 0 equals 1.
- **Line rendering**: at 11_1, MultisampleEnable only selects the line mode.

## AGX/ASAHI CONTRACT

- AGX rasterizes 1, 2 or 4 samples (Honeykrisp `sample_counts`, also for
  attachment-less rendering); there is no 8x/16x.
- There is no hardware conservative rasterization (Honeykrisp exposes none).
- Gallium has no TIR state.
- Asahi runs geometry shaders as compute (`agx_nir_lower_gs`) and accepts a
  driver-made NIR GS.
- The fragment shader can discard and can read and write the sample mask.
- 64-bit integer ALU is lowered to 32-bit pairs (`nir_lower_int64`).

## TRANSLATION

A draw whose rasterizer state forces N > 1 (and N = 1 with multisampled
RTVs, see UNKNOWN) runs a variant pipeline in one pass. Multiple passes
would repeat UAV side effects and blending, so they are not allowed.

1. **Expansion GS** (generated NIR, keyed by the VS output and PS input
   layout):
   - Snap the three post-viewport positions to D3D's 16.8 fixed point.
   - Push each edge outward by its half-pixel L-infinity extent, which gives
     the conservative (overestimating) triangle.
   - Express its vertices as barycentric (affine) combinations of the
     original three. Apply them in clip space to the position and to every
     interpolated varying, so the attribute planes, perspective-correct ones
     included, equal the original's. Flat varyings take the original
     provoking vertex 0.
   - Emit the original edge functions as three flat varyings, in integer
     16.8 coefficients plus the top-left fill-rule bias.
   - Zero-area primitives are dropped.
2. **PS variant** (translator option, forced count N):
   - At entry, evaluate the three edge functions exactly at the N D3D
     standard sample positions (1/16 px offsets) of this pixel:
     - E(center) in int64;
     - per-sample increments in int32.
   - A sample is inside when every biased E >= 0. Inside samples form the
     mask; mask 0 discards.
   - `SV_Coverage` reads the mask. Sample count and position queries report
     N and the standard pattern.
   - Centroid inputs use the first covered sample's offset, through
     interpolation at an offset.
3. **Frontend**:
   - The rasterizer state keeps N.
   - At draw time with N > 1, the bound VS/PS pair selects (or creates) the
     GS and PS variants; HS/DS feed the same GS.
   - An application geometry shader with N > 1 is refused, with a receipt,
     until its output can be chained.
   - DSV and depth are ignored as the spec allows (undefined).
4. **Caps**: after hardware validation, report `D3D11DDI_3DPIPELINELEVEL_11_1`.
   This needs logic ops, the 64 UAVs and TIR all PASS.

## WHAT IS STILL UNKNOWN

- Whether Asahi's GS-as-compute path accepts a driver GS together with
  tessellation here. Hardware check, once a tessellated forced draw is
  probed.
- Triangles with a vertex at w <= 0 cannot be expanded in screen space. The
  first version clips them in the GS against w = epsilon before expanding; how
  often real content hits this is unknown.
- ForcedSampleCount 1 with multisampled RTVs: the spec broadcasts center
  coverage to the output samples. Today the draw rasterizes at the RT's
  count (EXP1324). This is rare; it is measured before it is changed.

## Checkpoint

Probe step D11TIR in `agx_d3d11_fl111_probe`:
- A UAV-only draw of thin, edge-aligned and sub-pixel triangles with
  ForcedSampleCount 16 and 8.
- The PS writes `countbits(SV_Coverage)` into a UAV per pixel.
- The probe compares this with a CPU reference built from the D3D standard
  patterns and the top-left rule: zero mismatches.
- A TIR draw (16, single-sample RTV, additive blend) checks one invocation
  per candidate pixel.
