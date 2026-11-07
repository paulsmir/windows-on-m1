# C15 DXIL translator license and integration review

Review date:2026-10-18. Scope: evidence and integration boundaries for the
DXIL-to-SPIR-V option in `D3D12-C15-dxil-plan.md`; no external source imported,
no dependency installed, no implementation/build/hardware/capability change.
This advances the later C15 decision while the current checkpoint remains the
parent task's responsibility.

## Decision and exact candidate

The inspected core and linked dependency licenses permit a candidate integration
with this repository's MIT-licensed code, provided their individual conditions
and notices remain intact. **The distribution cannot be described simply as
MIT-only.** Bundled glslang builder files carry BSD3-style source/binary notice
conditions that the upstream top-level MIT summary does not replace. The
selected minimal build still includes a second MIT project, dxbc-spirv.

No dxil-spirv checkout/pin was found in the available `.local/reference`
collection. The immutable **review candidate**, not an adopted dependency lock,
is upstream `HansKristian-Work/dxil-spirv`
`8434b0639fb2a48ce13aac08a420ecce5d23df0e` (commit date2026-10-10T13:49:38Z).
Its CMake version and C API macros both say2.75.0. GitHub's commit/tree and raw
files were read at that exact revision; no moving branch is a reproducible
integration input. A different revision requires checking its actual graph and
license changes before import.

Primary sources:

- [candidate CMake](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/CMakeLists.txt),
  [gitlinks](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/.gitmodules),
  [third-party target graph](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/third_party/CMakeLists.txt).
- [core license](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/LICENSE.MIT),
  [public C header](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/dxil_spirv_c.h),
  [builtin LLVM replacement link graph](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/bc/CMakeLists.txt),
  [external LLVM selection](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/external/CMakeLists.txt).

## Dependency/license evidence

| Component and exact source | Evidence and obligations | Boundary |
|---|---|---|
|dxil-spirv core/API, candidate above|`LICENSE.MIT`, `dxil_spirv_c.h`, `llvm_bitcode_parser.hpp`, `bc/module.cpp` and `bc/module_dxbc_ir.cpp` carry MIT text/SPDX. Preserve copyright, permission and disclaimer.|Eligible UMD-side translation library.|
|Bundled `third_party/bc-decoder` at candidate|Inspected `llvm_bitreader.h` and `llvm_decoder.cpp`: MIT, Baldur Karlsson2019-2020. Root license includes this attribution.|Required by builtin `llvm-bc`; preserve its distinct attribution.|
|Bundled `third_party/glslang-spirv` at candidate|Inspected `SpvBuilder.h`, `spvIR.h`, `Logger.cpp`, `InReadableOrder.cpp`: BSD3-style terms with LunarG/Google and named nonendorsement clauses. Header variants differ; retain exact text. Root license also contains a Khronos MIT notice; retain it in addition.|Required core `spirv-module` dependency. Binary distribution needs corresponding notices in accompanying materials.|
|dxbc-spirv `8dae2d8573af08378c6059aca3d5987da99d20b5`|[LICENSE](https://github.com/doitsujin/dxbc-spirv/blob/8dae2d8573af08378c6059aca3d5987da99d20b5/LICENSE): MIT, Philip Rebohle2025.|Required by default builtin `llvm-bc`, which links `dxbc-spirv` even with CLI disabled. This does not require routing current SM4/5 shaders through it.|
|SPIRV-Headers `c63848ecf2200425511319fd8bf2c17b751e501e`|[LICENSE](https://github.com/KhronosGroup/SPIRV-Headers/blob/c63848ecf2200425511319fd8bf2c17b751e501e/LICENSE): permissive Khronos/MIT-style materials notice, including standards-modification wording. License identifies separate XML, JsonCpp, and CC-BY4.0 documentation/build-metadata categories.|Required headers; do not label an entire recursive source bundle MIT. Preserve exact applicable notices.|
|dxbc-spirv nested SPIRV-Headers `c8ad050fcb29e42a2f57d9f59e97488f465c436d`|Its git tree has this gitlink. [LICENSE](https://github.com/KhronosGroup/SPIRV-Headers/blob/c8ad050fcb29e42a2f57d9f59e97488f465c436d/LICENSE) has the same materials-license categories described above.|Include in recursive lock/inventory; establish whether its headers or parent headers supply each compiled include.|
|SPIRV-Tools `199cb207b911501ddd76dcddf100a6e21c15ef23`|[LICENSE](https://github.com/KhronosGroup/SPIRV-Tools/blob/199cb207b911501ddd76dcddf100a6e21c15ef23/LICENSE): Apache2.0.|Parent gitlink, configured by CLI path. Exclude from minimal runtime link; if shipped, retain Apache license, applicable notices, modification notices and patent terms.|
|SPIRV-Cross `be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a`|[LICENSE](https://github.com/KhronosGroup/SPIRV-Cross/blob/be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a/LICENSE): Apache2.0; repository also has a LICENSES directory.|CLI dependency; exclude from minimal runtime link. A source bundle containing it still needs its license inventory.|
|Mesa SPIR-V frontend and AGX compiler|Available pinned Mesa HEAD `9aa1215f878b504f66159dd2ead4c7973142126e`; inspected `src/compiler/spirv/spirv_to_nir.c` and `src/asahi/compiler/agx_compile.c` are SPDX MIT. Mesa `docs/license.rst` explains that individual files can differ.|Use existing Mesa ownership/notice policy. These two files do not license every new Mesa source or generated dependency automatically.|

These findings cover the named target graph and inspected license-bearing files;
this is not a claim that every source file, generated asset, test shader, tool,
or recursively vendored directory has been individually cleared for redistribution.
The final imported-file inventory must close that narrower implementation question.

Checksums of inspected remote byte contents:

| File | SHA-256 |
|---|---|
|candidate `LICENSE.MIT`|`b3ec61378a66f09835a73f53926d9b5cf79a09c9e2fe8bdd0e39aa5403325e1e`|
|candidate `dxil_spirv_c.h`|`4fbca14fe1de04b463cd8d8f6f449fc4e57490c5c900eaff9e6d6d65ea55303c`|
|candidate `third_party/glslang-spirv/SpvBuilder.h`|`2a83e287dcaaf465f4e143ee9f74b65920cbaa4a22290d98dde86a6103c9192c`|
|candidate `third_party/CMakeLists.txt`|`80067c584f59dd1b2e0520ffa8a84f1c9cbed23eb5c07e7c851ab9f85e86ea61`|
|dxbc-spirv pinned `LICENSE`|`0f8a5e687bea942a30a8e39b9915294fbe44eca866e71307a9b7083ee0e69ad8`|
|nested Headers pinned `LICENSE`|`ea43b1de38a6f90c488800d66dec1ed671e68cda530266bc96951fb5b6307613`|

## Permitted integration boundary

A suitable candidate boundary is the public C API in the user-mode compiler
owner, producing SPIR-V which the existing pinned Mesa frontend translates to
NIR. Keep the translator's C++ internals and allocator objects behind this API;
do not copy external implementation fragments into unrelated local code or put
this compiler in the KMD, broker, m1n1 or Mu. Either static or shared linking can
satisfy the inspected permissive terms; packaging must retain the actual linked
components' notices. Header-only integration still copies licensed material.

For the smallest graph, explicitly select `DXIL_SPIRV_CLI=OFF`,
`DXIL_SPIRV_NATIVE_LLVM=OFF`, `DXIL_SPV_MISC_CLI=OFF` and build only the selected
C API library/dependencies. The builtin LLVM API replacement avoids a full LLVM
runtime, but does **not** eliminate dxbc-spirv. Do not silently enable native LLVM:
`checkout_llvm.sh` names only abbreviated revision `2c4ca6832fa6b306ee6a` of
llvm-mirror/llvm. Full pin, its license/exceptions/dependencies and a new build
review are required for that alternate graph. DXC used to generate our own test
fixtures is another separately inventoried tool; this review does not authorize
redistributing an arbitrary DXC/validator binary.

Keep upstream file headers unchanged, carry complete component license texts
and all needed binary-distribution notices in the driver package's third-party
materials, and record modifications/provenance. Do not replace per-file BSD or
Khronos terms with this repository's MIT heading. Do not copy vkd3d-proton glue
just because it consumes the same MIT translator; any such code has its own
license review. Likewise, do not import private game/repro shader collections.

## Compatibility still to prove before C15 integration

1. Adopt a full source/dependency lock and an exact compiled-file inventory,
   including which SPIRV-Headers revision supplies each include. Review remaining
   imported per-file notices and generated content against this inventory; ensure
   package assembly actually retains the texts. This review is conditional
   source-graph eligibility, not a completed redistribution manifest.
2. Prove the pinned MSVC/ARM64 C API library build, runtime dependencies, symbol
   isolation and allocator lifetime. Upstream CMake has an MSVC path; that is
   source evidence, not a successful project build. Default graph includes
   dxbc-spirv C++17 targets although the top level sets C++14.
3. Design an explicit D3D12 logical-binding-to-SPIR-V-descriptor-to-NIR-slot map.
   The C API provides CBV/SRV/UAV/sampler remappers, root constant/descriptor
   mapping and feature queries, but a Vulkan-facing descriptor model is not a
   ready-made Gallium/AGX binding contract. Preserve current DXBC behavior.
4. Establish parsed-blob/converter/returned-SPIR-V lifetime and thread-allocator
   begin/end/reset ordering from the exact implementation; copy or consume
   borrowed output before its owner is freed. No pointer from the translator may
   outlive that owner merely because the API is C-compatible.
5. Set the actual SPIR-V capabilities/extensions/environment admitted by pinned
   Mesa and Asahi, and deterministically refuse unsupported DXIL features.
   License compatibility does not establish SM6 conformance, wave semantics,
   arithmetic precision, atomics, barriers, residency or binding correctness.
6. Revisit the C15 plan's cap/DDI prerequisite against current pinned WDK and
   official Microsoft contracts before advertising any shader model. Do not use
   an SM6 cap to discover a documented ABI, and do not advertise an incomplete
   model simply because one compute translation succeeds. No cap is changed by
   this review.

The next useful C15 work is an offline adapter design and minimal source/build
inventory after current texture/checkpoint work is complete. A license review
alone supplies no hardware hypothesis and requires no experimental package.
