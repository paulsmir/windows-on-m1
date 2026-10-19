# C15 native enum ABI: reviewed owner remedy and gates

Root review, 2026-10-19 UTC. Classification: RENDER_ONLY CPU compiler metadata.
This note authorizes an isolated original projector and regression prototype;
production/shared native source and existing archives remain unchanged until the
measured ABI and complete rebuild gates pass. EXP1457 is the four-field RED
checkpoint; the fifth hash field remains unmeasured.

License and compatibility decision: modification is permitted with conditions.
Primary Mesa checkout commit is `9aa1215f878b504f66159dd2ead4c7973142126e`.
The exact `src/compiler/shader_info.h` is 23,753 bytes, SHA-256
`893b5a63f7dd0fbaf1d96953451fb073e35fd38acd8be60a51528e2f37204a09`,
matching the actual builder pin from EXP1451. Root read its Intel 2016 MIT
copyright, permission and disclaimer at lines 1–22. That license permits local
modification/distribution and is compatible with the project MIT license; retain
the complete notice in generated copies and driver distribution notices. No new
runtime dependency or license is introduced. Keep external header bodies outside
Git; tracked material will contain only original projector/test code and precise
source/projection hashes. This review does not authorize copying unrelated Mesa
implementation bodies or expanding the previously selected dependency graph.

Source candidate: an explicit unsigned storage projection in the shared Windows
header, preserving enum constants, names, widths and order. Root has not accepted
packing or C++ source compatibility by inspection alone. Apply it only in a new
private projection for controlled tests; do not replace the reference checkout,
old native cache, or existing package. Before adoption, require five-field actual
behavior, C/C++ cross-TU compatibility, neighbor preservation, complete measured
layout/raw serialization compatibility, and every affected consumer rebuilt from
one consistent header. If physical bytes differ, establish cache invalidation
before reusing serialized metadata. Existing compiled shader caches also need a
proven identity policy; identical raw metadata alone does not prove their validity.

The bounded primary-source plan follows. Its unresolved items remain gates.

# Mesa shader_info enum-bitfield ABI remedy — SOURCE ONLY

Decision: prepare an original SHA-gated Windows header projection and rebuild its entire affected native consumer closure. Do not mask getters or alter DDI/caps. No code has been patched, compiled, uploaded or executed for this plan.

## Observed contract and owner

EXP1457 on actual J313 ARM64 confirmed four stores/reads: depth UNCHANGED4 -> -4, derivative LINEAR2 -> -2, stencil front/back UNCHANGED4 -> -4; full enum copies stay negative and unsigned conversions yield 4294967292/4294967294. NONE controls pass. shader_info size440 and all full enum sizes4 are observations, not a proof of field offsets. Raw evidence manifest cc81b97b4d9cb7001d17e1ec7cc24fe1f91152a071031108a25dd864d11f0b4d is parent-owned. Literal compute map399/384/7 evidence remains valid for its NONE-mode fixture only.

Owner is Mesa compiler shared CPU metadata under the existing Windows clang-cl/MS ABI, before GPU submission. No initialization, IRQ, DMA, firmware or recovery protocol changes are involved. This is not a WDK record defect: ordinary WDK enums are not the narrow Mesa storage declarations. Native source exposes negative values to downstream readers (agx_compile.c:3818 copies depth into full enum; agx_helpers.h:158–171 compares positive enumerants). Linux-host/native-Windows equality must be measured, not inferred from C enum portability.

## Exact licensed declarations and smallest candidate

Pinned shader_info.h carries Intel2016 MIT permission and disclaimer at lines1–22. Existing compatibility review permits modification/distribution with the copyright/permission notice retained. Keep generated modified header external; Git would contain only original projector/tests. Preserve source and projected SHA and the full notice; include modified-source provenance and current Mesa notices in distributions. No external implementation bodies copied into Git.

Five enum bitfields exist, not four:
* derivative_group:2 at370; values0/1/2 (shader_enums.h:1559).
* hash_type:2 at390; RAW0/DXIL1/DXBC2 (same header:40–44).
* depth_layout:3 at558; values0..4 (shader_enums.h:1057).
* stencil_front_layout:3 at569 and stencil_back_layout:3 at570; values0..4 (:1069).
The fifth field's value2 has the same signed two-bit risk; it has NOT been executed by1457. Add it to the next discriminator, do not claim an observed fifth defect.

Preferred review candidate: replace each affected enum storage declaration with explicit `unsigned` storage, keeping field names, bit widths, order, enum constants and all surrounding declarations unchanged. Proposed coherent five-field scope needs root acceptance, especially hash_type. This resolves storage interpretation at its owner rather than undoing sign extension downstream. It changes exposed C/C++ member types: any C++ implicit unsigned-to-enum assignment must be identified and explicitly reviewed, never repaired with global permissive flags. Bounded C++ audit: current project .cpp/.h named-field search found no getter; external Mesa .cpp/.cc/.cxx/.hpp named-field search found actual shader_info depth reads in Intel brw_compile_fs.cpp:562, elk_fs.cpp:6354 and radeonsi si_state_shaders.cpp:1716 as switch operands; r600_sfn.cpp:149–150 assigns to uint8_t ps_conservative_z (r600_pipe.h:126), not an enum destination. Other GLSL matches use distinct IR/variable records. Header named info/base accesses found no direct shader_info getter-to-enum in the selected shared headers; agx_helpers.h:188 reads agx_shader_info full enum, which stays unchanged. Therefore no concrete existing selected C++ unsigned-to-enum break was found in this bounded pass, but this does not waive the actual C++ consumer compile and complete dependency-based header audit. Generic external clients can rely on decltype/type semantics, so the projection is a Windows-private ABI/source policy, not an upstream-wide invisible fix.

No proven narrow compiler-flag remedy is selected. Existing actual C argv already uses /Zc:enumTypes, which clang-cl reports unused for C; Microsoft's option concerns C++ enum type deduction and warns about ABI compatibility. It is not a C narrow-bitfield remedy. Changing enum underlying types globally, short-enum settings, or bitfield ABI switches could change unrelated records and requires a distinct proven scope; do not guess them. Official references: https://learn.microsoft.com/en-us/cpp/build/reference/zc-enumtypes?view=msvc-170 ; https://learn.microsoft.com/en-us/cpp/c-language/c-bit-fields?view=msvc-170 ; https://learn.microsoft.com/en-us/cpp/c-language/c-enumeration-declarations?view=msvc-170 .

## Layout and neighbor gate before adoption

Use the exact1455 actual-header CPU probe as functional RED evidence (1457), then a separate fresh candidate probe. All assignments/reads must remain always evaluated under NDEBUG. Cover all legal values of the five fields, enum-copy and promoted/unsigned reads, independent front/back values, and NONE controls. Require explicitly observed results, never getter masks.

Add separate C and C++ producer/consumer TUs sharing the same projected header, with externally visible noinline functions and owned zero-initialized objects passed across the boundary. Compare sizeof/alignof shader_info, offsets of addressable enclosing/neighbor fields, size/alignment of enums and nir_shader, and raw object byte diffs from a baseline. No offsetof/address-of on bitfields. Verify each field changes only its measured bit mask and does not alter neighbors: use_legacy_math_rules, assume_no_data_races/forward-progress bits around derivative/hash; untyped_color_outputs, advanced_blend_modes, early_and_late_fragment_tests and front/back stencil independence. Exercise enclosing union/struct copy and serialization roundtrip. Derive bit positions from actual byte observations; do not invent layout constants. Control pointer/unused padding with zero initialization and compare only justified object regions. Record old/new layout independently; size440 alone is insufficient. If layout changes, prohibit mixing all old objects and explicitly version/invalidate persisted NIR metadata/cache bytes.

nir_serialize.c:2217–2239 writes a copy of shader_info directly with sizeof(info); deserialize at2302 copies bytes back. Thus cache compatibility is affected even if public WDK ABI is unchanged. Do not read old persistent NIR serialization through a changed layout without a proven version/invalidation mechanism.

## One shared header, all affected consumers rebuilt

Current build-native-asahi-state.py:6025+ imports flags from cached nir.c and :6047+ chooses build/src, original Mesa/src, Mesa/src/compiler plus optional native overlay roots. It has no current shader_info projection. nir.h:35 includes compiler/shader_info.h. A single state-only overlay does not repair prebuilt NIR/compiler archives. Include precedence must be checked against actual .d or /showIncludes for EVERY affected TU; quoted-source-relative includes can bypass an include-root overlay. Use one pristine-derived external source projection for the complete build, not mutable target-by-target header swapping.

build-nir-libraries.ps1 currently builds five cached archives: nir/compiler/util/c11/blake3. build-asahi-runtime-closure.py:31–33 and :367+ consumes their saved hashes; build-full-asahi.py consumes these cached dependencies and compiles the44 Asahi units. Existing native runtime objects, owner/DXBC C/C++ TUs and new14-TU libvtn checkpoint also consume the shared metadata. Rebuild all TUs whose actual dependency closure contains shader_info.h or a generated layout derived from it; conservatively rebuild all selected NIR/compiler/Asahi/frontend/runtime/owner graph in fresh roots. Any util/c11/blake3/SIMD library without this dependency may remain byte-pinned only after actual closure proves independence; no filename-based exemption. No stale old/new library mixture. Rebuild project tools/tests/serialization consumers with the same header. Preserve exact LLVM20, target,65defines, MD/NDEBUG and notices unless a separately reviewed requirement changes them. Record compiler-command coverage, header hash per TU, new archive hashes and final link inputs.

## Smallest sequence / still unknown

1. Root review license and five-field scope; preserve existing1457 RED and all pristine inputs.
2. Source-only narrow projector plus original cross-language/neighbor/layout fixture, exact input/command freeze, then separate root BEFORE for bounded CPU build/run. Existing no-mode map semantics remain a control.
3. If CPU GREEN and ABI controls pass, fresh ALL affected native TU rebuild, actual dependency closure and no-old-library-mix checks; then rerun the actual semantic map checkpoint. No GPU/caps/model claim follows.
4. Hardware is not needed to guess this CPU storage contract. Future package/hardware changes require their own parent-owned experiment, preserved recovery artifact and exact package manifest.

Unknown until gate: fifth-field actual behavior; old/new packing and C/C++ equivalence; any external C++ enum-typed assignment compatibility; complete transitive rebuild set and serialization-cache invalidation path. Do not expand admission to FS/derivative modes until their end-to-end behavior is separately verified. This source plan does not claim a fix, build PASS or a safe compiler flag.
