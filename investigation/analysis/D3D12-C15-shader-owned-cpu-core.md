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

## Tagged LEGACY/DXIL continuity (EXP1435 foundation, unlinked)

Ownedobjectmagic nowdistinguishesLegacyDxbc/Dxil via typedgetter; this isNOT a
legacyPROGRAMmagic orshaderFlags encoding. ExistingDXILmagic/88Bownedlayout and
Calc/CreateABI remainunchanged. SharedCalculate dispatches actualnewlegacyframer
(SM4.0/4.1/5.0/5.1) oractualboundedDXILparser(SM6.0), selectedCS/VS/PSonly. GS/HS/DS
remaininternalUnsupported, notnewdrivercaps. PROGRAM/signatures/rootbytes areowned,
kind/outputpublishedlast. Trustedspan/rootprovider/lifetime/VOIDpolicy unchanged.

PrivateHOST1747262e actualsixargvrc0/stderr0: unchangedDXIL114467 baseline and
continuity424029checks/9reallegacyFXCaccepts/3deferredstagefiles/realDXILcontrol,
60acceptedstoragecases/18internalrefusals;post45pinsunchanged. Outputmanifest
0857ef612b74939e7ffaf3b241ac31dcf4f69509a7d5b3fbec99a802d660857b.
NativeactualSDK88B/getter/typegate SOURCEproposal separate; doNOTclaimitsrunhere.

Existingdurableoptin isexpanded toSIXderivedcommands, defaultstillONEskip before
tools. BothactualCframers compile, unchangedDXILbaseline link/run(steps3/4), actual
continuityoracle link/run(steps5/6). Exactmarkers114467/424029, allstderrEMPTY,
60sbound/raw/posthashes. SourceclosureincludesactualcurrentlegacyframerC/H, actual
legacyrenderer +itscurrent12FXCfixtures/inputs, newcontinuityfixture, bothSDKpaths
andcurrentparser/zeroCache/core. Renderer generates12reallegacyPROGRAMs under
legacy-programs; noarchivedfallback ornewtest-onlyproductionhooks. Newdurable
executionrequiresitsOWNcurrentrepo/source-generation approval/BEFORE afteradoption;
privateHOSTPASSnotretrospectivelycalledadoptedtestrun. No productionlink/registration.

## Tagged legacy and DXIL continuity, observed follow-up

Private candidate HOST1747262e preserved the unchanged114467 DXIL oracle and
passed424029 continuity checks: nine actual legacy CS/VS/PS programs, three internal
GS/HS/DS Unsupported cases, actual DXIL control,60storage accepts/18refusals.
All6argvrc0/stderrEMPTY/post45pins stable. This internal domain is not a driver
stage/model advertisement. SharedCalculate frames once perCalc/Create, checked
self extent excludes caller tail, and private kind/output publish after copies.
Old DXIL magic, public helper signatures and88-byte header remain unchanged;
legacy uses a different owned-object tag, never PROGRAM magic or shader flags.
GetProgramKind requires a live owned object, not an arbitrary DDI handle. No
atomic cross-thread publication or provider lifetime guarantee is added.

Exact native1435 under6bc26807:3MSVC/WX objectsAA64/rc0/stderrEMPTY, actual SDK
records and88-byte/offset/getter assertions passed;4905postpins/21downloads match.
Manifest1e32d2310d9bfa685689f4f30d482ab20f3ad68c47e450273f05f56b1fadc7b6.
No native linking/execution.

After adoption, the EXISTING opt-in regression now derives6commands with both
CURRENT framers, unchanged DXIL fixture and new continuity oracle. No archived
candidate fallback. Undercfee2900 one unittestPASS1.987s/all6rc0/stderrEMPTY,
114467+424029 checks,57postinputs stable. Outputmanifest
cbd67d6e92b975a48712282506a5029e6fc4aab090a9843a59d856de866eb95e.
Full1593tests213.369s/exact119baseline failures-errors/no added or removed/11skips;
SHA0744348cba0274ce6dce34d50ac85762b625ea46a0707b1fe10109253603e48b.
No whole-suite GREEN. This remains unlinked CPU framing/storage; IR validity,
root/provider lifetime, VOID-DDI policy, registration, production link and SM6
capability are not established. Immutable hardware1430/1431 exclude this follow-up.
