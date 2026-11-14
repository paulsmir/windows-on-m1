# C15 standalone SM6-to-AGX checkpoint: source-first plan

2026-10-18. Prepared after parent's EXP1388 Windows ARM64 build-only PASS receipt:
83 TUs, nine archives, C caller link, ARM64 PE and /MD gates; **no ARM64 runtime
execution**. This note precedes compiling any new HLSL or implementing the host
adapter. No production import/link, shader-model capability change or GPU action.
This is RENDER_ONLY compiler work. Translator provenance/licenses remain the
reviewed candidate and N01–N14 inventory; tool licenses are separate.

## Available immutable inputs and tools

Translator: dxil-spirv `8434b0639fb2a48ce13aac08a420ecce5d23df0e`, builtin llvm-bc,
exact reviewed dxbc-spirv/H graph. Existing standalone host nine archives are under
`$HOME/public_windows/.local/c15-host-8434b063/build`; their hashes and complete
compiler input inventory are in `observed-inventory.json`, SHA-256
`93ac2f925e03b24a902056f088041970aac00301c6c1fff11386a380c86e0b22`.
No translator source/target change is proposed.

Read-only builder inventory found an existing Windows SDK DXC installation at
`C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64`. No installation,
source fetching or HLSL compilation occurred during inventory.

| Tool | FileVersion | SHA-256 |
| --- | --- | --- |
| dxc.exe | 1.8.2502.11 | `7c6918a0e2d4e437629fa8549f5ce800970494780f363bbbe1e3d3034f435aee` |
| dxcompiler.dll | 1.8.2502.11 | `78b5e21a91707b4a567ef281a519c52a45b709561202f54c5ad79c5a315847ed` |
| dxil.dll | 1.8.2502.11 | `77e039c905030a641e53658a008b74e90635a5ea9b6b79eabd0f2003bdfca59a` |

`dxc --version` reports dxcompiler1.8.2502.11 (239921522), dxil1.8.2502.11.
Receipt `/tmp/c15-dxc-readonly.txt`. These existing SDK binaries are build tools
only, not approved redistributable inputs to a driver package. The translator
license review does not license redistributing DXC or its validator.

Existing host `/opt/homebrew/bin/spirv-val` reports SPIRV-Tools v2026.3, build date
2026-07-22T20:34:54+00:00, unknown upstream commit; executable SHA-256
`88247eddaac79df193b44978ec9c71706ace239c60749dc2a6f1c568c6ae7871`.
It supports Vulkan1.0 through1.4 environments. Existing tool only: no new Tools
source, link dependency or installation is proposed. Record the exact validator
environment and result rather than treating its binary version as a source pin.

Pinned Mesa remains `9aa1215f878b504f66159dd2ead4c7973142126e`. Existing host build
has `src/compiler/spirv/libvtn.a`, `libnir.a`, compiler/util archives and the actual
Asahi compiler archives already used by DXBC fixtures. Its API is
`spirv_to_nir(..., MESA_SHADER_COMPUTE, entrypoint, &spirv_options, &agx_nir_options)`.
`src/compiler/spirv/nir_spirv.h` is MIT, SHA-256
`af160db74b5fed349804eb06b5e230de17b6aea85416e7666c8ef1840b52e130`.

## Smallest meaningful fixture and contracts

Original, self-authored HLSL will use one `RWByteAddressBuffer` at u0/space0,
`numthreads(64,1,1)`, and store `3*SV_DispatchThreadID.x+7` at byte offset `4*x` for
x<64. This keeps an observable side effect; an empty compute entry would not
exercise the resource and memory boundary. No wave, 64-bit arithmetic, texture,
atomic, barrier, root constant, dynamic descriptor or extra register space.

Once this note records tool pins, the authorized isolated builder-only command is:

```
"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe" \
 -T cs_6_0 -E main -HV 2018 -O3 -Ges \
 -Fo c15_compute.dxil -Fc c15_compute.asm c15_compute.hlsl
```

Use a dedicated C15 fixture directory, verify the three tool hashes before invoking,
keep validation enabled (no -Vd), preserve HLSL/DXIL/disassembly/log hashes, and fetch
only fixture outputs. No production/builder shared source directory or guest command.
This source is original and may be preserved with its provenance; no external shader
corpus or tool binary is imported into tracked source.

The C API adapter must begin a thread allocator context, parse the owned container,
verify compute entry `main`, create a converter, and explicitly remap only the
expected singleton raw UAV u0/space0 to non-bindless SSBO set0/binding0. Reject any
unexpected kind/space/register/range/counter and any SRV/CBV/sampler. Inspect exact
callback structures and defaults in `dxil_spirv_c.h:288-379,1013-1036`; resource remap
is a fixture-only admission contract, not a general D3D12 binding solution. Convert,
copy returned SPIR-V bytes before freeing converter/blob, then reset/end allocator.
Record actual SPIR-V version/capabilities/extensions and validate under an explicit
matching Vulkan environment using the existing spirv-val. Refuse unsupported output;
do not enable broad features merely to make one module pass.

## Mesa and AGX lowering gates

Sources inspected: Mesa `compiler/spirv/nir_spirv.h`,
`gallium/frontends/lavapipe/lvp_pipeline.c:394-410`,
`lavapipe/nir/lvp_nir_lower_pipeline_layout.c:195-223`,
`asahi/vulkan/hk_shader.c:829-862`,
`gallium/drivers/asahi/agx_state.c:1440-1478,1736-1822`, and AGX compiler header.
These sources show that SPIR-V parsing is not enough: Vulkan resource-index and
load-descriptor intrinsics must become the indexed SSBO form the Gallium/Asahi
buffer lowering expects. Existing DXBC fixtures stop at preprocessing; they do
not themselves prove backend machine-code compilation.

Proposed host stages, each with explicit NIR validation and retained diagnostic dump:

1. SPIR-V frontend: Vulkan environment, AGX NIR options, indexed32-bit-offset
   SSBO addressing and only capabilities needed by the actual module. Assert compute
   stage, workgroup64x1x1 and one expected descriptor. Keep actual store/dataflow.
2. Original fixture-only descriptor lowering: accept only the already verified
   constant set0/binding0 singleton, translate its resource-index/descriptor chain
   into direct SSBO slot0, and fail on every remaining Vulkan descriptor intrinsic.
   Lower explicit buffer I/O using actual Mesa passes. Validate indexed store_ssbo
   address/value shapes and binding0; do not guess a GPU address.
3. Asahi compiler preparation: use actual Gallium binding, SSBO, system-value and
   uniform-layout passes in their source ordering, then actual agx_preprocess_nir
   and agx_compile_shader_nir. Retain the returned owned binary and compilation
   metadata only; allocate no Screen, context, resource, GPU VA or executable BO.
   Require nonempty binary, compute metadata and absence of unsupported intrinsics.

The host lacks a built Gallium Asahi archive containing its binding/sysval passes.
If needed, compile those **unchanged actual source files** in the isolated harness,
not copied implementations, after reviewing their MIT headers and hashes:

| Actual Mesa source | License | SHA-256 |
| --- | --- | --- |
| agx_nir_lower_bindings.c | MIT, Valve2023 | `b292dd7ca0b4eebd25bae59185f5ebda0f3c681490b55a5053a7f3d69d3d7cb1` |
| agx_nir_lower_sysvals.c | MIT, Alyssa Rosenzweig2022 | `9fdce3f3623e0a58b83debe7527ab621976f896ba6f68f9bf88653c8c86c4499` |

Any additional source/link dependency discovered by compiling these files is a
reported graph delta requiring inspection before use. Do not patch translator,
Mesa or production sources or replace a failed pass with arbitrary physical
addresses, erased stores or synthetic NIR. If this narrow bridge fails, retain the
strongest successful boundary (e.g. validated SPIR-V and NIR) and report the actual
intrinsic/dependency blocker.

## Verification meaning and remaining unknowns

Invariant: the exact self-authored SM6 container reaches the reviewed translator,
valid SPIR-V and meaningful, valid buffer-writing NIR; then the actual AGX backend
accepts that shader without losing its store or inventing descriptor semantics.
Negative gates include unexpected binding, malformed input and residual unsupported
resource operations. Tests should catch these actual translation boundaries, not
only assert that a tool was invoked.

No device executes this shader in this checkpoint. Nonempty AGX binary is compiler
acceptance, not proof that its runtime descriptor table, constant upload, dispatch
or UAT mapping is correct. The planned 64-word `3*i+7` result is a later hardware
oracle only after production mapping and truthful shader-model admission are
independently designed. C15's older plan cap/DDI discussion remains a future review;
this fixture does not authorize any SM6 advertisement.

Strongest next offline risk: descriptor-model lowering between Vulkan-flavored
SPIR-V NIR and Gallium indexed SSBOs, followed by availability of unchanged Asahi
sysval/uniform passes in this standalone host harness. Tools and reviewed translator
archives are already available; no new third-party installation is needed.

## Implementation input recorded before DXC invocation

Parent authorized proceeding with the planned isolated checkpoint. Original HLSL
is `$HOME/public_windows/.local/c15-sm6-8434b063/compute.hlsl`, SHA-256
`d9493d63709a0fc4a546f97cfa5033f6a7f14c90360d56aa7d119ddc188356e0`.
It is exactly the one-u0/space0, 64-thread guarded `3*x+7` fixture above. Dedicated
builder fixture directory: `C:\Users\pauls\C15-sm6-8434b063`. Before compiling,
recheck the three recorded SDK tool hashes and this source hash. Only original
HLSL and generated fixture/log outputs enter this directory; no SDK binaries are
copied or redistributed and no production source is referenced.

## EXP1389 first observed boundary and dependency delta

Parent assigned EXP1389 BUILD_ONLY label. Existing pinned DXC compiled the original
HLSL successfully with validation enabled: DXIL SHA-256
`f3934fccef3b1428d62ed6a675540cab1bf674faaf85c67320e3ed83c6c62bb4`,
DXC disassembly SHA-256
`b5ace6839eb132ae1b1707eb5a0c1c72a7a67057d8df55aafbe3be44b40b3c13`.
C API adapter accepted only raw UAV id0/u0/space0/range1/alignment16/no-counter,
produced 980 bytes SPIR-V1.3, copied them before freeing converter/blob/allocator,
and wrote the owned bytes afterward. Explicit remapper rejection returned -2;
no output was admitted in that negative case. Existing spirv-val --target-env
vulkan1.1 passed. Disassembly has only Shader capability, no extension, SSBO
set0/binding0, LocalSize64x1x1. These gates passed before the Mesa link below.

The first host libvtn link stopped on unresolved SPIRV-Tools C functions from
vtn_debug.o (spvBinaryToText, spvContextCreate, diagnostics/text cleanup). Existing
Mesa host libvtn was compiled HAVE_SPIRV_TOOLS. No code has been patched to hide
that dependency. This **host harness link delta** is separate from the unchanged
reviewed translator target graph, and was reported to parent before relinking.

Available existing library: `/opt/homebrew/opt/spirv-tools/lib/libSPIRV-Tools.dylib`,
Homebrew cellar `spirv-tools/1.4.357.0`; SHA-256
`5d57212ec1d710fa3494be3efb1346ae57a22f4bdad03d899369e5585d4b9f70`.
The installed formula points to upstream vulkan-sdk-1.4.357.0 and declares
Apache-2.0; installed LICENSE was inspected and hashes to
`cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30`.
Its INSTALL_RECEIPT.json hashes to
`a121abdc2deebd5e5a68fc5c340ecc8c89a139943f97a014d76df288769e2fdc`.
It imports only host libc++/libSystem. Proposed continuation adds this existing
host dylib and libc++ to the isolated NIR harness link; no installation, new
source fetch, translator dependency or redistributable driver dependency.
Parent records ACTUAL/preregistered continuation before that changed link.

Source refinement before NIR admission: vtn_variables.c:232-307 derives descriptor
result shape from the chosen address format. `vec2_index_32bit_offset` is a
three-component address (two index channels plus offset); the fixture's direct
Gallium SSBO slot uses **32bit_index_offset**, a two-component `(slot,offset)`
address (nir_lower_explicit_io.c:126-135). Use that supported Mesa address format
rather than truncating a descriptor shape or inventing a GPU address. This is
fixture-only frontend configuration, not an external source change.

## EXP1389 final compiler-only result

**PASS: original SM6 fixture -> reviewed host C API -> validated SPIR-V -> actual
Mesa NIR -> actual AGX preprocessing/backend -> nonempty AGX binary.** No GPU
execution, production integration, capability or generic binding ABI is established.
Parent recorded Tools-only continuation BEFORE and then a separate compiler-only
symbolic-ABI continuation BEFORE before those changed build stages.

The approved Tools-only host link succeeded. Actual `spirv_to_nir` returned valid
compute NIR with one descriptor chain. The original strict fixture bridge accepted
only constant descriptor set0/binding0/index0, used supported
`nir_address_format_32bit_index_offset`, and actual Mesa initializer/return/inline,
explicit-IO, system-value, variables-to-SSA and cleanup passes produced one
`store_ssbo` at direct slot0. NIR validation passed, workgroup is64x1x1 and
writes_memory remains true. The value is `3*x+7`; raw byte addressing remains4*x.

An original deliberately narrow CPU interpreter executes this **actual indexed
NIR**, not a reimplementation of the HLSL, for invocation IDs0..127. It accepts
only the actual encountered ALU/intrinsic/control-flow subset and asserts on an
unexpected instruction. Result: exactly64 stores, exact3*x+7 values at4*x offsets,
no stores for IDs64..127. It is a translator dataflow/guard oracle, not AGX binary
execution or a general NIR interpreter.

### Compiler-only symbolic parameter boundary

The planned unchanged Gallium Asahi helper TUs were **not compiled**: their
`agx_state.h` pulls absent xf86drm.h, and no new header/dependency was installed.
Instead, after parent explicitly preregistered this narrower compiler checkpoint,
actual `nir_lower_ssbo` produced `store_global` plus `load_ssbo_address`. The original
fixture ABI pass accepts only constant `(slot0,offset0)` with a scalar64-bit result
and replaces that address load with the named **SSBO0_BASE** parameter represented
by `load_preamble(base0)`. Other indices/offsets or shapes assert/refuse before
backend compilation. No physical address value is supplied or invented.

This uses the actual compiler uniform-load mechanism: agx_compile.c:800-817 emits
`load_preamble` from uniform registers; Gallium's real uniform-layout pass likewise
uses `nir_load_preamble` at agx_nir_lower_sysvals.c:478. A64-bit parameter needs four
16-bit register slots; key.reserved_preamble=4 keeps them reserved. This is an
explicit **fixture ABI**, not a declaration that the current D3D12 owner uploads
this parameter or uses this binding convention.

Actual `nir_lower_compute_system_values` with no base-global/workgroup ID support
removes the Vulkan base offset for the fixture's ordinary zero-base Dispatch.
Actual `agx_preprocess_nir` and NIR validation then pass. Precompile NIR retains a
64-bit preamble load, the64-bit parameter+4*x address, the x<64 guard and
`store_global(3*x+7,address)`. Actual `agx_compile_shader_nir` returns:

```
C15_AGX_BACKEND_PASS bytes=68 instructions=10 push16=4 workgroup=64,1,1 COMPILER_ONLY_SYMBOLIC_SSBO0_BASE
```

Both original host adapters were compiled with ASan+UBSan; existing external
archives/dylibs are not rebuilt with instrumentation. No sanitizer failure was
reported. The C API remapper negative case refused conversion with result-2 and
wrote no SPIR-V output. There is no shader-model advertisement and no claim that
all raw-buffer, binding, DXIL or SM6 features are supported.

Source-proof hashes for the actual already-built Mesa routines inspected:

| Source | SHA-256 |
| --- | --- |
| asahi/compiler/agx_compile.c (MIT) | `1a5b056a18ffdd1fa6d3813129221e0ead2bf114c794844bbc14bdcd5ebc0c27` |
| compiler/nir/nir_lower_ssbo.c (MIT) | `a51a22be40f7d7ad3d7c0163dfc18a2697b8dbb5115c78b4ddff4d197e46b98d` |
| compiler/nir/nir_lower_system_values.c (MIT text) | `99b303c5b9c13690ac86792e419c939b9f697811599af1a624fcc9cf88378523` |
| compiler/spirv/vtn_variables.c | `bd539039fed2c7cc22840a023fc8171322ca6f4db18c9e755e754ae55d3b50ee` |

No external algorithm/source was imported or patched. No new third-party library
was installed. The reviewed translator nine-archive hashes were rechecked against
the prior inventory and remained unchanged. The added existing host Tools dylib
is solely the preregistered libvtn dependency, not a new translator/runtime package.

### Frozen artifacts and reproduction

All original sources, tool outputs, NIR dumps, adapter binaries and logs are under
`$HOME/public_windows/.local/c15-sm6-8434b063/`; EXP1389 labels distinguish
this compiler-only checkpoint. Original C adapter `translate.c`, original NIR
adapter `nir-check.c`, narrow `cpu-oracle.h` and `build-nir-check.py` remain outside
tracked production source. The full final inventory is
`EXP1389-artifact-hashes.json`, SHA-256
`4a0b5e6a669e1fab49e5a5593501411d402e7ef89f484dc86c1295e23f61144c`.
It includes all local artifact sizes/hashes plus the exact unchanged nine translator
archives, existing Mesa libraries and host Tools binaries/dylib.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| compute.hlsl | 263 | `d9493d63709a0fc4a546f97cfa5033f6a7f14c90360d56aa7d119ddc188356e0` |
| compute.dxil | 2824 | `f3934fccef3b1428d62ed6a675540cab1bf674faaf85c67320e3ed83c6c62bb4` |
| compute.spv | 980 | `60a637dee089e454508547866ba4cd8ef703fce85c831abedfbabe40ceeefc12` |
| EXP1389-precompile.nir | 1342 | `1a75e3bafd02c6229f0e3eaf9cc34e0a72e06e42f7386d11c8e5878809ec3cb1` |
| EXP1389-compute.agx | 68 | `c269f1119bea13afc7355eb49958ba9074d11eb1a9766162e3fc02881af81025` |
| EXP1389-backend.log | 1810 | `db258ae4bed2101b9723e62b7490238780c7ad8d3b809c39839f00a9a95267d3` |

Exact Mesa host compile/link arguments are retained in
`EXP1389-nir-link-command.json`. Reproduce that host gate from the worktree:

```
AGX_LOCAL_TREE=$HOME/public_windows/.local PYTHONPATH=tests \
 python3 $HOME/public_windows/.local/c15-sm6-8434b063/build-nir-check.py
```

The existing generated DXIL is the input; no builder or device is needed to rerun
translation/NIR/backend checks. The original DXC compile receipt is `dxc-build.log`;
C API conversion and rejection receipts are `EXP1389-translate.log` and
`EXP1389-binding-negative.log`. `EXP1389-spirv-validation.json` records command,
environment, zero return code and diagnostics. Earlier link-failure evidence
remains `EXP1389-nir-initial.log`; no old result was reinterpreted as success.

### What remains before production

The largest unclosed contract is production logical-binding/descriptor/pointer
lowering and owner upload/lifetime. The symbolic parameter demonstrates compiler
acceptance while deliberately leaving that runtime mapping unimplemented. Then
require a real supported-feature/capability matrix, the correct pinned Windows
shader-creation/model ABI, Windows runtime ownership/CRT verification, and validated
SPIR-V/NIR lowering for more than this singleton raw-UAV case. This result supplies
no justification to advertise SM6 or execute an unreviewed production integration.
