# C15 shader-owned CPU storage — unlinked, bounded foundation

Original CPU core and actual-module regression; no production project/CORE table,
DDI wrappers, caps, native owner or translator linkage. RENDER_ONLY shader byte
ownership within a future coherent FULL GRAPHICS profile, not R1/SM6 admission.

WINDOWS CONTRACT: pinned WDK26100 d3d12umddi.h SHAd338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28
CREATE_SHADER_0010 contains root/code/signatureunion/flags/libraryreference. Exact
runtime-r1-contract d8747afd supplies PROGRAM independently ofLibraryReference;
zero-cache-reference validator be917753 validates same live device/library. No
invented shadercachedblob field, NULL ordinaryinput inheritance or fakeVOIDDDI
error mapping is introduced. Standardlegacy signature fields are copied byname;
0012Stream padding is not read. Internal statuses are NOT permittedDDIerrorcodes.

AGX/ASAHI CONTRACT: this checkpoint does not access an AGX/Gallium/compiler context.
Owned PROGRAM bytes can feed a separatelyvalidated translator later. No BO/native
shader/pipeline is created, and no argument about hardware support follows here.

TRANSLATION: caller supplies actualreadable stable PROGRAM span, signature spans
and pointer-free canonical rootcompatibility snapshot from a futureverifiedroot
resolver while its lease is held. Core checks PROGRAM pointer matchesactualargs,
checks span/count/layout arithmetic and calls actualoriginal agx_dxil_parse with
PROGRAM framing, SM6.0 and CS/VS/PS stageprofile. FlagsNONE only. Unsupported
kinds/models/flags internalstatus does not imply legaladvertisedprofile refusal.
No allocatingdxbc_parse_program inCalc; existingDXBCprofile unaffected.

Checked runtime-private size stores header+program+canonicalsignature+rootbytes,
relativeoffsets only. It retains no borrowedhandles/pointers/libraryreference;
no externalallocation or cleanup-on-laterDestroy dependency. Create handles
poisonedstorage, verifiescapacity/alignment/alias beforewrites, initializesexact
extent/lifetimes and publishesoutput last. Device/LibraryReference provenance is
checked viaactualzero-cache module, whose resolver precedes its registrylock.
Provider/outputnonalias and legalcallbacklifetime are required; this core doesNOT
supply actualdevice/root/componentresolver or cross-threadpublicationprimitive.

WHAT IS STILL UNKNOWN: fullIR/bitcode validity; actualroot/componentlifetime and
canonicalrootformat integration; DDItrusted-span acquisition; per-deviceVOIDDDI
errorpolicy; PSOordinaryconstructor and nativecompiler/context concurrency; coherent
R1companions/caps. No hardwarecheckpoint follows fromthisCPUfoundation alone.

WHAT REAL BUG/INVARIANT: Calc/Create extent agreement, no borrowed-input retention,
poison/canary initialization, input/destinationalias rejection, checkedoverflow,
same-device libraryprovenance, and completeowneddata afterinput/librarydestruction.
Actualmodule fixture tests exact finite stage3xpoison3xordinary/library2 domain,
rawprogram/root/signatures, short/unalignedstorage, failedoutputclear and negatives.
VS/PS fixture changes ONLY actualCSprogramheaderstage: proves framing/storage,
never validVS/PSIR. No artificialRED or fullOOMcoverage claim.

Privatehost EXP1428 BEFORE48f17a5c: actualparsercompile0/actualcore+actualzeroCache
link0/fixture0,114467checks; rawmanifest497794f1b5ad22ca9348267200ac2b70aafd963cd09b58e8aa88a81d4c622347.
Originalnative BEFORE8f900653 STOP preserved: core0/ABI2 SDKC4201 underWX/thirdNOTRUN;
notfunctionalRED. Independentmanifest506ff49b42b98f9540f45371b364056c4c9df3ec05dc0411c4b40a8b7473e707.
Retry1 BEFOREa6c879bd addsONLYABIheaderlocalwarningguard, all3CLrc0,3AA64objects,
4904postpinsunchanged/21downloadsverified; independentmanifestbb8e4772ad6d77a6092fef02158e4b89d30f00be3491f45499d17b4aaa966ebc.
Privatepaths under.local/c15-shader-owned-core-prep and
.local/c15-shader-owned-native1428[-retry1]-prep. Nativeobjectonly: noWindowslink/run.
Nativefixture includescorrectedheaderpragma; CLIflags use explicitNOMINMAX.

Newdurablerunner defaultskipbeforetools. OptinrequiresparentBEFORE/source/tool/
generatedinputapprovalhash andexact3hostargv. It compilesactualCURRENTrepo core,
parser andzeroCache, neverarchivedcandidatefallback or duplicatemodulelogic.
Adoptedrunner execution must have its OWNsourcefreeze/BEFORE: archivedprivatePASS
is not retrospectively called a run of thenewdurabletest. Sources anddescription
are original; no externalimplementation copied in thiscore.

## Observed CURRENT-source verification

Private native retry1 a6c879bd produced3 AA64 objects, each CLrc0, all4904postpins
stable. The initial ABI-header C4201 STOP remains separate; only local header
warning guard changed, with no global flag/core change. Native object manifest
bb8e4772ad6d77a6092fef02158e4b89d30f00be3491f45499d17b4aaa966ebc.

After adoption, the first durable attempt9cd32276 stopped BEFORE any compiler on
strict Users/Volumes path-spelling inequality. All22pins matched; no functional
RED was claimed. New canonical approval6b7c3f98 matched55precompiler invariants;
one unittest PASS1.244s, actual CURRENT parser/core/zero-cache3argv rc0/0/0,
114467checks ASan/UBSan PASS, post22pins unchanged. Output manifest
 e79deaed2a64eedd7c53b5d475e588934c2a35106a888d1e0bd3bc26fe7673b5;
independent manifest29fe4cd921747a4d1e7aec50b3b61296c94ba7316f59804bd0275c06d899a472.
Default discovery skips this opt-in test before compiler work. Full1592/215.739s
retains exactly119 baseline failures/errors/no additions or removals/10skips.
No whole-suite GREEN, IR-validity, full-OOM, trusted-provider, VOID-DDI policy,
component lifetime, registration, production DXIL link or SM6 capability claim.
