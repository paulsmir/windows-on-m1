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

## Current live state — verify fresh
EXP931D fixedNTFS, SYSTEMflush0;931E cleanCode28 baseline proved.
EXP932 fresh930 failedbeforeGPU: Phase6/return0xffffa20141176e40 insteadABI1,
matchingfirmwareARM_CONSUMED250087441. Frozen evidenceROOT932/original-boundary.
No present/stabilitytest ran. Normal recovery nowloaded afterorderedguestreset;
Boot21:12:16.249038Z, Code43/exact930oem5/Armnull/serviceStopped. ROOT932/cleanup/
existingexactremover running exec62647, willorderedrestart afterpackage removal.
Normalrecoveryowner exec66980 (verifyfreshPID). Afterexit: freshSSH/USB/proxy,
launch ROOT932/launch-clean-recovery.sh (unchanged377/392) and run
ROOT932/verify-clean-baseline.ps1 copiedtoC:\Users\pavel\EXP932. NoGPU-hiddenboot.
Collectcleanupreceipts/hash before933stage. Do NOTstage933untilcleanbaseline.
Original932 CIMboot drifted36.666s from20:51:44 to20:52:21 due recordedKernelGeneral1
NTP+36661ms; KernelGeneral12 andsamecontinuousownerprovednoextra reboot. Guard
refusedfirstrestart; clock-verifiedwrapper thenaccepted exactgeneration/phase.

EXP933 HVC transport fixes: root e8cd2682 /m1n1661cbe31 preservesHVCnextPC;
root04456756 externalX0arg/result assembly leaves; root71316533 makesIPAstatus
uint32 as existingcallback (armreceiptreturn64). Source planEXP933-hvc-return-
contract.md. Actualepilogue RED->GREEN; pinnedMSVC19.44 originalintrinsicDLL RED,
fixedDLL sixcasesGREEN (status1/2, bothimmediates, poisonedX8, nonvolatiles/SP/PC).
Updated713probe6GREEN too. Fourarm +fourrenderHVC testsPASS. Foreignrust-fatfs/Mu
untouched. Newfirmware built ROOT933/m1n1-exp933.macho SHA
bdcf87154043535f4bcfcba0aa04e30c97d4877dd3ba9c87e18b6af287158395;
oldbuildmatchedacceptedd3e0f999, cfgandRustarchivehashunchanged. GNUwarningsoutside
changedlines; no newconditionwarning. MuR143 remainsunchanged.

KMD933build1 correctlyfailed /WX C4242 uint64 to existingULONG IPAstatus; noinstall.
Build2 source71316533, directoryROOT.local/experiments/EXP933-hvc-return-contract-build2,
source manifest76f5f19c/archivecbb058f8, builderC:\Users\pauls\EXP933-hvc-return-contract-build2.
Buildexec41282 running, logbuild-console-after-transfer.log. Initialdispatchbefore
filetransfercompleted wasrejectedbeforebuild; filesnowcomplete/sourcegatePASS.
NextfinishWDKzero-warning/sign/hash/PDB gates; nohardwareuntilsealed933manifest,
old932packagecleaned andnew933stagedfresh. PredefinedcoherenttransportATOMICCONTRACT
inplan; no WDDMcapability/layout/firmware-memory changes. Build2nativeprovenance
rebuildsfromcommitted553sourcefiles. No newhardwarehasusedfixedtransportyet.

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
