# C15 DXIL integration: consolidated license and provenance decision

Review date: 2026-10-18. **YES, the selected pinned DXIL-to-SPIR-V runtime may be
integrated into the MIT Windows UMD, with the conditions below.** It is a mixed
permissive-license component, not an MIT-only source bundle. **NO** to an
unrestricted recursive source import, silently enabled optional/native LLVM graph,
or SM6 capability advertisement based on license/build/compiler success alone.

This is the requested consolidated review. It reuses the actual file/hash/notice
work in [the first review](D3D12-C15-license-review.md) (`2673e5f6`) and
[the 187-file runtime inventory](D3D12-C15-runtime-license-inventory.md)
(`4587433f`), then closes missing top-level/optional dependency and project-license
boundaries. No external implementation code is copied into this repository by
this review. No builds or hardware runs were added for this consolidation.

## 1. Exact candidate and primary MIT evidence

Selected upstream is [HansKristian-Work/dxil-spirv commit
8434b0639fb2a48ce13aac08a420ecce5d23df0e](https://github.com/HansKristian-Work/dxil-spirv/tree/8434b0639fb2a48ce13aac08a420ecce5d23df0e),
C API2.75.0. Its [LICENSE.MIT](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/LICENSE.MIT)
identifies Hans-Kristian Arntzen/Valve and grants MIT permissions; SHA-256
`b3ec61378a66f09835a73f53926d9b5cf79a09c9e2fe8bdd0e39aa5403325e1e`.
Its public [C API header](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/dxil_spirv_c.h)
hashes to `4fbca14fe1de04b463cd8d8f6f449fc4e57490c5c900eaff9e6d6d65ea55303c`.
The immutable commit and per-file notices govern; a README summary or moving branch
is insufficient. Bundled glslang builder files retain their BSD3-style notices.

Selected build: `dxil-spirv-c-static`, CLI/native LLVM/misc CLI disabled,
SPIRV-Headers tests/install disabled; build only that target. The source-declared
and compiler-observed graph is nine archives /83 TUs /103 reached project headers.
The reviewed union has187 files because one additional IDE header is not reached.
All187 source/header and10 metadata/license hashes matched before the host build;
Windows configured/compiled83 TUs as well. The linked dependency graph includes
dxbc-spirv despite selecting DXIL entry points and disabling CLI.

## 2. All declared third-party, submodule and optional dependency boundaries

Primary graph inputs are the pinned
[.gitmodules](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/.gitmodules),
[third_party/CMakeLists.txt](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/third_party/CMakeLists.txt),
[bc/CMakeLists.txt](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/bc/CMakeLists.txt)
and [external/CMakeLists.txt](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/external/CMakeLists.txt).
Four root gitlinks exist; nested B Headers is recorded separately. Optional graph
coverage below is a classification/containment review, not permission to import
all optional repositories or their test data without an exact file inventory.

| Component / exact pin | License and primary evidence | Selected runtime? / obligation |
| --- | --- | --- |
| dxil-spirv core, C API, builtin `bc/*`, debug/util; D above | MIT, per-file variants inventoried N02–N07 | **Yes**. Preserve each attribution and complete permission/disclaimer text. Builtin LLVM-compatible parser is this MIT code, not full LLVM. |
| Bundled `third_party/bc-decoder`, same D | MIT, Baldur Karlsson2019–2020 in llvm_bitreader.h/llvm_decoder.*; N08 | **Yes**. Distinct parser attribution remains. |
| Bundled `third_party/glslang-spirv`, same D | Six runtime source/header notices include BSD3-style LunarG/Google/named nonendorsement variants, plus Khronos terms; N09–N13 | **Yes**. Preserve actual file texts; top-level MIT does not supersede BSD source/binary notice conditions. |
| Bundled `third_party/cli_parser`, same D | cli_parser.cpp/.hpp have MIT Hans-Kristian Arntzen2017–2019; marked reused from Granite | **No**, CLI off. If enabled, add these notices and the CLI link graph; do not infer Granite as an additional linked runtime. |
| [dxbc-spirv B](https://github.com/doitsujin/dxbc-spirv/tree/8dae2d8573af08378c6059aca3d5987da99d20b5) `8dae2d8573af08378c6059aca3d5987da99d20b5` | [MIT LICENSE](https://github.com/doitsujin/dxbc-spirv/blob/8dae2d8573af08378c6059aca3d5987da99d20b5/LICENSE), Philip Rebohle2025; N01 | **Yes**, selected dxbc/ir/util subset40 TUs. Does not replace current local DXBC shader path. |
| [SPIRV-Headers H](https://github.com/KhronosGroup/SPIRV-Headers/tree/c63848ecf2200425511319fd8bf2c17b751e501e) `c63848ecf2200425511319fd8bf2c17b751e501e` | [LICENSE](https://github.com/KhronosGroup/SPIRV-Headers/blob/c63848ecf2200425511319fd8bf2c17b751e501e/LICENSE): Khronos/MIT-style materials with standards-modification wording; separate XML terms, JsonCpp PD/MIT and CC-BY4.0 metadata/docs categories | **Yes**, the two checked-in generated runtime headers through parent H; N14 plus applicable materials text. No buildHeaders/JsonCpp/doc generator is used. Keep full applicable license; whole-repository import broadens scope. |
| B nested Headers HN `c8ad050fcb29e42a2f57d9f59e97488f465c436d` | [LICENSE](https://github.com/KhronosGroup/SPIRV-Headers/blob/c8ad050fcb29e42a2f57d9f59e97488f465c436d/LICENSE), same categories | **No** in selected CMake/observed include graph. Lock the gitlink; do not substitute it for parent H. |
| [SPIRV-Tools T](https://github.com/KhronosGroup/SPIRV-Tools/tree/199cb207b911501ddd76dcddf100a6e21c15ef23) `199cb207b911501ddd76dcddf100a6e21c15ef23` | [Apache2.0 LICENSE](https://github.com/KhronosGroup/SPIRV-Tools/blob/199cb207b911501ddd76dcddf100a6e21c15ef23/LICENSE) | **No** in translator runtime; CLI/optional tooling. Enabling adds Apache license/notice/modification/patent obligations and its actual dependency graph below. |
| [SPIRV-Cross X](https://github.com/KhronosGroup/SPIRV-Cross/tree/be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a) `be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a` | [Apache2.0 LICENSE](https://github.com/KhronosGroup/SPIRV-Cross/blob/be71ee8c12cd7dc5ca8fa9581f708c2e8561fe2a/LICENSE); LICENSES also contains MIT, KhronosFreeUse and CC-BY4.0 categories | **No**, CLI/misc tooling. A full Cross/shader/reference bundle needs its own per-file mapping; root Apache alone does not license every asset uniformly. |
| Full native LLVM, script pin resolved below | Apache2.0+LLVM exceptions, legacy NCSA text, separately identified third-party code | **No**, native LLVM off. Not cleared as an alternative complete runtime graph. |
| Catch header referenced by bc-decoder `#if ENABLE_UNIT_TESTS` | `3rdparty/catch/catch.hpp` is absent/unpinned in this D archive; its exact license is not established here | **No**, macro disabled. Enabling this branch is blocked pending exact source/pin/license review; do not assign a license by the Catch name alone. |
| D test/misc/sandbox/shader collections, B tests | Not runtime sources; own upstream/test asset notices may differ | **No**. Do not include arbitrary shader corpora in the package or treat these as approved by187-file runtime review. |

**No GPL/LGPL or source-release copyleft condition was found in the selected187-file
runtime inventory.** This statement does not classify every optional repository,
build tool, m1n1 third-party file, Mesa component or full source archive as GPL-free.
The selected runtime is permissively licensed; it is not exclusively MIT.

### SPIRV-Tools optional declared dependencies

Pinned T's [DEPS](https://github.com/KhronosGroup/SPIRV-Tools/blob/199cb207b911501ddd76dcddf100a6e21c15ef23/DEPS)
and [external CMake](https://github.com/KhronosGroup/SPIRV-Tools/blob/199cb207b911501ddd76dcddf100a6e21c15ef23/external/CMakeLists.txt)
identify these additional inputs. They are **all outside the selected translator
runtime**, not hidden dependencies of the nine archives:

| Dependency declared by T | Pin / license inspected at that input | CMake role |
| --- | --- | --- |
| Abseil | `30bba84041ba0aadd2c31b52742b8157db047a2f`, [Apache2.0](https://github.com/abseil/abseil-cpp/blob/30bba84041ba0aadd2c31b52742b8157db047a2f/LICENSE) | RE2/Effcee testing path |
| Effcee | `ae38e040cbb7e83efa8bfbb4967e5b8c8c89b55a`, [Apache2.0](https://github.com/google/effcee/blob/ae38e040cbb7e83efa8bfbb4967e5b8c8c89b55a/LICENSE) | Tests |
| GoogleTest | `d72f9c8aea6817cdf1ca0ac10887f328de7f3da2`, [BSD3](https://github.com/google/googletest/blob/d72f9c8aea6817cdf1ca0ac10887f328de7f3da2/LICENSE) | Tests |
| RE2 | `972a15cedd008d846f1a39b2e88ce48d7f166cbd`, [BSD3](https://github.com/google/re2/blob/972a15cedd008d846f1a39b2e88ce48d7f166cbd/LICENSE) | Effcee testing path |
| Protobuf | source-declared tag `v21.12`, [BSD3 LICENSE](https://github.com/protocolbuffers/protobuf/blob/v21.12/LICENSE) | SPIRV_BUILD_FUZZER path. A tag is not an adopted immutable full lock; resolve/review before enabling. |
| Additional SPIRV-Headers | `8c5559c134abcf432ec59db842404087b9906c1a`, [materials/JsonCpp/CC-BY categories](https://github.com/KhronosGroup/SPIRV-Headers/blob/8c5559c134abcf432ec59db842404087b9906c1a/LICENSE) | T fallback Headers. D's configured parent-H override and actual includes must determine provenance if T is enabled. |
| mimalloc | `fef6b0dd70f9d7fa0750b0d0b9fbb471203b94cd`, [MIT](https://github.com/microsoft/mimalloc/blob/fef6b0dd70f9d7fa0750b0d0b9fbb471203b94cd/LICENSE) | Optional allocator; T has MSVC-dependent defaults, static-build option separate. Never rely on a generic Tools-off assumption after enabling CLI. |

This is the complete named DEPS list, not an exhaustive imported-file/notice
clearance for optional recursive builds. Such a build must inventory its actual
configured closure, including those projects' own optional/test assets.

### Native LLVM is separately contained

D's checkout_llvm.sh abbreviation `2c4ca6832fa6b306ee6a` resolves through the primary
[commit record](https://github.com/llvm-mirror/llvm/commit/2c4ca6832fa6b306ee6a7010bfb80a3f2596f824)
to **`2c4ca6832fa6b306ee6a7010bfb80a3f2596f824`**. Its
[LICENSE.TXT](https://github.com/llvm-mirror/llvm/blob/2c4ca6832fa6b306ee6a7010bfb80a3f2596f824/LICENSE.TXT)
contains Apache2.0 with LLVM exceptions, legacy Illinois/NCSA terms and explicit
third-party per-directory/per-file boundaries; SHA-256
`8d85c1057d742e597985c7d4e6320b015a9139385cff4cbae06ffc0ebe89afee`.
Resolving this full SHA closes the abbreviation ambiguity; it does **not** approve
native LLVM integration. D selects Core/IRReader/Support and disables many optional
LLVM facilities, but pinned Support CMake can still select zlib and platform/system
libraries. Exact configured sources, third-party notices, generated files and
Windows link/CRT closure would require a separate review. Keep
DXIL_SPIRV_NATIVE_LLVM=OFF; the selected MIT builtin parser avoids this graph.

Additional primary-source notice/metadata receipts are outside the repository at
`$HOME/public_windows/.local/c15-optional-license-review/manifest.json`, SHA-256
`9a96134aa03bec0467fe7750dcd196d1e3e8b514d5f6a3fc0202b3cbd7527086`.
Only missing license/build metadata was inspected; no optional code was built/imported.

## 3. Compatibility with this repository and adjacent projects

| Existing component / inspected evidence | Compatibility conclusion |
| --- | --- |
| Repository `LICENSE`: MIT, copyright2026 paulsmir | Selected permissive runtime can coexist/link in UMD while retaining its own notices. Do not relicense upstream files as this repository's MIT copyright. |
| `m1n1_windows/LICENSE`: MIT, Asahi contributors | No conflict with separate MIT compiler/UMD component. m1n1 contains separately licensed third-party files (including a GPL license file); no blanket whole-tree relicensing or code transfer is authorized. Translator stays user-mode, not m1n1. |
| Pinned Mesa docs/license.rst and actual SPIR-V/NIR/AGX file notices | Mesa core/inspected frontend/compiler files are MIT; per-file licenses govern the real graph. Translator and Mesa may link with their notices intact. Host Tools dylib adds Apache2.0 only to that host checkpoint; do not infer a Windows runtime dependency from it. |
| `mu/LICENSE`: BSD2; `mu/Common/MU/LICENSE.txt` and `mu/MU_BASECORE/License.txt`: BSD-2-Clause-Patent | Permissive notice conditions and the BSD patent grant can coexist with MIT/BSD3 components. Retain all applicable conditions/patent text; do not claim an MIT license grants equivalent patent rights. No translator is proposed inside firmware; a combined firmware/import graph would need its own review. |

Root source hashes: repository LICENSE
`ecfc6775a35443afa9564f373567a8d994e9420b15d93ead785e5501f4ddedeb`;
m1n1 LICENSE `11ca423a564b52d3b7f260398baa20d016c25ae180fbdd0067c27b7d1ed7b420`;
Mu root `1771806741b29aff99b7972131ce7ae0c1ed4ccff7ec1d8d85d62eb557ee23e3`;
MU Common license `d8f295a76b57091fd68b6df2ff718ebc6de6fadf247f2d431878561b1a64fcd5`;
MU_BASECORE license `50ce20c9cfdb0e19ee34fe0a51fc0afe961f743697b068359ab2f862b494df80`.

## 4. Signed driver package and attribution conditions

Retain upstream headers unchanged and distribute full applicable copyright,
permission, conditions and disclaimer texts. The proposed package must include
`THIRD-PARTY-NOTICES.txt` plus complete component licenses, with an index mapping
component/version/pin and file groups to the texts. Reuse runtime inventory N01–N14;
in particular retain **every glslang BSD3 notice variant**, nonendorsement wording,
Khronos materials terms and the distinct MIT author/date variants. Source bundles
also retain raw per-file headers and any modifications/provenance notices.

Driver signing authenticates bytes; it does not remove notice requirements or
license an SDK/CRT redistributable. Put notices/licenses into the actual package
assembly and artifact manifest before catalog/sign/hash gates. If a package file
changes later, regenerate applicable manifest/catalog/signature; do not append
unmanifested material to an already pinned experimental package. Confirm the
recipient receives the texts alongside the signed driver materials. No credits UI
or runtime prompt is required by the inspected selected licenses.

Do not add assistant attribution, conversation/session URLs or Co-Authored-By
trailers. Preserve upstream human/company copyrights and the project's required
provenance; removing them is not a substitute for avoiding assistant attribution.
DXC/validator, MSVC CRT/SDK and CMake build-tool redistribution are not authorized
by this translator review. EXP1388 /MD PE import evidence includes MSVCP140,
VCRUNTIME140 and UCRT dependencies: the final deployment must satisfy their actual
Microsoft redistribution/deployment conditions. Use licensed existing tools;
distribute them only after a separate applicable license/redist-rights check.
/MD import success is not that check. Tools/native LLVM Apache paths additionally require their license, applicable
NOTICE content and prominent change notices when modified; not selected by default.

## 5. Reproducible external fetch, SHA checks and Windows toolchain

Use an external/ignored reference checkout with immutable D; no current vendoring
occurs. Before a future approved import/build:

1. Fetch/checkout D at its full SHA detached; verify HEAD and clean diff. Read
   D's git tree to verify all four gitlink object IDs against the table. Fetch
   only parent H and B required by the selected target; verify their HEADs.
   Record B's nested HN gitlink without initializing it into a second header
   search path. Do not use an uncontrolled recursive update or moving branch.
2. Verify the187 source/header hashes and10 metadata/license hashes against the
   runtime inventory. Keep parent-H include provenance fixed. Any changed source,
   generated header, target or dependency is a new graph/license/build review.
3. Configure explicit CLI/native-LLVM/misc-CLI OFF and H tests/install OFF; build
   **dxil-spirv-c-static only**. Compare CMake JSON target identity, exact83 TUs,
   actual compiler dependency closure and archive outputs with the lock. Do not
   select targets by prefix wildcards (EXP1386 caught a test-target collision).
4. Use the same pinned Windows ARM64 toolchain already observed: MSVC toolset
   14.44.35207, cl FileVersion19.44.35228.0; SDK10.0.26100.0; CMake3.31.6; explicit
   /MD Release policy. Preserve cl/link/lib/dumpbin/tool/source/archive hashes,
   PE machine/imports, C API caller link and CRT directives. The exact four tool
   hashes and preparation commands are in D3D12-C15-host-build-inventory.md.
5. Preserve **all nine archives**. The C API static archive contains only its
   wrapper object, not the dependency objects. Upstream install exports shared
   artifacts, so a default all/install step is not the selected static package.
6. Capture final selected source/header/notice/artifact manifests; include licenses
   before signing and retain a deterministic source-to-package provenance index.

## 6. Evidence, limits and yes/no conditions

Existing evidence is deliberately separated:

- Host C API build/link/inventory: exact reviewed83 TUs; API2.75.0 and malformed
  input refusal. Windows EXP1388: parent-reported ARM64 BUILD_ONLY PASS, nine
  archives/C caller/PE and /MD; **no ARM64 runtime execution**.
- EXP1389: original cs_6_0 fixture, reviewed host C API, valid SPIR-V1.3, actual
  Mesa indexed NIR, CPU dataflow oracle and actual AGX backend68-byte binary.
  Its SSBO0_BASE parameter is **compiler-only symbolic ABI**, not production
  root binding, owner uploads or GPU execution. Manifest SHA-256
  `4a0b5e6a669e1fab49e5a5593501411d402e7ef89f484dc86c1295e23f61144c`.
- None of these results proves SM6 model conformance, truthful capabilities,
  Windows runtime ownership/threading, actual GPU results or a signed package
  containing the notices. Detailed receipts remain in the host/SM6 notes.

**YES** to integrating the selected pinned runtime into the user-mode compiler
through its C API, retaining notices, immutable dependency/include graph, Windows
runtime/packaging checks and the staged plan. No copyleft obstacle was identified
in that selected graph. **NO** to claiming the package is MIT-only, importing all
optional/test/native LLVM sources under this review, shipping unlicensed build
binaries, changing source pins silently or advertising SM6 on the compiler result.

Unresolved constraints are explicit: native LLVM's complete configured closure;
unpinned Catch/test branch; optional Tools/Cross full per-file bundle clearance;
protobuf tag-to-full-lock if that optional path is enabled; final Windows runtime
and CRT redistribution rights; signed package notice delivery; and production
binding/feature/capability correctness. These do not block the selected minimal
runtime design, but each blocks the corresponding broader use until resolved.
