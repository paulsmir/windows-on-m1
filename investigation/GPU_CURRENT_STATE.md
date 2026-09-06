# GPU current state

Updated2026-09-06T17:11Z. Authoritative live state; read first after context reset.
Detailed handoff: .local/experiments/EXP506-cdd-blt/handoff.md.
Historical snapshot: .local/experiments/EXP506-cdd-blt/state-before-final-compact.md.

## Binding scope and current executor

The post-EXP506 model handoff is complete. Latest user instruction explicitly
authorizes continuation here by the current main process without agents. No
parallel implementation or hardware executor exists. Continue autonomously from
the exact Submit boundary through the original AGX/OpenGL/CS1.6 goal. No ANS
changes; preserve unrelated dirty work/evidence. Final proven driver stays active.

## EXP506 result and next boundary

Exact30.0.506.0 R2 ran ONCE after hash-verifiedoem5 staging. Host saw A408swap9 ->
exactD589latch9 then WindowsPSCIreset before SSH. RecoveredEvent41Record18293:
0x119(0x2,0xffffffffc000000d,0xffffe18baf662a00,0xffff980aa7910a60).
Subtype2 is failed submission; arg3 is SUBMITCOMMAND, not PATCH. Address matches
candidate reset stack region. Exact route/guard/arguments remain unknown.
No fresh minidump/MEMORY.DMP; volmgr46 dump initialization failure. No Present or
PresentTransfer receipt persisted. SourceAddressStatus0 remains proven.
VERDICT REJECTED as successful submitted-BLT/completion; exact guard INCONCLUSIVE.
No copy/fence/AGX TA3D/OpenGL/CS1.6 proof follows from the physical display latch.

Evidence relative to .local/experiments/EXP506-cdd-blt/:
hardware.log SHA92d6975bde894613b0346c922dfe768fadba82b9f2aae672384e7e6bf3919766;
observation/system-events-full.json SHAf3d6a105f7c4fd579fbb987d21dabfe1a71cf062573689f28a2abff08814b09c;
admission.etl SHA964c20e6cf2f4cea262ced5478a98f220120401769a52fb1e34b3f3f369f318e.
Guest/host clocks differ. Ordinary recovery reset has no separate stop record.

EXP507 commitf9ed33077e8ca377b3a5497ceef87eeb137bb531 ran ONCE and is
CONFIRMED discriminator / rejected functional candidate. Crash-durable trace and
fresh dump independently prove first Submit: IRQL2 Flags2 routePresent,
privateStage0, exact context/resident locations, DMA4096 range0..184,
privateSize8192 range0..0, fence253,node0,engine0, guard15
PrivateEndLow -> C000000D ->119/2. Do not rerun507. Hardware log SHA
d8bb2efd64c8b90197252f8ec0256db8fd684d1eaa764d3871f5bc04ea54a5b5;
decoded SHAb990ca15ebbe395397de67c889501be0054a7be5a7f6ce4e8545acf1132f3849;
dump SHA592eb813ac64adaeb9ea6c930913f463595eb958f17358bb236853873e33b3c2.
No copy/fence/AGX render proof.

Causal fix commit413704ab53d9a062fc4f5e1b4c521cb856c5c811 accepts zero
private submission end for nonpaging while still rejecting nonzero ends below
validated shadow BytesUsed and all ends above DmaBufferPrivateDataSize. Microsoft
documents start0 for nonpaging and the range as only the associated portion of
the full private buffer. Actual RED then GREEN;92 render+12shared tests PASS.
EXP508 HARDWARE PROVEN: first Submit guard0/status0; complete2560x1600x4
CPU-assisted copy16384000bytes; exact fence253; DMA_COMPLETED NotifyInterrupt1
and NotifyDpc1. Receipt SHA451e759d1f484ee94d94ca8434feaaceac0c554ba26c77ce82ea23f1a6401465.
This closes CDD presentation copy/fence only, not AGX rendering. Next boundary is
interactive Windows GDI producer -> RenderKm/Patch/SubmitRender -> physical AGX
TA3D/completion. Prepare qualification receipts before requesting local sign-in.

EXP509 current: generic nonpaging range commit
c264acc724f24e73a2b47c2708971c46e37a3a5b and GDI hardware receipt commit
a1a27c4c5459e9bf46f153fb7089ef5bbcab8975; R2 IRQL-analysis-only correction
bb0c656d9d07fe863e2814d007833c1d46224993.94render+12shared PASS. Pinned
R2 normal+SubmitQualification build/sign/Universal/analysis PASS with only
inheritedC28251. Exact30.0.509.0 ZIP SHA
2ba514c403240ef425dd08cdb7ceb0331f7d1c4afea365c25d0bd270162cf487;
SYS SHAef51d5838ff6f443fda62fba44c6009b9635f2eea856d1dd42b06fc75b226f13.
R1 prohibited from staging dueC28167; R2 is installed and active once.

At17:11:04Z R2 live: SSH8CPU/APPL0002 Code0/serviceRunning/input/xHCI/NVMe;
InteractiveUsernull; no GDI receipt or0x5090 trace before workload. This proves
clean no-workload control only. Fullowner launcher exec36440/Python75166 soleL41;
L43present/unowned. KEEP RUNNING. Await local sign-in and one execution of
C:\Users\pavel\EXP509-LIST-GDI.cmd; it only lists displays and writes a log.
Then derive exact display/LUID and run one explicit draw. No cleanup/reboot yet.

Operator reports black physical panel. Read-only17:13 reanchor: Windows healthy;
session1 winlogon/LogonUI/dwm running, exact DCP swap9/latch9 occurred, but no
later latch and no GDI receipt/0x5090 trace. Current primary contents are black;
this is not a crash verdict. Before blind login, request one Space/touchpad input
to trigger natural LogonUI damage; then inspect new DCP/Present/GDI receipts.

Space input produced no new latch/Present/GDI receipt; Windows remained stable.
Next minimal physical action is blind local sign-in only. Confirm InteractiveUser
over SSH before asking to run the prepared read-only list helper. EXP509 remains
installed/Code0 with PID75166 soleL41; no reboot/cleanup.

Blind sign-in succeeded: InteractiveUser J313-WIN\pavel, Explorer+DWM session1,
LogonUI gone. Physical swap10/latch10 occurred; no GDI receipt/0x5090 trace yet.
Await one Win+R execution of C:\Users\pavel\EXP509-LIST-GDI.cmd, then read exact
display/LUID over SSH. Candidate remains installed and stable; no cleanup/reboot.

List helper completed: exact target \\.\DISPLAY2, attached/nonmirror state0x5,
source0,LUID high0 low0x3eb39; draw_calls0 and clean result0. Prepared exact one-
draw C:\Users\pavel\EXP509-DRAW-GDI.cmd SHA
b92665bc679bf7d6758c72cda1fd6dc8aa8ca51af7e5f2091cdbc4e3fa11bcd8;
remote hash verified and output absent. Await one Win+R execution only, then
collect producer/0x5090/GDI receipt/host evidence. No reboot/cleanup yet.

Offscreen draw actually ran once and log result0, but no KMD/GDI trace/receipt:
REJECTED as hardware producer, Windows used software DDB path. Do not repeat.
EXP510 commit6cb52954096b98b027e14c3d0ed496b06f558b39 then ran one exact
direct display-DC PATCOPY for DISPLAY2/LUID0:3eb39. Producer returned result0,
but no0x5090/KMD receipt or new DCP work appeared: REJECTED as KMD producer,
driver not rejected. Output SHAab41a1bf80ee8b93b93359a37b5392c0f03d9b13615edc2747b114bedc0ba735;
temporary task removed with cleanup SHAb8005df0704d7cf3bdbaf2d2358a98d62ea96692e79ae95aa53a570c0237924a.
APPL0002 remains Code0, AppleAgxAdmission RUNNING,8CPU/SSH live and exact EXP509
R2/PID75166 soleL41 retained while deriving the next supported D3D runtime/UMD
producer. Do not repeat GDI producers or restore TestContext/private one-shot ABI.

EXP511 current: commits a0817e3dcba326e07d2f4b32987a24c98af3330e,
5cacde457d4c7111ff7a98820dc38c9b11bbd3fc and
f85bec3f6afbfee80fff460491984e439dbda4f6 implement the documented normal
D3DKMT/OpenGL-style flags0 context and DxgkDdiRender->Patch->Submit seam with a
pointer-free48-byte command and no TestContext token.98 render tests PASS. Exact
30.0.511.0 pinned R2 build/sign/Universal/analysis PASS; ZIP SHA
dea30440fbe3d977974139a7c2eb921f9f38007826e626628043b87d233db5df,
SYS SHA7a6dac96eac9372280c0768a0f5e4744e7acc9ceec8b539eb83322a378c441cb,
producer SHA3c576b5b46c5d320516bb55d3f547cba0c7f7b6e6863d9e37c90e892992c0b43.
EXP511 is preregistered. Next execute exact EXP509 evidence/cleanup, restore
ordinary Code28, then one EXP511 natural bind and one SSH producer invocation.

EXP511 live18:35Z: exact30.0.511.0 bound Code0/serviceRunning/hash-matched with
8CPU/SSH/platform healthy and no fresh fault events. PnP LUID property is null;
session0 GDI enumeration returned zero without invoking render. Producer-only
commit d6314b273816ed86892c392574c0ca30c932865e uses supported bounded
D3DKMTOpenAdapterFromGdiDisplayName on exact \\.\DISPLAY2. R3 ARM64 static build/
analysis PASS and remote SHA
27a74e32c1433a1854c5d0cb9a8f59b94b07a6d41f9ab8098250b5620b2a59c8 matches.
No workload has run. Next action is exactly one R3 invocation over SSH, then
collect receipt/trace/health before exact cleanup.

R3 returned C0000001 at GDI-name open in session0 before any KMD callback:
INCONCLUSIVE BEFORE DRIVER. R4 kernel-inventory source compiled but the pinned
ARM64 import library omitted modern EnumAdapters3, so no artifact. Commits
fc897a8c6fddb41e118480358845af9765ac422b and
a2a7562595de85b3f07422c73f445a79843c6dc0 select exactly one nonsoftware
render+display adapter with one source and resolve the real gdi32 export
fail-closed. R5 build/analysis PASS; remote SHA
78c2b86d2303fcf3661ee8d5941fe0ce8e9a086e65cac0183f02450e94106382.
Driver30.0.511.0 remains Code0/unchanged. Next is one no-argument R5 run.

R5 enumerated2 but strict predicate matched0; all driver stages unexecuted.
Commit940622911506de81b801fb6aa296eb5fc5010026 adds read-only per-adapter
LUID/source/query/type scalar output. R6 analysis build PASS and remote SHA
385519319b7a467add06d4e7c16c795dbe385bffcda0e0b1ed918147153ea7d0.
One R6 diagnostic run is next; no driver/package/platform change.

R6 exact inventory: hardware Apple adapter LUID0:262440,type0x10b
Render+Display+Post+ACG,sources0; software adapter LUID0:25221,type0x105.
Commit18393385522a92f189035a4f674389430076cf60 removes only the invalid
session0 source-count condition while preserving exact-one hardware identity.
R7 analysis build PASS; remote SHA
3c9e342ed916fecc166b3a960d0a9c14e6809fa1f9be3f8273010fa5ef78bc2f.
Next is one exact R7 run against unchanged Code0 driver30.0.511.0.

EXP511 FINAL: exact R7 selected Apple hardware adapter; device/context/allocation
all status0; D3DKMTRender C0000001; every cleanup status0; Windows Code0/healthy.
No0x5090 Submit trace or GDI receipt: Patch/Submit/backend/TA3D did not execute.
Exact cleanup and ordinary recovery complete; health SHA
ca832b49292bce7b4879a0560dff045cdbad28cd47a9a9bd03be139037f671d9.
One Code28/null INF/service, no package/SYS/UMD,8CPU/SSH/input/xHCI/NVMe healthy,
no fresh fault events. Do not repeat511.

EXP512 current: commit c5b246fec3918ab72a96a4fcd0f010cef8abc196 adds
qualification-only crash-durable0x5120 UMD Render args/copied-command/numbered-
guard/status trace.101 render tests and pinned30.0.512.0 build/sign/Universal/
analysis PASS. ZIP SHA
b50191190f3fb6c03bea96ae75133ed3366b32e43c159e57cc6a12bcd02fa931,
SYS SHAb4a8cf4c9a61ba849639855148715ecd90d9e5dc5bd0d5470cb2aac643cca6fd,
producer SHAf4a26107a5a0544b23fc5df28ad5e49fd66ca711971af2190f2c81747edbb997.
Preregistered next: one natural bind and one producer run; decode exact first
Render guard, cleanup, then fix only that guard.

EXP512 FINAL: exact30.0.512.0 Code0/healthy; producer device/context/allocation0,
RenderC0000001; zero0x5120 Render-entry words, zero0x5090 Submit and no hardware
receipt. This proves failure before KMD DxgkDdiRender. Exact cleanup and ordinary
health SHA c795df040e42d4593c1e048cd08ce86318348d60d993f9d6d628a59d5d680c2f;
Code28/no package/service/files,8CPU/SSH/platform healthy. Do not repeat512.

EXP513 current: Microsoft says dxgkrnl converts the Render allocation list via
DxgkDdiOpenAllocation before Render. Commit
d9e866cec2a77edb0895172801dbe56794b7fd6c adds qualification-only0x5130 exact
OpenAllocation input/guard/status trace;103 render tests PASS. Pinned30.0.513.0
build/sign/Universal/analysis PASS. ZIP SHA
a3d9d2d77d19e645acae641f4b4c6984e63270b3e1d61f015045a51e11e0637d,
SYS SHA36e61aee166f197b8713a8170cbd346dfcb78a3fa42d641e39dcd6b3934035b1,
producer SHA369265698727db368e7e6c7d54fd559db8af0e8d534faa48ed8bd56366774652.
Preregistered next: one exact natural bind/producer, decode first OpenAllocation
guard, cleanup, then change only its owner.

EXP513 FINAL: OpenAllocation guard0/status0 exact receipt; Render still absent.
Cleanup/ordinary health SHA954f229f6d6f8bac427a016a143c8dd04a91856968eeb803975a411054c655da.
EXP514 commit1d8d6412f48d827ad5bf08e33e67241b645bc2ee records first
BuildPagingBuffer operation/status only;105 tests and pinned30.0.514.0 gates PASS.
ZIP95b914689aab1ac27f83f882301673deeb91f616780e368981b678d226c76c6f.
Preregistered next: one exact bind/producer then decode paging receipt.

EXP514 FINAL: global first BuildPagingBuffer op5/status0 was not correlated;
producer Render still absent. Cleanup/ordinary health9a355419da07fd27b4f8fc2aca864e2de4d7a2d85387b4f4b3d9c4774792452e.
EXP515 commit aed39ef56cf709d3405129a7ea6a3539918e618a adds qualification-only
allocation nonce correlation normalized before production validation and armed
only from matching OpenAllocation to CloseAllocation.106 tests and pinned515
gates PASS; ZIP9247498782b88cb66b4212f11b84847a3fd98172be8c9e268d151dc38731b1e9.
Next: one exact EXP515 bind/producer and correlated paging decode.

EXP515 FINAL: correlated OpenAllocation success, zero correlated paging calls,
zero Render entry; residency closed. Cleanup/ordinary health
71da7aa3a310003ffa6634b9139bc2b8340cca2d899c526d1dc64bcb8e687244.
EXP516 commit6e762320b2d8d3f7e8887e2f7cb4a5fa1fd539a5 adds producer-only
device/context/render buffer scalar output; build SHA
e2ab5db6d0265fbf8b92da0dcaeef11c69aaca490f66b736f7a493718d394974.
EXP516 FINAL: CreateContext returned valid4096/64/64 buffers and D3DKMTRender
used/returned those exact addresses with GPUVA0 and no resize. Render remains
C0000001 before KMD entry. Buffer owner/capacity/rotation closed. Exact cleanup
and ordinary health SHA
c30fb3b2dd7e2f3085bb0485e6a8c812b6da26d7f50214fac5e655fcdcec8542;
Code28/no package/service/files,8CPU/SSH/platform healthy. Do not repeat516.
EXP517 preregistered: reuse exact515 driver and516 producer, add only bounded
Microsoft-Windows-DxgKrnl ETW around the one producer call to identify the first
pre-Render Windows owner/status; then exact cleanup and causal next experiment.
EXP517 FINAL: session0 producer reached dxgkrnl Render Event169 with exact DMA
buffer; 3.1us later Event467 marked exact device error, FromUserMode=false,
Reason16. D3DKMTRender C0000001. Thus pre-Render/session/paging/buffers are
closed and EXP512 missing-DDI inference is superseded: first-global0x5120 could
have been consumed earlier. Cleanup/ordinary health SHA
e0e1c4b4a33c7b9b2c466b37a12659bbd1f076cd1276feb4fb636d4133603045.
EXP518 commit75784c6686f4e2c124951c3d3c4c400c7b63530d arms0x5120 only during exact
producer allocation lifetime.106 tests and pinned518 normal+qualification/
analysis/Universal/Inf2Cat/sign/version gates PASS. ZIP
e122914b25a9e118e118f7dbf93eb1fd67b1b0e691a07df8c5374f18613740c8.
Next: one exact518 bind/producer, decode the real KMD Render guard/status, exact
cleanup, then one causal correction only.
EXP518 FINAL: exact correlated OpenAllocation guard0/status0 again, but no0x5120.
This is inconclusive between callback non-dispatch and the two pre-trace context/
device guards. Cleanup/ordinary health SHA
d9d9b9ba889fec94d779355ceed6723e8152747666e49a42c40bddfeb69cc851.
EXP519 commit6d730bb16ff6c1c66ddf0f36518fae84f2affc0a arms exact adapter pointer at
correlated OpenAllocation and traces Render before context validation; normal
build unchanged.106 tests and pinned519 gates PASS. ZIP
5068cc42afa8203d462b87caa655963952514a6e5d92c28b1a03be989cbe5dfc.
Next: one exact519 bind/producer. A guard names the causal fix; no receipt with
successful0x5130 confirms callback dispatch owner and ends equivalent probing.
EXP519 FINAL: exact OpenAllocation success and pre-context adapter arm, but zero
0x5120. Therefore AdmissionDdiRender is not dispatched; all callback-internal
guards are closed. Cleanup/ordinary health SHA
9133d7eca50617e485de13aae4e43e5fc445560fbb5de5737beb5f8149037f67.
EXP520 commitdb2b8d4a8e8366fd692326aad5d76582d2fe027e changes only producer
CREATEDEVICE.LegacyMode=1 for documented OpenGL command-buffer client routing;
exact519 KMD reused.106 tests and pinned producer analysis PASS. Producer SHA
e65a8ffddb1b79721c33f375073b38cc9a2d08c8b84fcfcf68b2dbbf967bf67b.
Next: one exact520 run; require changed device classification plus0x5120 or reject.
EXP520 FINAL: LegacyMode rejected; device_flags stayed2, Open success, zero5120.
Cleanup/ordinary health231bb7625cbd3956496d8ce6719537f753727b4e2f048b3b052c160b13553c20.
EXP521 direct GDI-context path rejected offline by pinned WDK (GdiContext and
DXGK_RENDERKM_COMMAND are KMD-only); no hardware run. EXP522 commit e591b88 adds
supported CreatePagingQueue/MakeResident/exact paging-fence wait before Render;
producer SHAd8084560fa88a64ad184e94bb0d0ae6b4393fa547a13c043689972c15347f8fc.
Next one exact522 run; require paging operation/fence then Render progress.

## Final live ordinary clean baseline

At16:36:09Z after exact508 cleanup and ordinary restore: SSH8CPU,
AppleInput/USBXHCI/stornvmeRunning, no fresh41/46/129/161/1001 after this boot;
exactlyoneCode28APPL0002/INFnull; noAGXpackage,
service,CIMdriverentry,runningdriver,signedbinding,SYSorUMD. AppleInput/USBXHCI/
stornvmeRunning. No fresh41/129/1001/46/161. WPRnotrecording.
InteractiveUsernull/TermServiceStopped; RDP and interactive input/display not
workload-tested. final-ordinary-health.log SHA
1d262413da4fe1e7495be62e7f80e1982a4f9ce4dbd85c485d641f1780582594.
Launcher exec97110/Python60927, soleL41owner; L43present/unowned. KEEP RUNNING.
EXP508 ordinary-contract.bin SHAb1ab2b612813447ff812aae35544eb172266952ecc6b98e6ceafb4a279d6dcfd.
Always freshly check SSH/process/endpoints before any replacement.

Recovery: ordinary377/392 withR2 failed to reachSSH and reset. Emergency377/385
restoredSSH for evidence. Exactoem5 and phantom removed. Stale stoppedservice
Owners{oem5.inf} removed only after package/INF/devnode absence and exactidentity
checks. SYS/UMD moved to C:\Users\pavel\EXP506-observation\removed-orphans
and copied locally (recoverable, not installed). Final graceful restart restored
ordinary377/392. No candidate retry/unrelated removal/ANS change.

## Source, verification and artifacts

Branchfeature/j313-gpu-acceleration HEAD3f75dfed139987cab1819bb09480fb928dd1b553.
Featurec43a2352c91152b4baffed414e3008781b83955e implements deterministic buffered
CDD BLT/prepatch/repatch/submittedsnapshots/shared64FIFO/CPUtransfercompletion/
preemption/reset/worker-DPClifetime. R2 keeps reset locks in residenthelper.
1:1BGRA8888 sourcelocal2/aperture1 -> local2,64KiBbacking/logicalbounds retained;
general overlappingmultipass/arbitraryqueuepressure not established.
CPU presentation data transfer is NOT AGX rendering.89render+12shared testsPASS.
NativeR2 KMD/UMD/Universal/analysis/Inf2Cat/TestSign/versionPASS,0errors,
C28150absent/onlyinheritedC28251. R1 held and neverstaged. No source editsafterR2.

Finaldir .local/experiments/EXP506-cdd-blt/r2/:
source.tar.gz d9d4daa5d81ee689c7e2272deb9c7707f0baeb2fe96ab1c47134f71338a0860a
EXP506.zip89a3fa5a587e99627ea7daca35cf54ec404d9d499e1da0ac496171882856be1c
manifest.json99b9f71d363739a408cda35afe45d8d60929518b561bf058f8d18898e09aa69a
SYSfc5f0cf36e00a4a72ee77e4b5a148b4bdb0530e43dda983744d274f3cb7cf3b9.
Fullhashes/buildpaths/source scope inhandoff. Oldsealedpreservation unchanged,
not currentcandidate. No whole-repoGREEN claim: legacyCHANGESschema issues preexist.

## Accepted boundaries — do not reopen without contradictory evidence

475retained-root/broker/context0;477nativefirmware/initdata/DC_Init;
478BackendRuntimeStart/Ready/arena/context/queues;490SystemDevicenullruntime;
491SystemContextnullruntime;492onepageaperture;494StartDevice0/Code0/truthful
UpdateMonitorLinkInfo;495native2560x1600 total2642x1682 pixel266630000Hz;
496preservesOSMinimumVSyncFreqsentinel;497rawtoken/498legacyGetHandleDataREJECTED;
499modernAcquire/Release;500pagingPatchroutefix(noexecutionproof);
501CDDshadowmetadata;502CPUvisibleSupported3Preferred2/primarylocalonly2;
503internalIRQ0;504internallatchIRQseparatedfromOSVSyncsubscription,
SourceAddressStatus0+A408swap10/D589latch10;505actualFlags1bufferedCDDPresent.
FUNCTIONAL_READY_MASK14/14 is implementation readiness, not hardware proof.
No Windows-originAGXTA3D/repeatedcompletion/fence/OpenGL/CS1.6hardwarePASS.

## Immutable runtime/recovery and trusted tools

Fullowner broker1: .local/experiments/EXP477-initdata-firmware/m1n1-final.macho
b970a7fee599f384031487b715dc575ceb9e5129d248c0f726fca45f1758e9c0 plus
.local/experiments/EXP-20260904-406-coherent-abi-admission/J313_EFI-exp406.fd
c7ddcfb256ad20788b0a8a54ab87c42d42b4cbe7a94f701da632da6a079bf4a0.
OrdinarybrokerUNSET:
.local/experiments/EXP-20260903-377-secondary-cpu-receipt/assisted-boot/m1n1.macho
fae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a plus
.local/experiments/EXP-20260903-392-current-gpu-mu-publication/assisted-boot/J313_EFI.fd
16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06.
EmergencyONLY377plus
.local/experiments/EXP-20260903-385-hvc-single-page/recovery/J313_EFI-no-agx-autoboot.fd
279bd36ad3bbb1ee5e2393fa965343ea856b4c2b0dd4df2b2add6a8010e3f32c.
Neverold164/241; noBCD/NTFSarchaeology. SIGINTsnapshots/resumes;
SIGTERMsnapshots/resets; neverkill-9. Gracefulrestart/naturalexit/USBreset preferred.

Airpavel@192.168.1.37 key/Users/pavel/.ssh/air; builderpauls@FRYZZING
key/Users/pavel/.ssh/windows_builder. BatchModeyes/ConnectTimeout5/
StrictHostKeyCheckingno/UserKnownHostsFile/dev/null. RemoteCMD: uploadps1 then
powershell -NoProfile -ExecutionPolicy Bypass -File. SDK/WDK10.0.26100.0,
MSVC14.44.35207, invocation-localWindowsKits10override only. TrustedR2root
C:\Users\pauls\EXP506-cdd-blt-r2. Freeze exactpreviousarchive plus committedscoped
changes, neverentiredirtytree. CollectorEXP488-collect-current.ps1 uses-EvidenceDir.
