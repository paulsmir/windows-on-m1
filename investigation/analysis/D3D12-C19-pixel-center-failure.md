# C19 EXP1390: pixel-center translation boundary

2026-10-18. One bounded read-only pass. No patch/compiler/builder/guest/hardware/
commit/ledger operation. Queued1392 must not be launched unchanged on this result.

## Evidence and strongest causal decision

Actual `.local/experiments/EXP1390-d3d12-cube-init/evidence/EXP1390/tool-d3d12-on-std.txt`
lines283–318: C19A/B/C/D each created resources/views/PSO, executed and completed
fence1 with S_OK. Each had64/4096 mismatches, first at(x21,y0): expected face1,
received face0. C/D still had the correct selected cube1/mip1 G/B values, while
A/B had cube0/mip0 values. The face-dependent R/A changed exactly with wrong face.
The all-pixel oracle has not silently ignored a column. These receipts localize
the strongest candidate before cube sampling: pixel shader coordinate evaluation.

The requested tool-d3d12-on-umd.log is not present in the local EXP1390 evidence
at the time of this pass; no missing log is claimed read. Existing generated FXC
headers and actual source supply the precise shader instruction evidence below.
No guest fetch was attempted while parent controls recovery.

Shader formula is uint(position.x*(3/64)) plus3 when position.y>=32. Probe oracle
is integer floor((6*x+3)/128), plus3 for y>=32. 3/64=0.046875 is exactly binary
representable. At pixel center x21.5 it gives1.0078125 -> face1. Integer x21 gives
0.984375 -> face0. Across x0..63, only x21 changes face under omission of the half
pixel; both grid rows repeat that difference, predicting exactly64 errors across
64 rows. The y threshold32 does not distinguish integer from half-integer centers.
This is an exact mathematical prediction matching first-error location/count,
not a claim that the receipt listed every bad coordinate.

Official Microsoft [SV_Position semantics](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics)
and [D3D10 pixel coordinate system](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-coordinates)
require pixel centers offset by(0.5,0.5). Thus the current probe oracle is correct;
changing it to integer positions would conceal a translation defect.

## Owner state is already correct; exclude that proposed fix

`mesa/winsys/agx_d3d12_windows.cpp::AgxD3d12OwnerCreateGraphics` sets
rast.half_pixel_center=1 and clip_halfz=1. Blame attributes these to8e2923fb before
C19; the exact C19 snapshot53e025377af0ea6ceadb80dec78d5b4ade67869f also contains
half_pixel_center=1. Upstream D3D10 Rasterizer.cpp uses the same setting. Therefore
adding that bit is not a new variable and cannot fix the observed source path.

`dxbc_to_nir.c` sets fragment origin_upper_left=true and pixel_center_integer=false,
then its PS position declaration directly emits nir_load_frag_coord and channels
that value. No subtraction or deliberate integer-center offset was found there.
Those metadata flags alone do not insert the missing arithmetic.

## Primary native/compiler path explains the lost fraction

Pinned current Mesa sources inspected:

1. Asahi agx_pipe.c caps advertise fs_coord_pixel_center_integer=true and
   fs_coord_pixel_center_half_integer=false.
2. Asahi agx_compile.c calls nir_lower_frag_coord_to_pixel_coord before final
   lowering. That NIR pass explicitly assumes pixel-center lowering was applied
   already. It replaces raw load_frag_coord XY by u2f(load_pixel_coord), with no
   +0.5. Asahi instruction selection loads pixel_coord from integer thread-position
   registers. Rasterizer half_pixel_center does not change this arithmetic rewrite.
3. The native create_shader path hands DXBC NIR to create_fs_state without a
   nir_lower_wpos_center transformation. Upstream agx_create_shader_state and
   agx_preprocess_nir do not insert it for that path either.
4. Asahi's Vulkan frontend hk_shader.c explicitly runs nir_lower_wpos_center.
   That pass adds load_sample_pos_or_center to XY, leaves Z/W unchanged and must
   run before I/O lowering. Asahi's sample-intrinsic lowering supplies(.5,.5) when
   sample shading is off, which is the admitted single-sample C19 profile.
5. nir_build_frag_coord can add a half pixel when the compiler option
   nir_frag_coord_use_pixel_coord is set, but actual agx_nir_options leaves
   frag_coord_form at0. Its fallback is raw nir_load_frag_coord. Merely changing
   the DXBC helper name would NOT close this exact backend contract.

The source-owned defect is therefore missing native fragment coordinate-center
translation before the Asahi backend consumes raw frag_coord. It is not evidence
for changing cube face/layer/mip addressing or the C19 expected pixels.

No external code should be copied: use an existing reviewed NIR lowering entry,
with explicit target semantics. The smallest candidate is a once-only D3D12
native fragment transformation in the owner shader preparation path, before
create_fs_state and Asahi preprocessing/IO lowering. Confirm the owning call site
with an actual-function test before implementation. Do not patch a generic DXBC
semantic definition with an unconditional target-specific offset, change the
backend's GL/Vulkan contracts, or apply the offset twice.

## Smallest deterministic regression and hardware checkpoint

Offline regression should use actual current C19 FXC bytecode, actual DXBC->NIR
and actual owner create_shader with a captured native FS submission. Show the
current raw fragment coordinate loses its half pixel under the actual Asahi late
frag-coordinate pass; after the native preparation fix, XY must equal pixel+0.5
while Z/W remain unchanged. Check x0/21/22/42/43/63 and y0/31/32/63, both the cube
and array shaders; the x21 transition must select face1/4. At minimum validate
actual NIR dependency/arithmetic from integer pixel_coord to these values using
existing real passes, not a test that only checks a source string for +0.5.

A bounded original NIR evaluator or constant-substitution/folding can evaluate
that fragment-coordinate slice. No new FXC invocation is necessary: exact
`agx_d3d12_probe_ps_cube.h`, `_cube_array.h`, `_cube_array_second.h` already contain
both disassembly and bytecode. Their FXC disassembly has mul v0.x,0.046875 then
ftou; no shader-side coordinate adjustment. C19 texture-result axes are centers,
so cube-edge precision is not an alternate cause at that selection boundary.

After deterministic RED/GREEN and parent build/sign/hash gates, use a NEW package
experiment ID with one independent variable: native D3D12 FS center preparation.
Keep existing C19 HLSL/FXC/probe/oracles unchanged. Require all four C19 variants
bad0/4096 and correct selected cube/mip, plus existing41 D3D12 regressions. The
old64-error signature must disappear; any recurrence, extra mismatch or regression
fails the candidate. Ordinary parent-controlled visible recovery remains required.
No hardware run is authorized by this note.

## Once-only owner decision and prepared actual-function RED

nir_lower_wpos_center is NOT idempotent: it leaves the raw intrinsic as an input
to a newly added sample-position sum, and sets no already-normalized marker.
Calling it again adds another offset. Existing origin/pixel_center flags alone
cannot distinguish already-lowered GL/Vulkan NIR. Therefore a generic shared
agx_create_shader_state insertion is not safe. Do not infer that a flag change
makes the transformation once-only.

The smallest proven boundary is D3D12 owner create_shader, on freshly translated
DXBC fragment NIR, immediately before its one create_fs_state call and before
Asahi I/O preprocessing. The candidate should add the existing NIR center-lowering
entry only there, never for VS, and preserve the immutable compiler flags. This
changes neither the generic DXBC parser nor GL/TGSI nor the D3D11 fallback paths.
The generated D3D11 AgxD3d10CreateShaderState also has a fresh DXBC route and may
need the same target normalization, but repairing a shared helper requires an
explicit per-caller once-only contract and separate TGSI review. It is not a
reason to risk double-normalizing all native shader inputs for the C19 repair.

NEW test-only preparation:
`tests/d3d12_pixel_center_fixture.cpp`, `tests/test_d3d12_pixel_center.py`, and
`tests/d3d12_pixel_center_pins.json`. No production edit or compiler run yet.
The test extracts actual owner create_shader, uses exact three C19 FXC bytecode
arrays and actual DXBC parsing/translation, and captures the native FS submission.
It runs actual nir_lower_frag_coord_to_pixel_coord and actual
agx_nir_lower_sample_intrinsics, then recursively evaluates only the actual SSA
coordinate slices consumed by the shader's mul/ge. It does not emulate the cube
shader or replace the pixel oracle.

The first numeric check is pixel(21,0): expected(21.5,.5), current backend path
predicts(21,0). Remaining points cover x0/22/42/43/63,y0/31/32/63:72 coordinate
pairs across the3 exact C19 shaders. The test also verifies preserved Z/W at
nontrivial(.25,.75), metadata unchanged, exactly one center intrinsic before
Asahi lowering, and a real legacy VS bytecode with no fragment normalization.
A functionally failing coordinate assertion is the desired RED, not a source
string or missing-function check. Keep expectations unchanged after the repair.

Existing host archives do not contain agx_nir_lower_sample_intrinsics. The proposed
isolated contract compile therefore builds that EXACT pinned existing Mesa source
into one test object, with its MIT notices preserved in the external source;
there is no tracked external-code copy and no dependency/archive rebuild. Existing
libnir provides the other actual passes. Parent must preregister this specific
single-source test compile before any execution.

Prepared manifest:
`.local/c19-pixel-center-offline/before-red.json` SHA256
`b5fa4ac84f46425532e8364589eec4b6d059c3a1a62c3c01017791a9f2659dbd`.
It records17 original-source/header/probe hashes and27 pinned external library,
source, generated-header and tool inputs. Pins JSON SHA256
`9177bc2163452244745e5b36fc3f379fcb8d3d9470fa92ca7bfe2a0256fc1eb2`.
Fixture SHA256 `53184dc87e477e236060c9a8df7f95569c00f569cf37c54c9f9e278378d78236`;
runner SHA256 `4fb395c7e87531d6b7ebfc331de82e4cd3cd06d33e8ca0e45d580c12c60544c4`.

Exact proposed command, NOT EXECUTED:

```
AGX_C19_CENTER_RECEIPTS=/Users/pavel/public_windows/.local/c19-pixel-center-offline AGX_C19_CENTER_APPROVED_MANIFEST=/Users/pavel/public_windows/.local/c19-pixel-center-offline/before-red.json AGX_C19_CENTER_APPROVED_SHA256=b5fa4ac84f46425532e8364589eec4b6d059c3a1a62c3c01017791a9f2659dbd PYTHONPATH=tests python3 -m unittest test_d3d12_pixel_center -v
```

Six exact compile/link/run commands are in prepared-red-commands.json, SHA256
`5a4d079b69a77aa35057879b256ffeaaad1596bdfca9f55c8b7bb6d788f16c72`.
No new FXC, guest or package operation is needed for this CPU discriminator.
Candidate1404 remains reserved for a separately approved repair snapshot/build/
hardware verification; no queued package is made acceptable by this prepared test.

### EXP1403 first attempt: test compiler failure, not functional RED

Parent BEFORE0400bd22 authorized the six commands above. All three C objects
compiled, including the isolated actual Mesa sample-intrinsics pass. The C++
fixture then failed because pipe_shader_state required pipe/p_state.h and the
pinned nir_def API uses nir_def_instr(), not a parent_instr member. No executable
ran, so no fractional-coordinate result is asserted from this attempt.
Original raw results remain in `.local/c19-pixel-center-offline`, step3 stderr.

Prepared test-only correction includes that header and uses the actual accessor;
no numeric/once-only/ZW/flags/VS expectation or owner source changed. New receipt
root `.local/c19-pixel-center-offline-retry1` preserves the failed attempt. New
before-red.json SHA256
`47e4bb55271683a470f6f9ddb5da6d72e415f6c540a7bddb3ee22499353455c6`;
fixture SHA256 `682b96ae6400a5d97fe8ee6c7139b802d16cec3a132538d470fa3fc243c7e99c`;
pins SHA256 `500157b52f72822aa80bda3234d586703b22cffb42804e47557f4a3c7ddc3fa8`
adds the required pinned p_state.h. The revised command-array hash is
`ce9b70a98ef624281f249b6a0cebd22bac0fdd0dd3a2bc55fa1cfb0bff487698`;
only receipt paths changed. This retry is prepared, not executed, pending parent
approval. The driver defect still needs its numeric actual-function RED.

### EXP1403 retry: actual numeric RED and frozen owner candidate

Parent retry BEFOREa95b9ece authorized the corrected ABI-only test. All five
compile/link steps passed, then the executable failed numerically:

```
CENTER_MISMATCH pixel=21,0 got=21,0 expected=21.5,0.5 center_intrinsics=0
```

This directly reproduces the source-derived half-pixel loss using actual owner,
C19 bytecode and Asahi passes. Original failure receipts remain under retry1/.
Only afterward was the authorized minimal owner candidate prepared: include nir.h
and invoke existing nir_lower_wpos_center once for freshly translated fragment
NIR before native create_fs_state. No rasterizer bit, DXBC generic parser, GL,
TGSI, D3D11, FXC, probe or oracle changed.

Frozen owner candidate SHA256
`5974f1b36d578aed2f4e5a085611279b797c910bfcb98a2bbe5cc3fd43a7fbee`.
New GREEN manifest `.local/c19-pixel-center-offline-green/before-green.json`
SHA256 `811b44e99518c7c5ef3b20c466c7e26ad68e96355659fcaa2de66935ad40ba7b`;
prepared command-array hash
`bc1b0dcdaf4abcd254479bf401d20076b283512d8d6721d33ceaf903fd8decea`.
Tests and28 external pins are unchanged. GREEN compile remains pending parent
BEFORE; a full native Windows TU/header/symbol build gate is separately required
before parent snapshots any1404 package.

### EXP1403 GREEN: actual owner / exact C19 bytecode / actual Asahi passes

Parent GREEN BEFORE36946622 authorized owner5974f1b3, manifest811b44e9,28 pins and
six commands. All checks passed on the first GREEN execution, without changes to
test expectations, probe, FXC or owner after source freeze:

```
C19_PIXEL_CENTER_PASS actual_owner=YES exact_FXC=3 actual_Asahi_passes=YES coordinate_pairs=72 x21=21.5 ZW_preserved=YES once_only=YES VS_untouched=YES NO_GPU
```

Both the old integer coordinate and new half-integer coordinate were numerically
observed through the same actual-function test. The patch remains confined to
fresh D3D12 fragment preparation; no shared native factory or D3D11/TGSI/GL path
changed. Existing nir.h inclusion is outside the owner's extern C block. This
host isolated-function success is not a native Windows whole-TU/header/symbol
build result; parent must complete those gates before package1404.

Durable root `.local/c19-pixel-center-offline-green` contains approved sources,
pins, six exact commands, raw outputs, original objects and executable, generated
actual-owner source and artifact hashes. `EXP1403-verdict.json` SHA256
`ef5a2852cacb5f095027f2a594646cbc9dd59e3a278a2fe00612ada3c2e195fd`;
executable SHA256
`20b6f4022de9c65a88b37436c944d771a72d6cde81f2c7c5caa2e0f788a4da1b`.
All prior compiler-fail and numeric-RED receipts remain at their original paths.
No package/build/guest/hardware/commit/ledger action was performed by this lane.
Hardware C19A-D bad0/4096 and unchanged old41 remain the future1404 discriminator.
