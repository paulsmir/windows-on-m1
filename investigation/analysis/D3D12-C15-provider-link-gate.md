# C15 CPU provider/core native link gate

The original `agx_d3d12_shader_owned_core.cpp` and
`agx_shader_input_provider.cpp` have host tests, but neither is currently a
production UMD project source. The provider calls the owned-core API to copy
validated shader inputs, so compiling the provider alone cannot establish a
complete native link. The preceding EXP1598/1599 gate established the original
zero-native-cache module and its live Device/PSO resolver hooks in that project.

## WINDOWS CONTRACT:

The pinned WDK26100 header defines typed CORE_0010 shader input records and
LibraryReference fields. These original CPU modules implement input ownership
and checking for a possible future R1 adapter; project linkage does not
publish CORE_0010 or any callback pointer. The current CORE_0003 and
SHADER_MODELS SM5.1 admission stay unchanged. The exact target project is
`drivers/apple-agx/render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj`.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A future typed shader adapter
must link both source-owned input storage and its provider under the pinned
Windows ARM64 compiler/runtime profile. A compile-only fixture cannot prove
the production linker has all project symbols or unchanged DLL imports/exports.
Existing host fixtures already exercise the modules' CPU contracts; source
text assertions about two XML entries would only mirror this edit.

## AGX/ASAHI CONTRACT:

Both modules operate on CPU input and private metadata. In current CORE_0003
they are uncalled. The accepted native AGX owner, queues, BOs, UAT, m1n1, Mu,
power, interrupt and recovery paths do not call them. No upstream converter
implementation is copied, linked or admitted by this gate. The owned-core's
existing local framing helpers must still resolve under the current native
toolchain; a linker failure is an actual dependency finding, not a reason to
stub an unresolved symbol.

## TRANSLATION:

Add exactly two C++17 ClCompile inputs for the original owned-core and provider
sources. They form one indivisible native link graph because provider copying
calls owned-core entry points. Preserve the zero-cache module from EXP1598.
Use a fresh one-project-file source manifest on the hardware-qualified EXP1594
baseline plus the accepted zero-cache link delta. Build a fresh normal runtime
and production UMD package in a build-only profile. Require both CPP source
records in actual CL tlogs, both objects in the UMD link input group, recognized
dependencies, unchanged UMD import/export list, zero added warnings and passing
stack checks. Do not sign or install a package solely to test an uncalled module.

## WHAT IS STILL UNKNOWN:

Actual Windows ARM64 project compile/link and any missing dependency or new
import. These are deterministic builder facts. Even a successful link does not
close R1 typed state/PSO callbacks, pipeline-library recipe serialization,
runtime null-field ordering, safe dxil-spirv internal OOM handling, native
owner/context concurrency or SM6 shader execution. No J313 diagnostic hardware
run is justified by this link-only gate.

## Checkpoint and recovery

Perform one bounded native builder graph with exact source/tool/input hashes;
preserve any failure and repair only the owning module or project under a new
ID. Successful data-only review records source/object/link proof and keeps J313
in its normal GPU-visible Code28/PackageAbsent/noService/8CPU recovery state.

## EXP1603 source-closure correction

The first production ARM64 compile stopped before linking. The frozen source
manifest contained `agx_d3d12_shader_owned_core.cpp` but omitted the original
tracked `agx_shader_input_provider.cpp` and its header; the branch workspace
presence did not make them part of the isolated source ZIP. The owned-core
translation unit includes `agx_dxil_program.h` and
`agx_legacy_program_frame.h` from `mesa/dxil` and `mesa/dxbc`, whereas the UMD
project supplied neither include directory. Its calls also require the
original `agx_dxil_program.c` and `agx_legacy_program_frame.c` definitions
in the native link. These are concrete source/project dependencies, not
hardware findings. EXP1604 freezes the two missing provider files and fixes
the project include/compile graph; all four original modules remain uncalled
by CORE_0003. A new builder gate must verify the actual compiler and linker
receipts. EXP1603 remains a failed build receipt and must not be relabelled.

## EXP1607 Windows header macro correction

The corrected source snapshot reached the actual ARM64 compiler, which found
`minwindef.h`'s function-like `max(a,b)` macro expanding in the provider's
`std::numeric_limits<T>::max()` calls. The pinned Windows SDK wraps that macro
in `#ifndef NOMINMAX`. The UMD project now defines `NOMINMAX=1` only for the
two original C++ translation units that use `numeric_limits::max`; this keeps
the original CPU modules and all other project compile flags unchanged.
EXP1607 remains a failed `/WX` build receipt. A fresh source-bound native
compile is required before any link or capability claim.

## EXP1611 duplicate parser owner correction

The first compile after `NOMINMAX` reached the native linker. The accepted
eight-object request compiles `agx_dxil_program.c` into `program.obj` and
`agx_legacy_program_frame.c` into `legacy.obj`; the actual EXP1611 linker
diagnostic reported `LNK2005` for `agx_dxil_parse` and
`agx_legacy_frame_program` when the UMD project also compiled those same C
files. The eight-object prepared owner already supplies both definitions, so
the project removes only its duplicate C ClCompile entries. The owned-core
include directories and per-source `NOMINMAX` remain. This follows observed
symbol ownership instead of accepting duplicate definitions or weakening the
linker. A fresh project source snapshot and link gate must prove the change.

## EXP1615/1620 native link verdict

The corrected isolated source `02b79418` built the full ARM64 SYS and UMD
with version 30.0.1615.0. Actual CL command/read groups contain the original
zero-cache, owned-core and provider modules; the UMD link lists each object
once. The prepared `program.obj` and `legacy.obj` provide the two parser
definitions once, and no project parser object is present. All 3,147 actual
read records are recognized after adding the exact hash-proven provider header
to the data review's pin set. The produced UMD import set equals the accepted
1527 baseline, exports only `OpenAdapter10_2` and `OpenAdapter12`, has no new
warnings, and both ARM64/WOW stack checks pass. The final combined receipt is
`.local/c15-provider-dedup-package1615-result/root-provider-link-review1620.json`.

This establishes a native build/link contract for original CPU modules only.
The current CORE_0003/SM5.1 callbacks do not invoke the provider. Typed R1
adapters, owned pipeline recipe/library behavior, converter OOM handling,
threading and actual GPU shader execution remain separate admission gates.
