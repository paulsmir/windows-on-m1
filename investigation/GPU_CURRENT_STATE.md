# J313 GPU — EXP942 transfer-failure observation

## Objective and rules
Continue until the physical screen shows a correct stable Windows image.
Accepted/stable graphics package: NONE. Selected clear/copy pixel workloads pass;
DWM composition and physical presentation do not. User photo proves real fragments,
not a blank panel. Keep short falsifiable tests and collect immediately on failure.
No subagents or messages to other chats. Preserve foreign dirt and existing evidence.
Read this file first after reset; use only referenced ledger entries/plans.
Every hardware build/run/recovery: BEFORE + ACTUAL in EXPERIMENTS.md.
Every implementation: verify, commit, then CHANGES.csv row with full commit hash.
Do not equate Code0, Present S_OK, SDK exit0, or a zero cached sample with visual success.

## Workspace
Worktree /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler
(physical /Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler).
Branch integration/ad04-windows-compiler. ROOT=/Users/pavel/public_windows.
Artifacts ROOT/.local/experiments, not worktree/.local.
Foreign dirt: m1n1_windows/rust/vendor/rust-fatfs, mu;
untracked drivers/apple-agx/render-admission/pauls@192.168.1.24.ps1 and
investigation/analysis/EXP895-standard-blt-stimulus-plan.md.

## Current — EXP946 rejected, exact cleanup complete; no accepted graphics
No working/stable physical-screen result. Last operator photo941 showed moving
wallpaper fragments and scanlines. Physical observation requested for944/946,
no new answer yet. Do not equate Code0 or Present S_OK with a correct panel.

EXP946 source9808992403f1d048d4d4d53f9d1ccaf0601e846b broadened UMD
WrittenPrimary classification to every BindFlags PRESENT resource. Real x64 UMD
fixture oldRED/newGREEN and package ARM64 WDK/sign/hash/PDB gates passed.
Full boot125909 Start13:34:52.1487742Z Code0/CPU8. Same ce544 windowed
SDK7052 Present1 S_OK/exit0, but DWM1224 SubmitCommand carrying one
WrittenPrimary returned E_INVALIDARG (0x80070057) repeatedly for distinct
handles; no successful DWM Present in bounded receipt. Microsoft
D3DDDICB_SUBMITCOMMAND remarks specify D3D11 FlipEx/primary uses the
DISPLAYABLE_SURFACE flag; legacy PRESENT alone is not sufficient. 946 verdict
REJECTED. Original946 state/UMD/ETL hostgate
cf686b92e4fce8f0947c48f2fadbbcd3a526a075cc9f95358646759cc14109e3.
Corrective commit d49f3013b510ce680e40e58778d2188a63d99767 restored
narrow marker and added negative regression: x64 real fixture RED bad946,
GREEN revert. Correction is software-only, no new hardware package built.
Exact946/oem5 removed and ordinary GPU-visible377392 recovery durable boot126116
Start13:53:35.7943112Z: oneCode28/CPU8/packages0/services0/signerfalse/
filesfalse/SystemFlush0/Cnotdirty/free12537999360. No AppleAgx installed.

EXP945 source774d64c4f5f867c68085ca3b4c925655281bf80c was diagnostic
only. Full boot125612 Code0/CPU8. Same ce544 windowed Present1 S_OK/exit0.
DWM Present source allocation40003500 VA1f0000 had WrittenPrimary0, and
its actual written BO submissions had NumPrimaries0/S_OK. This is a legacy
copy-style path, not proof of a missing flag. Original945 hostgate
648d7569f09aeb91c3d13402c85aa3bc768654b9e7a6021e912f23be6399c2cb.
Normal recovery with945 first self-PSCI-reset and retry stalled (CPU IRQ
counts static); immutable hidden385 restored SSH, exact945 package removed,
normal clean Code28 confirmed before946.

EXP944 sourceae8f50a07fa16da540c1adaa51609cb986cdca7c fixed documented
DISPLAYABLE_SURFACE/pPrimaryDesc written list. Full boot125308 Code0/CPU8;
SDK direct displayable submitted NumPrimaries1 and Present1 S_OK. DWM UMD
Present S_OK but KMD Present/VirtualPresent0; DWM114 render submits completed.
Transient wait_paging in first DWM dump absent in second, so permanent paging
hang REJECTED. DCP-selected PA8e0110000/VA1500110000 unchanged; m1n1 CPU
window snapshots zero even at600s, coherency caveat. Original944 hostgate
297504768b5307d048a5b33a60985057e2a5931f8214084e71318d8799263b7b.
No physical success proof. 944 exact cleanup and durable normal Code28 done.

NEXT CAUSAL TARGET: determine whether DWM's completed render target GPUVA
(around0x1f0000 in its process) resolves to the same local physical surface
that DCP scans at PA8e0110000, or whether an explicit copy/flip is missing.
Read primary Asahi DCP, m1n1 selected-surface, Mu ACPI and official Microsoft
DXGI/WDDM contracts already inspected this turn. Current KMD owns the
GpuvaG3 logical PTE graph and has DWM FrameArm context/process/VA under lock;
it can observe this mapping without changing render or presentation. Design one
minimal receipt-only discriminator with expected result and rollback before any
hardware. No more capability-bit or broad WrittenPrimaries probing. Avoid
another old-reference archaeology pass. WHY CLEAN RECONSTRUCTION: current
runtime boundary is observable through narrow contracts; old admission history
cannot distinguish the current DWM-to-DCP gap.

EXP942 source2b6e8792ca02caab0597a45af974b0ab269f58d6 added separate
UPLOAD/DOWNLOAD failure receipt withoutcopybehaviorchange. Boot124717,
Start10:35:38.0757362Z/gen242360341/Code0/Stage12/HVC1/CPU8.
Samece544 --windowed SDK3904 actualPresentS_OK/devicehealthy/exit0 (~11s),
stdoutSHAe8e3fba418749898cb3c9fa311c877aa36857883d516e00d2d659e394b993713.
Explorer6184 ResourceCopyRegion failed at QUERY step1 BEFORE transfer.
QUERYpredicate56: no logicalPTE for VA0x11001f0000/64KiB, process9,
root9df3a0000/mappinggen21048. Receipt reasonnone because walk was unfilled.
No Wom1G3CopyTransferFailure key. High-VA real-handler alias QUERY replay
passed16/64; arithmetic workswhen tablelinks exist. Original942 final files/ETL
hostverified internalEXP942-original, gate
 da45f71d8841c5e1c4e9535b097facfb7a6c6c298558b9b7c24996628bf9636.
ETL had no map eventsinfirstfailure interval; one offlinepass exhausted.
942cleanupanddurableCode28 verifiedabove.

WHY CONTINUE COMPARISON: 942 actually rejected a resource QUERY on absent logical
PTE, but did not identify where the page-table walk broke. 943 adds only that
missing classification. Admission reconstruction and old archaeology do not
resolve this current runtime boundary. No repeated fullscreen/GDI capture.

## EXP941 findings and rollback
Source382a333d65368453879155a21895bbef280ba126 changed native GPUVA+DXGI1.1
successful CreateDevice policy to S_OK; software/base retain NO_REDIRECTION.
Current Win11/D3D11 scope; D3D9 interoperability is not implemented/proven.
Same ce544 windowed SDK3940: actual Present1 and device-after S_OK, previously
OCCLUDED. stdout971B SHA df80d167478f4b545f3ceb464b8112cc6c3d880ad207256716286f9545c93055.
Wrapper267014 terminated before manifest; later host receipt verified stdout
and process absence. Do not claim a bounded successful process exit.
Fullscreen3732 still pending12s; paired SDK/DWM1244 snapshots, then onlySDK killed.
Matched941PDB decode EXP941-paired: SDK waits DXGI proxy->DWM ALPC; DWM at
privateRELEASE->ContextRetire->UpdateSubresourceUP. Moving snapshots are not proof
of a permanent deadlock. All owned tasks removed.

Operator photo5A3341E7 confirms repeated wallpaper strips and scanlines on941.
Photo archived internal EXP941-crash/operator-photo.jpg SHA
 d33fdff73e787583c6cdf77e5dca289e519ce1fe9d4749dddfe953f4fd2fbf64.
Same boot124399/gen242524939 was still alive4hourslater. DWM186submit184complete
plus3/3, Present0/Virtual0, source address assigned once0x1500110000 ->PA8e0110000.
Existing120/300/600s snapshots confirm sameDCPswap9/DVA102a0000 with0/537413/539383
nonzero pixels. Not blank; pixel correctness of offscreen copies is insufficient.

GDI CopyFromScreen timedout30s/noPNG; task removed. Afterwards DWM1244 crashed
10:01:46Z c0000005 in dwmcore!CComposition::PreRender+2e4 reading00000000f1fb9f70;
new DWM2356. Crash preceded by UMD Flush E_INVALIDARG, causality not established.
Crashdump3355744B independently verified SHA
6db8f4e415f059a62a0c91b11751d20de0ab13db90a188d5e65ce000b7273f99, internal EXP941-crash.
New1a8 Source10 is a diagnostic live dump, NOT a real bugcheck; retained WERqueue
copy and decoded EXP941-crash/kernel-decoded.txt. Do not blame it for earlier photo.
Visual hold app0f8d237c (fullhashGit), binarybd68cae693fd276082ec4b230ca39bbd75b3cac427ae925677b8150bf3cd54dc:
SDK2492 failed EnumOutputs0=887a0002/exit6 BEFORE drawing. No green frame produced.
Active consolePavelSession1/noRDP session; Code0/resolution persisted nonetheless.

Original941 final fourfiles+ETL frozen/verified internal EXP941-original with
ROOT941/original-final symlink; hostgate903ad13e7e3b745c1416ef07ab36b513c28e85777d3ab77591eb88570576b060.
Ordered restart10:11:03 delayed; Win32ShutdownFlags6 returned1115. SYSTEM/SOFTWARE
and volume C: flush PASS10:14:47.830Z. Windows finally issued PSCI RESET itself.
Planned SIGTERM was NOT sent (set-e stopped on SSH timeout); only SIGINT snapshot
and continue occurred. Normal377392 recovered,941/oem5 removed, ordered clean
restart10:19:12.129Z; durable Code28 gate above passed. No forced host reset.

## Earlier proven invariants
935 a1d58340: unbound RTV Clear binding corrected, hardware clear executes.
936 4e7bba96: direct-primary rotation compares KernelAllocation, rotation S_OK.
937 2c66edbb: RunFragment headermerge/tilecount offsets68/6c/78 corrected;
this alone did not fix pixel corruption.
938 8e947fc4: ISP_MTILE_SIZE y/x3e8/3ea corrected (old3e0/3e2); same fullGPUcopy
changed204800 bad pixels to0/4096000. Shared GPU clear/open/cross-process also passed.
939 coordinate pattern0ab19bc4: all4096000 unique coordinates matched, not just green.
939 bb2fbabf: CPU-only FrameArm software entry; owner guards retained.
940 ec734378: private RELEASE software entry with safe deferred reclamation;
retains queued/submitting/job/lease holds, quarantine and broker checks.
AuxFBInfo dimensions still16 atb8/bc and6e0/e4: known omission, unbuilt commit
2aaf9e38 reverted90db80be before938. No new evidence identifies it as current cause.
DCP basic uncompressedBGRA descriptor matches m1n1 reference. Asahi pix_size1
comment is for compressed/multiplanar surfaces; do not guess a stride/format fix.
One938 shared clear exited7 with unlogged device reason; later receipt-only retry
passed. Earlier unexplained failure remains, not erased.

## Controls and recovery
Air pavel@192.168.1.37 key /Users/pavel/.ssh/air; LogLevel=ERROR BatchMode=yes
ConnectTimeout=5 ConnectionAttempts=1; knownhosts ROOT/.local/experiments/EXP641-standard-present/air_known_hosts.
Builder pauls@192.168.1.24 key /Users/pavel/.ssh/windows_builder.
USB /dev/cu.usbmodemC02HDNCCQ6L41 andL43. Check both control planes before physical
requests. SIGINT exactowner snapshots+continues; SIGTERM snapshots+resets. No nested
proxy calls in handler, no rawUSB with liveowner. SSH timeout alone is not a GPU hang.
Normal recovery ROOT/.local/experiments/EXP810-g4-package817/recovery:
 m1n1-exp377.macho SHAfae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a
 J313_EFI-exp392.fd SHA16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06
GPU visible/broker disabled. Hidden385 emergency only. Exact cleanup every package.
Fullm1n1 EXP933-hvc-return-contract/m1n1-exp933.macho
 SHAbdcf87154043535f4bcfcba0aa04e30c97d4877dd3ba9c87e18b6af287158395
Mu EXP928-selected-primary-lifetime/firmware/J313_EFI-r143.fd
 SHAe54c009847e64a4b2b327f54385eedb94f5a9e5fd3b459fd6101b07af4c023fc
Signer E9BE15BD2A184BFABA0C8035B3C620C58037A241; certSHA97145866a1530003077eacd8457f1a7a644d662423278fd94e450f903c85cbda.
SDK ce544e78e8f12e0d3e71f8b9f34c6c57f79c8490125fedb5ff13514ac4a4529b.
Dumphelper046e4063d23e8cb3451d5dd61e160ceb58018e1e2a92b5925b1babc250bb6301.
Frameprobe EXP928/AppleAgxBltProbe.exe28f636f19efb2ba165a59c70a7ff7c117a04a6401cc911ec40948f7a553b7bf1.

## Build/evidence constraints
Source manifest scripts/g3_build_source_manifest.py --root WORKTREE --commit HASH
--manifest ROOTNEW/source-manifest.json --archive ROOTNEW/source.zip.555inputs.
Pinned SDK26100/v14314.44.35207; exact sign/hash/ARM64/PE-PDB gates each build.
PDB check /tmp/j313-hvc-abi-test/bin/python and /opt/homebrew/opt/llvm/bin/llvm-pdbutil.
External launch gate>=5GiB.939/940/941 UMD copies now verified original-path symlinks
to internal archive; mapping EXP942/evidence-relocation.json. No evidence deleted.
Large ETLs/PDBs internal /Users/pavel/J313-evidence-archive/2026-10-03.
Unrelated existing test limitations: frontendprepare reference path/dirtyMesa;
virtual-submit replay missing AdmissionMemoryRuntimeLocalView/Inner declarations.
Do not claim whole-suite GREEN or fix unrelated harnesses.
