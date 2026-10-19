# C15 native frontend: selected graph and current build boundary

Updated 2026-10-19 UTC. Classification: RENDER_ONLY compiler preparation.
The Windows ARM64 frontend is **not yet accepted as built**. No output PE from
EXP1445–1448 executed, no package was installed, and no SM6 capability changed.

## Selected primary build contract

The actual package build consumes the builder's `AD04-fullcompiler-001/nir-arm64-ndebug`
cache. Its compile database, build.ninja, forced `uatomic_clang_compat.h`, source,
generated headers, LLVM20 tools and SDK/CRT libraries were inspected and pinned.
The proposed frontend graph retains the actual C11/ARM64/O2/MD/NDEBUG flags and
65 defines. It compiles the fourteen original VTN translation units, including
two generated C files. Three primary Mesa generator calls reproduce four exact
staged outputs before upload.

The native graph also needs Mesa's separate one-TU SIMD library. Primary
`src/util/meson.build` and build.ninja identify `streaming-load-memcpy.c` as that
target; it is not part of the existing core util archive. The nineteen commands
are SIMD compile/archive, fourteen VTN compiles/archive, and a typed volatile
`spirv_to_nir` consumer compile/link. The consumer proves link closure only and
must not be executed as a substitute for the semantic checkpoint.

Existing source/cache/core libraries remain read-only. Each attempt uses a new
builder directory, verifies 8,414 inputs before/after, checks actual showIncludes
against the selected inventory, checks AA64 objects/PE and the finite actual SDK
import set, and fetches a hash-verified frozen output bundle. This preparation
does not add external implementation code to this repository.

## Observations that determine the next action

- EXP1445: generators reproduced four exact files; native inputs matched.
  First SIMD compile returned zero but the controller stopped on two clang-cl
  unused `/Zc:enumTypes` and `/Zc:preprocessor` warnings. No archive/link proof.
- EXP1446: SIMD compile/archive and `spirv_info.c` compile succeeded with the
  actual dependency checks. `vtn_gather_types.c` also returned zero, then its
  additional NIR header warnings stopped the exact-warning gate. Inspected
  `nir_builder.h:1926–1929,2209–2210` and `macros.h:319–334`: these identify an
  early-return indentation diagnostic and locals used by disabled assertions.
  They do not justify changing upstream behavior or suppressing warnings.
- EXP1447: zero commands ran after successful input preflight. An exception
  with an empty message exposed a reporting bug: `error=str(exc)` followed by
  truthiness incorrectly selected BUILD_COMPLETE. The outer controller rejected
  commands=0. The underlying exception was not captured and remains unknown.
- EXP1448: explicit exception handling was introduced and tested. The next
  attempt failed earlier, during ZIP extraction, with a captured Python
  `MemoryError: Unable to allocate output buffer`. No compiler started.
  Read-only builder inventory at 00:02:20Z showed free virtual memory 141,184 KiB
  and PowerShell PID 41676 private bytes 103,846,612,992. This establishes builder
  resource exhaustion at that time. It does not establish that EXP1447 had the
  same cause or that PID 41676 belongs to this work. Identity queries twice met
  SSH connection resets; no process was terminated.

Raw evidence is under `.local/c15-libvtn-simd-native-build-attempt2-result`,
`...-attempt3-result`, `...-build-1447-result`, `...-build-1448-result` and
`.local/c15-builder-memory1448` in the shared workspace. The full experiment
ledger records exact commands, input hashes and immutable STOP receipts.

## Reporting correction and remaining acceptance gates

The frozen EXP1448 runner/controller in `.local/c15-libvtn-simd-build-1448-prep`
use `None` as the only no-failure sentinel and capture exception type, message,
repr, traceback and stage. READY SHA-256 is
`df91b82e715c05885da81166d277a805be8ab67a0094d565504bffbf8acc03a8`.
Four local regression tests execute the actual AST-extracted helpers, including
an empty AssertionError, and reproduce the old status expression's defect.
Both scripts now produce STOP/exit 1 for that case; root independently checked
the same helpers. This is a reporting correction, not an allocator recovery fix.

Warnings are retained in full. Completion of nineteen commands with diagnostics
produces `BUILD_COMPLETE_DIAGNOSTICS_REVIEW_REQUIRED`, `accepted=False`; root
must inspect every diagnostic and its primary source before accepting archives.
Return-code, timeout, input, dependency, AA64 and import failures still stop the
build. No warning classification or compiler-source repair is inferred from
successful compilation alone.

Next: establish builder process ownership and recover resource availability,
then preregister a fresh experiment ID. Do not retry builds while commit is
exhausted or kill the unidentified process. After accepted archive hashes exist,
bind the separate real NIR-map semantic fixture to them, compile/link it, review
its actual imports, and preregister CPU-only execution on the inert guest. Its
fixture checks remain active under NDEBUG and must explicitly report that native
`nir_validate_shader` is a no-op. Asahi backend, runtime ownership and truthful
DDI/model admission remain separate gates.
