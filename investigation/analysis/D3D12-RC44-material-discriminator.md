# RC44: captured material path and the next pixel discriminator

The EXP1969 capture does not prove a frontend defect. All 103 unique program
parts translate through `tests/test_dxil_to_nir.py`'s real frontend and Asahi
preprocessing: 10 vertex, 11 fragment, 82 compute. No rendering behavior is
changed by RC44. The complete offline evidence, hashes and results live in
`.local/handoff/codex/rc44/REPORT.md` and `replay/` (shared handoff root).

## Identified paths

The strongest geometry-material candidate is VS `aec1419337bc88d7-s0-r0.bin`
with FS `9fa121805cf5ea58-s4-r0.bin`. Their four varying registers match; the
same-thread input receipts are at EXP1969 `gm1969a-umd.log:9811` and `:9813`.
The VS consumes packed normals/tangents, UVs, and a uint4 at input register 3.
It reads material metadata at u4 and transforms at u6. The FS reads u4,
selects eight Texture2DArray descriptors t3..t10 using the high 16 bits of
packed material words, and uses their low 16 bits as texture-array layers.
Sampler s3, three samples (normal, colour, material), normal reconstruction
and four G-buffer outputs survive in raw and final NIR.

A separate large pair, VS `e71b85a6cda7a1bb` / FS `71405b8756e2cea9`, reads
material/transform data and arrays plus environment cubemaps. It is another
candidate pass, not an interchangeable version of the four-target material
shader. No captured VS reads SV_InstanceID. Instance stepping can still matter
through the IA: Asahi `agx_nir_lower_vbo.c:169` divides the instance ID by the
attribute divisor and `:173` adds base instance.

These are shader-role identifications from dataflow, not proof of the PSO used
by every counted draw. `agx_d3d12_ddi.cpp:5936` logs ExecuteIndirect while
recording; proximity to creation/translation receipts does not identify replay
state. The application also has an intentionally constant-white FS
`b32725b310f243e9`; its input DXIL already writes (1,1,1,1).

## Why a probe, not a speculative renderer change

1. The material FS preserves descriptor index, array layer and sample values.
   A local metadata replay confirms all three samples have `is_array=1` and
   three coordinates. Mesa's NIR printer (`nir_print.c:2215`) prints only the
   dimensional name, so its text `2D` must not be read as lost arrayness.
2. The map is bounded slots (flags=0), not the C47 bindless heap path. The
   eight registers become slots 0..7; u4 and s3 each become slot 0 in their own
   namespaces. There is no evidence for changing heap-range indexing here.
3. Neither graphics candidate loses a sampled colour to constant white or a
   new undef. The large VS's three undef values enter unsuccessful-search phi
   edges and are excluded by the success branch. Some compute local arrays
   become undef-initialized scalar loop phis during preprocessing; their runtime
   contents and whole-application role remain unproven, not silently certified.

The remaining ranking is (1) runtime material/descriptor/content selection,
(2) lighting/HDR/later-pass or wrong-pass state, (3) IA instance fetch/arguments,
(4) an unproven compiler semantic issue. This is a causal work order, not a
claim that the first item is the root cause.

## Implemented checkpoint

Explicit named steps `RC44D` and `RC44I` run the same original synthetic HLSL,
resources and root bindings. RC44D issues direct indexed draws; RC44I issues
one-argument, stride-20 counted indexed draws, with count=1 and max=2. The valid
second command selects a different material and must be suppressed.

Each step draws two 4x4 cases with per-instance uint4 input register 3,
StartInstance=3/5, raw u4 material words, eight two-layer SRVs t3..t10 and
point-clamp s3. Readback checks all 16 pixels from both colour and selection
MRTs. Literal expected RGBA8 values are:

| Case | Colour | Instance tag / descriptor / layer / alpha |
|---|---|---|
| 0 | 62,64,70,255 | 3,2,1,255 |
| 1 | 150,106,78,255 | 5,7,0,255 |

This is a minimal synthetic root signature for the observed registers; the
application's root signature was not captured. It omits normal decoding,
transform lookup, compressed formats, and deferred lighting. The system
D3DCompiler_47 compiles original SM5.1 HLSL on explicit invocation; the normal
R7 runtime converts it to DXIL. Confirm that route in the translation receipt.
There are no captured/proprietary shader binaries in Git.

Integration owner, after normal build/hash/preregistration:

```text
agx_d3d12_probe.exe warp RC44D RC44I
agx_d3d12_probe.exe only RC44D RC44I
```

Run WARP first to check the test itself, then AGX with the GPU indirect-stream
switch disabled and the existing normal profile/recovery contract. Do not
change package, caps, signer or firmware for this discriminator. Require both
`AGX_D3D12_RC44D: PASS` and `AGX_D3D12_RC44I: PASS` and the exact case values;
the existing named-step dispatcher does not propagate step failures through
its process exit code. Archive stdout, UMD receipts, compiler diagnostics,
shader captures and hashes. No hardware was accessed while writing RC44.

- Direct passes, indirect fails: investigate IA/indirect parameters and the
  diagnostic index output; no extra StartInstance bias in DXIL.
- Both fail with correct indices: narrow texture upload/view/sampling.
- Both fail with wrong indices: narrow IA/material buffer/root binding first.
- Both pass: this synthetic selection path works; move to actual app bindings
  or G-buffer/lighting readback. Do not call the white asteroids fixed.

CPU probe code owns test data and the oracle. Runtime/UMD/Asahi retain shader
translation, vertex fetching and descriptor construction. No firmware/m1n1/Mu,
KMD DMA, interrupt, power or recovery ownership changes are proposed.
