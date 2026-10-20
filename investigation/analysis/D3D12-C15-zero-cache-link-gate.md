# C15 zero-cache module compile and link gate

Native link verdict: EXP1598 compiled the module and linked the ARM64 UMD;
EXP1599 confirmed the source in CL input/read tlogs and its object exactly once
in the UMD link source group. The accepted UMD import set and two exports are
unchanged; no new warnings or unknown compiler inputs occurred. The original
EXP1525 reviewer stopped on its intentional old-profile forbidden-object guard;
that STOP is retained, and the fresh EXP1599 reviewer altered only that exact
guard while adding positive source/object checks. This remains a build-only
result with no GPU run or R1 table publication.

The original `agx_d3d12_zero_cache_library.cpp` already implements six typed
early-R1 library callbacks and an explicit LibraryReference validator, with
CPU and native object tests. The current accepted `00_03` driver does not
publish those callbacks, and the production UMD project does not compile the
module. EXP1594 validated live Device/PSO resolver metadata needed by that
module without admitting R1 or SM6.

## WINDOWS CONTRACT:

Pinned WDK26100 `d3d12umddi.h` defines distinct callback signatures for
CalcPrivatePipelineLibrarySize, CreatePipelineLibrary, DestroyPipelineLibrary,
AddPipelineStateToLibrary, CalcSerializedLibrarySize and SerializeLibrary.
Compiling their implementation does not publish a callback table. The current
FillDDITable continues to expose CORE_0003 and current GetCaps continues to
report SM5.1. The build project source is
`drivers/apple-agx/render-admission/umd/AppleAgxRenderAdmissionUmd.vcxproj`;
its normal ARM64 UMD includes `agx_d3d12_ddi.cpp` but omits this module.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Linking the standalone
module must resolve its two real Device/PSO owner hooks with the exact Windows
ARM64 WDK ABI. If the module introduces an unexpected import, export, unresolved
symbol, warning/error or changes current DDI table/caps publication, it must
not be admitted as part of a later coherent R1 package. Existing host/native
module tests cover callback behavior; a new source-text mirror test adds no
independent evidence.

## AGX/ASAHI CONTRACT:

The library stores CPU metadata only. Current00_03 runtime paths do not call
its six callbacks, so this link checkpoint makes no new owner/context, BO,
GPUVA, compiler, interrupt, power, m1n1, Mu or recovery call. There is no
change to signed EXP1594 package or its hardware verdict.

## TRANSLATION:

Add the existing original CPP as one C++17 ClCompile input to the UMD project.
Do not assign its function pointers in FillDDITable, change SHADER_MODELS,
enable DXIL translation, or copy external implementation code. Keep the same
Release ARM64 native frontend/toolchain/CRT inputs. In a fresh build-only root,
verify the compiled object exists, the linker consumes it, and compare the
resulting UMD imports/exports/version with the accepted EXP1594 binary. A
later production source manifest must pin the one project-file delta exactly.

## WHAT IS STILL UNKNOWN:

The module's native project-level compile/link result and imports are now
measured by EXP1598/1599. A diagnostic J313 run is not justified for an uncalled
module. A successful link does not establish complete
R1 library semantics: typed0010 shader/state/PSO adapters, owned serialized
recipe storage, actual runtime null-field ordering, device-child lifetime,
native owner/context concurrency and the reviewed dxil-spirv internal OOM
blocker remain separate gates.

## Checkpoint and recovery

Run one bounded fresh ARM64 build-only gate with exact source/tool input pins.
If native compilation or link fails, preserve the failure and fix the owning
source under a new ID. If it passes, retain CORE_0003 and do not run hardware
for an uncalled module. The normal GPU-visible recovery profile remains the
accepted Code28/PackageAbsent/noService/8CPU state from EXP1594.
