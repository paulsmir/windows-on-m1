# J313 GPU investigation — NTFS recovered; reconcile AGX packages

## Objective and constraints
Continue until correct stable physical desktop. Accepted graphics package: NONE.
User explicitly authorized reboot after the earlier live-update-only request.
No subagents or messaging other chats. Short falsifiable GPU checks; no prolonged
stability waits before correct physical output. Moving image fragments were real;
KMDPresent=0 never proved a physically blank screen.

## Workspace and control
Worktree: /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler
(physical /Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler).
Branch integration/ad04-windows-compiler. Artifacts are ROOT
/Users/pavel/public_windows/.local/experiments, NOT worktree .local.
Root checkout has unrelated dirt; do not edit it. Preserve foreign worktree dirt:
m1n1_windows, mu, untracked pauls@192.168.1.24.ps1 and EXP895 plan.
Air SSH pavel@192.168.1.37, key /Users/pavel/.ssh/air; known-hosts ROOT.local/
experiments/EXP641-standard-present/air_known_hosts. Builder pauls@192.168.1.24,
key /Users/pavel/.ssh/windows_builder. USB endpoints L41/L43:
/dev/cu.usbmodemC02HDNCCQ6L41 and /dev/cu.usbmodemC02HDNCCQ6L43.
Always check bounded SSH plus launcher/USB before physical requests or reset.
No raw USB/proxy NOP while a launcher owns the guest.

## CURRENT — EXP935 clear fix built; removing934 in normal recovery
935 source a1d58340b2b5fabdac897335c08853f261c16e8e fixes proven unbound RTV clear:
934 windowed SDK ce544e78 created swapchain/RTV, then E_NOTIMPL Clear line302 ->
DXGI_ERROR_DRIVER_INTERNAL_ERROR887a0020 beforePresent. Native fallback wrongly
required that target already be sole OM binding. Now temporary single-view FB,
real clear, restore Device::fb (MRT/depth untouched). Actual projected-body replay
RED old -> GREEN new underASanUBSan;11relatedtestsPASS. Separatefrontendprepare
suite blocked by preexistingworktreepath/dirtylocalreference; notclaimedgreen.
Plan analysis/EXP935-clear-view-contract.md. Do not expand depth/otherDDIs.
ROOT935=ROOT.local/experiments/EXP935-clear-unbound-rtv. Native/source553/WDK
0warning/hash/signature/PDBGUIDagePASS. SYS9d4c303c, UMDf6479d21, INF5f8dfe0d,
CAT870e682e (fullhashes kmd-build-receipt.json). Manifest0a59d69a/sourcezipb8d6a455.
Same m1n1bdcf/MuR143e54; no935installedyet. NewwindowedSDK source81df9375,
exeSHAce544e78e8f12e0d3e71f8b9f34c6c57f79c8490125fedb5ff13514ac4a4529b.
Firstverifyclear thenPresent/physical, shortbudget, no stabilityclaim yet.

934 original finalized hostgate359fb847a766cf7105e286b620ea390345e956eb567471bb8077df3c60581063
(ROOT934/original-final). Orderedrestart23:54:09Z completedPSCI itself; proposed
SIGTERMnot sent (ownerPIDalreadygone). Freshdualplane/proxy thenROOT934/launch-recovery.sh.
Normal377/392owner15638/session82269, recoveryevidence23:58:09Z Code43/exact934/
CPU8/SSH/armnull/Cnotdirty. One nonresetSIGINT snapshot duringnetworkstartup;
no physicalactionneeded. Exact934cleanup currentlyexec61422: outputprovesdevnode
andpackage removed, staged0/SYSfalse/UMDfalse; durablerestart scheduled, notyet
verifiedafterrestart. Pollcompletion/controlplanes thenROOT934/launch-clean-recovery.sh
(same377/392). Run ROOT934/verify-clean-baseline.ps1 afterboot; requireCode28/no
packages/services/signer/files/flush0, immutableKernelGeneral12 eventidentity.
Onlythencreate935manifestusingthatbaseline, stage andfullboot.935stage/orderedrestart/
first-checkpoint/query wrappers alreadyadapted from934; manifestandhardwaremanifest
notyetcreated. Preserve noexistinglogs; rootartifactsoutsideworktree. Seeledger latest.

## EXP934 original evidence (closed; do not treat this as live guest)
Fullowner96715/exec40531 (verifyfresh), package934/oem5, bootEvent122291 Start22:33:09.4783648Z (CIMshift22:33:55.992455),
firstready22:35:19.946, CPU8/SSH/Code0/Stage12/Status0/Armnull/HVC1/gen239153593.
Fixedm1n1bdcf8715+MuR143e54. ROOT934=ROOT.local/experiments/EXP934-direct-gpuva-submit.
Latest current-checkpoint.out completed. Fresh SSH and owner96715 alive22:56UTC. No934rollbackyet;
keepcurrentoriginalguestforphysicalobservation/read-onlyinvestigation. No nextexperiment.
Before935mustcollectthenremoveexact934package throughnormal377/392 recovery.

LiveframebeforeSDK: DWM1240submit27+2/complete26+2, VSync8110+, Present0/Virtual0,
TDRcaptured0. AfterSDK: submit140+2/complete138+2 last98685/12803, stillPresent0/TDR0.
Physical photos now CONFIRM repeated blocks and horizontal lines, NOT correct desktop.
Photos archived ROOT934/physical-evidence with independent SHA256, captureUTC unknown.
Correction: seq2 at120/300s was zero; existing600s snapshot contains15012 nonzero
pixels/hash35ce83ad3947aaed/corner00180003. Earlier zero reads do not describe whole run.
At22:56:37Z frame-after-photos.out: sameBoot, DWM140/138 and2/2 unchanged since prior
sample; generic submissions151678, allocationdestroy11216/11216, Present0/TDR0,
VSync79862. Background progress does NOT prove continued DWM render progress.
Current diagnostic verdict (2026-10-03 local):
D1 same DWM1240 snapshot at23:30UTC shows LPC idle WaitForNextMessage, rejecting
persistent CreateAllocation stall; stack-only dump47289f2e/103134B hostverified.
D2 existing original ETL stopped+archivedinternal
/Users/pavel/J313-evidence-archive/2026-10-03/EXP934-original/original-after-photos.etl
268435456B SHA33c53a09fb4ae1aedd4490e154c9d2c7081dc4c4455ed0e855acd941da70c7e1.
IMPORTANT EXP801DxgBoot session STOPPED; no activeETL now. Boundedfirst5000selected
rows only23:21:28..52:1463VsyncInterrupt/1463VsyncDPC/1462DWMVsyncSignal, DWMCount0/
Waiters0/hFlipDevice0. DWM17cycles presentneeded1 butdraw0/token0/refresh0/tPresent0.
So VSyncdelivery works and DWMwakes; don't infer permanentclockwaitfromstack.
D3 late SDK after observedidle stillnoCreateSwapChainresult at12s; SDKALPCproxywait,
pairedDWMLPCwaits realpagingfence0x26e2/object40001f40 insideSubmit/UpdateSubresourceUP.
This is currentboundary, NOT persistentpagingdeadlockproof. OwnedSDK2616 stopped,
ownedEXP934-LateSdk taskremoved; sixfileslate-sdk manifesthostverified.
D4 read-onlyprobeinconsole1 provesNumOfSources1 (SSHsession0 reports0); sourceabsence
hypothesis rejected. frame-console.decoded.out: DWM235/230 last190568 and2/2, noPresent.
EXP934-ConsoleQuery taskremoved. No934rollback/newdriver/firmware/reboot.
Next shortdiscriminator: --windowed variant ofsameSDK (nullfullscreenDesc), preserves
samebuffer/adapter/clear/present anddefaultfullscreen. Sourceeditpendingbuild/commit;
ROOT.local/experiments/EXP934-windowed-sdk build.ps1 currentlydispatched. Readplan
analysis/EXP934-windowed-sdk-discriminator.md. Do notrununtilbuild/hashgates.
CachedDCPIOVA/poolarithmeticdiagnostic limitation remains, no mappingdefectproven.
EXP806 physicalpattern previouslyvalidatedDCPpath; do notspeculativelychangepitch.
OneSDKstimulus e1d5875c ranviaEXP934-LivePairedCreation, importedexacttwoBGRAfullscreen
backbuffers, paired SDK+DWM dumps beforeownedSDKtermination. TaskResult0 thenremoved;
no SDKleft. Paired9hosthashgate8b9eee2a375554a75218b037b134180a159c592da5202ea7fadb12f093a9e469.
FilesROOT934/paired-evidence. CDBlogsdecoded withmatched934UMDPDB: SDKproxywindow /
NtAlpcConnectPort; DWM LPC inCreateAllocation forclass3/512KiB/64KiBalign/flags6
insideencodercreation/borderDraw. CompositorWaitForNextTick. IMPORTANT oneallocation
stackNOTproofpersistentallocatorstall, becauseallocation/submit/completioncounters
continue; nextmustdistinguishtransient/slowallocation/compositionfrompersistentwait.
No newSDKuntilnewdiscriminator. Reportanalysis/EXP934-current-display-verdict.md.

934 ONLYcoherentBUILD2source2602f23dc12498265326746299888c656ca147f6. Initial383build
supersededbeforehardware; heartbeatwasalsoASCconsumer, separatefromshared-memoryring.
ReplacementboundednonblockingTryReceive/SessionDrainRuntime beforeG3submit+duringpoll,
max64/no clock/pause/transmit/Pongwait. Wake/pong consumed, realcrash/unknown/MMIO
errorsfailclosed. BeginJob/manager/queue/completionunchanged, nofakefence.
Actualworker RED/GREEN+nativeASC/session/queuePASS; oneunchangedoldlegacyprojecttest
failsregistration, nativebackendASanlifecyclepasses. WDK934zero-warning/native
provenance/signatures/hash/PDBGUIDagePASS. Build2ROOT934-direct-gpuva-submit-build2.
Fullmanifest235242c5b431bdb0131da26d2846302a0a61d7b9121372fe34f83c11ab2ab445.
Controllerboot-event122194 Start22:18:06.3133993 usedforstage/restart; CIMshiftedbyNTP,
soimmutableKernelGeneral12 guardretained. Host5GiBgatePASS afterverifiedarchive
relocationof933PDBs and928ETL249561088B, originalsymlinkspreserved.

933exactcause: kerneladapterffffd307f7a02000/runtimeffffd307f7c25000,
SchedulerFault0x40bcd=backend933line3021 managementHBbeforehardwareSubmit.
Fence9189 softwareactive/notyetsent, completed9188. HBseq134/Calls67/Result3Timeout/
Start153791/End158890/Deadline154291/Rx553->561/EP20payload0042000000000000.
Running/CpuReady1/StopIdle/CrashlogCrashed0/BackendReady. Why5.099sgapunknown.
RawFrameProbeRAOAfile1b490000+560/2520 fitsone4KiBpage; CDBpointerchainconfirmed.
933TDR0x116paramsffffd307f4c30010/fffff8014c54adf0/ffffffffc0000483/3;
AutoReboot1, guestPSCIreset itself. OnlyhostSIGINT, noSIGTERM.
Fourfileshostgate8854c883446318a9098633b50209fed14c0d6c30dde175abf57ba5fa2a6e2458,
archive /Users/pavel/J313-evidence-archive/2026-10-02/EXP933-failure.
933removeddurably; cleanupgatececf542930612b29a34e3dff0a33ea484304e9faa0033dd1892c6e09946f6a62.
HVCfixese8cd2682/m1n1661cbe31(nextPC),04456756(X0ASM),71316533(IPAuint32) hardware
proved933: ArmPhase6Status0Hypercall1/gen241926950. Firmwarebdcf87154043535f4bcfcba0aa04e30c97d4877dd3ba9c87e18b6af287158395.

ROOT931 = ROOT.local/experiments/EXP931-ntfs-system-log-recovery.
Current launch ROOT931/launch-winpe-d.sh, verification verify-launch-d.py,
manifest launch-d-manifest.json SHA
fbadf8f0e084e3c4d0a7afdfbdda6d18393a0ea973541064a57c6fb9a45be710.
Media /Users/pavel/J313-evidence-archive/2026-10-02/EXP931-recovery-media/
winpe-exp931d.img SHA
478821c28bf969b5da367a74cca62cc2544ae8ab9d044133d844c30b5aef7124.
Helper RecoveryVolumeIdentity-d.exe SHA
67f924e1f52922f21ccbc6ebd3ce3d14ea9e1b1e37680fd2298d8ca5193c6c8d.
Logs winpe-d-full.log, winpe-d-owner.log, winpe-d-contract.bin.
ESP evidence directory J313-EXP931D-recovery; collector uses separate guest
C:\Users\pavel\EXP931\offline-evidence-d and offline-evidence-d-manifest.json.

## Recovery evidence and immediate boundary
Target GPT54ba924f-c5e4-4898-8192-327d5cad900e, partition length118997647360,
NTFS serial6612cadc12caaffb, sector4096, NumberSectors29052159. Evidence ESP
GPTf05b9952-ee05-44e1-bcc4-b678a8501b2a, FAT32, approximately501MB free.
A: mountvol assignment parameter error, stopped before repair.
B: operator photo D3762602 proves exact GPT/serial/size match, then persistent
SetVolumeMountPoint T: error87. Same GUID path as installed Windows, so earlier
GUID-change hypothesis unsupported. No CHKDSK.
C: native QueryDosDevice + DefineDosDevice temporary aliases succeeded; photo
A922EBC4 shows NTFS details then pause. Offline WIM inspection proved findstr.exe
and fc.exe absent in BOTH indices. Script used findstr next, fc later. Builder
fsutil emitted ASCII; encoding hypothesis rejected. No CHKDSK.
D source7e28e09d replaces absent utilities: native exact identity remains, native
ReadFile byte comparator checks offline SYSTEM copy. Precise FAILED_STEP receipt.
Tests: actual C payload dependency RED; packedD dependencies GREEN both indices;
x64 identity/alias collision/read/cleanup and equal/corrupt/truncated/missing/
locked backup tests PASS; SDK26100 ARM64 build PASS. WIM integrity, both-index
scripts/helper exact hashes, FAT readback PASS. Plans analysis/EXP931C-temporary-
volume-alias.md and EXP931D-winpe-command-dependencies.md. Read only EXP931 ledger
entries when needed; don't reread the whole historical ledger.

Host-verified live SYSTEM backup16,449,536B SHA
e26113cf60386cecd853335b3c2143d5678d3aa457181e1de0a432975fb030a3;
BCD32768B SHA41cd3373706bd0fd82c85ea3eca924575774344150a8085d6e995d130dd5816d.
Stored /Users/pavel/J313-evidence-archive/2026-10-02/EXP931-system-backup,
ROOT931/backup-host-gate.json PASS. No manual hive replacement/reinstall/format.

## Confirmed blocker before recovery
Windows oldboot2026-10-02T17:06:29.489308Z. Live930/oem7 loaded unarmed;
explicit arm/restart returned Code43 Stage1. ArmBuild930 Phase4 ZwFlushKey,
Status0xc000014d STATUS_REGISTRY_IO_FAILED, Generation2805416909, HVC0/notcalled,
Armnull. Independent RegFlushKey=1016. Ordinary WriteThrough+Flush(true) PASS.
C:Dirty, Ntfs55 MFT corruption FRN0x10000000201f2 SYSTEM.LOG2, Ntfs50 same file.
Errors predated929 and occurred in normal broker-disabled recovery. This proves
current arm blocker, NOT original graphics root cause. Do not bypass durability
gate. Online scan was stuck35%; original boot now ended. AutoReboot set1->0 and
left0. Installed/staged928/oem5,929/oem6,930/oem7 may survive; inspect actual state
before cleanup (registry persistence was broken). Do not blindly execute old
cleanup scripts deleting shared signer/service or scheduling reboot.
Analysis/EXP930-live-update-verdict.md; ROOT930/registry-flush-probe.ps1 reusable.
12 live929/930 receipts independently verified, gate396925858f5eef0be7f5f1c180fdb50a78e047a058ca752aedf094bda65ac680.

## Graphics correction still awaiting actual GPU execution
Commit6d37f17b57322d6de10320691bbdd424e5199ee0 in929/930: private ACQUIRE/PREPARE
Flags0 (software-only); KMD accepts0 or legacy1, RELEASE requires HardwareAccess1.
Own JobInFlight/LeaseToken guards run before reap. Mapping/owner/range/generation/
broker guards retained. m1n1 rejects owner jobs then syncs tables/invalidate slots.
Real Mesa->KMD->broker replay RED old rejects0 -> GREEN16/64 ASanUBSan, retained
lease and unsupported flag/release rejection tests PASS. Earlier927 CPU-copy fix
f99dad9e retained. No capability/NO_REDIRECTION/pitch/layout/firmware changes.
Physical result of929/930 untested because registry arm failure blocked GPU.
930 diagnostic source0fb0191090f2ee1dbd2770ee2919507ec9797d5d only adds phase receipt.
PDB929/930 saved and PE/PDB IDs matched. Plans EXP929-private-acquire-software-
entry-plan.md and EXP930-live-arm-status-plan.md.

EXP928 firstready53.42s Code0Stage12CPU8. Destroy323/323 then4033/4033, selected
primary match0, so lifetime hypothesis unsupported. GPU1500110000/PA8e0110000
was explicitly Windows-selected primary; shared DCP/VidMM range alone not a bug.
Native primary import LINEAR and stride validated. Paired SDK/DWM snapshots
before SDK termination: SDK NtAlpcConnectPort/proxywindow, DWM LPC ACQUIRE1
1024x1024 utile32x32 inside ResourceCopyRegion/D2D/DComp border update. Matched
PDB locals and nine host receipts verified. Earlier idle DWM snapshot alone did
not establish stall. 72 normal ETL flips belonged to Basic adapter before AGX;
AGX only RedirectedFlip0x8000, not compositor screenPresent. Don't toggle
NO_REDIRECTION without documented companion contract.

## Firmware, storage and practical pitfalls
Immutable normal recovery ROOT.local/experiments/EXP810-g4-package817/recovery:
m1n1-exp377.macho SHAfae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a;
J313_EFI-exp392.fd SHA16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06.
Full m1n1 SHAd3e0f999fe23bffa58f2343cbd0ee1f696da0c6fae08e1510f0ca58d7ca7db69,
MuR143 SHAe54c009847e64a4b2b327f54385eedb94f5a9e5fd3b459fd6101b07af4c023fc.
Hidden385 emergency only. Pinned signer E9BE15BD2A184BFABA0C8035B3C620C58037A241.
Air ARM64 signtool C:\Users\pavel\J313-tools\signing\signtool-arm64.exe
SHA097bdc4805f0cdcb4c1689a1533b0eb9a6143c3751c421b7ecc26b5c8cd5f0b1.
UMD catalog-signed, not embedded. Builder native x64 verifier, temporary root restored.
External pwdev ~5GB free; use internal archive for large media. Old guest cleanup
retained latest50; preserve archives and symlinks. SIGINT only snapshots+continues;
SIGTERM to verified run_uefi PID snapshots+reboots. Don't repeat blindly.
WinPE FAT starts offset512. PyFatFS case-sensitive /SOURCES/BOOT.WIM; remove then
recreate file on fresh image copy. BothWIMindices must be checked. Shell scripts
>2800 bytes: SCP then short PowerShell -File. CDB input command and decoded output
filenames MUST differ. Avoid broad symbol reload. TAR dereference symlinks.
