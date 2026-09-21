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
build/link0warnings0errors.

EXP684 exactcandidate hardware KMT qualification PASS. CleanHEADd0b2045
source archive SHAe0cb364151dfa38107a81d1f980bc8a0c2aa74d511d0927433a31480e152035d.
Package684 fullbuild/analysis0warnings0errors; INF/SYS/UMD30.0.684.0. Fresh
nonexportable EXP684 signer thumb8055D20754EF031B539B5499170A7EEDD6E5FE8C,
publicSHA0ad4dd9a95d74dd0c5d9cdc01e46ff71f9948b0370fb2b372b9b500a58a64f64.
Finalpackage INF4e41f4d7...,SYS7ba5d090...,UMD91726a6c...,CAT4a5c25e0...;
explicit-cert embedded/catalog verificationPASS. Exact ARM64 expectedbuild684
clientSHAe37bef254b2142f7338a12e87355b02ebec16dea1eba395282d42f7b9ce19814.
Same candidate sourceSHA90d8fb801481941e843761b644339a407d70422c96f4fde47c2c86f2e3f74ba5
x64actualproducerPASS and ARM64build/linkPASS0warnings/errors. Frozen target
manifestSHA79e9e78a3765c446c03e1b9f325a856861e5473bec333a64d983735538d78ed6.
Single attached client exit0 in347ms. Actual30refs/126relocs/9allocs/29objects,
destination23/index8/count9,137encoder,36496source,44952DMA and16KiB target.
Durable6248-byte command hash9105be4676f0f94f has952nonzero bytes; dump stage6,
error0. Fresh registry/client receipt build684,boot294088076,generation235405313,
fence293,snapshot1; registry and client receipt files byte-identicalSHA881820cb....
Independent readback FNV9219a8781a475585 and Morton pixel verification:
0mismatches,128foreground. Alloc/free12/12,lock/unlock11/11. NoSystem41/1001/
129/4101 during run. Exact package and8055signer removed after evidence.
This validates KMT native lifecycle only; standardD3D/Present/DWM remain open.

## Verified runtime-owned native private factory

Implementation0e4dcf7 replaces the map-only AGX_D3D10_WINDOWS_DEVICE with the
existing authoritative Runtime+AsahiOwner+Backend+nativeScreen+primaryContext.
The stable heap owner is linked before callbacks. Stages are Allocated,
RuntimeReady,NativeScreenReady,NativeContextReady,Ready,Closing,
NativeContextReleased,NativeScreenReleased,RuntimeReleased,Freed. Initial create
failure and cleanup status remain separate; failed cleanup returns an unusable
nonnull private cleanup token, while clean failure returnsNULL. Context accessor
returns only atReady.

Close blocks new native work, preserves active/submitted batch ownership, tracks
all direct screen context_create/destroy calls, destroys the primary context once,
retries screen BO detach/deallocation, and only after native detach calls
ScreenBeginClose plus explicit RuntimeDeviceFinalize(HRESULT,Consumed). No owner
is freed unlessConsumed. Projected Asahi rodata creation now validates BO/map
before va access and retains retryable screen cleanup when deallocation fails.

TDD: map-only RED native-factory-map-only-red-20260914a failed only real shader/
state/framebuffer/clear/draw/flush operations. Final x64 sourceSHA
782a319ea3ae51c3d99407adf20a031f8d9a632d2fc250df6e6683f7f80098ca
executes shared factory scene through actual RuntimeRender/Signal and existing
30/126/9/29 consumer; pending marker and extra context returnBUSY; two-device
isolation and cleanup retries PASS. Failure matrix covers explicit finalizerBUSY,
clean alloc/map failure, retained creation token, and screen-deallocation retry.
ARM64 closure build/link0warnings/errors. Installed ARM64 UMD `/analyze` compiles
changedumd.c/runtime_device.c and all units0warnings/errors; evidence
AD04-native-factory-installed-umd-20260914a. Pipeline mask and exports unchanged.

## Verified pinned D3D10 frontend projection

Implementation7e119c8 projects pinnedMesa9aa1215 d3d10umd sources build-locally;
pinned checkout remainsunchanged and every overlay is gated by exact upstream
hash/one replacement. Adapter OpenAdapter owns metadata only; Device CreateDevice
uses privatefactory runtime callbacks/native context before CSO/default shaders.
Exports are test-renamed; installedumd.c/pipeline mask remainunchanged.

Actual DDI test uses Resource.cpp/InputAssembly/Shader/OutputMerger/Draw/Flush:
privateBGRA8 RT uses soleAPPLE_GPU_TILED modifier (uncompressed), boundedfull-view
ClearRTV maps to native pipe->clear only for the exact bound singleRT/noZS view,
and nullIA slots remain genuine unbound non-user buffers. All mismatch paths
reject before allocation/clear/Render; genericVB initial data remainsoriginalpath.
Actual frontend Clear/Draw(3)/Flush produces30refs/126relocs/9allocs/29objects,
137encoder/36496source/44952DMA, both KMD placements and orderedretirement.

TDD REDs localized upstreamOpenAdapter screen ownership, missingclear_render_target,
compressedgenericRT and dummyuserbuffers. Final sourceSHA
3694a9837ca816e93803ebcb00928d68ee3c49146c773bca3c2c7436b1a186e5;
x64exeSHA638a3d89d7dc05b52da2a7b03d33f26afdd51aa27e7f52d020d0359ea741e897
execution0. ARM64exeSHA214f0726b6c716495e700014b9162cd482782f2be9454abd025d2361f34bc4a8,
build/link0warnings/errors. Negative RT/allocator/IA/two-device/create-rollback/
destroy tests PASS. Pinned Meson trace/index generators and primary CSO/u_vbuf/
translate/rtasm closure are hash-recorded; no stubs/new renderer.

## Verified synchronous projected D3D10 destruction

Implementation7224f76 closes voidDestroyDevice lifetime offline. Runtime
DestroyKernelContext calls pfnDestroyContextCb exactlyonce whilecallbacksvalid;
success preserves exactQuiescedKernelContext and clears live handle/command/lists,
then retirement accepts only the same already-entered native capsule. Ordinary
ownership/newwork reject quiesced state. Retirement matches owner,generation,
context,request,fence,phase,active transaction and all allocation identities/holds;
it cannot create/recover a missingmarker or replayRender. Final consumption clears
the token and never destroys kernel context twice.

Projected DDI Destroy releases frontend state, blocks in kernel-context destroy,
retires ordered marker/consumer, then destroys native context/screen/runtime in
one invocation. Persistent context/marker/unlock/deallocate/create failures call
SetError with actualHRESULT before callback invalidation. Unresolved heap owner
is converted in-place after fullZeroMemory to a smaller CPU terminal record; no
new allocation can lose it and CloseAdapter only frees CPU records.

RED native-d3d10-destroy-red-20260914a: pendingdraw leftBUSY/noDestroyContext.
IdentityGREEN43667d5d...; HRESULT REDa753af4a... exposed threeBUSY substitutions;
GREEN1e4e4821... propagatesE_FAIL. Final x64 archive
SHA92aec7cf607744afe1e4a420bf346edd116429db79eeb62486420cc4f5d3d62c,
exeSHA6d68de4c558c8c2c478412df150e9a5bbbd42dc24ced163b206281463d1eb281,
execution0. ARM64 content manifest454256a9...,build/link0warnings/errors;
installed ARM64 UMD ClCompile/analyze0warnings/errors. Five terminal records,
four quiesced identities,zero callback/destructor pointers and obligationsPASS.
Installed export table/pipeline mask remainunchanged.

## Verified projected D3D10 capability/dispatch safety

Implementation756ae6d keeps101/101 ordinaryD3D10 slots and7/7 baseDXGI slots
nonnull in the contract executable but removes unsafe reachability/false success.
Query create/begin/end/getdata/predication/destroy,SO/DrawAuto,GS-with-SO,
ClearDepth,GenMips,Copy/CopyRegion/Resolve reject E_NOTIMPL before storage/hook/
resource/native mutation. All7 projectedDXGI callbacks reject through one helper
before flush/resource/output mutation; gamma/residency outputs remainunchanged.

CheckFormatSupport now reports only BGRA8 RT|BLENDABLE; XR_BIAS returns the sole
WDK NOT_SUPPORTED flag; all other formats0. Multisample quality is1 only for
BGRA8 samplecount1 and0 otherwise. This matches currentCreateResource subset and
does not claim vertex-format/sample/SRV/depth/shared/primary/present support.
Every negative call proves zero allocation/Render/Signal delta; actualfrontend
triangle/seconddevice/synchronousDestroy remainPASS.

REDs reproduced nullhook crashes forQuery/ClearDepth/Present/SO/GenMips/GS-SO,
silentResolve/DrawAuto and sixDXGI false-success plus five format mismatches.
Final x64 archiveSHA5f903f88e7f5f71e9876aa0cdf561d6c00a444fc8fa3112f61998b12c71ad483,
exeSHA08c9c9d4529870a40c74cd196d59fa347b1930ad52f8fd8fec5fa4786bc514dc,
execution0. ARM64 archive26774b4b...,exe1e38b9f...,build/link0warnings/errors;
content manifest73480793... equal. Installedumd.c/table/pipeline unchanged.

## Verified D3D10 EVENT query

Implementation1f89245 supports onlyD3D10DDI_QUERY_EVENT/MiscFlags0 over the
existing ScreenFences owner. Query storage holds magic,device/owner/generation,
issue serial,fence token,phase,error; noGallium query/nativebatch pointer. Begin
is identity-validated no-op. End detaches oldissue,collects completed detached
slots,flushes prior native work once,checks exact active Render/PostStatus,then
enqueues a distinct ordered query marker even for emptylocalstream.

GetData accepts only(NULL,0) or(BOOL*,sizeofBOOL),flags0/DO_NOT_FLUSH. Both poll
timeout0; pending preserves output+SetError(WASSTILLDRAWING),completion consumes
once/writesTRUE/caches,repeat has no callbacks. Reissue gets newtoken; pending
Destroy clears runtime Query while pointer-free slot survives for collection.
Device finalization closes all attached/detachedquery events; terminal records
count query markers through explicit boundedpackedfield. Otherqueries/predication
remainE_NOTIMPL; invalid/cross/stale operationsE_INVALIDARG.

RED EVENTCreate63afed63... andDraw-End6df30857...; final lifetimeGREEN includes
actualRender+separate draw/querymarkers,pending/completed/repeat,bothflagmodes,
70reissues,empty stream,pendingDestroy,deviceDestroy,stale/cross-device,enqueue
anddraw-flush failures with no falsequery marker. Final x64 archive
SHAe56a07e9c0fe0806a74335d56aca85ddaeb2cc23cdfc4eaf6c3b045f873a2b56,
exeSHAe27601d6cdce6c1fccfbf6fe18da681a8d44173621d71197a614c131ebb192cd,
execution0. ARM64 archive8b0acb9b...,exe51efe9ca...,build/link0warnings/errors;
installedARM64 UMD ClCompile/analyze0warnings/errors. Exports/pipeline unchanged.

## Verified resource-backed D3D10 constant buffers

Implementation7205b67 admits default resource-backed BUFFER/DXGI_UNKNOWN with
CONSTANT_BUFFER-only bind,16-byte aligned16..65536bytes,slot0 VS/PS only. Binding
validates exact frontend device/owner/generation/logicalsize before mutation;
null unbinds touchedslots. Default update accepts subresource0/no box and copies
exact logical bytes only after synchronous native flush+retire; failed ordering
leaves oldbytes/requestholds intact. No user buffer/rename/newallocator.

Stage-uniform capture validates actualubo_base=construction+offset andubo_size,
registers RoleConstant read interval and UniformAddress64 edge, preserves USC
edges and source/submission holds. Pinned agx_set_cbuf_uniforms now zeros all UBO
base/size entries before active mask population, closing stale unbound addresses.
Same CB VS/PS uses one identity; separate buffers remain separate references.

Test-only sidecar512 measured mandatory graphs without changing production seal:
baselineclear126,PS clear128/load130,VS load131,same VS+PS load133. All distinct;
combined command worstcase64refs/133edges7616<8192. Central v4 max is133; legacy64
and command8192 unchanged. Exact133 succeeds,134rejects,overlap precedescapacity.
Old HEAD6d4b2af validator explicitly rejects full current133-edge wire/result16
withouttruncation; evidenceAD04-v4-legacy-validator.

Final x64 archiveSHA554ceaf2afc3e8bd0eb9d0b59f9b582d9d29a6efb3168c35108e8cd880e09820,
exeSHAed28c44b84f5fbba6a9c8a6d3dd8ccd574b4b89c5214ce2cd437737e04e52640,
execution0. Actual same-CB clear31/131 and separateVS/PS load32/133 bothKMD
placements, immutable firstcommand/images/DMA,EVENT query/DestroyPASS. ARM64
archive5ad272b8...,exe9cd879c7...,build/link0warnings/errors. Installed UMD and
KMD ARM64 ClCompile/analyze0warnings/errors. Exports/pipeline unchanged.

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

## Verified indexed native batch slice
Implementation a8f338046227c8be8ad386837fe11f729b5fbce5 extends the same
pinned D3D10 producer and existing physical/patch-list path with one exact direct
R16 indexed triangle. Resource creation admits only an8-byte DEFAULT sole-index
buffer containing little-endian `{0,1,2,0}`; IA offset0/R16 and DrawIndexed
count3/start0/base0/oneinstance/triangle-list are the only accepted indexed
operation. Restart,indirect,user indices,R32,instancing and other shapes remain
rejected before submission.

The real `agx_vdm_draw` INDEX_LIST is24bytes. Version5 relocation kind15 owns
the packed40-bit index address from Encoder offset52 to one read-only RoleIndex
object. Producer validation preserves header bits and requires tag3,U16,
count3,instances1,start0,size2,address4-aligned/<2^40. Materialization copies
immutable Encoder/Index/Constant sources, patches request-scoped bytes only and
validates exact index contents. Old versions reject kind15. Measured final v5
bounds are34references/134relocations; v4 remains64/133, legacy64 and command
8192 unchanged. The pre-finalization census33/134 was superseded by the exact
post-scissor/native-root count34/134.

Fresh x64 actual producer closure PASS:
evidence/AD04-runtime-closure/native-d3d10-indexed-production-20260919n-x64/.
ArchiveSHA4a4d26b95c47b6ae3f5fadcf0956888762b96f12d0a804d225e801d4acfe048d;
EXESHAa88c2c9c3c570d5fba9a3d466ac0faf1087ea01b2f24042b686be1d83b988c8e.
Receipt:34refs,134relocs,13allocations,33objects,145encoder bytes,36656source
bytes; both placements,materializer,KMD plan,DMA patch,native roots,query marker
and ordered retirement PASS. Fresh ARM64 build/link PASS,executionNOT_RUN:
native-d3d10-indexed-production-20260919o-arm64; archiveSHA8c4c591078eee61a
582445a9f7cffe43d6fbe8e715085cea0c43c4c0d6def0b5,EXESHA
b841374aa9dde5e2861e6831876d1c63a00910dacfd3cec1e03e648da8232311.
Full pinned-WDK ARM64 UMD/KMD build/code-analysis/package685 PASS0warnings/
0errors; Inf2Cat errors/warningsNone. Evidence native-d3d10-indexed-analysis-
20260919q-arm64. Package was test-signed offline with the existing WDK test
certificate and was not staged,installed or hardware-run. Installed exports,
UMD project and pipeline mask remain unchanged.

## Verified installed native UMD link seam
Implementation b75336dfde5b5bf63459801bb06cb762b3ce28ab adds a default-off
`EnableNativeFrontend` package build. When selected it imports the exact
hash-recorded `NativeRuntime.props`, compiles the existing per-device factory,
owner,batch-adapter and relocation-owner units, and routes the sole exported
`OpenAdapter10_2` to the complete projected Mesa adapter family. Legacy and Mesa
adapter/device/resource private layouts are never mixed. The default package
path remains unchanged when the option is false.

The installed projection advertises exactly `D3D10_0_DDI_SUPPORTED`; THREADING
and 3DPIPELINESUPPORT return exact zero caps,wrong sizes returnE_INVALIDARG and
unknown caps returnE_NOTIMPL. Pipeline mask remains0. X64 exact `_2` route plus
the full Draw/DrawIndexed/lifetime closure executesPASS:
native-installed-umd-seam-20260919aa-x64. Fresh ARM64 closure/build/linkPASS:
native-installed-umd-seam-20260919af-arm64.

Conditional ARM64 package690 build/analyze0warnings0errors,Inf2CatNone and
Universal API validationPASS: native-installed-umd-seam-20260919ag-arm64.
DLLSHA9f688813bc67f55dfcdecd58c9da29ded69c8127f6b89d33bf7a7ef7c24c0405.
Export table contains exactlyOpenAdapter10_2. Link map proves reachable
MesaD3d10OpenAdapter10_2,AgxD3d10WindowsOpenAdapter/CreateDevice,
AgxWin32AsahiScreenCreateForWindows,AgxWin32AsahiBatchFinish and UMD composer.
Imports are system API-set UCRT/synchronization plusKERNEL32; no msvcrt or
vcruntime DLL dependency. Default-off package691 from the same source snapshot
also builds/analyzes0warnings0errors: native-installed-umd-default-20260919ah-
arm64. Both packages were test-signed offline only; neither was staged,installed
or hardware-run. The conditional remains disabled in the normal build and does
not authorize D3D runtime admission or a pipeline bit.

## Verified opened-resource DXGI Present bridge
Implementation d87c37987ab33c35e5a2e678c7f9ce4c79f70f85 reuses the
existing legacy allocation description,retirement queue and runtime Present
callback without casting Mesa private handles to legacy resources. A Mesa
Resource stores an opaque presentation record owned by its exact
AGX_D3D10_WINDOWS_DEVICE; that owner fabricates internal handles only while
calling the same AdmissionUmdOpenResource/DestroyResource/SubmitPresent logic.

Admitted slice: one runtime-opened allocation with exact valid A8R8G8B8
2560x1600/pitch10240/size0xfa0000 description,one subresource,nonzero kernel
allocation,FlipIntervalOne,flags0x2,no destination and one owning device.
Present sends the exact kernel allocation,authoritative kernel context and
opaque DXGI context to pfnPresentCb. Wrong flags,subresource or ordinary private
render target reject before callback. Destroy queues the existing hResource
retirement and device teardown proves exactly one deallocation. Present1,
SetDisplayMode,rotation,UMD-created displayable resources and desktop-sized AGX
render remain closed.

Fresh x64 full closure executionPASS: native-present-open-20260919d-x64,
archiveSHA87b481906354eea6370d85934704d7d132b44334f5ecf129a44f6b8d57121607,
EXESHA1d803e56c5b399232c3ec9c00c1dc7946e868149945854b810c557e40cac8295.
Fresh ARM64 build/linkPASS: native-present-open-20260919e-arm64. Conditional
package692 build/analyze0warnings0errors,Universal validationPASS,one export and
typed Present bridge symbols retained in map: native-present-open-20260919f-
arm64; DLLSHA07b03aa4e157ec00fc11ba259ac1041715f555f6cfe78ec835c60802b1e370a5.
Default-off package693PASS: native-present-open-default-20260919g-arm64. Neither
package was staged,installed or hardware-run; native option remains default-off
and pipeline mask0.

## Verified UMD-created primary and SetDisplayMode bridge
Implementation 8c9c58e5d0329fa382376c278b3cab58b013e766 extends the same
typed presentation owner to a D3D10-created displayable primary. The Mesa
resource keeps only its opaque presentation record; the Windows owner calls the
existing AdmissionUmdCreateResource/DestroyResource and SetDisplayMode paths,
so allocation identity, callback ownership and retirement remain in the
physical/patch-list architecture.

Admitted slice: one BGRA8 2560x1600 texture2D primary, pitch10240,
size0xfa0000, one mip, one array slice and sample count1. Creation reaches the
existing runtime allocation callback and records kernel allocation0x775;
SetDisplayMode submits that exact allocation on the owning device. Wrong
subresource, cross-device or ordinary private resources reject before callback.
Teardown proves independent retirement of both the runtime-opened resource and
the UMD-created primary. Present1 and identity rotation remain closed rather
than being inferred from base D3D10 Present.

Fresh x64 full producer executionPASS: native-present-created-20260919i-x64,
archiveSHAcefc0fe1cad938bda7026778f1a6a2b6c3e64e366080a088cecce8d0bb619afe,
EXESHA45f34ce4f8a8c63a3f4978ba80062e5dab51cf02522a56097d9c9453ee89f48b.
Fresh ARM64 build/linkPASS: native-present-created-20260919j-arm64,
archiveSHA582664033e3a5a5afe550bcdf7cb6846a7fcf7e63fa6a3e9e71a316546685382.
Conditional package694 build/analyze0warnings0errors, Universal validationPASS,
one export and typed Create/SetDisplayMode symbols retained in the link map:
native-present-created-20260919k-arm64;
DLLSHA82c9bdbac8382d974c167ad4d882c42aa3f86af408b18a627ff2c58f6764ac8b.
Default-off package695PASS: native-present-created-default-20260919l-arm64.
Neither package was staged, installed or hardware-run; native option remains
default-off and pipeline mask0.

## Verified desktop-primary native render and Present lifecycle
Implementation d25eff54a13440955c0f3cae6713823a2829829d registers the
existing runtime-created primary allocation as one borrowed ScreenBuffer and
wraps that same token in the native Asahi BO/resource graph. It does not create
a scanout copy, second allocation owner or second composer. The exact
2560x1600 BGRA8 linear allocation, pitch10240 and size0xfa0000 is the color
attachment seen by BG/partial/EOT capture and the destination allocation seen
by the physical/patch-list KMD path.

The actual projected D3D10 producer now executes Create primary -> RTV/state ->
clear/draw -> native batch finalization -> immutable capture/materialization ->
UMD composer/pfnRenderCb -> KMD plan/DMA/native roots -> DXGI Present. The x64
execution proves destination kernel allocation0x775, desktop destination bytes,
two independent KMD placements and deferred retirement. D3D DestroyResource
drops its owner reference without blocking; an in-flight Gallium reference
keeps the borrowed allocation registered until the ordered batch retires, then
the existing primary retirement releases the runtime hResource.

The pre-change executable RED built and linked but faulted before Render because
the presentation resource had no Asahi backing:
native-primary-render-red-20260919a-x64. Intermediate gates then exposed and
closed the old 16x16-only backend binding and native-root routing guards plus an
incorrect synchronous teardown wait. Final x64 actual executionPASS:
native-primary-render-final-20260919k-x64,
archiveSHAea511fcc391dd98a347958dc6d9b0b5163a58cff28bf7a3939deabfee027983f,
EXESHAd7c341ecf1fd067bbb30ee3108a55ae263e114d25f8cd98edfa6353dcad5027a.
Final ARM64 closure build/linkPASS:
native-primary-render-final-20260919l-arm64,
archiveSHAf689d88d5eebd044bc89b4705f7c2cbffaee5c5eb91a692dca8d798aa259f030.

Conditional package697 build/analyze0warnings0errors and Universal API
validationPASS: native-primary-package-20260919m-arm64;
DLLSHA24141827239944b2939ca06325ef7a1d8afaf6e015b5f53a09872de6cbc2717b.
Its link map retains the primary import BO/resource and deferred presentation
collector. Default-off package698PASS:
native-primary-default-20260919n-arm64. Neither package was staged, installed
or hardware-run; conditional native option remains default-off and pipeline
mask0.

## Verified native presentation identity rotation
Implementation 3f39a1cb604a0b980bcc044ea59911e3e59b4008 implements the
base-DXGI RotateResourceIdentities callback for typed native presentation
resources. After the existing ordered retirement gate it rotates the allocation
identity atomically across the presentation record, its retirement record and
the authoritative borrowed ScreenBuffer slot. Native BO tokens and Asahi
resource objects remain stable; subsequent capture therefore resolves the
rotated kernel allocation without a copy or secondary handle table.

The executable rotates the runtime-opened and UMD-created desktop resources in
both directions, then performs the same real desktop draw/Present proof and
verifies original allocation0x775 and retirement. X64 executionPASS:
native-present-rotate-20260919o-x64,
archiveSHA4e7ed2fd0dd2649cb88a6ed46524c5a6e82225058b49b18d4b5b53d34288c2d7.
ARM64 closure build/linkPASS: native-present-rotate-20260919p-arm64,
archiveSHA27060642dd2d0527fda56435847e57b825afa6a425336357a72a633df3657dbb.
Conditional package699 build/analyze0warnings0errors and Universal API
validationPASS: native-present-rotate-20260919q-arm64;
DLLSHA191c090858ca62b520eaeb63c11a257e2448edee91e3374c063802fc8624fe8e.
It was not staged, installed or hardware-run; native option remains default-off
and pipeline mask0.

## Exact next target
Continue production integration from
investigation/agent_tasks/AD04-NATIVE-RUNTIME-ACTIVATION-NEXT.md. Existing
private factory,pinned frontend actual DDI path,EVENT query,constant-buffer and
minimum indexed IA/DrawIndexed slice are complete offline, and the same closure
now has a conditional installed-UMD link proof. Next complete and update the
exact D3D10_0/FL10_0 machine-readable inventory. Synchronous Destroy lifetime is
closed, but this bounded draw subset does not authorize pipeline level0 or
enabling the installed native option by default. Map every required callback,
format,resource,state and DXGI path to implemented or fail-closed status and
close the smallest truthful atomic table. Advertise a pipeline or enable native
packaging by default only after all required companion invariants pass. Keep
instancing,other query types,application textures/depth and unsupported
topologies rejected until independently proven. The typed UMD-created
displayable allocation, desktop-sized native render, SetDisplayMode, Present
and retirement chain is now closed offline. Next finish the exact
D3D10_0/FL10_0 machine-readable admission inventory and implement the smallest
truthful missing companion callbacks needed before setting a pipeline bit or
enabling the native frontend by default. Keep Present1 and
RotateResourceIdentities closed unless the selected base D3D10 runtime actually
invokes and requires them; do not upgrade the interface to manufacture
coverage.
Do not use another KMT helper or adapter-global rendering context.
Installed pipeline mask remains0 until the selected DDI/FL contract is truthful.
Standard Present/shared resources/redirection/DWM and desktop stability remain open.

## Verified standard DXGI Blt native producer boundary (2026-09-20)
The projected D3D10 frontend now implements resource priority/residency callbacks
and the selected base-DXGI Blt: two distinct allocation-backed linear BGRA8
2560x1600 primaries, whole-source/full-destination, identity rotation and the
exact Present flag.  The real Mesa util_blitter graphics path performs the
texture-sampling draw; unsupported forms fail before native submission.

The native textured command is ABI version 6.  Its final complete graph is
exactly 36 references and 136 relocations: the pre-finalization 35-reference
census gains the mandatory scissor root.  TextureReference is mandatory,
IndexReference is optional, and VdmIndexBufferAddress40 remains version-5-only.
The source texture remains an external physical allocation resolved through the
existing KMD local-segment path; it is not copied into the overlay arena.
Vertex capture now materializes only the exact three-vertex attribute span,
instead of the lazy uploader's full 1 MiB backing BO.  Present flush scans real
active batch slots, including a util_blitter batch detached from ctx->batch,
before calling the standard runtime Present callback.  Retirement remains tied
to the same ordered completion marker.

Fresh x64 full producer execution PASS:
evidence/AD04-runtime-closure/native-textured-v6-final-20260919bm-x64/;
source archive SHA-256
adcbbd5e1822d65c6da8c6731941c38842b1cef219a7a85db65c1110ab695dba,
EXE SHA-256 52799c32ddc71ed47db631f6557703beaf5b7c37be89305476b56070786f4309.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-textured-v6-final-20260920bn-arm64/;
source archive SHA-256
a07eebcf15c2d2cf076605701a42abb5c97e8340c5900dcb26bab3c81e17ad10.
The deterministic relocation/capture gate is GREEN for x64 execution and
ARM64 build-link under
evidence/AD04-runtime-closure/native-v6-reloc-final-20260920bo/.

The machine-readable D3D10_0/FL10_0 inventory now records priority, residency
and Blt evidence.  It has 121 mandatory rows, 7 implemented/tested rows and 114
unresolved rows.  Installed pipeline mask remains 0; no native package was
staged, installed or hardware-run.  The next causal boundary is source-first
reconciliation and implementation of the remaining mandatory FL10_0 admission
contract, followed by final ARM64/package/sign/hash gates and preregistration.

## Preserved hardware/recovery boundary
Currentordinary377/392 GPU-visible recovery is healthy,brokerdisabled,oneinert
APPL0002Code28,noAppleAgx package/service/module/files/signer. Exacthealth evidence
main-root EXP684/final-health.json at2026-09-14T08:17:54Z:8CPU,SSH/NVMe/xHCI/
keyboardOK,no currentboot41/1001/129.
AppleInput71CD0A trust anddriver preserved;TESTSIGNINGYes. Operator's SACchange
was verified; assistant didnotchangeSAC/BCD. EXP683-specific1F20 trust removed.
SSH pavel@192.168.1.37, key ~/.ssh/air, STRICT known-hosts main-root
.local/experiments/EXP641-standard-present/air_known_hosts. Defaultknown_hosts
isstale;do notchangeit. ActiveEXP684 ordinary-restore launcher ownsproxy/vUART.
Emergency377/385hiddenDISKboot is proven recovery exception only; EXP378WinPE
contains automatic oldcleanup,so preferdocumentedhiddenDISKboot for evidencefirst.
Emergencyartifacthashes: main-rootEXP683/emergency-recovery-hashes.json.
Beforehardware recordhypothesis/commands/source/packagehashes inEXPERIMENTS,
checkbothcontrolplanes, preserveknownrecovery,collectevidence thenexactcleanup.
No routineGPUhiddenboot. Neverrequestphysicalaction beforebothcontrolplanechecks.

## Verified truthful zero gamma capabilities
Implementation 2e7963d3db0790e2b09d9e49ae159cb2c35f7683 returns a successful
zeroed DXGI gamma capability structure. This matches the existing KMD display
contract, which accepts only the default gamma ramp; no programmable control
points, scale or offset are advertised. X64 actual producer executionPASS:
native-gamma-caps-20260919r-x64. ARM64 closure build/linkPASS:
native-gamma-caps-20260919s-arm64. Conditional package700 build/analyze and
Universal API validationPASS: native-gamma-caps-20260919t-arm64;
DLLSHA10edc88c41c4e3625315f624272d7c97f524afa23f8ab0e6f67b8a5bec9a02cd.
It was not staged, installed or hardware-run; native option remains default-off
and pipeline mask0. Next inventory targets are resource priority, residency and
Blt; none may return success until their actual ownership semantics exist.

## Verified application PS texture binding group (2026-09-20)
The projected D3D10 frontend now keeps shader-resource views and sampler states
owned by their creating device and admits the exact minimum fragment slot-zero
create/bind/unbind/destroy lifecycle.  Cross-stage, multi-slot and extended
forms remain fail-closed.  This reuses Mesa's existing SRV/sampler objects and
the already proven typed TextureReference path; it adds no allocator or
composer.  X64 full execution PASS:
evidence/AD04-runtime-closure/native-texture-ddi-lifecycle-20260920bq-x64/;
source archive SHA-256
79feabc004eefbb8d0b45e0f81be238825657763a16d0d8c528f88587d76e4bc.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-texture-ddi-lifecycle-20260920br-arm64/;
source archive SHA-256
b30045146ed2b74a921c3914d186ece7b24f752f18712cceed7b4b4dfaf9d49a.
The machine-readable FL10_0 inventory is now 21 of 121 implemented/tested,
with 100 mandatory rows unresolved.  Pipeline mask remains zero; no package was
staged, installed or hardware-run.

## Reconciled D3D10_0 callback inventory (2026-09-20)
The machine-readable inventory was reconciled against direct successful calls
in the final x64 projected-runtime executable rather than callback pointer
presence.  Adapter, resource, RTV, element-layout, blend/depth/raster state,
VS/PS shader, constant-buffer, IA, Draw/DrawIndexed, EVENT query, format/MSAA,
flush and device teardown rows now carry exact execution evidence.  The matrix
is 73 of 121 mandatory rows implemented/tested, with 48 unresolved.  The
remaining rows are actual contracts: dynamic/staging mapping, instancing,
geometry stage, copy/resolve, depth views, predication/mips, hazards/counters,
required feature formats/limits, translation/winsys and cross-process sharing.
Pipeline mask remains zero.

## Verified small D3D10 companion callbacks (2026-09-20)
Valid resource/SRV hazard notifications preserve Mesa's intentional no-op
semantics; counter info reports zero device counters and CheckCounter rejects the
unsupported namespace; device-function relocation is pointer-independent; the
required 1x1 text-filter size is accepted.  X64 executable evidence:
evidence/AD04-runtime-closure/native-companion-ddi-20260920bs-x64/; source
archive SHA-256
55c02ae78ac41a4b52f5ebade0cd65d2a01f6eb86fa50411af4b30771c7a0192.
The machine-readable inventory is 79 of 121 mandatory rows proven, with 42
unresolved. Pipeline mask remains zero.

The same final executable also directly proves the pinned DXBC-to-TGSI
frontend, TGSI-to-NIR-to-AGX backend and device-owned Windows winsys aggregate
rows.  Inventory is therefore 82 of 121 mandatory rows proven, with 39
unresolved; pipeline mask remains zero.

## Verified D3D10 primary copy callbacks (2026-09-20)
ResourceCopy and full-box ResourceCopyRegion now route two distinct typed
presentation resources through the existing native textured Blt lifecycle.
Subresource, offset, partial-box and non-presentation forms remain fail-closed.
Each callback has its own x64 Render/Present/retirement transaction:
evidence/AD04-runtime-closure/native-resource-copy-region-20260920bu-x64/;
source archive SHA-256
d5412e4899e8f717cc38ec2416e7f20751b2d51c303d41b18a4c4c101c674f2b.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-resource-copy-region-20260920bv-arm64/;
source archive SHA-256
dea25abf38c37149649cdaf79be528b0d7987466b6ac3d5d70f6aba01087cb63.
Inventory is 84 of 121 mandatory rows proven, with 37 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

Generic ResourceUpdateSubresourceUP is now directly proven for the exact
device-owned constant-buffer contract after prior draw retirement. X64 full
execution PASS: evidence/AD04-runtime-closure/native-resource-update-20260920bw-x64/;
source archive SHA-256
0875eccf48e6dda1299936147e9af5a80b6983d1bd818db973910aef3d4595d7.
Inventory is 85 of 121 mandatory rows proven, with 36 unresolved; pipeline mask
remains zero.

## Verified dynamic IA and resource mapping (2026-09-20)
Resource records now retain their D3D usage/bind contract.  The shared map owner
admits only a device-owned dynamic IA buffer at subresource zero with exact
WRITE_DISCARD or WRITE_NOOVERWRITE semantics, flushes prior native ownership,
and rejects invalid state before mapping.  The same validated implementation is
called directly through DynamicIABuffer, DynamicResource and generic Resource
map/unmap table entries.  The mapped vertex bytes feed the subsequent real draw.
X64 execution PASS:
evidence/AD04-runtime-closure/native-dynamic-map-aliases-20260920ca-x64/;
source archive SHA-256
6f1993e21e105fd85d2fdd63b41a2caf98cffc894638acf45b836eae520a580f.
ARM64 product closure/client build-link PASS:
evidence/AD04-runtime-closure/native-dynamic-ia-20260920bz-arm64/;
source archive SHA-256
019c02e7781799b9ffbb3d0cb1fc1af8dc400883decec5a9418e003e569ed638.
Inventory is 92 of 121 mandatory rows proven, with 29 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

## Verified dynamic constant-buffer mapping (2026-09-20)
The resource owner admits exact device-owned dynamic constant buffers for
WRITE_DISCARD only; no-overwrite remains rejected.  Mapped bytes feed the
subsequent VS constant binding and real native draw.  X64 execution PASS:
evidence/AD04-runtime-closure/native-dynamic-constant-20260920cb-x64/;
source archive SHA-256
7126e9acb5a462555444544aaa813ab84230171afdc38f8119ce77d320c76f89.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-dynamic-constant-20260920cc-arm64/;
source archive SHA-256
a2d022cef29c3aa015f4db04203ef3116b7f4715f81e4ad83dcd9b662f128514.
Inventory is 94 of 121 mandatory rows proven, with 27 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

## Verified staging buffer mapping (2026-09-20)
The shared resource map owner now admits bindless staging buffers for exact
READ, WRITE or READWRITE modes while preserving dynamic-buffer restrictions.
A deterministic write/unmap/read round trip verified the authoritative Windows
BO bytes.  X64 execution PASS:
evidence/AD04-runtime-closure/native-staging-map-20260920cd-x64/; source archive
SHA-256 62fe7cd00df78ee071c70aa1b0d3e6fe3bf07f3ed0a75dd424a7b92b7c94ef2a.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-staging-map-20260920ce-arm64/; source archive
SHA-256 73175af3082b44c5d7127683331d4e9cf0d75db11292f237815049f15f476497.
Inventory is 96 of 121 mandatory rows proven, with 25 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

ResourceIsStagingBusy now has direct evidence that a matched-unmapped staging
resource reports not busy without mutating ownership. X64 full execution PASS:
evidence/AD04-runtime-closure/native-staging-busy-20260920cf-x64/; source archive
SHA-256 8a8758c8f5800cdc29497f58ee1abe7df4c1d23b44d4f22885a6c68d8934b810.
Inventory is 97 of 121 mandatory rows proven, with 24 unresolved; pipeline mask
remains zero.

## Verified VS/GS stage binding callbacks (2026-09-20)
Device-owned constant buffers, SRVs and samplers now admit exact slot-zero
bind/unbind lifecycles for VS and GS as well as the previously proven PS path.
Invalid ranges/counts remain fail-closed, and every GS binding is removed before
draw; geometry execution remains unadvertised. X64 execution PASS:
evidence/AD04-runtime-closure/native-stage-bindings-20260920ch-x64/; source
archive SHA-256 73968da0df15b884e6849fbe7ed9c90906ad5790c0cc1a4c0baf9b01fd4a1eb5.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-stage-bindings-20260920ci-arm64/; source
archive SHA-256 43aa0e9504a4d8b25f06d79d01c2854d9ca263f2b6dce7ab21feaf26df9014f7.
Inventory is 102 of 121 mandatory rows proven, with 19 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

## Verified one-instance D3D10 entrypoints (2026-09-20)
DrawInstanced and DrawIndexedInstanced now admit exactly one instance at start
instance zero and delegate to the already proven non-indexed/indexed native
producer contracts.  Multi-instance and nonzero-start forms remain fail-closed
and are still covered by aggregate resource-limit semantics. X64 execution PASS:
evidence/AD04-runtime-closure/native-instanced-entry-20260920cj-x64/; source
archive SHA-256 14c8215e2b209e42b59fc8e820968dfec4792e3721cfeccd304350b2aeab4c7c.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-instanced-entry-20260920ck-arm64/; source
archive SHA-256 1b0af6d2105d13cd1ea0129f8c6688c3fa1ddc3e6ac306fecfa4c44f71de17f9.
Inventory is 104 of 121 mandatory rows proven, with 17 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

## Verified no-work D3D10 companion forms (2026-09-20)
Null predication, zero-target stream-output unbind and GenMips on a one-level
same-device SRV now complete without backend mutation.  Non-null predicates,
active SO targets and multi-level mip generation remain fail-closed and are not
counted as feature support. X64 execution PASS:
evidence/AD04-runtime-closure/native-noop-companions-20260920cn-x64/; source
archive SHA-256 7fbcbff1de5ab8dfbd38154306c80b208fc36aec9f57afea61c94ac77e96ba81.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-noop-companions-20260920co-arm64/; source
archive SHA-256 a52e535c16f12d68a70d08056e7437fa9a1d86e40e1131316817c11685573c97.
Inventory is 107 of 121 mandatory rows proven, with 14 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

## Verified depth-stencil view lifecycle (2026-09-20)
A device-owned 16x16 D32 resource now has direct CalcPrivate/Create/Destroy DSV
lifecycle evidence, followed by resource teardown with no render side effects.
Depth clear and depth attachment execution remain unresolved. X64 full execution
PASS: evidence/AD04-runtime-closure/native-depth-view-20260920cp-x64/; source
archive SHA-256 09a7d3da61f52e1e804f17180bb323b6718d0a4af418ef729f10ec8623381ba1.
Inventory is 110 of 121 mandatory rows proven, with 11 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

CalcPrivateGeometryShaderWithStreamOutput already has direct executable size and
storage-preservation evidence from the GS-with-SO rejection matrix. The size
callback is now recorded separately from the still-unimplemented Create path.
Inventory is 111 of 121 mandatory rows proven, with 10 unresolved; pipeline
mask remains zero.

## Verified single-sample/no-MSAA contract (2026-09-20)
The selected FL10_0 contract advertises no multisample count above one. BGRA8
sample count one reports one quality level; counts two and four report zero;
multisampled resource creation is rejected before allocation; direct Resolve is
deterministically rejected without side effects. Resolve is therefore a proven
unsupported callback under a coherent no-MSAA contract, not implemented MSAA.
X64 execution PASS:
evidence/AD04-runtime-closure/native-no-msaa-contract-20260920cq-x64/; source
archive SHA-256 4c6edc5b7119fa11e2f77c72fdde119da27f76cd7cb370a3d1bfddccab92fd3c.
Inventory is 112 of 121 mandatory rows proven, with 9 unresolved. Pipeline mask
remains zero; no package was staged, installed or hardware-run.

## Verified D32 depth clear and depth-attached native batch (2026-09-20)
The projected D3D10 frontend now advertises the exact single-sample D32 format,
creates an uncompressed Apple-tiled private depth resource, binds its DSV,
executes a real depth-only clear and emits a depth-attached native draw. Native
command ABI version 7 carries the external read/write depth allocation plus the
producer's exact depth stride, ZLS control, pixel dimensions and clear values.
The request-local KMD materializer resolves the physical/local-segment depth
address without copying the resource into the overlay arena and routes every
G13/V13_5 initial/reload depth duplicate, including the load-only BGOBJVALS
0x400 bit. Unsupported stencil, compressed depth, multisample and combined
texture/depth forms remain fail-closed.

Fresh x64 real producer execution, two-placement materialization, DMA/native
route and retirement PASS:
evidence/AD04-runtime-closure/native-depth-v7-final-20260920dc-x64/;
source archive SHA-256
3dec03e91d0a6d9083130ddc583136528b69278a8600f2fc212890ae1bd0b545,
EXE SHA-256 cc29fcd76830d8850c964d001258bd832bab5bb8178939cdea570372faf9a89a.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-depth-v7-final-20260920dd-arm64/;
source archive SHA-256
44aef59e62ad221de29abdd73b956ae4c9ffa142be8b85ef85cff520e41816a2,
EXE SHA-256 9edcbc5bc8132cc553aab944e9d39aa4d710d6ed94cfbffdd0777060de8c538f.
The frozen machine-readable inventory is now 113 of 121 mandatory rows proven,
with 8 unresolved. Pipeline mask remains zero; no package was staged, installed
or hardware-run. The next causal group is real geometry shader/stream-output
execution and DrawAuto over the existing native producer/submission lifetime.

## Verified cross-device shared-resource identity and lifetime (2026-09-20)
The existing D3D10 shared create/open path now has direct cross-device evidence.
A shared primary allocated by one runtime device was opened by a second runtime
device using the exact same kernel allocation identity and validated allocation
description. Destroying and flushing the creator released only its runtime
resource; the peer object retained the same allocation identity and remained
valid until its own independent destroy/flush. This uses the existing runtime
allocation/resource ownership and retirement queue without a parallel sharing
registry or allocator.

Fresh x64 full runtime execution PASS:
evidence/AD04-runtime-closure/native-cross-device-share-20260920de-x64/;
source archive SHA-256
e723f61648e8ba493b2395382502206872a496bd28069dc1af53f3c6fc190898,
EXE SHA-256 a43819919d8a3224262d3728f6ce72ebf893f7db28c316c69b94ffd86f5a4d98.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-cross-device-share-20260920df-arm64/;
source archive SHA-256
f36c3675499dec67615c8bd0e23500f74cf4bcf1ce5eefec27a4cafbea9cfc4a,
EXE SHA-256 cc66471de45d20d1f37261de2d86531b9643c835d3ce1d814dd4939bbf9565c2.
The frozen inventory is 114 of 121 mandatory rows proven, with 7 unresolved.
Pipeline mask remains zero; no package was staged, installed or hardware-run.
The current causal target is the source-derived G13/V13_5 compute work object,
compute queue and CP-to-TA dependency required by the real Asahi GS producer.

## Verified source-derived G13/V13_5 RunCompute image foundation (2026-09-20)
The frozen GS/stream-output group requires the real Asahi mixed CDM-to-VDM
producer.  The first request-owned KMD foundation now builds the exact packed
G13/V13_5 RunCompute image from validated inputs: command tag/counter/VM slot,
notifier, the 0x7fa0-byte compute preemption allocation and all five derived
pointers, CDM start/end, USC base, optional helper fields, microsequence root,
and CP stamp/event lineage.  It rejects malformed or inconsistent inputs before
mutating destination bytes.  A compiler layout mirror asserts the selected
pinned Asahi Rust offsets and total 0x31c-byte image.

Fresh x64 full runtime execution PASS:
evidence/AD04-runtime-closure/native-g13-compute-work-20260920dj-x64/;
source archive SHA-256
592f6a962c127421aaf5926e0d373988995fcd8c4c4940dc04ba10adfebfa060.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-g13-compute-work-20260920dk-arm64/;
source archive SHA-256
de1db2efa04949806c83b738565cba805bfe49136d1856ad89d02a6053b4eb02.
This is an internal prerequisite and does not close a frozen inventory row.
Inventory remains 114 of 121 and pipeline mask remains zero.  Next is the
source-derived StartCompute/WaitForIdle/FinalizeCompute/RetireStamp
microsequence and a distinct compute queue/stamp owner before TA/3D.

## Verified minimal G13/V13_5 compute microsequence (2026-09-20)
The source-derived RunCompute foundation now builds its exact minimal firmware
microsequence: StartCompute, WaitForIdle on the compute pipe, FinalizeCompute,
and RetireStamp.  All inner RunCompute pointers, queue-info/statistics roots,
notifier state, event generation/sequence, counter, firmware stamp and restart
branch are request-derived and validated before destination mutation. Optional
timestamps and attachments remain absent in this selected form.

Fresh x64 full runtime execution PASS:
evidence/AD04-runtime-closure/native-g13-compute-microseq-20260920dl-x64/;
source archive SHA-256
8d1938340477e6d46e1e01ab47f12895ae64fcaf534c2bd98578c74b2014b077.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-g13-compute-microseq-20260920dm-arm64/;
source archive SHA-256
68fdf9c734c34a83fda5d8e6e376c28e57e4ae3cc559cadb9d8021104b0aee6a.
This remains an internal GS prerequisite: inventory is 114 of 121, pipeline
mask is zero, and no package or hardware run occurred. Next is the distinct
compute queue/event/stamp phase that must complete before existing TA/3D
publication under the same Windows transaction.

## Verified CP-before-render queue phase (2026-09-20)
The existing G13 queue runtime now optionally owns a distinct compute binding.
When enabled, one request publishes and runs the CP work object first. TA/3D
events cannot complete it: only the exact compute event, expected stamp and done
pointer move the request to ComputeComplete. The same fence owner must then
explicitly begin the existing TA/3D phase. Timeouts, cancellation, quiesce and
reset include the compute queue. HasCompute defaults false, preserving every
previous render-only publication and completion contract.

Fresh x64 full runtime execution PASS:
evidence/AD04-runtime-closure/native-g13-compute-queue-20260920dn-x64/;
source archive SHA-256
37927c8013369774a570e5596e559d28019b65d694ef5b8a5ca07c270435a4e6.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/native-g13-compute-queue-20260920do-arm64/;
source archive SHA-256
dafe10ec311bbc5df96040fa8b58875ee32b43b6aa4f2698819b2531efd662d5.
This is an internal GS prerequisite. Inventory remains 114 of 121, pipeline mask
zero, and no package or hardware run occurred. The next causal target is the
real Mesa mixed graphics batch: capture and finalize its CDM encoder and shared
geometry objects into the existing request before enabling GS DDIs.

## Verified real mixed GS producer through KMD materialization (2026-09-20)
The projected D3D10 frontend now creates and binds a real SM4 passthrough
geometry shader and executes the upstream Asahi mixed graphics lifecycle.  One
request owns ordered VDM and CDM encoder roots, the real GRID system-value
table, geometry parameter/output-index shared objects and all native shader,
uniform, PPP, attachment and BG/EOT references.  ABI version 8 carries a
32 KiB command envelope, two exact encoder roots and the source-derived
32-bit GS-copy index edge into the same read/write shared geometry allocation.

The Windows flush path finalizes the 72-byte CDM root before restoring and
finalizing VDM.  The existing UMD composer and KMD validator/materializer admit
the immutable request at two placements; the render root stays in the existing
template encoder object while the compute root receives a distinct mapped
general span.  Retirement clears GS-only GRID/table/parameter state before the
next independent batch.  Unsupported GS combinations still reject before
submission.

Fresh diagnostic-free x64 real producer execution PASS:
evidence/AD04-runtime-closure/mixed-v8-real-gs-final-20260920fd-x64/;
source archive SHA-256
32924fe14140fb1473f3e5e0b2112893a97d12165e7458449f48226d299361cb,
EXE SHA-256 9e7a66e3a707838c852f5e8613cbcc2e4adf8956702a074ca6c808bdb25d5936.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/mixed-v8-real-gs-final-20260920fe-arm64/;
source archive SHA-256
27c5bd3d56081b96dbba42944d37a665a3cf20d4d83211c94238985aa297992d,
EXE SHA-256 a4d487d1ae00b08b5af6792890b2836618fca9a1f0d0eb3f5f0a64e8a0a5a6de.

This closes the real producer/materializer boundary but not production compute
queue publication, so the frozen inventory deliberately remains 114 of 121 and
the pipeline mask remains zero.  The next causal target is to build the existing
G13 compute work and microsequence from the materialized CDM root, publish it on
the distinct compute queue, accept its exact completion, and only then publish
the existing TA/3D render work under the same fence.

## Verified provider compute-before-render transition (2026-09-20)
The production G13 queue provider now preserves the already built render
submission while publishing its optional Compute work first.  A mixed request
enters a distinct ComputeSubmitted provider phase; unrelated TA/3D events cannot
open render.  Only the exact compute event plus expected stamp and done pointer
lets the existing queue runtime transition through BeginRenderAfterCompute and
publish the unchanged D3 then TA work under the same fence.  Stop, reset and
timeout paths include the compute-submitted phase, while HasCompute false keeps
the prior render-only behavior unchanged.

The existing provider host test now exercises the complete transition and
passes under Windows LLVM /W4 /WX.  This is an internal GS prerequisite and
does not yet raise the frozen inventory: it still remains 114 of 121 with
pipeline mask zero.  The next causal target is for the production backend
BuildSubmission path to build the G13 RunCompute image and microsequence from
the materialized v8 compute encoder and populate the provider's Compute
prepared ranges and GPU work addresses.

## Verified CL_1 compute transport ownership (2026-09-20)
The platform provider now binds the source-defined group-1 compute command
channel: m1n1 ChannelInfo index 5 (CL_1), RunCmdQueue type 2 and doorbell 6.
Its state/ring mappings are validated alongside TA_1 and 3D_1, and RunCompute
messages can no longer be sent through a render channel.  The provider event
lease now owns three distinct events (TA, D3 and Compute) atomically; release
and exhaustion remain fail-closed and reuse all three slots together.

Strict Windows LLVM /W4 /WX builds and executions of both the event allocator
test and full platform-provider test PASS.  Inventory remains 114 of 121 and
pipeline mask zero.  The next causal target is to add mapped compute
QueueInfo/ring/pointers/stamp storage to the existing render-shared-memory
owner, then enable HasCompute with the owned compute event and CL_1 transport.

## Verified mapped compute queue memory ownership (2026-09-20)
The existing render-shared-memory owner now allocates, maps, validates and
rolls back four additional context-0 objects for compute: CommandQueueInfo, the
firmware work-address ring, combined CPU-write/GPU-done/stamp storage and a
64 KiB request sidecar.  Production queue-arena placement assigns them
non-overlapping addresses inside the existing shared arena after the render
queue objects.  BuildQueueConfig enables HasCompute only when all four objects
are mapped with the exact capacities and derives every CPU/GPU pointer from
their owned mappings.

The full render-shared-memory test and platform-provider test compile with
Windows LLVM /W4 /WX and execute PASS.  Inventory remains 114 of 121 and
pipeline mask zero.  Next is to partition the sidecar into the source-derived
0x7fa0 preemption allocation, 0x31c RunCompute work, 0x1f0 microsequence and
small notifier/statistics roots, then fill Submission.Compute from the
materialized v8 CDM encoder.

## Verified production mixed compute-to-render submission (2026-09-20)
The physical KMD path now owns the complete source-derived compute lifecycle.
Render shared memory creates a firmware-compatible CL_1 CommandQueueInfo by
copying the proven common queue template and replacing only its compute
pointers, ring, shared job-list and private 0x2c18 GPU buffer roots.  The 64 KiB
sidecar contains the exact 0x7fa0 preemption allocation, 0x31c RunCompute work,
0x1f0 StartCompute/WaitForIdle/FinalizeCompute/RetireStamp microsequence and
request-owned statistics, notifier and firmware-stamp roots.

AdmissionExternalBuildJob derives CDM start/end from the materialized v8
compute encoder, uses the request fence for monotonic counter/stamp/sequence
lineage and publishes one already-relocated sidecar range plus one RunCompute
work address.  The production provider submits it on CL_1, accepts only its
owned compute event/stamp/done pointer, then publishes the unchanged D3 and TA
work under the same fence.  Render-only command versions remain unchanged.

Fresh x64 full real-producer execution PASS:
evidence/AD04-runtime-closure/mixed-v8-compute-provider-20260920ff-x64/;
source archive SHA-256
93c5c959704abc49c2d553de7b17ac740342d673508537ade9bda5d1058a9cf3.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/mixed-v8-compute-provider-20260920fg-arm64/;
source archive SHA-256
f7d971e1e2099394b0d86f934de73fb831217728628454d8b26ef1e4995705e1.
Strict Windows LLVM provider, render-memory and platform tests PASS.

Pinned-WDK ARM64 package701 build/code analysis PASS with 0 warnings and
0 errors; Universal API validation and Inf2Cat pass with no warnings/errors:
evidence/AD04-runtime-closure/mixed-v8-compute-kmd-20260920fh-arm64/.
Package hashes: SYS
7c2c15e548bab43a7a3c3c2476bca3056ba85319d2cec9d8102dc42fbf54e379;
UMD 4261546326367f020bcdeaf1e52ff4bceb0abbb9deb4b557a62967cf01106d19;
CAT ececbbe434998858c1e334faa6e16938432306ea89a2b0b0682826afa1872e6f;
INF d33842928210f25087fc0c80559a4590f9c5c065b606a028e9051f58ed06439d.
The existing WDK test certificate thumbprint
E9BE15BD2A184BFABA0C8035B3C620C58037A241 was preserved.  This package was not
staged, installed or hardware-run.

The frozen inventory is now 117 of 121: CreateGeometryShader, GsSetShader and
the minimal SM4 VS/PS/GS execution contract are proven offline.  Pipeline mask
remains zero.  The next causal target is active stream-output shader creation,
target/offset capture and DrawAuto over the same mixed lifecycle.

## Verified native stream output and DrawAuto producer lifecycle (2026-09-20)
The projected D3D10 frontend now admits one exact FL10_0 GS-with-stream-output
declaration: slot/register zero, float4 mask, 16-byte stride and one 256-byte
VERTEX_BUFFER|STREAM_OUTPUT target.  The TGSI-to-NIR boundary materializes the
matching native XFB metadata instead of losing the sideband pipe declaration.
The real Asahi GS producer writes the target and offset resource, captures the
internal CS system-value table and completes the existing mixed VDM/CDM
submission before retirement.  New batch initialization clears stale native
stage-table pointers before a later independent draw.

DrawAuto remains the upstream two-stage producer path.  The exact
count-from-stream-output request is validated before any batch exists; Asahi
reads the completed offset, divides by the native target stride and recursively
emits the resulting direct draw through the ordinary lifecycle/graph gates.
Unsupported indirect forms still fault before submission.  The selected mixed
draw requires 69 typed references, 445 relocations, 17 physical allocations and
68 materialized objects, so mixed-v8 storage and overlay limits are fixed to
those measured minima while native-v4 remains capped at 64 references.

Fresh diagnostic-free x64 full producer execution PASS:
evidence/AD04-runtime-closure/real-so-draw-auto-clean-20260920gq-x64/;
source archive SHA-256
4d56126248b9698b50efaea64599cd25f2360b57b36061cd47f269332972fc74.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/real-so-draw-auto-clean-20260920gr-arm64/;
source archive SHA-256
82d5030897b9281588151e2a4cb411c303ca9f337ac4e1563fe425a7b99a46a5.

The frozen inventory is now 119 of 121: DrawAuto and
CreateGeometryShaderWithStreamOutput are proven through the real producer,
composer, both KMD materialization placements and ordered retirement.  Pipeline
mask remains zero.  The only remaining frozen rows are feature-resource-limits
and feature-required-formats; no broader pre-hardware inventory expansion is
permitted without a pinned-WDK requirement or a causal standard-runtime RED.

## Verified FL10_0 resource-limit admission (2026-09-20)
The projected Resource.cpp now applies one common pinned-WDK limit gate before
any resource private-state write or native allocation.  It checks the 14-level
mip cap, 128 MiB total mip-chain cap, 8192 1D/2D/cube dimensions, 2048 3D
dimensions, 512 array axes and the 2^27 buffer-texel bound with overflow-safe
arithmetic.  This does not add resource classes: the existing exact buffer,
attachment and presentation admission remains authoritative after the common
envelope, and unsupported forms return E_NOTIMPL without mutation.

The deterministic regression sends six boundary+1 descriptors through the real
D3D10 CreateResource callback and proves no allocator, Render or Signal delta
and no private-storage mutation.  RED
resource-limits-red-20260920gt-x64 reached downstream allocation/failure paths;
GREEN x64 full producer execution PASS:
evidence/AD04-runtime-closure/resource-limits-green-20260920gu-x64/;
source archive SHA-256
13c01ecc0762928f51b4723b3a1b2c7d71b495a0140e01245f9fa4f439e2152e.
ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/resource-limits-green-20260920gv-arm64/;
source archive SHA-256
0821e7c80e8650c9da6d67a83859324a40d55cc4e8bad19c894c22571d18b69c.

The frozen inventory is now 120 of 121 and pipeline mask remains zero.  The
only remaining pre-hardware inventory row is feature-required-formats.

## Required-format closure in progress: RGBA8 family (2026-09-20)
The final aggregate row remains RED after source-first review: FL10_0 base
format requirements cannot be withdrawn by returning zero optional bits from
the D3D10 CheckFormatSupport DDI.  The previous D32 result incorrectly returned
the API-level 0x10000 depth flag, which is not a valid base D3D10 optional DDI
bit; D32 now returns zero optional bits.  SampleCount one now returns the
required single quality level for every currently mapped hardware-candidate
format, while counts above one remain zero.

The first missing mandatory family is now implemented through production.
R8G8B8A8_UNORM resource, RTV, clear and draw use the existing uncompressed
32-bit tiled color path with a distinct typed wire format.  The native Asahi
producer supplies its own PBE/BG-EOT state; the existing composer, both KMD
materialization placements and ordered retirement accept it without a second
renderer or allocator.  BGRA8 remains the presentation/scanout format.

Fresh x64 full producer execution PASS:
evidence/AD04-runtime-closure/required-rgba8-final-20260920hg-x64/;
source archive SHA-256
96dde0f6ed55f28186c6293c17ebb53b1b4048426179258147f8ed0a737081b4.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/required-rgba8-final-20260920hh-arm64/;
source archive SHA-256
010fd6c6417fcb82b6076bbba2e0a7ba2d30b41226535e7730700a3aaad142c2.

Inventory deliberately remains 120 of 121 and pipeline mask zero.  Remaining
work inside feature-required-formats includes the other mandatory color/typed,
depth-stencil, BC and packed-format families.  The next causal family is
D16_UNORM plus D24_UNORM_S8_UINT and their compatible resource/view ownership.

## Required-format closure in progress: D16 depth (2026-09-20)
D16_UNORM now shares the existing single-plane uncompressed depth owner from
resource/DSV creation through ClearDepthStencilView, native Asahi draw capture,
depth-v7 wire metadata, both KMD placements and retirement.  Its producer sets
the source-defined integer-depth-bias flag even with zero depth bias.  The
request ABI translates that flag to the G13 hardware bit only at the two exact
WorkCommand3D AuxFBInfo copies: initial +0x0b0 and reload +0x6d8.  D32 materializes
0x0000c000 at both; D16 materializes 0x0004c000 at both.  Producer format/flag
mismatch and use of the flag outside depth-v7 reject before submission.

Fresh x64 full producer execution PASS with both ISP_CTL copies asserted:
evidence/AD04-runtime-closure/required-d16-validated-20260920hn-x64/;
source archive SHA-256
88053539e4b440cc85eb15757b68e045a8e7194d095a98796f6e6052ebb3975f.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/required-d16-validated-20260920ho-arm64/;
source archive SHA-256
a62368945d39267ee50709e3ad26c71ae2ebab82be2f199d4fb682a922d82bf9.

Inventory remains 120 of 121 and pipeline mask zero.  The next required depth
family is D24_UNORM_S8_UINT, which needs explicit separate-stencil ownership;
it must not be collapsed into the single D16/D32 depth reference.

## Required-format closure in progress: D24S8 separate stencil (2026-09-20)
D24_UNORM_S8_UINT now follows Asahi's real split ownership: the logical D24S8
resource has a physical uncompressed Z32_FLOAT parent and a separate physical
S8_UINT stencil resource.  Native batch capture records the two immutable
attachment identities independently, and Windows finalization resolves both
through the existing allocation list.  The depth-v7 overlay patches the exact
G13 initial and reload stencil load/store addresses, strides, compression fields
and BG-object stencil values; uncompressed compression addresses and strides
remain zero.  Combined depth+stencil clear is admitted only for the exact
producer form already emitted by the D3D10 frontend.  Unsupported clear and
resource forms still fault before submission.

Fresh x64 full producer execution PASS, including exact materialized stencil
field assertions at both independent KMD placements:
evidence/AD04-runtime-closure/required-d24s8-final-20260920hx-x64/;
source archive SHA-256
edb4a56201f65dfb2acae6879133066f03ef50311cbe4e1f6f74dc28dea2f4dc.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/required-d24s8-final-20260920hy-arm64/;
source archive SHA-256
e0c1804b95709ab8b68c1dac43cb4bea90bfe25422f33713e5e37a39ed81577b.

Inventory remains 120 of 121 and pipeline mask zero.  The remaining work is
strictly inside the frozen feature-required-formats row: mandatory color/typed,
BC and packed-format families.  Do not expand the pre-hardware inventory unless
the pinned WDK or a standard-runtime causal RED proves another prerequisite.

## Required-format closure in progress: shared FP16 color storage (2026-09-20)
R16G16B16A16_FLOAT is the first non-four-byte color target on the real native
path.  One shared wire helper now derives bytes per pixel for color payload
validation and batch pitch construction; resource, clear, graph and KMD gates
admit the exact 8-byte format while Mesa/Asahi remains the owner of the emitted
PBE and BG/EOT encodings.  The 16x16 discriminator therefore uses pitch 128 and
proves that the Windows path no longer assumes all color storage is width*4.
Unsupported formats remain rejected before submission.

The preregistered RED failed format capability/sample-one checks and private
resource admission before any valid FP16 draw:
evidence/AD04-runtime-closure/required-fp16-red-20260920hz-x64/.
Fresh x64 real producer execution PASS through both KMD placements and ordered
retirement:
evidence/AD04-runtime-closure/required-fp16-green-20260920ia-x64/;
source archive SHA-256
34ecf0e51b512f46b03525f4df04d3304dea87aac498e9668ae29f0b49a71d33.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/required-fp16-green-20260920ib-arm64/;
source archive SHA-256
cfeb6c2fe69dc23cc5a296ad27f05ddafec292407e3c2875a3a0773da1ad1895.

Inventory remains 120 of 121 and pipeline mask zero.  Continue the same frozen
feature-required-formats row with representative 1/2/16-byte typed color and
compatible-view interpretation, then D32S8, BC block storage and packed formats.

## Required-format closure in progress: ordinary color storage widths (2026-09-20)
The shared uncompressed color path now covers representative one-, two-, four-,
eight- and sixteen-byte pixels.  R8_UNORM, R16_FLOAT and
R32G32B32A32_FLOAT each pass real resource/RTV creation, clear, native draw,
immutable capture, both KMD placements and retirement.  The wire format remains
explicit for every interpretation while one bytes-per-pixel contract owns
pitch and minimum-span validation; Mesa/Asahi continues to emit all format-
specific PBE and BG/EOT state.

Causal RED rejected the new capability/sample-one queries and all three private
resources before draw:
evidence/AD04-runtime-closure/required-color-widths-red-20260920ic-x64/.
Fresh diagnostic-clean x64 producer execution PASS:
evidence/AD04-runtime-closure/required-color-widths-final-20260920ie-x64/;
source archive SHA-256
4303d08039298525c4490985fba9c78eba71b20d3b8699ce25b624cf0eae5b01.
Fresh ARM64 full closure/client build-link PASS:
evidence/AD04-runtime-closure/required-color-widths-final-20260920if-arm64/;
source archive SHA-256
0ab2a88761dd829ec206b8cb112fce529d78e4bc9e81ac112d1cafad5725061c.

Inventory remains 120 of 121 and pipeline mask zero.  Storage width is no longer
the active color blocker.  Continue inside feature-required-formats with typed
numeric/sRGB and compatible typeless views, then D32S8, BC block storage and
packed formats.

## Required-format closure in progress: D32S8 split stencil (2026-09-20)
D32_FLOAT_S8X24_UINT now reuses the proven split depth/stencil owner.  The
common constructor preserves the logical D32S8 parent while allocating physical
Z32_FLOAT and independent S8_UINT resources.  Existing depth-v7 capture,
allocation-list resolution, exact initial/reload stencil patching and retirement
apply unchanged.

Causal RED rejected sample-one and resource/view admission:
evidence/AD04-runtime-closure/required-d32s8-red-20260920ig-x64/.
Fresh x64 full producer execution PASS:
evidence/AD04-runtime-closure/required-d32s8-green-20260920ih-x64/;
source archive SHA-256
7d4567f78fadf5948483a7b9ee3950f5becf8ebc668cb304e9ad88113884feaf.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-d32s8-green-20260920ii-arm64/;
source archive SHA-256
cf0525391657403d270e7e6efef50aed3260dc655fc2373442d14be244176b0c.

Inventory remains 120 of 121 and pipeline mask zero.  Remaining frozen format
work is compatible typed/typeless and sRGB interpretation, BC block storage and
packed formats.

## Required-format closure in progress: typeless storage and typed RTV (2026-09-20)
R8G8B8A8_TYPELESS storage now uses the existing physical RGBA8 allocation and
real producer through a compatible R8G8B8A8_UNORM RTV.  A shared logical-family
predicate accepts the UNORM, sRGB, UINT, SNORM and SINT view family and rejects
an equal-sized R16G16 view.  RTV creation validates ordinary resource ownership
and RT bind before creating the pipe surface while preserving the existing
exact BGRA imported-presentation exception required by standard DXGI.

The causal RED series isolated two integration errors: an ambiguous source
projection anchor and a missing presentation-owner exception in the RTV guard.
The final x64 producer execution PASS is:
evidence/AD04-runtime-closure/required-typeless-rtv-final-20260920it-x64/;
source archive SHA-256
b523ea2b4487675c3592a76d25ff0e70487159b9b0df6442c51eb2777509cda6.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-typeless-rtv-final-20260920iu-arm64/;
source archive SHA-256
6457a6d3cdf374838bf781b0b3d50867c81acaaa3fe1dc4eb27c788fae72e9ad.

Inventory remains 120 of 121 and pipeline mask zero.  Combined RT|SRV lifetime
and actual typed/sRGB sampling remain fail-closed and are the next compatible-
view boundary; BC block storage and packed formats follow.

## Required-format closure in progress: combined typeless RTV/SRV lifetime (2026-09-20)
The same R8G8B8A8_TYPELESS owner now admits the exact combined RT|SRV bind.  A
real typed UNORM RTV draw is flushed and retired before an sRGB SRV is created,
bound and unbound at PS slot zero, destroyed, and finally followed by the
existing query/present lifecycle.  Both RTV and SRV callbacks validate owner,
bind and logical view family before constructing Mesa pipe views.  Imported
presentation RTV/SRV remains an explicit exact-BGRA exception.

Fresh x64 full suite and real producer execution PASS:
evidence/AD04-runtime-closure/required-typeless-srv-life-20260920iv-x64/;
source archive SHA-256
6c1999811ab28abca8fed2efd409401745dbb177a980654b7831b61f4d2a12e5.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-typeless-srv-life-20260920iw-arm64/;
source archive SHA-256
b1a5ec657118eceb589f64f67ca83910d05081eb954fb607d22a8a3b5a672fa7.

Inventory remains 120 of 121 and pipeline mask zero.  The next compatible-view
discriminator is actual typed/sRGB sampling plus mip/layer range capture; view
object lifetime alone is not sufficient to close the format aggregate.

## Required-format closure in progress: real typed/sRGB sampling (2026-09-20)
The typeless RGBA8 resource now participates in an actual version-6 textured
producer after its typed RTV transaction retires.  Its compatible sRGB SRV
records the selected sample interpretation on the resource owner; the existing
ResourceCopy DDI admits only an exact 16x16 private shader-resource to proven
color-target copy and calls Mesa pipe->blit.  Mesa util-blitter and Asahi emit
the texture descriptor, shaders, PBE and BG/EOT state.  Windows captures and
materializes the resulting 36-reference/136-relocation graph without a second
renderer.  Presentation-to-presentation ResourceCopy remains on its existing
standard DXGI path; all other copy forms stay fail-closed.

Fresh x64 full producer execution PASS with the additional version-6 graph:
evidence/AD04-runtime-closure/required-typeless-srgb-sample-20260920ix-x64/;
source archive SHA-256
b84a38e39a38f9dc3209bcae1a8ff14a6cdd345cdf8de44ef8255f32eecc65fb.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-typeless-srgb-sample-20260920iy-arm64/;
source archive SHA-256
ce8a89c90d88f19690f3039025f99bbd94a5f47cded3332c66942a97ae6c9810.

Inventory remains 120 of 121 and pipeline mask zero.  The next format boundary
is block-aware BC resource/upload/view arithmetic and sampling; mip/layer range
validation must be carried by that shared texture implementation rather than a
second texture path.

## Required-format closure in progress: BC1-BC5 block storage (2026-09-20)
The shared texture resource/view/blit path now uses block-aware size accounting:
for every mip it computes rounded block width and height before multiplying by
block bytes, depth and array size.  BC1 sRGB proves an 8-byte block, BC3 sRGB a
16-byte block, and BC5 SNORM signed 16-byte interpretation.  Each uses a 7x5
initial upload (2x2 blocks), compatible typed SRV, real version-6 Mesa
util-blitter draw, both KMD placements and ordered retirement.  BC2 and BC4 are
covered by the same explicit compatible-view family table and storage classes.

The causal executable RED rejected BC1 at SRV family admission before
submission:
evidence/AD04-runtime-closure/required-bc1-red-20260920ja-x64/.
Fresh x64 BC1/BC3/BC5 producer suite PASS:
evidence/AD04-runtime-closure/required-bc-family-green-20260920jc-x64/;
source archive SHA-256
6972e712c6fc2c42fe1b3d47be0f8980df9ac8a03788e53018268866c08496d3.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-bc-family-green-20260920jd-arm64/;
source archive SHA-256
dfc1beee4639a2b709b9a06a8da2885be05c6d80334e05f45ac1f709dad2b450.

Inventory remains 120 of 121 and pipeline mask zero.  Packed formats and the
complete frozen format-use descriptor-table gate remain before aggregate PASS.

## Required-format closure in progress: packed render and sampling (2026-09-20)
R10G10B10A2_UNORM and R11G11B10_FLOAT now use the proven four-byte color
resource/RTV/clear/native-draw path with distinct wire format identities while
Mesa owns their PBE encodings.  R9G9B9E5_SHAREDEXP samples natively through the
shared texture path.  The two required packed-pair formats are not native Asahi
textures: initial uploads are explicitly expanded to physical RGBA8 using their
source byte layouts, then sampled through the same version-6 graph.  The
logical DXGI identity remains on the Windows resource owner.  The pair lowering
requires an even width and is not exposed as generic hardware support.

Expanded format coverage crossed the host fixture's synthetic handle namespace;
the fake allocator now skips the reserved 0x771-0x777 presentation/KM range so
distinct allocation identities cannot alias.  The production composer continues
to reject any such alias.

Fresh x64 full packed suite PASS:
evidence/AD04-runtime-closure/required-packed-final-20260920jn-x64/;
source archive SHA-256
6b55ba3d437c10e1a17a41a71dcdf26f0a8d411ac58651fa4d6038c8a16bdf8b.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-packed-final-20260920jo-arm64/;
source archive SHA-256
93334ebdcc632aea25fdfb9db97e4b75c5229b153c44249bed4f24de0f0e05ea.

Inventory remains 120 of 121 and pipeline mask zero.  The remaining frozen
format work is the complete data-driven required-use table, Windows 8 B5/B4
family and nonzero mip/layer range proof before aggregate admission.

## Required-format closure in progress: Windows 8 B5/B4 family (2026-09-20)
B5G6R5_UNORM now uses the existing two-byte color target path through real
clear/draw, capture, both KMD placements and retirement.  B5G5R5A1_UNORM and
B4G4R4A4_UNORM use the shared uploaded texture/SRV/native-blit path.  The pinned
D3D10 translator lacked B4G4R4A4; the projection now maps it to the matching
Mesa/Asahi physical format.  No optional uses were enabled for B5G5R5A1 or B4.

Causal RED rejected the missing formats before submission:
evidence/AD04-runtime-closure/required-win8-formats-red-20260920jp-x64/.
Fresh x64 full producer execution PASS:
evidence/AD04-runtime-closure/required-win8-formats-green-20260920jq-x64/;
source archive SHA-256
e3f4b7769a82d7ac1873f8dcfa3facdf389786d76a74a362981db3dd9c1f9de6.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-win8-formats-green-20260920jr-arm64/;
source archive SHA-256
a9fb0f3dd384d6ace26e0dfbc13ed2369d5a2a995879d16bcbc3531138657710.

Inventory remains 120 of 121 and pipeline mask zero.  All distinct physical or
lowered storage families selected before hardware now have representative
executable evidence.  The remaining gate is the complete machine-readable
format/use descriptor contract plus nonzero mip/layer range validation.

## Required-format closure in progress: bounded mip/array texture capture (2026-09-20)
A two-mip/two-layer typeless RGBA8 resource now creates a typed SRV selecting
mip1/layer1 and drives the real version-6 native blit.  Mesa util-blitter's
internal sampler view exposes mip1 across layers0-1 while shader coordinates
select layer1.  Capture therefore preserves the source-authentic layer-base
address and bounds the immutable TextureReference to the descriptor-visible
layer range through the end of mip1; it does not copy or expose the remainder
of the allocation.  The host KMD consumer verifies the texture span is nonzero
and strictly smaller than the validated allocation size before both placements.
Multi-mip SRVs remain rejected by the chosen pre-hardware slice.

Fresh x64 full producer execution PASS:
evidence/AD04-runtime-closure/required-subresource-final-20260920jz-x64/;
source archive SHA-256
e72e980b43b2cdaa94529cbccad657df37206e8c661d9e828598f8387e34fb36.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-subresource-final-20260920ka-arm64/;
source archive SHA-256
3730e1e1a3a848d28362ccd7ec20e6698853b7a4706bf3ba5ea5a058621c56ee.

Inventory remains 120 of 121 and pipeline mask zero.  Representative execution
now covers every selected physical/lowered storage and subresource family.  The
last gate is the exhaustive machine-readable required-format/use descriptor
contract that binds all mandatory DXGI entries to these production families.

## Required-format closure in progress: A8 and RGB32 texture lowering (2026-09-20)
The two explicit pinned-Asahi texture exceptions now use one frontend-owned
representation function across allocation, SRV and blit.  A8_UNORM uploads to
physical RGBA8 while preserving alpha, and R32G32B32 FLOAT/UINT/SINT upload to
the corresponding physical RGBA32 representation with alpha supplied by Mesa
format conversion.  Logical DXGI identity remains on the Windows owner; native
capture sees only the supported physical resource.  Real A8 and RGB32_FLOAT
sampling execute through the existing version-6 graph, both KMD placements and
retirement.

Causal RED reached the Asahi non-native format rejection before submission:
evidence/AD04-runtime-closure/required-lowered-textures-red-20260920kb-x64/.
Fresh x64 full producer execution PASS:
evidence/AD04-runtime-closure/required-lowered-textures-green-20260920kc-x64/;
source archive SHA-256
eddb288c84ad1984db9a272d9d03649681b567bdcf54741724e5393033d21b70.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-lowered-textures-green-20260920kd-arm64/;
source archive SHA-256
b35ef38fc5aaa5cd1a1e2d8d079f17746e085521b6b8a232892a2a9c94a184bd.

Inventory remains 120 of 121 and pipeline mask zero pending the exhaustive
machine-readable required-format/use table.  No physical/lowered storage family
remains without representative executable evidence.

## Required-format closure in progress: A8 render and RGB32 typeless storage (2026-09-20)
The common lowering contract now also admits A8_UNORM as a logical render target
on physical RGBA8.  Typed A8 SRVs force RGB to zero and preserve physical alpha,
so extra representation channels are not observable.  R32G32B32_TYPELESS now
allocates physical RGBA32_UNORM storage and compatible typed FLOAT/UINT/SINT
views select the matching physical interpretation.  A8 real clear/draw and the
existing A8/RGB32 sampling gates pass through the same producer/KMD lifecycle.

Fresh x64 full producer execution PASS:
evidence/AD04-runtime-closure/required-a8-typeless-final-20260920kf-x64/;
source archive SHA-256
569a877c1e818ce875379e96c8e7475074e8c0a0f0b81a3a999ecb68f3da5a03.
Fresh ARM64 closure/client build-link PASS:
evidence/AD04-runtime-closure/required-a8-typeless-final-20260920kg-arm64/;
source archive SHA-256
eaf8603da770118dbc2ba6c93417c9989a9f6d9e3c1fbac6ba4e5dcb654c6e25.

Inventory deliberately remains 120 of 121 and pipeline mask zero until the
exhaustive required-format contract validator passes against this exact source
and all referenced evidence.

## Frozen FL10_0 admission inventory complete (2026-09-20)
The final feature-required-formats row is now backed by
`drivers/apple-agx/mesa/d3d10umd/required-formats-fl10.json`: 93 unique mandatory
DXGI entries with required uses, compatible families, native or explicit lowered
representation and concrete executable evidence.  The validator requires the
exact format set, pinned Mesa commit, permitted use vocabulary, the exact seven
lowered logical formats, disabled optional MSAA/XR_BIAS, existing evidence paths
and bounded subresource evidence.  It rejects missing entries, optional-policy
drift and invented lowerings.

Current deterministic results:
- required-format validator PASS: 93 formats, 7 lowerings;
- desktop contract validator PASS: 121 mandatory requirements complete;
- exact advertised pipeline mask equals selected mask 1;
- 14 required-format, desktop-contract and change-ledger tests PASS.

The frozen machine-readable inventory is now 121 of 121.  No new pre-hardware
inventory expansion is permitted without pinned-WDK evidence or a causal
standard-runtime RED.  The next boundary is installed native UMD standard-
runtime admission, followed by the exact ARM64 package/sign/hash and hardware
preregistration gates.

## Exact standard-runtime qualification client built (2026-09-20)
The existing `AppleAgxD3dKmRender.vcxproj` now has a mutually exclusive
`EnableStandardRuntimeClient` mode; it does not create another helper project or
alter the direct-KMT/native-batch build.  That mode builds
AppleAgxD3d10Standard.exe, which uses only the standard D3D10/DXGI APIs:
D3D10CreateDeviceAndSwapChain, Apple vendor-id verification, runtime HLSL
compilation, RTV/vertex-buffer/shader state, ClearRenderTargetView, Draw, Flush
and IDXGISwapChain::Present.  It reports PASS only after standard Present
returns success.

Fresh source archive SHA-256:
9f047853c096ade828c26f2014cb196739651a8bf3e9061b9fa9bcaf8c0365f5.
The same snapshot passed the complete x64 real-producer closure and direct native
client link under
`evidence/AD04-runtime-closure/standard-client-source-20260920kh-x64/`.
Standard client builds:
- x64 0 warnings/0 errors, EXE SHA-256
  746b2a88b169de6a5e4907f9e49e748bdc3d4c5dcc4a988e2b21f888cb6e1451;
- ARM64 0 warnings/0 errors, PE machine AA64, EXE SHA-256
  41db196ab80277c095043cf107843f0e31daad3cdd7b5731f0183bcd3076549a.
Evidence is under `evidence/AD04-standard-runtime-client/standard-client-20260920ki-*`.
The client was not executed on the builder, which has no installed target Apple
adapter/UMD.  Its first execution belongs to the preregistered Air experiment.

The next boundary is the final native-enabled ARM64 package build/sign/hash and
preregistration; no additional frontend requirement is implied.

## Native pipeline advertisement and package-analysis correction (2026-09-20)
The installed UMD GetCaps now reports exactly
D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0), mask 1,
only when APPLE_AGX_UMD_NATIVE_FRONTEND is compiled.  Default-off packages retain
zero.  THREADING caps and all higher feature levels remain disabled.

Native package702 proved Universal/Inf2Cat but was rejected for one UMD code-
analysis C6011 at the device-owner unlink helper.  The helper now validates its
owner/inout and Adapter before dereference.  Fresh ARM64 native closure for
expected build703 PASS (archive SHA-256
2ceea9c711b9e33bb70e62a29b74f858417bb4bb7ac3a28cf63edafc30201b4a),
and package703 then built/analyzed with 0 warnings/0 errors; Universal and
Inf2Cat PASS.  It was signed by the preserved WDK test certificate
E9BE15BD2A184BFABA0C8035B3C620C58037A241 and never staged.

Package703 is not the hardware candidate because its source snapshot contained
these changes as a dirty diff.  Rebuild from the next clean HEAD as package704;
do not stage 702 or 703.

## EXP685 standard-runtime hardware candidate preregistered (2026-09-20)
Clean integration HEAD 097573751bb341d6c54fc0c8e428dbb65b5529c0 produced
native ARM64 package build704.  Fresh runtime closure/client link PASS, archive
SHA-256 7441daf2e5f2e93a3cd070d81151a88d7e0a73a3608dd7e368bb61bc1b14fc4b.
UMD and KMD code analysis are 0 warnings/0 errors; Universal validation and
Inf2Cat pass with 0 warnings/0 errors.  Signed package hashes: INF
55107e16f01c3d213404f9ae952c5c021a54e0d022c7726fadd68c454a1cb29e,
SYS c3eb32020145c4207a532392caf038f212f4b3e9ffae31418aae5dfdd4b8386a,
UMD 7bc8a5de3fdf0f086a8d968d20a6a60925513a0f654e9367ea2a74fc3c933285,
CAT b1032dfb8ddafca4e8ae6a5e7f1192e377a7ddec06725175267840fd070bf773.
The preserved signer is E9BE15BD2A184BFABA0C8035B3C620C58037A241.

The clean-source ARM64 standard client builds with 0 warnings/0 errors and is
signed by that same signer; EXE SHA-256
edbc48c789d39ea924c8de2f0c631747040a0226597970764e5e1cc9b3f780fe.
Immutable hardware artifact is
`.local/experiments/EXP685-standard-runtime-704.zip`, SHA-256
`df82dd136dc2e841aa97cc989e5287dc5aa2704f84dea532fd8ed4c4ded5431b`.
EXP685 preregistration is committed in the experiment ledger with exact
hypothesis, atomic contract, commands, evidence and ordinary377/392 recovery.

At preregistration hardware had NOT_RUN: two bounded control-plane audits found
Air SSH and proxy/vUART unavailable, so no signer/package/reboot/launch occurred
until the later EXP685 execution recorded below.

## EXP685 bound-preflight failure and offline correction (2026-09-20)
The Air control planes returned and EXP685 executed only through bound preflight.
Exact package704 and signer hashes matched, but APPL0002 ended Code43 before the
standard client. Durable device receipts are StartStage8/PlatformStage9 with
`0xC000007B` (`STATUS_INVALID_IMAGE_FORMAT`); DriverEntry/DxgkInitialize passed.
Therefore the standard D3D10 client remains NOT_RUN and EXP685 is rejected as a
hardware candidate rather than classified as a runtime/Present result.

Evidence-first exact cleanup is complete. Ordinary377/392 broker-disabled is
active with one APPL0002 Code28, ACPI-only stack, no AppleAgx package/service/
SYS/UMD/E9BE15 signer, eight CPUs, NVMe/input and TESTSIGNING Yes; no fresh
41/1001/129/4101. Evidence is under main-root
`.local/experiments/EXP685-live/{failure-evidence,ordinary-recovery}`.

The stage-9 OR-chain was reconstructed in production order.  Compute queue
metadata is initialized after `PrepareBroker`, while all 40 render objects are
Prepared and before the context-0 broker makes them GpuMapped.  The initializer
incorrectly required GpuMapped.  The same 36-to-40 object expansion left four
literal broker checks at 90 mappings even though the authoritative derived count
is now 94 and 207 leaves.  Deterministic REDs reproduce both exact failures.
The minimal fix requires Prepared for initial CPU writes and derives broker
mapping count from `APPLE_AGX_INITDATA_MEMORY_MAPPING_CAPACITY - 1u`; focused
render-shared/initdata/compute queue/provider tests are GREEN.  Next: commit this
correction, rebuild a clean ARM64 native package with a new version/signer/hash,
preregister one replacement experiment, then repeat bound preflight and proceed
directly to the standard client only if StartDevice is complete.

## EXP686 replacement candidate preregistered (2026-09-20)
Implementation d657ec07 and ledger0a4f304d form the clean source boundary.
Production-order stage-9 and 94-mapping/207-leaf REDs are GREEN; focused host,
platform, render-admission, package and ledger tests pass. Fresh ARM64 runtime
closure `package706-stage9-20260920ko-arm64` and expected-build706 direct-KMT
client link pass from clean archive SHA
74754930a168f8e1268b13313fa7256a987295b41237ef28b766e9d754445fdf.

Package706 version30.0.706.0 passes UMD/KMD analysis0/0, Universal and Inf2Cat.
Hashes: INF b42ab9628a1651e963a8c6ab44e30d9d27c784a55c783f9b6820299c739902e3;
SYS bee53de5bbedbf09f22c70a829c597b35dca1eb29aa0ed7e7cca2da1c624ce8c;
UMD b49612ed7d4c7814d77a488da1aa55dc481df55cbc471fe68580b419016e5592;
CAT eed0a30f235e87cca81896d89f3e897cf67667aa35e52a895da6aadbabffff27;
standard client a4be15615ab77cfd936c96840365a20c3092ffa9fde8af7076c73df20413bb05;
certificate97145866...; signer E9BE15BD2A184BFABA0C8035B3C620C58037A241.
All three package files are catalog members. Immutable EXP686 archive SHA
76ee8964e044f277d4d48193ca500cf851c5df3805c0b9b0aa1bd5263276387c.
Builder chain status is untrusted-self-signed-root as expected; Air verification
must become trusted only after exact experiment certificate import.

Current Air state is healthy ordinary377/392 Code28 recovery. EXP686 is NOT_RUN.
Next perform its exact stage/reboot/full-owner bound preflight; execute the
standard client once only if APPL0002 reaches Code0 and
StartStage12/PlatformStage14. No inventory expansion precedes it.

## EXP686 hardware result and context-0 fix (2026-09-20)
EXP686 exact package706 confirmed the stage-9 fix: Platform advanced to stage12.
Bound preflight still failed Code43, so standard client remains NOT_RUN. Receipts
are BackendStartResult6/FirmwareResult5/ProviderFailurePhase2 and
Context0MapResult1 with zero leaves; retained prepare/activate and firmware IO
passed. Exact cleanup restored ordinary377/392 Code28 with no package/service/
files/E9BE15 signer and healthy 8CPU/NVMe/input.

The next cause is deterministic: context0 broker retained literal 90/200 guards
after the authoritative graph became 94 mappings/207 leaves. The dormant broker
test now executes in worktrees and REDs at Map; GREEN derives leaf count from the
validated inventory and passes every one of 207 injected MAP failures, QUERY,
retire/retry and second-lifetime cleanup. Current fix is offline-only and must be
committed, rebuilt and preregistered before the next hardware preflight.

## EXP687 preregistered (2026-09-20)
Clean HEAD b1135ad6 includes the derived 207-leaf context0 broker fix. Fresh
ARM64 closure/package707 passes analysis0/0, Universal, Inf2Cat and catalog
membership. Immutable archive SHA is
557cf818c740a08a93e5c99f4a97644a256601db7f987274628b9f47dede79ad;
exact member hashes are recorded in EXP687. Current Air is ordinary377/392
Code28 and EXP687 is NOT_RUN. Next stage exact package and require Code0 before
the single standard-runtime execution.

## EXP687 admission PASS; interactive DXGI boundary (2026-09-20)
Package707 hardware startup PASS: Code0, service Running, Start12/Platform14,
BackendStart0, firmware mask0x1ff and context0 207/207. The once-only SSH client
returned DXGI_ERROR_NOT_CURRENTLY_AVAILABLE before adapter creation because no
interactive user/Explorer session existed. No draw was attempted. Exact cleanup
and ordinary recovery are in progress/complete.

EXP688 freezes the same package/client and changes only launch context to an
authenticated interactive console. Archive SHA
d9bff6e8d23de341641bdf028306846fe35c05e2b84df7adec7c9b432d422d39.
After full-owner Code0 the operator must log into the Windows desktop; then run
the client once via an Interactive scheduled task. This manual login is the only
current blocker to the first truthful standard-runtime draw/Present.

## EXP688 standard-runtime admission RED (2026-09-20)
Interactive launch succeeded, but D3D10CreateDeviceAndSwapChain returned
E_INVALIDARG before adapter reporting/draw. Driver remained full PASS and KMD
Type0 query succeeded. Installed native OpenAdapter delegates to projected Mesa
Adapter.cpp, whose 3D pipeline GetCaps still advertised0; the fallback UMD mask1
was unreachable. Source RED requires mask1 in the real projection and updates
the real Mesa adapter contract. EXP688 was run once, cleaned exactly, and must
not be replayed. Next build a clean candidate from this projection fix.

## EXP689 preregistered (2026-09-20)
Projected native FL10 mask1 passes fresh x64 execution, ARM64 closure and
package708 gates. Immutable archive SHA
ec50f1b84fc3fc12e3571c4dd8a722911dc1e7bb2c3e83571705c832307aa075.
Next stage exact package, boot Code0 and run once in authenticated console.

## EXP689 CreateDevice interface RED (2026-09-20)
Mask1 reached interactive runtime but CreateDevice still returned E_INVALIDARG
before adapter reporting/draw. Native owner accepted only exact D3D10_0 although
the projected upstream entry accepts D3D10_0, _x and _7. Current fix admits the
three documented variants through the same D3D10 core callbacks; no feature or
hardware path changes. EXP689 cleaned; next run requires fresh gates/package.

## EXP690 preregistered (2026-09-20)
D3D10_0 interface variants pass fresh x64 execution and ARM64/package709 gates.
Immutable SHA ff1a4bb3226726d68eb76bd227648fe7a375eb64ee3f2210200775e3a5de62a1.
Next stage exact package and run once in authenticated console.

## Current proven boundary correction after EXP690
EXP690 returned E_INVALIDARG from the combined D3D10CreateDeviceAndSwapChain call. Neither UMD CreateDevice entry nor rejection of any particular interface/callback is proven. Earlier causal claims about pipeline/interface rejection are superseded by this uncertainty; retain those source changes but do not weaken callback guards speculatively. Recovery continuation verified SSH, 8 CPUs, NVMe/xHCI/input, no service/SYS/UMD/signer and no fresh critical/storage/display events. Re-enumerate the unbound stale Code0 devnode to Code28 before staging. Next discriminator splits standard D3D10CreateDevice from IDXGIFactory::CreateSwapChain and logs HRESULT at both boundaries. Client x64/ARM64 builds are 0 warnings/errors; signed ARM64 SHA8ff9cedbb6a5ac29a3529614be6fdddfbe2efda8d3e9696c197367e5df1b9d78. Driver package709 stays byte-identical.

## EXP691 standard D3D10 device admission PASS
Standard D3D10CreateDevice returned S_OK on actual Apple AGX. Client then
rejected measured ACPI identity vendor4c505041/device32303030 because it wrongly
required PCI vendor106b. Swap-chain/draw/Present NOT_RUN. Exact package709
cleanup and ordinary377/392 recovery verified Code28/no package/service/files/
trust, 8CPU/NVMe/input and no fresh monitored events. One recovered Event129 in
the full-owner boot remains unassigned, with evidence preserved. Next change
is client-only exact ACPI pair guard with executable positive/negative test.

HARDWARE ROADMAP
[PASS] Frozen native/ARM64 gates, KMD firmware startup and now standard D3D10
device admission (EXP691 S_OK).
[NOW] Client identity guard: measured ACPI pair accepted and foreign IDs
rejected by deterministic executable regression; complete fresh x64/ARM64 build.
[NEXT] Sign/hash exact client, preregister with unchanged package709 and proceed
directly to Air; entirely offline until launch.
[HW] Standard swap-chain, native draw, physical completion and DXGI Present
remain unproven; hardware evidence required.
POST-HARDWARE: accelerated desktop stability and desktop acceptance.

## EXP692 — swap-chain creation is current boundary
Device S_OK and actual ACPI adapter accepted; CreateSwapChain E_INVALIDARG.
No draw or Present. Full-owner preflight and ordinary Code28 recovery were
healthy with no fresh monitored events. Current hypothesis: BGRA format is
filtered under advertised base D3D10 DDI; not proven. Actual resource allocation
is an alternative. Next client adds only standard CheckFormatSupport/sample
quality receipts for BGRA and RGBA before the identical swap-chain call.
Microsoft Extended Format Aware Requirements and projected Adapter.cpp base-only
DDI list inspected; do not advertise extended DDI without companion invariants.
Debug runtime DLLs absent on Air; no environment installation is needed for
these ordinary API receipts.
HARDWARE ROADMAP
[PASS] Frozen native/package gates; firmware startup; standard device and AGX
identity on hardware (EXP692).
[NOW] Runtime swap-chain admission: discriminate format filtering from resource
allocation using read-only standard queries; x64/ARM64 compile gates then Air.
[NEXT] Causal fix and focused deterministic verification, sign/hash and immediate
preregistered hardware run; do not expand the frozen feature inventory.
[HW] Swap-chain, real native draw, physical completion and standard Present.
POST-HARDWARE: accelerated desktop stability and desktop acceptance.

## EXP693 format discriminator confirms current blocker
Actual base D3D10 runtime rejects BGRA87: E_FAIL/support0, sample1 E_INVALIDARG.
RGBA28 passes S_OK/support001ef3f3 and quality1. Same BGRA swap-chain returns
E_INVALIDARG. Source shows presentation owner hardcodes BGRA at UMD descriptor,
Mesa import and projected metadata; this must carry actual RGBA for minimal
base-D3D10 backbuffer. Existing native pfnBlt is the conversion owner into BGRA
primary/DWM; do not relabel bytes or loosen direct scanout format guards.
All EXP693 evidence collected and exact cleanup followed by ordinary377/392
Code28 recovery verified; no installed package/files/trust or fresh events.
HARDWARE ROADMAP
[PASS] Frozen producer/package gates, KMD firmware startup, standard device
admission and actual RGBA runtime support (EXP693).
[NOW] Carry true RGBA backbuffer format through existing allocation/import/
metadata and native presentation BLT. Required because BGRA is unavailable in
selected runtime contract. Gate: actual frontend resource create/open/BLT tests
with distinct R/B values plus x64 execution and ARM64 closure; entirely offline.
[NEXT] Exact package/sign/hash and preregistration, then immediate Air run.
[HW] Standard RGBA swap-chain, real native draw, AGX completion, DXGI Present.
POST-HARDWARE: accelerated desktop stability and desktop acceptance.

## Required visual observation at the next standard-runtime run
Operator reports prior installed-driver display was predominantly black; input
could reveal isolated window-caption controls (minimize/maximize/close). This is
an operator observation, not yet causally attributed or tied to a specific EXP.
Next standard-runtime preregistration must record the symptom before the client,
after Present attempt and after window interaction. Verdict: persists,
disappears, changes, or unobserved; distinguish physical display observation
from desktop capture/Present HRESULT. Do not infer a normal display from S_OK.

## RGBA backbuffer implementation checkpoint (offline, uncommitted)
Source-first review: pinned Mesa9aa1215 agx_pipe.c sets AIL format from actual
pipe format; agx_blit.c keys source/destination formats independently. Inspected
current umd.c create/open/present/scanout predicates, native Windows bridge,
projection and existing frontend tests. Microsoft DXGI_DDI_BASE_FUNCTIONS
assigns backbuffer-to-DWM conversion to pfnBlt and flush-before-present ordering.
Classification FULL GRAPHICS. UMD owns logical format/import/view/lifetime; KMD
retains physical allocation/patch/DMA/fence ownership. Firmware, power and IRQ
owners and recovery stay unchanged. No Asahi source copied.
Actual x64 closure rgba-present-20260920mc-x64 PASS (archive641d7c769596e546e661ff08418c68bd30079128d481c2eac8efcf20cd482a80),
including real producer/materializer/KMD plan/retirement and new RGBA create/RTV.
Initial mb test failed because fixture reused one allocation handle; mc gives
RGBA a distinct identity and verifies cleanup. Direct-scanout guard executable
and RGBA descriptor RED/GREEN pass. Open/blit changed-branch tests and ARM64/
package remain pending. KMD RGBA buffered-BLT conversion is deferred until the
runtime actually selects that route; current unsupported path remains rejected.
Smallest next hardware checkpoint: standard RGBA CreateSwapChain/GetBuffer/RTV,
then same draw/Present; ordinary377/392 exact-package cleanup is recovery.

## RGBA offline gates complete; package710 next
Final rgba-present-20260920mf x64 execution PASS and ARM64 build/link PASS,
same source archive63fc78284d3393c6f8a999676087665488a4195e877aeae9ad3b22615458c505.
x64 executable5310c07ffdc32c9fe7dbc8a351c3df46c2b8b6a20e89db6042ff1598c6b0d15b;
ARM64 executable8fec9a465d828ba332841246434434a6c0a8fb75d4b2c3015ca5be35b2538bf4.
Actual frontend RGBA create/open/RTV succeeds; observed native blit arguments
are RGBA source/BGRA destination; direct display mode rejects RGBA. Native
producer/materializer/KMD plan/patch/retirement gate remains PASS. Intermediate
md/me test builds exposed missing test include, shadowed name and typed handle;
fixed without production changes. Package/sign/hash and final EXP694
preregistration are next, then immediate Air run with the required physical
mostly-black/partial-caption observation. No expanded inventory is pending.

## EXP694 package710 ready for exact preregistered test
Native RGBA source843665a2; package710 passes analysis/Universal/Inf2Cat and
catalog membership. Immutable archive SHAb219c80f4616e985a85a8c86a1458add5848e326906ca190bb30dd39a0873807.
Air-side trust verification remains required during stage; no policy toggle.
Next exact stage/full-owner bind and once-only standard runtime experiment.
Physical mostly-black/isolated-caption observation is explicitly preregistered.

## EXP694 runtime DeviceRemoved; next diagnostic boundary
Package710 hardware startup PASS, standard device S_OK, RGBA format support
S_OK, but RGBA CreateSwapChain returns887a0005. No draw or Present. KMD remains
Code0 with no fresh monitored errors. Do not infer which resource guard/callback
failed. Actual caller parameters and SetError origin are now the causal target.
Physical mostly-black/isolated-caption observation is UNOBSERVED: operator was
away. No claim about symptom persistence or resolution is supported.
Exact cleanup and ordinary377/392 Code28 recovery verified after evidence.
HARDWARE ROADMAP
[PASS] Frozen contracts, RGBA offline create/open/RTV/blit and x64/ARM64/package
710 gates, actual KMD/device startup and runtime RGBA support.
[NOW] Diagnose actual swap-chain DDI rejection using bounded opt-in records and
GetDeviceRemovedReason. No guard/capability change. Existing executable gates
confirm behavior preservation; diagnostic file is then collected on Air.
[NEXT] Fix only the identified cause with deterministic proof where possible;
exact sign/hash/preregistration then immediate once-only hardware test.
[HW] Standard swap-chain, real draw, physical completion and DXGI Present.
POST-HARDWARE: accelerated desktop stability and desktop acceptance.

## Resource diagnostic offline gate PASS
Opt-in APPLE_AGX_UMD_TRACE_FILE records actual projected CreateResource inputs,
originating frontend SetError, downstream allocation/import/open and runtime
SetError; max128 attempts/process and saved last-error restored. Client reports
GetDeviceRemovedReason after failure without replacing original HRESULT.
resource-trace-20260920mh x64 execution and ARM64 link PASS, same source archive
e01ead729e5068b68893c4e7fc401722eb23bfc2d373818c8718f8625f11b0f6.
Additional x64 trace-enabled execution exit0, 128 records, actual frontend-create
seen. Existing resource/capability decisions unchanged; no artificial RED added
for receipt-only instrumentation. Source review found no behavior-changing issue.
Next commit/package711, exact preregistered EXP695 with per-client log enabled.

## EXP695 exact diagnostic package preregistered
Package711 source2341240c, analysis/Universal/Inf2Cat/catalog gates PASS.
Immutable archive SHA8c9b621755b9779a5745c32cafdf45d2645b8e050902459f17d4beba5d02e965.
Next stage exact package and once-only trace-enabled standard client after
full-owner bound/hash gate. No resource policy change.

## EXP695 identifies sampler-binding error; physical partial UI observed
Exact standard process emits SetSamplers E_NOTIMPL four times, before any
frontend-create trace; runtime device reason887a0020. No resource shape or
Offset/Count is yet measured. Source range restriction (Offset0/count<=1) is
incompatible with ordinary D3D10 sampler-state ranges; review/fix this state
contract with deterministic range/no-mutation tests, not resource guard changes.
Physical CHANGED: operator saw initially black screen, then wallpaper with black
taskbar/open Start menu, before error commentary and before package cleanup.
May have appeared on pressing Start (tentative). Photo/hash are in EXP695 ledger.
No normal/accelerated desktop claim. EXP694 remains explicitly unobserved.

## Sampler range correction in verification
Source inspected: Microsoft PFND3D10DDI_SETSAMPLERS critical-error contract and
D3D10 16-slot API; pinned Mesa Shader.cpp/State.h range storage and Asahi
agx_bind_sampler_states valid-mask/count update. FULL GRAPHICS state contract;
no new texture-rendering capability or initialization/power owner change.
Actual EXP695 proves the rejecting function, not its unlogged parameter values.
The projection now prevalidates the entire legal range/ownership, updates only
that range and makes zero count a no-op. Existing diagnostics add stage/offset/
count/absolute non-null mask for the next observation. Native draw validators
remain unchanged. Executed host projection RED then GREEN; actual frontend
PS/VS/GS full null reset and non-textured draw with unused slot15 added to the
existing suite. sampler-ranges-20260920mj x64/ARM64 gates currently running.
Air recovery after EXP695 verified Code28/no package/service/files/trust,
8CPU/NVMe/input and no fresh events. Physical photo remains preserved as CHANGED.

## Scope decision after sampler-ranges-mk additional probe
Binding/reset correction compiles x64/ARM64. A newly added non-sampling draw
with an explicitly owned sampler in slot15 failed the actual x64 DrawReceipt,
then the old test continued into an AV. Preserve mk archive
aed94b4f76c62eb659806c036ebe48fe85a0a67817fe922529a92d7e55fbb751 and its logs.
One trace-enabled reproduction ended at sampler-range S_OK offset15/count1/
mask8000 and Flush E_FAIL; no exact emitter cause established. No validator,
count clamping, logical-state clearing or missing-edge suppression was applied.
This additional draw is not made a pre-hardware gate without evidence that the
selected standard runtime requires it. The client has no explicit sampler bind.
It remains an explicit APPLE_AGX_TEST_UNUSED_HIGH_SAMPLER opt-in RED probe;
first failure now stops that test instead of causing later unrelated faults.
Default gate retains actual high-slot binding followed by full null reset and
the original minimal draw/capture/retirement path. Host range/atomicity regression
passes. sampler-ranges-20260920mm x64/ARM64 gates are running. Next hardware must
record actual runtime ranges/non-null masks; if it reaches this draw limitation,
promote it to the current causal requirement. Do not claim high-slot draw support.

## Sampler binding/reset selected-path gates PASS
sampler-ranges-20260920mm x64 actual execution and ARM64 link PASS on source
archivebb6b6c2afb65e7fde9d9240a2e2d0b36e9d15c324bfbf73683c61bb6b14477dc.
Actual PS/VS/GS high-slot binding then full null resets preserve the original
minimal producer/materializer/KMD plan/patch/retirement path. This excludes the
explicit opt-in known-RED unused-high-slot draw and does not claim it supported.
Host range/zero-count/atomic-rejection tests and direct scanout guards pass.
Next package712/sign/hash and EXP696, retaining diagnostics to measure actual
runtime Offset/Count/absolute non-null mask. No further inventory expansion.

## EXP696 package712 preregistered
Source51a4201c selected-path x64/ARM64 and package gates PASS. Immutable archive
SHA5c841936cd4ae2757447dbca31bac9cbe72c38cd726bd2811279f2fcda3f4e60. Next exact stage and once-only standard
runtime test with actual sampler-range receipts. Known optional slot15 draw RED
remains documented; no claim of support or physical rendering yet.

## EXP696 standard swap-chain PASS; full default sampler state mandatory
Actual runtime binds16 non-null samplers in VS/GS/PS (maskFFFF), all bindings now
S_OK. CreateSwapChain S_OK; RGBA allocation/import, RTV/shaders/vertex buffer
advance to Draw/Flush. Flush E_FAIL then Present887a0005/device reason887a0020.
No physical completion proven. The former additional high-slot draw limitation
is now mandatory and its reproduction is always enabled with all16 owned states.
Physical partial UI persists/changes under Start/AltTab; three operator photos
and hashes are preserved in EXP696, separately from the client's Flush failure.
Exact cleanup and ordinary377/392 Code28 recovery are verified.

One bounded native receipt (default-sampler-boundary-20260920mo-x64) localized
failure to native draw body: backendFailed1, draws0, capture recording/root clean,
VS/FS sampler counts16. Source windows_graph_draw_supported explicitly rejects
any sampler_count without its one-texture special case. That pre-emission
classification, not USC packing overflow or sampler heap, is the exact defect.
Pinned XML ordinary Sampler fields contain no GPU addresses; full native table
span including reservedtxf is already captured by the USC table relocation.
Narrow fix permits ordinary no-texture sampler state up to16 with bounded valid
mask; keeps texture/image/SSBO/CB/custom-border guards and all capture/materializer
validation. Native emission/logical state unchanged; no clamp/drop/filter.
Source review found this correction justified. default-sampler-table-20260920mp
x64/ARM64 gates are running, with the measured all-owned16 draw mandatory.
HARDWARE ROADMAP
[PASS] Standard device, sampler-range state, swap-chain and resource creation on
Air (EXP696); prior selected producer/ARM64/package gates.
[NOW] Actual full-default-sampler native draw/flush/retirement gate with corrected
preflight classification. Required by runtime maskFFFF; deterministic offline.
[NEXT] Package713 analysis/sign/hash and preregistration, then immediate Air run.
[HW] First physical native draw/completion and successful standard DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP708 resident set proven; reliable KMD entry receipt NOW
All9 are RESIDENTINGPUMEMORY after successful wait, then pfnRenderCb EFAIL with
no KMD slot; Evict S_OK. Residency is closed. Cleanup complete. KMD Render guard
now mirrors synchronously to service key on every path; host tests5 PASS.
HARDWARE ROADMAP
[PASS] Native graph, locks, sequence, handles, residency and paging wait on Air.
[NOW] Package diagnostic-only KMD service-key guard mirror.
[NEXT] One Air client: obtain exact KMD guard or prove pre-KMD rejection.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP707 pre-client storage reset; EXP708 stable-interval retry
Exact package721 bound but two stornvme129 events occurred before the client, so
no diagnostic run. Cleanup complete and recovery launched. EXP708 may reuse the
same artifact once only after a 60s event-free interval with healthy NVMe; any
recurrence becomes the current environment blocker.
HARDWARE ROADMAP
[PASS] Offline post-wait aggregate diagnostic and package721.
[NOW] Restore Code28 recovery; then exact bind plus 60s stable storage gate.
[NEXT] If stable, one client; otherwise environment blocker.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP706 paging wait succeeds; Render remains current boundary
Valid package720 run: MakeResident all9 E_PENDING fence0x1b61, monitored-fence
wait S_OK, then sequence1 pfnRenderCb E_FAIL with no retained KMD receipt; paired
Evict S_OK. Async residency wait is hardware-validated but not sufficient.
Exact cleanup complete. Next add one post-wait aggregate residency receipt; no
submission change or broader inventory.
HARDWARE ROADMAP
[PASS] Explicit residency and async paging wait on Air; paired Evict.
[NOW] Confirm aggregate residency after wait immediately before Render.
[NEXT] If resident, isolate context/runtime validation before KMD; if not, fix
the exact residency synchronization contract.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP705 INCONCLUSIVE — package720 did not bind
Full-owner preflight showed Inf=null, no service/SYS/UMD or start stages. The
client was launched too early and used Microsoft Basic Render Driver; discard
its result. Exact staged oem5/task/trust cleanup completed and ordinary recovery
launched. Async residency-wait fix remains hardware-untested. Next causal target
is one fresh exact package720 bind with mandatory Code0/Running/Start12/
Platform14 and installed-hash gate before any client.
HARDWARE ROADMAP
[PASS] Offline explicit residency and async paging-fence wait; ARM64/package720.
[NOW] Restore verified Code28 recovery, then preregister exact package720 bind.
[NEXT] Only after bound preflight PASS, one standard client.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## Measured full-default-sampler native gate GREEN
The same always-enabled all16-owned VS/GS/PS reproduction is now GREEN after
only the proven preflight classification fix: native draw, flush/materializer,
KMD plan/patch, completion simulation and retirement pass. No optional bypass
remains for this measured case. default-sampler-table-20260920mp x64 exit0 and
ARM64 link0; source archive550d2649f1fdb9b3bd81dc499a7b39c973253e90ac5ab4d2d1cf15f3e3f03c35.
x64 exe5b25a794b649ed48761aeb3cfaac404aea65cb0e31436991997d9b69c6e2b3d5;
ARM64 exed617245af5cb141b43c173a089d3c91dfcd6691e4c639ada0de5c3d85e446e68.
Next package713/sign/hash/preregistration and immediate Air standard-runtime run.
Physical GPU completion/Present remain unproven; partial desktop photos remain
separate evidence rather than accelerated-rendering proof.

## EXP697 exact package713 ready
Source6083eca5, full-default-sampler native gates and package gates PASS.
Immutable SHAae721222d228a707740e760762857f52dc7ea8e974181d29d097fa3cf4ad5c46. Next exact stage/full-owner
preflight and once-only standard runtime draw/Present test; no readiness claim
for physical completion before its evidence.

## EXP697 remaining actual Flush seam
Package713 still returns Flush E_FAIL/Present887a0005 after successful standard
swap-chain and allocation/import. Do not assume the now-GREEN default-sampler
guard is the remaining cause. Native stderr did not appear in the client log.
No hardware completion or new physical-screen verdict claimed. Exact cleanup
and ordinary Code28 recovery verified. Next diagnostic-only slice sends native
state/bindings/capacities before/after clear/resolve/draw and flush, seal/dispatch
and pfnRenderCb HRESULT to the established bounded UMD file. Source review found
no behavior-changing issue; native-seam-20260920mr x64/ARM64 gates running.

## Native seam diagnostics verified offline
native-seam-20260921mt x64 real producer execution0 and ARM64 link0.
Diagnostic-enabled x64 execution0; bounded128 records include native state.
Both archives match all473 current driver source files; archive container hashes
differ from tar metadata only. ARM64 archive853f8f8876232230b88c2b1e9112bd27134e7119931e6eeb6a133600f37085ee.
mr failed only on diagnostic enum-to-UINT narrowing; explicit cast resolves it.
Air fresh read-only health:Code28/no package/service/files/trust,8CPU/NVMe/input,
no fresh monitored events. Next diagnostic-only package714 and EXP698.

## EXP698 exact native-to-Windows handoff boundary
Package714: real draw emits1, capture18refs/117relocs clean; native seal S_OK.
Actual pfnRenderCb E_FAIL80004005 is first failed measured seam; Present887a0005
reason887a0020. Existing KMD guard/correlation compiled out in production, so no
KMD entry conclusion from absent receipt. Source/WDK review finds UMD arguments
coherent. Next diagnostic-only task exposes existing KMD Render guard/result
with bounded client correlation; do not change composer or enable qualification
behavior. Physical operator confirms EXP698 damage PERSISTS. Exact cleanup done;
ordinary377/392 recovery launched.
HARDWARE ROADMAP
[PASS] Actual standard runtime native draw/capture/seal EXP698; offline producer
retirement, ARM64 and exact package gates.
[NOW] KMD Render entry/guard discriminator for actual pfnRenderCb E_FAIL.
[NEXT] Causal owner fix after receipts; deterministic gate and exact package.
[HW] Successful Render/Patch/Submit, physical completion and standard Present.
POST-HARDWARE: complete desktop composition and acceptance.

## EXP699 bounded KMD receipt candidate gates PASS
Existing Render guard export moved outside qualification-only compile guard.
Fixed32 process-owned slots preserve client against DWM writes; entry16ULONG
scalars captured once, exit overlays guard/status. PASSIVE check, checked
registry handle open, service fallback for earliest invalid context/device.
No Render decision/capability change. Source review correction applied.
Host transport/correlation/decoder4 PASS; package715 UMD/KMD analysis0/0,
Universal/Inf2Cat/catalog PASS. Native/UMD inputs unchanged from mt verified
producer; archive6e386d3c840b96410fd416e5e66a0be1128b50bae048ecc75fd013f71ce409f7.
Recovery after698 verified Code28/no package/service/files/trust,8CPU/NVMe/input.
Next exact signed715 standard client once; missing matching receipt alone remains
inconclusive. Physical damage EXP698 operator verdict PERSISTS.

## EXP699 and current locked-allocation boundary
Package715 repeats native seal S_OK then pfnRenderCb E_FAIL; no matching KMD
receipt is retained in device, Device Parameters, or service keys. Exact SYS
contains the logger, but write availability is unproven, so absence remains
inconclusive. Operator reports corruption PERSISTS; photo SHA03ff5ac4d80090c459241df4982236c24a7c2a43e3f03b46c899b7369f7a0926.
Exact cleanup and ordinary377/392 recovery after rescan are verified Code28/no
package/service/files/trust,8CPU/NVMe/input/no events.

Microsoft's lock callback contract requires the UMD normally to unlock cached
allocation mappings before Render. Current native BOs remain mapped across
pfnRenderCb. Actual producer RED render-unlock-red-20260921mw-x64 exit17 proves
17 participating native allocations are still mapped at the callback boundary.
Implement request-scoped unlock after immutable capture/seal, invalidate the
native cached pointer atomically, and allow lazy remap only after submission
holds retire. Do not infer local-memory placement or exact HRESULT from the
contract conflict alone.
HARDWARE ROADMAP
[PASS] Standard runtime native draw/capture/seal on Air; exact EXP699 cleanup
and recovery; deterministic mapped-allocation RED at Render.
[NOW] Unlock submitted native allocations before pfnRenderCb while preserving
capture holds, lazy remap and same-fence retirement; x64 execution+ARM64 link.
[NEXT] Exact package/sign/hash/preregister, then immediate Air discriminator.
[HW] Successful KMD Render/Patch/Submit, physical completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## Submitted native allocations unlocked before Render — offline PASS
Microsoft's lock contract and the mw RED identified the exact deterministic
violation: submitted native allocations retained pfnLockCb mappings at the
pfnRenderCb boundary. The request-scoped fix validates every deduplicated
allocation identity, performs one pfnUnlockCb before Render, atomically clears
UMD, native-BO and transport mapping state, and preserves source/submission
holds until the existing fence retirement. Persistent BOs remap lazily only
after retirement. No allocator, command composer, physical mapping, capability
or submission architecture changed.

The first GREEN attempt exposed a mirrored Transport.Mapped teardown defect;
clearing it with the same successful unlock restored zero-ref collection.
The frontend stream-output fixture now performs its CPU update after retirement
and proves the real lazy-remap path. render-unlock-20260921ne x64 execution0 and
ARM64 link0; both source archives match all473 current driver files. x64 archive
9be2e77ec1b7c5c102a11ce986bc6a81d6b1a424fa07e3ca8be00cbbd7a6c545;
ARM64 archive75d95f0d94b40208c572747189645e49435aea09622ae4578c366e05906d3072.
Host transport/correlation/sampler/change-ledger tests7 PASS. Next exact package
build/sign/hash and preregistered Air run; physical Render acceptance remains
unproven.
HARDWARE ROADMAP
[PASS] Actual standard draw/capture/seal; documented unlock-before-Render full
offline lifecycle including retirement, lazy remap and teardown; ARM64 link.
[NOW] Exact package analysis/sign/hash and preregistration for Air.
[NEXT] One standard-runtime Air discriminator; if Render advances, collect
KMD Render/Patch/Submit/completion/Present evidence before any new scope.
[HW] Successful physical KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP700 exact package716 ready
Implementation ffc5e6cd and render-unlock ne gates are packaged. Package716
analysis/Universal/Inf2Cat/catalog PASS; immutable archive SHA
2e48f9996ed9ae756814a2fcab78901381f9e58aad1c3c2e3e016b41cbdb15db.
Current Air recovery baseline is Code28/no package/service/files/trust,
8CPU/NVMe/input/no events. Next exact stage/Valid-signature check, full-owner
bind and one standard client. No physical Render/Present claim before evidence.

## EXP700 pre-client environment failure; EXP701 exact retry
Package716 exact bind/Code0/Start12/Platform14/signature gates passed, but one
fresh stornvme129 appeared before the client. No client task was created or run,
so Render-unlock hardware remains untested. Exact cleanup and ordinary377/392
recovery after rescan are verified Code28/no package/service/files/trust,
8CPU/NVMe/input and no events. EXP701 reuses the same immutable package once;
if the pre-client Event129 recurs, stop the retry path and treat the environment
as the current blocker. Otherwise run the single preregistered client directly.
HARDWARE ROADMAP
[PASS] Offline Render unlock lifecycle, ARM64/package/sign/hash; exact EXP700
cleanup and clean ordinary recovery.
[NOW] EXP701 full-owner pre-client health discriminator with unchanged package716.
[NEXT] If clean, one standard client; then locate the first Render/KMD/AGX/Present
boundary and continue causally.
[BLOCKED] Only if the pre-client storage reset repeats.
[HW] Successful physical KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP701 rejects locked-allocation causality; RenderCBSequence is NOW
Clean pre-client package716 gate and one standard client reproduce native seal
S_OK then pfnRenderCb E_FAIL/Present887a0005. No KMD Render slot is retained.
Four stornvme129 events follow the client at ~10s intervals; correlation is
recorded without GPU attribution. Exact cleanup and ordinary recovery after
rescan are verified Code28/no package/service/files/trust,8CPU/NVMe/input/no
events. No EXP701 physical-screen observation was supplied.

The pinned WDK and official D3DDDICB_RENDER contract require a unique
RenderCBSequence: single-threaded UMD contexts start at1 and increment for each
pfnRenderCb. This UMD advertises threading Caps0 but currently submits zero.
That is the nearest deterministic dxgkrnl-side rejection cause and is mandatory
for the selected callback; add no broader contract inventory.
HARDWARE ROADMAP
[PASS] Native producer/capture/seal; correct unlock/retirement/remap lifecycle;
ARM64/package gates; exact EXP701 cleanup and recovery.
[NOW] Implement monotonic single-thread RenderCBSequence for every pfnRenderCb.
Offline gate: actual clear/composer/native callbacks see1 then increment without
wrap; x64 execution and ARM64 link. Fully offline until package verification.
[NEXT] Exact package/sign/hash/preregister, then one Air client directly.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## Mandatory RenderCBSequence offline PASS
The exact callback now receives the pinned-WDK-required single-thread sequence:
first value1 and monotonic increment for every pfnRenderCb from one UMD device.
Threading Caps remain0; wrap is terminal. Both legacy clear and native composer
share the counter. ng RED exit15 proved zero on real callbacks; nh x64 execution0
and ARM64 link0 share archive
b4e353906c1e8e2247899d954816b1809ed0ca9c78bb63480112e0078803a44f, matching
all473 current driver files. Actual producer through retirement/remap/teardown
and host7 PASS. Next package/sign/hash/preregister and direct Air discriminator.
HARDWARE ROADMAP
[PASS] Native graph/capture/seal, unlock lifecycle, mandatory RenderCBSequence,
x64 real producer and ARM64 link.
[NOW] Exact package analysis/sign/hash and preregistration.
[NEXT] One Air client; collect first KMD/AGX/Present boundary and storage events.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP702 package717 ready
Mandatory RenderCBSequence implementation f4b1e987 is packaged. Package analysis,
Universal/Inf2Cat/catalog gates PASS; immutable archive SHA
308816af6bb056293cab4acdc917e85ee69616f82d3b268bb556d59d3de1012e.
Air recovery is clean. Next exact Valid-signature stage, full-owner bind and one
standard client. Physical KMD/AGX/Present advancement remains unproven.

## EXP702 rejects RenderCBSequence causality; allocation residency is NOW
Clean package717 pre-client gate and one standard client still return
pfnRenderCb E_FAIL/Present887a0005 after native seal. No KMD Render slot is
retained and no fresh monitored event occurred. Mandatory sequence remains a
correct contract fix but did not advance hardware. Exact cleanup is complete;
ordinary recovery is running. No EXP702 physical-screen observation supplied.

The official pfnQueryResidencyCb contract permits querying an allocation list at
any time. The next diagnostic-only discriminator queries each of the nine exact
submission handles immediately before Render and logs HRESULT/status/handle,
plus context/sequence/count. This separates invalid or stale allocation handles
from context/runtime rejection without changing submission data or KMD policy.
HARDWARE ROADMAP
[PASS] Native graph/capture/seal, unlock lifecycle, mandatory RenderCBSequence;
EXP702 clean standard attempt and exact cleanup.
[NOW] Opt-in per-allocation residency diagnostics at the pfnRenderCb boundary.
Offline gate: existing producer remains GREEN and trace records are bounded.
[NEXT] Exact diagnostic package and one Air discriminator.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## Per-allocation residency diagnostics offline PASS
The opt-in standard-runtime trace now queries each submitted allocation handle
individually immediately before pfnRenderCb and records index, handle, allocation
flags, residency status and HRESULT. It also records context, mandatory sequence,
allocation count and command bytes with the callback result. With diagnostics
disabled there are no added callbacks. nk x64 execution0 and ARM64 link0, archive
cbb1d2d093feede8d1da9bad7060d62527aa5104bdb5ee2d5e54b00bb446c4b5;
trace-enabled x64 execution0 writes9 residency records and1 callback record in
the bounded128 log, SHA6a6e25e6d0046c8c65e0d6b6bf0e3f664cd0b43eaeb07328221b021431363886.
Air recovery after EXP702 is clean Code28/no package/service/files/trust,
8CPU/NVMe/input/no events. Next exact diagnostic package and one Air run.
HARDWARE ROADMAP
[PASS] Native graph/capture/seal, unlock lifecycle, RenderCBSequence, bounded
per-allocation residency diagnostic, x64 actual producer and ARM64 link.
[NOW] Exact diagnostic package/sign/hash/preregistration.
[NEXT] One Air client to identify invalid/stale handle versus context/runtime.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP703 package718 ready
Residency/context diagnostic implementation08c71ca7 packaged; package analysis,
Universal/Inf2Cat/catalog PASS. Immutable archive SHA
b214b8658dfac4f1dc643b9406e9034866b0543100c0fc714ee2604044daf927.
Air recovery is clean. Next exact stage/Valid-signature bind and one client.

## EXP703 proves explicit residency is the current mandatory boundary
All9 submitted handles are valid (QueryResidency S_OK) and all9 are
NOTRESIDENT immediately before sequence1 pfnRenderCb; entry8 is the write target.
The callback then E_FAILs before a retained KMD Render receipt, exactly matching
WDDM2+ scheduler rejection of nonresident references. No monitored event or new
physical observation. Exact cleanup complete; ordinary recovery launched.

Implement one device paging queue, MakeResident for the deduplicated sealed set
before Render, and paired Evict only after same-fence retirement. Handle partial
failure atomically and preserve holds. Existing KMD BuildPagingBuffer owns the
physical page-in/out. This is required by the actual WDDM3 runtime and does not
reopen GPUVA or change the physical patch-list architecture.
HARDWARE ROADMAP
[PASS] Native graph/capture/seal, unlock lifetime, RenderCBSequence; EXP703 exact
handle validity and nonresidency proof.
[NOW] Device residency lifecycle: paging queue + MakeResident before Render +
paired post-fence Evict. Offline gate must cover success, partial failure,
callback failure, retirement retry and teardown; then x64/ARM64.
[NEXT] Exact package/sign/hash/preregister and one Air client.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## Explicit WDDM2 residency lifecycle offline PASS
The UMD device now owns one normal-priority paging queue. Each sealed deduplicated
submission set is MakeResident with a final required attempt before Render;
partial/failure paths evict any acquired prefix and never submit. Residency is
held across callback/fence lifetime and paired Evict occurs only after the same
fence signals, before fence retirement and source-hold release. Paging queue is
destroyed during device teardown. Existing physical KMD BuildPagingBuffer owns
page movement; GPUVA and allocator/composer architecture remain unchanged.

explicit-residency-20260921nn x64 execution0 and ARM64 link0; archive
8a66826b8e3cf70fb78cb767c6ddfe7c68c294a1e8bc75100a868c54d15a31df matches
all473 driver files. Actual Render mock requires ResidencyHeld; retirement and
teardown prove paired Evict. Host tests PASS. Air recovery after EXP703 is clean.
Next exact package/sign/hash/preregister and one standard-runtime Air attempt.
HARDWARE ROADMAP
[PASS] Native graph/capture/seal, unlock lifetime, RenderCBSequence, valid-handle
residency proof and explicit MakeResident/Evict offline lifecycle; ARM64 link.
[NOW] Exact package analysis/sign/hash and preregistration.
[NEXT] One Air client; collect KMD paging/Render/Patch/Submit/completion/Present.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.

## EXP704 package719 ready
Explicit residency implementation214d23be packaged. Analysis, Universal,
Inf2Cat and catalog gates PASS; immutable archive SHA
8dd203bf40d0f625fa2061dffc473670887801d2ef3d5f5f1abc6d0f1380afed.
Air recovery clean. Next exact stage/Valid signatures/full-owner bind and one
standard client; physical paging/KMD/AGX/Present remain unproven.

## EXP704 accepts residency; paging-fence wait offline PASS
All9 allocations entered the residency list; MakeResident returned E_PENDING
with fence0x1b61. The candidate evicted and stopped before Render, so no KMD
result. Official required wait is implemented using PagingSyncObject and
PagingFenceValue. nq x64 execution0 and ARM64 link0, archive
e7ed6a3a58bc13ffbf02ca19595288e32aa056166b03df3799c79d42b2980595;
forced-async mock proves wait before Render and paired post-render Evict.
Exact cleanup complete; recovery launched.
HARDWARE ROADMAP
[PASS] Native graph/capture/seal, unlock, sequence, explicit residency and
mandatory async paging-fence wait offline; ARM64 link.
[NOW] Exact package/sign/hash/preregister.
[NEXT] One Air client; expected first KMD Render/Patch/Submit evidence.
[HW] Successful KMD Render/Patch/Submit, AGX completion and DXGI Present.
POST-HARDWARE: complete desktop composition and desktop acceptance.
