# J313 current GPU state — same-boot update, arm-gate refusal

## User constraints and current objective
Continue to a correct stable physical desktop. Latest user authorization: reboot is now allowed to recover NTFS/system hive,
then resume the corrected graphics-driver experiment. Earlier NO_REBOOT
restrictions below are historical; preserve evidence and recovery artifacts. No subagents / no messaging other chats. Short falsifiable
checks, no prolonged stability trial before correct physical output.
Accepted desktop package NONE. Moving image fragments were real observations;
KMDPresent0 and an early zero buffer do not mean the current panel is blank.

## Workspace and control
Worktree /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler
(physical /Volumes/pwdev/public_windows/.worktrees/...). Branch
integration/ad04-windows-compiler. All artifacts ROOT .local/experiments, not
worktree .local. Root main checkout has extensive unrelated dirt: never edit it.
Foreign dirt in worktree: m1n1_windows, mu; untracked pauls@192.168.1.24.ps1 and
analysis/EXP895-standard-blt-stimulus-plan.md. Leave untouched.
Air pavel@192.168.1.37 key /Users/pavel/.ssh/air; known-hosts
ROOT.local/experiments/EXP641-standard-present/air_known_hosts.
Builder pauls@192.168.1.24 key /Users/pavel/.ssh/windows_builder.
Owner35884 run_uefi full profile remains active (verify fresh). No raw USB/NOP
while Windows is reachable. Full m1n1 d3e0f999/MuR143e54c0098 unchanged.
Boot2026-10-02T17:06:29.489308Z (earlierCIM .490552Z differs1.244ms due clock;
use <=1s tolerance plus exact package/state). Windows SSH alive, no reboot done.

## Latest installed state
EXP929 package30.0.929.0/oem6 installed via pnputil/add-driver/install, Exit0,
no reboot required, same boot. It did not start: Code43 Stage1/c00000bb,
G3Armnull. AutoReboot changed1->0 per user no-reboot constraint; prior recorded
in ROOT929/live-before.json. Keep0 while this constraint applies.
Old928/oem5 remains unbound/staged for live rollback; preserve exact928 and929
artifacts. Do not run old cleanup scripts that delete shared signer/service or
schedule restart. Live user override supersedes normal cold cleanup order.
EXP929-R1 explicitly rearmed installed929 then pnputil/restart-device APPL0002;
Exit0, sameCode43/Stage1/c00000bb/Armnull. No new ARM_CONSUMED serial print.
R1 script has no arm cleanup, so installer-finally race alone is insufficient.
Do not repeat third identical restart. Current boundary is before GPU access.
SYS929 29439782a13404d0a29cafddae2a1b9cce2a7a159d5fc0cd7a5cd0dfc109ff26
UMD929 6f8cd5288248f7d3a46fb5dc289aef2b45e5da4d4b1238782e00996cf272241c

## Pending EXP930 diagnostic
Source0fb0191090f2ee1dbd2770ee2919507ec9797d5d adds exact arm-gate receipt:
phase1=open,2=query/type/value,3=delete,4=flush,5=missing generation,6=HVC;
Wom1GpuvaArmBuild/Phase/Status/Generation/Hypercall. Original admission and
one-shot delete/flush remain unchanged. AddDevice enforces nonzero generation;
928/929 HVC instruction bytes identical. m1n1 handler has no CPU0 restriction.
Plan analysis/EXP930-live-arm-status-plan.md. Actual resource/admission replay
GREEN. Build930 nativeARM64/pinnedWDK26100 zero-warning PASS; signature CAT/SYS/
UMD pinnedE9 PASS. Artifacts ROOT.local/experiments/EXP930-live-arm-status.
Package download in exec session47921 (verify completion). No930installed yet.
Next: seal/preregister exact package manifest; install930 UNARMED into current
Code43 devnode, verify new receipt Build930/Phase2 before GPU. Then one explicit
arm+device-only restart and read exact failing phase/status. No OS reboot.
Prepared installer/restart scripts in ROOT930; NOT sealed or executed yet.

## Correction carried by929 and930 (not physically validated)
Commit6d37f17b57322d6de10320691bbdd424e5199ee0: private ACQUIRE/PREPARE Flags0,
KMD accepts0orlegacy1; RELEASE stillrequiresHardware1. Own process JobInFlight/
LeaseToken checked before reap after bounded wait. Existing graph/pool/owner/
range/generation/broker checks retained. m1n1 publish_entry rejects owner jobs,
syncs tables and invalidates owner slots. Asahi VM mappings use per-VM locks.
Real Mesa prepare->KMD->m1n1 broker RED(old rejects0) to GREEN16/64 ASanUBSan;
retained lease prevents reaping; software RELEASE/unrelatedflags rejected;
legacy1 combined lifecycle GREEN. Plan analysis/EXP929-private-acquire-software-entry-plan.md.
Known CPU copy fix927 f99dad9e also remains: copy escape Flags0, KMD accepts0/1.
No capability, NO_REDIRECTION, scheduler, pitch, memory-layout or firmware fix.

## Evidence that selected929 correction
EXP928 original firstready53.42s Code0/Stage12/CPU8; two read-only queries5s apart,
no SDK in initial test. Destroy323/323 but first selected primary match0; later
4033/4033 match0. Primary lifetime hypothesis not supported for measured interval.
Original15 host gate c72c67a1582ff06d8c8387399d28cd946d5e086923452ab783dbfffb2ed59f74.
Primary atGPU1500110000, PA8e0110000 was selected by Windows, not an independently
reserved bootstrap buffer. Shared DCP/VidMM range alone is not a defect.
Native primary import explicitlyLINEAR and validates stride/offset.

Read-only live DWM1244 snapshot17:12:02 (101246B, SHA19bffee39317bfc1981f9a0b0f76b2f4c5ad3f98ad94b6c91aaa122192c944b3)
showed compositorWaitForNextTick/LPCWaitForNextMessage, no transfer stack. VSync
continued (29674 notifications) and DWM26submit/26complete. Do not call it stuck
in copy from that snapshot. Focused original ETL:72 normal screen flips were
Basic adapter17346000 before switch; only AGX19de6000 Present wasRedirectedFlip
0x8000 on LPC1668, not compositor1408 screenPresent. NO_REDIRECTION remains per
existing documented no-D3D9 contract; do not toggle experimentally.

EXP928-LIVEPAIR: one SDK in existing boot, simultaneous dumps BEFORE owned SDK
termination; temporarytask removed. SDKwaitNtAlpcConnectPort/proxywindow;
DWM LPC private_escape ACQUIRE1,1024x1024,utile32x32, zero scene/manager handles,
inside ResourceCopyRegion/D2D/DComp/window-border update. Matched928PDB locals.
SDK105818B SHA02401da957342059cb4a6e6cf9c3e4e78e74cb518f7f2818ba8b99280d40590b
DWM126782B SHAdcf4013e4cbc1158b02f7f090bf690ed1386bdf38b0e2245422da0493bf112c2
Paired9 host gate PASS. No physical success or sole-cause proof.
Files ROOT928/clean-cdb-*.log (.decoded for text), private-request.log,
live-paired-host-gate.json, dwm-present-focused.jsonl. Partial broad ETL export
is not the final focused decode. Original ETL/file hash gate preserved.
CDB command and decoded-log filenames must differ: an earlier collision caused
syntax errors; corrected clean paired logs are the valid analysis.

## Recovery and storage
Immutable normal377 m1n1 SHAfae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a
Mu392 SHA16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06
ROOT.local/experiments/EXP810-g4-package817/recovery. Hidden385 emergency only,
currently NOT authorized to reboot. Last full normalCode28 proof was927 final
Boot16:31:53.933519Z, twicechecks +3cleanuphostgate0f8aa4e6...
Air free~17.3GB. Prior authorized guest cleanup retained last50 experimentIDs;
old archives preserved internally. Shared ARM64 signtool now
C:\Users\pavel\J313-tools\signing\signtool-arm64.exe
SHA097bdc4805f0cdcb4c1689a1533b0eb9a6143c3751c421b7ecc26b5c8cd5f0b1.
Host closed927 ETL/EVTX663240704B and closed922 ETLs536870912B verified relocated
with original symlinks under /Users/pavel/J313-evidence-archive/2026-10-02/
EXP927-closed-before928 and EXP922-closed-before928. Do not delete archives.
929 PDBs saved and PE/PDB GUID+age matched in ROOT929/pe-pdb-id-match.json.

## Confirmed current blocker (EXP930 live result)
Installed930/oem7 in SAME boot17:06:29.489308Z. Unarmed install provedcompiledBuild930/phase2. Explicitrearm then device-onlyrestartreturned0 butCode43Stage1: ArmPhase4, ArmStatus0xc000014d STATUS_REGISTRY_IO_FAILED, Generation2805416909, HVC0/notcalled. Armnull; AutoReboot0. IndependentRegFlushKey returns1016 with17.3GBfree. Bounded131072BWriteThrough+Flush(true)PASS SHA59f410ae5e17962412e2aed4f815918f634932f2abf084f00bb638c4db017850. C:Dirty; Ntfs55 MFTcorruption FRN0x10000000201f2 pointsSYSTEM.LOG2; Ntfs50 delayedwritefailure samefile. Errorsprecede929installation (including16:48 duringnormalrecovery). Thisprovespresentlive-startblocker, NOToriginalgraphicsrootcause. Do notbypassdurabilitygate orrepeatGPUrestart.
12liveinstall/rearmreceipts929+930 hostsizeSHA PASS 396925858f5eef0be7f5f1c180fdb50a78e047a058ca752aedf094bda65ac680. ROOT930/live-update-evidence. PDB930PE GUID/age matchPASS. ReadonlyRepair-Volume -DriveLetter C -Scan isrunning inexec71440; scanonly/noSpotFix/noOfflineScanAndFix/noOSreboot. Next readonline-scan-result, preserveit andassessrequiredrepairagainstuserNO_REBOOT. FileSYSTEM.LOG2corruptionbelongsstorage/NTFS, notAGXdriver; nofsrepairdone.

OnlineScan gracefulcancel exactjob returnedNotSupported at18:34:36Z; jobstillRunning35%, originalexec71440 pending. No forcibleprocess/servicekill. Currentstorageblocker andvalidateddiagnostic inanalysis/EXP930-live-update-verdict.md. No furtherGPUrestartuntilSYSTEMhiveflushhealthy; userNO_REBOOTremains.

USER AUTHORIZATION 2026-10-02T19:30:54.692216+00:00: reboot now explicitly permitted for filesystem recovery and continued display repair. Inspect live scan and backup availability before scheduling/launching repair.

EXP931 recovery update 2026-10-02T20:15:16.030063+00:00: User authorized reboot. Cached SYSTEM16,449,536B SHAe26113cf60386cecd853335b3c2143d5678d3aa457181e1de0a432975fb030a3 andBCD32768B independently copied/hashed to/Users/pavel/J313-evidence-archive/2026-10-02/EXP931-system-backup; gateROOT931/backup-host-gate.json. Targetdisk0part7 GPT54ba924f-c5e4-4898-8192-327d5cad900e length118997647360 NTFSserial6612cadc12caaffb. ESP GPTf05b9952-ee05-44e1-bcc4-b678a8501b2a forlogs.
EXP931A WinPE booted; operatorreported incorrectparameter/pressanykey (scriptpauseonlybeforeCHKDSK). Recoveredpausedguestviaexactowner63798 SIGTERMafterSSHtimeout/USBcheck; oldimagepreserved. No repairclaimed.
EXP931B usesnativeARM64RecoveryVolumeIdentity.exe SHA3208098971acd69ce09276c0707171ef91d6c0c789d3104f5ba7e35942c19000: enumerateactualWinPEvolumenames, matchGPT+NTFSserial+partitionlength+geometry, mountT:/R:usingabsoluteAPIpaths. X64matcherSelfTestPASS, ARM64pinnedSDKbuildPASS. Sourcefb4d8515. Finalimage/Users/pavel/J313-evidence-archive/2026-10-02/EXP931-recovery-media/winpe-exp931b.img SHAe10dc229738745eb8302fa04cc187b67e8b9251e9380aca267f013b8bfab0edc; bothWIMindexscripts/exe+integrity+FATreadbackPASS. ManifestROOT931/launch-b-manifest.json SHAde068d5ebe890ee278b4b02364ac18185d64fa64b09362f8b16fff7d85210719.
CURRENT WinPEB owner66970/session86291 running afterkernelready, normalWindowsSSHexpectedunavailable. Repair status notobservable overSSH; asyncuserquestionaskscurrentphysicalscreen. Do NOT interrupt ifCHKDSKcouldberunning. Expectedscript: copy+fc offlineSYSTEMtoESP/R:J313-EXP931B-recovery, chkdskT:/f/x, logESP, wpeutilreboot. Onlypauseisidentity/backupfail. Onownerexitfreshdualplane/proxyNOP thenROOT931/launch-normal.sh (377/392 brokerdisabled), SSHcollectROOT931/collect-offline-result.ps1 (expectsESP/Bdirectory), independenthashgate andRegFlush/dirtycheck beforeGPUwork. No new graphicsrununtilregistryhealthproved.

EXP931B verdict 2026-10-02T20:26:54.410364+00:00: photo proves exact target identity matched; SetVolumeMountPoint T: error87, pre-repair pause. No CHKDSK. EXP931C source4b437c7017b077c27e0ae90093e46eb1e4dc0907 replaces only persistent mount creation with verified temporary DOS aliases; native real alias and collision tests PASS, ARM64buildPASS, sealed imageSHAa601b9f0bbb348f6fafb0bb391ba5c4bb3b43d8ade42c08e95c8343e211e6171. Recovery/relaunch preregistered; B stillpaused until exactownerreboot. NextC logs ESP J313-EXP931C-recovery.

CURRENT 2026-10-02T20:28:00.740338+00:00: B recovered from confirmed pre-repair pause; C launching owner71176/session84165 after freshdualplane/guardedNOP. Imagea601b9f0. Await C automaticcompletion or physicalpauseevidence. Collector ROOT931/collect-offline-result-c.ps1 uses ESP/J313-EXP931C-recovery and separate host/guest evidence names. Never interrupt activepotentialCHKDSK.

EXP931C runtime checkpoint 2026-10-02T20:30:12.722082+00:00: RAM upload and WinPE kernel boot completed; guest runtime ready, CPU0-7 online receipts and xHCI IRQroute857 recorded in ROOT931/winpe-c-full.log. Owner71176 remains active. No automatic reboot or CHKDSK completion receipt observed; physical console result requested. Verdict pending, do not claim filesystem repair or graphics success. Do not interrupt without confirmed pre-repair pause.

EXP931C verdict 2026-10-02T20:34:21.216825+00:00: T: alias andfsutilreadPASS; pausedbeforeCHKDSK. DefinitiveofflineWIMcheckbothindicesmissingfindstr/fc, scriptdependencybug. EXP931D source7e28e09d429ad4777a5ba8afb24cd32be6e31a47 removesunavailableutilities, nativeexactidentityretained/bytecompareadded. Native tests+oldpayloadRED/fixedpackedGREEN PASS. SealedimageSHA478821c28bf969b5da367a74cca62cc2544ae8ab9d044133d844c30b5aef7124; launch/recoverypreregistered. Cpausedowner71176 untilfreshverifiedreboot. DlogsJ313-EXP931D-recovery.
