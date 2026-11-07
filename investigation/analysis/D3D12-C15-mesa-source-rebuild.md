# One source identity for the native metadata rebuild

The isolated unsigned `shader_info` projection passed EXP1462 on J313: five
getters, 4569 C/C++ cross-TU checks, neighbor preservation and the measured raw
layout comparison against EXP1460. Production adoption requires fresh native
consumers of that header. The current GPU package remains the recovered EXP1439
baseline.

Three builders accept an explicit `--mesa-source` path:
`build-full-asahi.py`, `build-native-asahi-state.py` and
`build-asahi-runtime-closure.py`. Their existing source-copy and include paths
consume the selected root. The argument's default preserves the previous root
and compiler profiles. A complete pristine-derived source projection preserves
the accepted Windows platform bytes and applies the reviewed header projector;
its actual includes must identify one canonical projected header.

Override builds record `source-root.json` with canonical `mesa_source`, canonical
`mesa_build`, and `shader_info_sha256`. Compiler evidence requires the core build
receipt to match before it records its own identity. Runtime closure requires
explicit build/evidence arguments and matching core, native-preparation and
compiler-evidence receipts. This prevents old unqualified core evidence from
being labelled with a new source root. Actual compiler commands, include closure,
archive hashes and successful build results remain mandatory acceptance evidence.
The source identity receipt is one additional guard in that evidence chain.

The bounded primary census of the actual 860-entry Windows compile database
selects 338 core translation units: 237 NIR, 3 compiler, 91 util, 3 c11 and 4
BLAKE3. VTN adds 14 and SIMD adds 1, giving 353 fresh objects and seven fresh
archives. All recorded commands use LLVM20 clang-cl. Five selected core TUs use
C++; the actual C++ profile adds EHsc/C++17/permissive- and removes C11. Runtime
closure subsequently builds 44 Asahi compiler units and its selected additional
units. UMD/owner compilation uses MSVC14.44: the DDI has explicit C++17, while
the grouped owner command has TP/EHsc/MD with no explicit std option. Source and
dependency evidence determines which consumers access the complete record.

For this storage-only change, 19 recorded generator rules have 28 unchanged
primary inputs. Source IO inspection establishes that the changed header is not
a generation input. The rebuild preserves the exact 20 generated files as pinned
immutable inputs and freshly compiles their consumers. EXP1464 captured 363
additional primary/tool hashes and one missing extensionless Meson entry;
EXP1465 captured the last eight generated-output hashes. Generator execution is
outside this build path. A change to those rules, scripts or grammar/data requires
a new generation closure and invalidates this reuse decision.

EXP1463 tests exercise the actual source-selection assignments and receipt guards
against owned trees, including mixed core/native/compiler identities. Three tests
first produced five assertion failures, then passed. The shared projection test
loader now uses the public source-root argument; its original behavioral assertions
are retained. Full attempt1 preserved 23 caller-fixture failures. After that repair,
full attempt2 ran 1617 tests in 219.389 seconds with exactly the previous 117
failure entries, no additions/removals and unchanged implementation/test hashes.
The complete suite remains red. Its log is
`.local/c15-current-root-adoption1463/full-attempt2/full.log`, SHA-256
`d08d1277aa792c8d30b7eec03987c345a90a050fb1373b07381adedb434a133d`.

Full native rebuild, actual owner dependencies, serialized/cache identity and a
new signed-package GPU regression run remain pending. No architecture artifact is
declared unaffected by its filename, and no old archive is admitted solely because
an include path points at the projected header.


EXP1466 completed the selected core closure: 353 fresh ARM64 objects and seven
archives, 361 successful commands, 250 actual header consumers selecting the
canonical projection. Root checked every collected artifact hash, exact command
list, archive member counts and source identity. It accepted that build scope
only. The raw output manifest is `d5205c4cb5dcf1299512fc87f00e537bd3f1c0e475a41bec67d4be0cd8440067`;
the detailed review is `.local/c15-core353-build1466-result/root-review1466.json`.
The next-stage database pin must identify the actual Windows-written database:
6867713 bytes, SHA-256 `91dede240314809bf2f934225bf7089f40ee51e8fe33a756e08336944c71667e`.
Its 860 parsed records match the staged database exactly; native JSON writing
changes line endings. EXP1467's preflight refusal preserves the incorrect staged
pin. EXP1468's first actual compiler invocation then exposed missing generated
and platform header pins. The finite source-derived four-header read in EXP1469
closed those identities without weakening the dependency policy.

ARM64 adoption and retention of the accepted WOW stack have a concrete boundary.
The frozen owner translates DXBC into process-local NIR before calling the local
Gallium shader creation functions (`agx_d3d12_windows.cpp:273` and `:1112`).
`agx_win32_gpuva_batch.c:698` builds a native command BO, and
`umd_gpuva_windows.c:1354` submits native private data and GPU virtual ranges.
The scalar versioned envelope in `apple_agx_g4_submit.h:84` and its KMD parser
`gpuva_g3_windows.c:4128` carry native commands, addresses and attachment data.
They do not marshal a `shader_info` compiler object on the inspected path.
This permits conditional retention of the complete, internally consistent WOW
closure while rebuilding every selected ARM64 participant. Exact WOW hashes and
unchanged shared submission ABI remain package gates. No claim is made that the
WOW binaries receive the ARM64 storage correction.

Raw compiler serialization is a separate interface: Mesa `nir_serialize.c:2239`
writes a by-value `shader_info` using `sizeof(info)`, and `:2301` reads that raw
layout. The isolated raw snapshot comparison does not establish arbitrary cache
compatibility. No serialized NIR or compiler-cache blob may be exchanged between
these separately built closures without an explicit identity contract. Actual
new-runtime preprocessing must demonstrate that disk-cache store/init bodies are
empty and retrieval returns null, with the real compile flags and cleared
inherited CL options. Historical WOW cache policy remains a baseline condition,
not a new measurement. The bounded primary review is
`.local/c15-wow-wire-boundary-source-plan/plan.md`, SHA-256
`a431d2fa3caa293c2c55f9be5877e6d0c66c66bb7e5133e990d14e10606e8c69`.
