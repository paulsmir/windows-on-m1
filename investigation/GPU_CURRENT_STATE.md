# GPU current state — 2026-09-13

Integration worktree: .worktrees/integration-ad04-windows-compiler.
Read root investigation/GPU_CONTINUATION_PLAYBOOK.md after this file.
Do not read full EXPERIMENTS without a specific referenced evidence need.

## Mission and closed architecture
Full Graphics hardware-accelerated Windows desktop NOT ACCEPTED. Acceptance:
standard hardware D3D device, real dynamic Asahi draws and physical completion,
standard DXGI Present, accelerated DWM/interactive desktop,1000 Presents,
100 window lifetime cycles,30-minute stability and reset/re-entry.
OpenGL/CS1.6 come after desktop acceptance. Leave final accepted package installed.
Physical/patch-list WDDM is fixed; GPUVA migration CLOSED/NO.
Real Asahi graph -> typed capture -> immutable request materialization -> existing
UMD composer/pfnRenderCb -> existing KMD Render/Patch/Submit -> AGX completion.
No native ANS work, no merge/push, no platform/recovery redesign.

## Verified qualification implementation
Base native lifecycle checkpoint36ae38d (implementation9829689). The next coherent
slice adds a shared production scene, actualKMT bridge/client, reference-counted
residency in existing ScreenBuffers, fresh-output correlation and native KMD
completion/rawreadback receipts. The installed UMD pipeline mask remains0.
Source contract: agent_tasks/AD04-NATIVE-KMT-CALLBACK-CONTRACT.md.
Final evidence: AD04-runtime-closure/native-kmt-final-20260914i-x64 and
native-kmt-final-20260914j-arm64. x64 actual2scenes/2placements, pending/immediate
markers, residency accounting, stale-receipt rejection, cleanup/recreate PASS.
Both contracttest and realKMT client build/link PASS onARM64 (execution NOT_RUN).
i sourceSHA be10756af046c0dacf6b6288b81f9f86340c410157e3bb7dd21c9f95d039304c;
i clientSHA e7e6cec8a17359fd6e7c005539d49924cf26de959f11bc52f16de4eaa640a328.
j sourceSHA 8aa7baceed972d8e25686bda287ed90f210cb2b325ba0a28ceaf3701e0120d16;
j clientSHA 4ebcf1b951292483372844900166e29512e36f19e07826aae2b874557065a69e.
Clients above use expectedbuild0 and refuse qualification; full native callgraph
is linked (12MB), build expectation passed at runtime from the compiled caller.
Native scene compilation/BOupload stays synchronous on the owning thread.
ARM64 imports realKMT APIs andVCRUNTIME140/UCRT; sameEXE loader and realKMT entry verified onAir.
PE/source review: evidence/AD04-native-client-pe-20260914j.
16hosttests + qualification-enabled KMD ClCompile/codeanalysis99objects PASS:
evidence/AD04-native-qualification-host-20260914b and
AD04-native-qualification-kmd-20260914a. KMDsourceSHA
 a772df95611046914d45accc45af71bf7f58dbf7f6b2c7cac15ff6d76c7a910e.
Only later package-script pinning changed outside compiledsources; actualWindows
PowerShell parser PASS, two pinnedMSBuild invocations:
evidence/AD04-package-script-parse-20260914c.
EXP683 is preregistered at HEAD6954cac (native qualification implementation85a2623,
ledger6c5f3a2). Package30.0.683.0 VisibleAgxQualification build04 passes complete
UMD/KMD build and analysis with0warnings/errors. Source archive
f2526540fab82b8f5f72003cc08a4ba3725cf8efb67c5409c76db07f20621b10.
Build environment fixes pin stableVS and26100 UCRT/UM paths; worker SAL definition
matches declaration. Default WDK cert lacks DigitalSignature: rejected, not staged.
Final package uses experiment-scoped nonexportable signing cert thumbprint
1F20D29FFD6905597AE3D8F5F25B0B31E10C7C03. SYS/DLL/CAT signatures and binary catalog
membership independently PASS; Windows INF membership found but builder chain is
untrusted. No candidate package/trust installed on Air. Expected683 ARM64 client build/link
PASS: native-client-683-20260914a, SHA31ce32fd386525afabb4af3774349a54eced3b26a024a406ba09b7f13265af2f.
Desktop ARM64 CRT14.44.35211.0 app-local and hashverified. Frozen input manifest
SHA71b07f6708e42d87b2f691226e030cc166ba47cfe42ddf05d173f8c75e46d645.
## EXP683 actual hardware boundary — 2026-09-14
Operator disabled SAC; read-only CiTool confirms policy no longer enforced,
TESTSIGNING Yes retained. SameEXE loader-onlyPASS; exact1F20 testcert trust and
SYS/DLL/CAT/INFcatalog verification PASS. Package683 stagedoem5 then naturalbind
Code0 on immutableEXP584/406 GPU-enabled launch. No desktop acceptance.
First detachedSSH launcher killedchild beforegraph; harmless5-secondchild test
confirmed parentexit killschild, parentWaitForExit completes. Corrected attached
native-run02 then constructed REAL30refs126relocs6248-bytecommand, nativeDMA44952.
Client printed commandhash4aaaa637f4c32ec4, generation127664129; Windows reset in
KMD submit. Bugcheck0x119/Arg1=2/STATUS_INVALID_HANDLE c0000008; otherparameters
fffff5845fdafa00,ffffc004ecc2ac00. No nativecompletion/readback proven.
Dump091426-10750-01.dmp SHA1d9155da06d0d128e2db09a6bfe329365c17830c4403333babfd72bd6f28436a;
source/pinnedPDB analysis identifies dxgmms2 VidSchiSendToExecutionQueue,
Submit fence295 and44952DMA bytes. Context/private heap unavailable in minidump.
Recovered commandfile6248bytes is allzero (not replay evidence); stdout records
actualproducer census. Preserve originals. DWM0x889800b0 existedbeforeclient;
Event129 also recorded, neither attributed to nativegraph withoutcausalproof.
Guesttimestampsrewind acrossfirmwareboots; correlate dump addresses/receipts.

Offlinefixc8245eb confirmed: actualproducer targetreference23/index8/count9
fails legacyDestinationIndex>=2 in RED, passes capturedAllocationCount bounds in
GREEN. Existing packetdescription uses padding, oldfieldoffsets preserved;
Render/Patch supply actualAllocationListSize, Patch preservesvalidatedtargetindex.
Capture/Adopt/Prepare/Matches enforcecount; no arbitraryconstant widening.
Final x64native-submit-index-green-20260914b PASS actual2placements+retirement+
recreate, sourceSHA9750c6130d4e1e84584c6afa700ae97c1fbfca64c13cd15bd70dbf5eaa7c3702.
ARM64native-submit-index-final-20260914c-arm64 build/linkPASS,
sourceSHAd9eb8c81099706a427022ca5496ce726a3a3ebed50920282e969fbb059b0100c.
15focusedhosttests and99KMDobjects analysis0warnings/errors PASS;
KMDsourceSHA060e8cc4bb362ff4e8dd18064b8494edf81773b885e32c27d28778a1544b9c8d.
Fixedcandidate stillNOT hardwaretested. Next add durablecommanddump flush before
Render (EXP683 cachedfile lost onreset), then new684build/sign/hash/prereg gates.

Durable evidence fix803a7c2 is complete offline. Existing bridge CREATE_NEW name
is unchanged; full write, FlushFileBuffers and CloseHandle now complete before
D3DKMTRender. Create/write/short-write/flush/close errors preserve operation,
Win32 error and bytes in the bridge receipt and reject before KMT Render.
TDD evidence: behavior-preserving refactor bb08f047..., behavioral RED
b1dd83f7... execution38/errors38, final x64 GREEN sourceSHA
a035eda36ce2e7b39222982ae5096feb1c56a65eadc01d0f9d31273e0b891435
with six cases and actual producer PASS. ARM64 sourceSHA
8c9d9b8de519dc8339c96a82c5c7ff35c4f8e1383d13375f9e988432f3ebef13,
clientSHA8ed89c3a57ed441a16b69f9ab3fe56d9b9ac866ad69b4cb68943a3ae9e121b2a;
build/link0warnings0errors. Fixed client remainsNOT hardwaretested.

Recovery: ordinary377/392 withpackageinstalled remainedSSHunavailable>180sec;
SIGINTsnapshotCPU/timersalive. Documented emergency377/385 GPU-hidden DISK boot
(noRAMdisk), compatibility scopedEXP491-R2/emergency.sh, recoveredSSH/evidence.
Saved System/Applicationevtx,rawregistry,native-run02,small dump BEFOREcleanup.
Exactoem5 uninstalled/deleted;staleAPPL0002removed;stoppedserviceexplicitlydeleted;
SYS/UMD/module/package absent;exact1F20cert removed;AppleInput71CD0A preserved.
Ordinary377/392 brokerunset has been relaunched to restoreoneinertCode28;
finalhealthPASS2026-09-14T06:46:01Z:8CPU,oneCode28,SSH/NVMe/xHCI/keyboardOK,
noAppleAgx package/service/module/files/signer,no fresh41/1001/129;TESTSIGNINGYes.
Active launcher logordinary-final-boot.log.
All EXP683 artifacts under main-root .local/experiments/EXP-20260913-683-native-batch;
append-only EXP683 ledger records exactcommands/hashes/recovery/results.

## Verified native lifecycle slice
The real native screen/context/resource/NIR shader/state/clear/draw/flush path
now executes through stable batch-owned root/capture/adapter storage and the
existing KMD validator, bounded placement, materializer, DMA patch and native
BG/partial/EOT root routing, then ordered retirement. Existing UmdContractTest
runs this producer; manually assembled capture is not its source.
Contract/source review: agent_tasks/AD04-NATIVE-LIFECYCLE-INTEGRATION.md.
Implementation index: CHG-20260913-NATIVE-BATCH-LIFECYCLE in CHANGES.csv.

Supported minimum: one nonindexed3-vertex triangle, first0/one instance, one
uncompressed BGRA8 single-layer/mip/sample RT; no depth/bias, geometry/tessellation,
compute/indirect/query/streamout/application texture/UBO/SSBO or scratch/spill.
Unsupported operations reject before submission. No false feature-level claim.
Actual census:30references,126relocations,9Windows allocations,137encoder bytes,
16384RT bytes,29copied objects,36496source bytes,44952DMA bytes. Both actual KMD
placement variants pass. v4 bounds64refs/128edges/8192command bytes; legacy limits
remain versioned. No second allocator/composer or GPU mapping was introduced.

Lifetime tests prove pending-marker holds, wrong-fence rejection, ordered release,
unsupported next topology without a new batch, retry after two deallocation
failures, and a second real screen/context on the same backend. Detach retains
configuration on failure and clears it only after successful owner leave.
Capture owns source BO holds; composer owns Windows submission holds. Render
entry forbids replay/abort. Pools/framebuffer/capsule survive until retirement.

## Final evidence and precise limits
Fresh x64 full native archive/build/link/execution PASS:
evidence/AD04-runtime-closure/native-lifecycle-final-20260913aa-x64/.
Archive SHA2564c18095f5cb47f09cd0121b37fc75f14b6e77915d7ce06dcd7611d490870d9eb.
EXE SHA25650ac86a0c655685f66badb5ac1548235468298e1f035932fa3776aa3bc5ac6e3.
All462source files match that snapshot; archive AppleDouble metadata is counted
separately in final-source-match.json. No source changed after these final gates.
Fresh ARM64 full native archive/build/link PASS, execution NOT_RUN:
evidence/AD04-runtime-closure/native-lifecycle-final-20260913ab-arm64/.
Archive SHA256493b43a73a016c8b59a70def172c754e8b5a1814d1972090c98661283a735995.
EXE SHA2563376416d21c1f82040241603224f7ee8782164f364576a1b95f4c870e0d33384.
UMD MSBuild both0warnings/errors; native compiler logs preserved separately.
Ten focused ASan/UBSan hosttests PASS: evidence/AD04-final-checkpoint-host-20260913a.
Pinned26100 ARM64 KMD ClCompile + code analysis PASS:99objects,0warnings/errors;
evidence/AD04-final-checkpoint-kmd-20260913a. Subsequent native detach/test changes
do not change those KMD sources. No SYS/package/sign/install/hardware was run.

Retained causal failures m..x are scoped under AD04-runtime-closure, summarized
in the integration contract. Key fixes: disabled timestamp conversion; unsigned
packed native blend key; absent texture/query roots; tagged32-bit USC restoration;
explicit native empty-tile flag translation; exact even-byte v4 shader spans.
AD04-native-blend-abi disproves NIR layout mismatch. No NIR assertion disabled.
No-query scratch slots are unused by admitted VS/FS and canonicalized only at
root upload; global Asahi scratch semantics remain unchanged.

## Exact next target
Prepare next nativequalification candidate with verified packetboundsfix and
verified durablecommanddump flush. Build exactpackage/client684 from cleanHEAD,
use a new experiment-scoped signing identity, and keep SSHparent attached untilclientexit;
preservefailed/pendingresources. New hardware requires exact684package/client,
fullgates/sign/hash and freshordinarybaseline; no stale683package onAir.

Continue production integration in parallel with that boundary:
agent_tasks/AD04-NATIVE-RUNTIME-ACTIVATION-NEXT.md. Existing
AgxD3d10WindowsCreateDevice still uses a map-only pipe. Replace that factory owner
with real native screen/context after runtime callback initialization; defer Mesa
frontend screen creation to CreateDevice and link this same closure into UMD.
Installed pipeline mask remains0 until the selected DDI/FL contract is truthful.
Standard Present/shared resources/redirection/DWM and desktop stability remain open.

## Preserved hardware/recovery boundary
Currentordinary377/392 GPU-visible recovery is healthy,brokerdisabled,oneinert
APPL0002Code28,noAppleAgx package/service/module/files/signer. Exacthealth evidence
main-root EXP683/ordinary-final-health.json at2026-09-14T06:46:01Z.
AppleInput71CD0A trust anddriver preserved;TESTSIGNINGYes. Operator's SACchange
was verified; assistant didnotchangeSAC/BCD. EXP683-specific1F20 trust removed.
SSH pavel@192.168.1.37, key ~/.ssh/air, STRICT known-hosts main-root
.local/experiments/EXP641-standard-present/air_known_hosts. Defaultknown_hosts
isstale;do notchangeit. Activeordinary-final launcher ownsproxy/vUART.
Emergency377/385hiddenDISKboot is proven recovery exception only; EXP378WinPE
contains automatic oldcleanup,so preferdocumentedhiddenDISKboot for evidencefirst.
Emergencyartifacthashes: main-rootEXP683/emergency-recovery-hashes.json.
Beforehardware recordhypothesis/commands/source/packagehashes inEXPERIMENTS,
checkbothcontrolplanes, preserveknownrecovery,collectevidence thenexactcleanup.
No routineGPUhiddenboot. Neverrequestphysicalaction beforebothcontrolplanechecks.
