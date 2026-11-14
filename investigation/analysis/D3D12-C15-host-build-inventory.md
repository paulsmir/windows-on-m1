# C15 isolated compiler-observed host inventory

2026-10-18. Extends license review `2673e5f6df9d2d3f77e083da2c830f72e44a5a6f`
and runtime inventory `4587433f94128d35db51fdce63094290e78b3eba`. No source import,
production link, model admission, remote build, driver package or hardware run.
This is RENDER_ONLY user-mode compiler preparation, with no KMD/firmware ownership.

## Exact source and license gate

Candidate D remains dxil-spirv `8434b0639fb2a48ce13aac08a420ecce5d23df0e`, B remains
dxbc-spirv `8dae2d8573af08378c6059aca3d5987da99d20b5`, and H remains SPIRV-Headers
`c63848ecf2200425511319fd8bf2c17b751e501e`. The prior review's local archives at
`/tmp/c15-runtime-license-8434b063` survived. Before configuring, every one of the
187 reviewed source/header hashes and all 10 build/provenance/license hashes were
recomputed and matched. No source/graph discrepancy was found; no revision changed.
The complete per-file license texts and N01–N14 notice groups remain in
`D3D12-C15-runtime-license-inventory.md`; this build does not replace that review.

An isolated copy under `$HOME/public_windows/.local/c15-host-8434b063/source`
restored B into `subprojects/dxbc-spirv` and H into `third_party/spirv-headers`.
File contents remained unchanged. Nested HN, SPIRV-Tools and SPIRV-Cross were not
assembled into the selected graph. Full upstream archives contain unused files;
this is not an approved whole-repository redistribution inventory.

## Host configure/build observation

Tool: isolated PyPI CMake 3.31.6, Unix Makefiles; AppleClang 21.0.0
`clang-2100.3.34.2`, arm64-apple-darwin27.0.0, MacOSX27.0 SDK. Source/build/tooling
remain in `.local`; only this note is tracked. CMake command:

```
cmake -S source -B build -G 'Unix Makefiles' \
 -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
 -DDXIL_SPIRV_CLI=OFF -DDXIL_SPIRV_NATIVE_LLVM=OFF -DDXIL_SPV_MISC_CLI=OFF \
 -DSPIRV_HEADERS_ENABLE_TESTS=OFF -DSPIRV_HEADERS_ENABLE_INSTALL=OFF
cmake --build build --target dxil-spirv-c-static --parallel 4
```

Before compilation, CMake file-api codemodel-v2 lists for the nine selected targets
were compared with the reviewed inventory: exactly 83 TUs, no additions/deletions.
Build passed. It built the reviewed nine archives only; declared unused test/shared
targets were not compiled. Actual dependency `.o.d` output from those 83 objects
contains **186 project files: 83 TUs and 103 headers**, each still matching its
reviewed SHA-256. The only reviewed but unobserved entry is IDE-only
`dxbc-spirv/util/util_vle.h`, exactly as predicted. The observed graph uses parent H;
no nested HN headers, external LLVM, Catch, Android, Tools or Cross enters it.

The same dependency output names **838 toolchain/SDK inputs**, including C++ libc++
headers, Apple SDK headers, compiler resource headers and SDK settings metadata.
These are host-specific compile inputs, not new permissively licensed runtime
sources or a Windows SDK inventory. Their exact paths/hashes are in the local
manifest. The observed sources exercise all N01–N14 notice groups; N01 has 91
observed files instead of the prior 92 including the unused IDE header. Existing
notice aggregation therefore remains sufficient for this host project subset.

## C ABI/link/runtime observation

An original C99 consumer includes only public `dxil_spirv_c.h` plus C standard
headers, compiles with `clang`, and links with `clang++` against all nine archives.
It calls version query, allocator begin/reset/end and malformed-container parser
refusal. Observed result:

```
C15_CAPI_HOST_PASS version=2.75.0 invalid_result=-4
```

This is a C ABI and lifetime-sequence smoke test, not successful DXIL translation.
No valid DXIL shader, SPIR-V validation, Mesa lowering or SM6 conformance is claimed.

Important static packaging result: **`libdxil-spirv-c-static.a` contains only
`dxil_spirv_c.cpp.o`**, not its dependency archives. A manual static consumer needs:

1. `libdxil-spirv-c-static.a`
2. `libdxil-converter.a`
3. `libspirv-module.a`
4. `bc/libllvm-bc.a`
5. `third_party/libglslang-spirv-builder.a`
6. `third_party/libbc-decoder.a`
7. `libdxil-utils.a`
8. `libdxil-debug.a`
9. `third_party/libdxbc-spirv.a`

CMake propagates static private link dependencies in its target graph; distributing
only the wrapper archive would lose them. Upstream `install()` exports the **shared**
library, shared pkg-config and public header, not a complete static package. The
candidate must collect explicit static outputs; do not use an unrestricted upstream
install/default-all target as a minimal-runtime packaging step.

The host link map extracts 79 of the 83 library objects. Logger.cpp,
dxbc_interface.cpp, ir_disasm.cpp and ir_pass_lower_io.cpp are unextracted in this
smoke executable, but remain built archive members and stay in source/notice scope.
`otool -L` reports only `/usr/lib/libc++.1.dylib` and `/usr/lib/libSystem.B.dylib`.
The executable imports allocation, TLS, guard and C++ exception/unwind services
(including `__cxa_throw` and `_Unwind_Resume`). Do not infer a C-only runtime or
exception-free final artifact from the C API or selected core `-fno-exceptions`
flags; dxbc-spirv has its own flags. No MSVC CRT result follows from this host link.

## Evidence and artifacts

Local durable preparation directory:
`$HOME/public_windows/.local/c15-host-8434b063`.

- `build/.cmake/api/v1/reply`: configured target/source manifests.
- `build/compile_commands.json`, per-object `.o.d`: compiler-observed inputs.
- `capi-smoke.c`, `.o`, executable, `.map`, `link-command.json`: exact C caller/link.
- `observed-inventory.json`: full project/toolchain path+hash inventory, archive and
  executable hashes/sizes, actual extracted archive members.
- Logs `/tmp/c15-configure.log`, `/tmp/c15-build.log`; verification scripts
  `/tmp/c15-verify-config.py`, `/tmp/c15-observed.py`.

Manifest SHA-256:
`93ac2f925e03b24a902056f088041970aac00301c6c1fff11386a380c86e0b22`.
Wrapper archive: 111480 bytes, SHA-256
`bf105d3d1862bffc861bcf8375a843c1b99ba1c9116246e1156cf11cbb03b711`.
Smoke executable: 3502552 bytes, SHA-256
`a536a9034330b08ff918449dfd11acff0b85d419ce14522ae0316c8b4b26667a`.
These are host artifacts; hashes do not imply cross-host reproducible builds or a
redistributable driver package. All nine archive hashes are in the manifest.

## Strongest blocker and next gate

The strongest remaining build blocker is the exact Windows ARM64/MSVC artifact:
CRT choice, C/C++ ABI, compiler-observed Windows headers, archive dependency order,
PE machine/imports and package notices. A host build closes none of those Windows
claims. Prepare one isolated build-only candidate under an experiment-specific
builder directory; do not link it into production or stage a driver. Parent owns
BUILD_ONLY BEFORE, experiment ID, ledger and authorization to execute that build.
The later functional blockers remain binding remap, SPIR-V environment/NIR support,
allocator/output lifetime and truthful Microsoft shader-model admission.

## Prepared Windows ARM64 build-only candidate (not executed)

Parent reserved name `EXP1386-dxil-runtime-build-only`; parent must write BUILD_ONLY
BEFORE before execution. Isolated inputs are under
`$HOME/public_windows/.local/c15-win-arm64-1386/`, with source staging/build
root fixed to `C:\Users\pauls\EXP1386-dxil-runtime-build-only`. No shared production
directory is referenced. The script refuses existing source/build/out directories.

Nonmutating SSH queries found `C:\VS2022Community\VC\Tools\MSVC\14.44.35207`,
cl FileVersion `19.44.35228.0`. The presumed VS-bundled CMake path does not exist;
a separate CMake 3.31.6 win_amd64 wheel was downloaded locally, not installed on
the builder. Its executable is pinned independently. CMake-tool licensing remains
with that tool archive; it is excluded from any proposed driver artifact.

| Input | SHA-256 |
| --- | --- |
| `EXP1386-dxil-runtime-build-only.zip` | `448e71882ffdf9f7087950cbce17a1a7bb0e442b4145bd4d1191239d186b5f78` |
| `source.tar.gz` | `8b3f1f85c61b3b573d11165b50879d7c4228b77a311d65ce7b99ff0579394261` |
| `reviewed-source.json` | `c2f64cff93b3aa77d8977994d55cb595ef6d2a5d46d1c168fed3bbc1bbb91c23` |
| `build-c15-arm64.ps1` | `cf35bf75de301da43972a3b777618ef1d020fe22fbde113251bfde2bb22ac17c` |
| `cmake-tool.zip` (original wheel bytes) | `bbaed969cef3c427f4f17591feb28db4ae595e3a4bbd45cb35522cee14df6a32` |
| extracted `cmake.exe` | `3fe22eb02e1c6184ec207366ed21a6f2f9c3828c1e9f3324546befadfb362be3` |
| `THIRD-PARTY-NOTICES.txt` | `f2fbe8bc1c8eb9bd544198cf53ecbf74dd37f2b765f2f4b074d0cd6cc18c893f` |

`reviewed-source.json` binds 197 reviewed file hashes (187 source/header plus 10
metadata/notice inputs) and the exact 83 TU/nine-target lists. `input-hashes.json`
also covers the original C smoke caller. Source archive contains the private full
upstream staging tree, including excluded files needed for unchanged CMake target
declarations; it is not a licensed final source-distribution subset. Build gates
allow only the reviewed target graph/compiler-observed project inputs.

The prepared script uses Hostx64/arm64 MSVC, SDK10.0.26100.0, NMake, cross target
Windows/ARM64 and explicit `MultiThreadedDLL` (/MD), not production defaults.
It verifies cl/link/lib/dumpbin SHA-256 pins from the read-only builder query:

| Tool | SHA-256 |
| --- | --- |
| cl.exe | `603e49cb5bb11dc60929d8ebd892a195dd24172744238858fe79ab2a1a6a9e17` |
| link.exe | `984ba7990bcecac78fe28a20a748077a1337c3d3cdd7d84e09501ef722b5a619` |
| lib.exe | `8a687fa0af784302109f1a217ef4ce71c16acb36ae2ccc91e186e11bd622b869` |
| dumpbin.exe | `06ccefd7211c7a1c13d240efe96ef61448181f2a2c8d85faf6891b7cc7ca09aa` |

Intended gates: configured TU equality before selected compilation; actual MSVC
/sourceDependencies project paths/hashes; nine archives only; C11 caller compile;
explicit all-nine static link; ARM64 PE machine; no unexpected translator DLL;
/MD directives and no static/debug CRT directives; import/link-map/dependency/tool
and output-hash evidence. The ARM64 executable is **never executed** by this
builder script. These are prepared gates, not passing Windows results. The
PowerShell script has not been run or parsed on the builder; any build-tool failure
must be reported from that build-only candidate before modifying the selected graph.

After parent records BEFORE and verifies the outer zip hash, transfer/extract it
into the fixed root and expand `cmake-tool.zip` into `cmake-tool`. Ready invocation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-c15-arm64.ps1 `
 -ExpectedSourceSHA256 8b3f1f85c61b3b573d11165b50879d7c4228b77a311d65ce7b99ff0579394261 `
 -ExpectedManifestSHA256 c2f64cff93b3aa77d8977994d55cb595ef6d2a5d46d1c168fed3bbc1bbb91c23
```

Collect `out/`, `artifact-hashes.json`, all nine archives, manifests and notices;
do not install, link into the UMD or execute on GPU hardware. A successful Windows
build would still leave runtime allocator/C ABI smoke, valid-DXIL conversion,
SPIR-V/NIR/AGX compatibility and model admission unproven.

### EXP1386 configure stop and prepared EXP1388 retry

Parent recorded EXP1386 BUILD_ONLY BEFORE `45182574` and executed the isolated
script. Configure succeeded, then the graph gate stopped **before compilation**:
its filename wildcard `target-dxbc-spirv-*.json` matched both dxbc-spirv and
unused dxbc-spirv-test. Read-only builder inspection confirmed their JSON names
and source counts (88 and 32 source/header entries). This was a gate selection
bug, not evidence of runtime graph drift. A separate exact-JSON-name comparison
of all nine configured targets matched the reviewed 83 TUs. Configured flags
retain /MD everywhere, C++14 for core/builder/decoder, C++17 for llvm-bc and
 dxbc-spirv, and upstream MSVC flags. Captured evidence:
`/tmp/c15-1386-configured-targets.json`; parent owns ACTUAL/ledger interpretation.

Prepared retry root is `C:\Users\pauls\EXP1388-dxil-runtime-build-only`.
Local bundle `$HOME/public_windows/.local/c15-win-arm64-1388/EXP1388-dxil-runtime-build-only.zip`
SHA-256 `0c416a3b0601ba6255e4dda639c17d9a3ff4e0032395b8c729cdabc29a641be3`.
Corrected script SHA-256
`49bb9cf29fd68affe8f35150642ed3fe91ec0f5a1e1a47a44db11c018ca2b462`.
Source archive, reviewed manifest, tools and all remaining inputs retain their
EXP1386 hashes. The only script differences are the fresh isolated root and
exact JSON.name identity selection, retaining the same TU/hash gates. No flags,
selected targets or source changed; failed EXP1386 remains untouched. The retry
is prepared locally only; parent must record continuation BEFORE before execution.
