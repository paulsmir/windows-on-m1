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
