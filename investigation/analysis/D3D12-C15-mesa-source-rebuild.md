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
