# J313 GPU — EXP940 build; EXP939 original freeze in progress

## Objective and working rules
Continue until the physical Windows screen shows a correct stable picture.
Accepted/stable graphics package: NONE. Pixel correctness is now proven; physical
presentation is not. Never equate a zero cached scanout sample or KMDPresent=0 with
a physically blank panel: the user's photos showed real moving image fragments.
Use short falsifiable checks, collect immediately on failure, no multi-minute
stability wait before correct output. No subagents or messages to other chats.
Read this file first after reset; consult only referenced experiment records.

## Workspace
Source worktree /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler
(physical /Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler),
branch integration/ad04-windows-compiler. ROOT=/Users/pavel/public_windows.
Artifacts ROOT/.local/experiments, never worktree/.local. Preserve foreign dirt:
m1n1_windows/rust/vendor/rust-fatfs, mu, untracked
 drivers/apple-agx/render-admission/pauls@192.168.1.24.ps1 and
 investigation/analysis/EXP895-standard-blt-stimulus-plan.md.
Every hardware build/run/recovery has BEFORE and ACTUAL in EXPERIMENTS.md.
Every code change: test, commit, append CHANGES.csv row with full commit hash.
Ledger-only commits need no row. Do not overwrite an old result as successful.

## CURRENT —940 built; normal recovery after939
940 sourceec73437819d57a721694976855ded432b5ddfcc9, ROOT940=.local/experiments/EXP940-private-release-software.
Buildsource555/native/WDK0warnings/signatures/hosthash/PDBGUIDagePASS; notstaged.
SYSa8748efa750df0f3e2fba3cfbbf0b082477f1582d0293b0a0039c0e46d24096c
UMDa89ecaa21aba054543da8433a632e542920a2d1b07b4836fdefc820017f85f80
INF00f2ae4fe2782d670ce661cd0f4e515084e65658d0030725b83b5b6a29317a3b
CATcd3f683e2ec5dc942c01f167ee0bf90c87dda0ef0249799b35811148912ad682
SymbolsinternalEXP940-build/symbols. Samebdcf/MuR143. Firsttestsamece544fullscreen12s.

939originalendedafterorderedrestart04:57:04.140Z/RegFlushPASS, owner54779exited,
nohostreset. Originalimmutable123801 Start04:20:23.7571887Z/gen238787205.
Finalframe/state/UMD/ETLfourfiles independentlyhostverifiedgate
7d6cebb18dbccca45a6850e5d5be5f4b7b0f7d7cd13bed52afecf1aaffe6a676;
ROOT939/original-final symlinkinternalEXP939-original/original-final; ETLstopped.
All939SDKtasksremoved. No physicalscreen/Present success, pixelpatternPASS below.
Firstnormal377392(session74616)selfPSCIresetbeforeSSH; onediagnosticretryowner59674/
session70655 recoveredCode43/exact939/CPU8/Armnull/Cnotdirty. ResetcollectorKernel41,
no1001/newMEMORY.DMP, causeunknown. No hostforcedreset orGPU-hiddenboot.
Exact939/oem5cleanup nowexit0/staged0/SYSfalse/UMDfalse; durabilityrestartaccepted
05:09:31.290Z, BeforeBootCIM05:05:48.832610Z, manifest
6cbc65abab7d3df0d904f8f7bf6453b24704c6d7e5774343038db1c7bcb2392f.
Checkowner59674exit/boundedSSH+USB/proxy; thenROOT939/launch-clean-recovery.sh
normal377392 andverify-clean-baseline.ps1. OnlyafterdurableCode28stage940.
940hardware-manifest.pending.json prepared; needfreshbaseline/stagereceipt and
currentgitdiffhashrefreshbeforeseal. No940stageyet.
ROOT939=.local/experiments/EXP939-frame-arm-software-entry.
939SYS7cc8541e05bddb8630335a6b575d5742676f3657067321a6bc4c1de11ed88a75
939UMD5f56d7ea9d2bf74a1764abc0457c8458fb202412aebd656662f01315d5a4440c
939INF204cb945aa7b769969b9059a9efab7b3cca174253a227e13890c20efc409e053
939CAT577a758b547aa3d2cb2b1ed1b2286af319423d6b4d9e51a6cdc14901c83b5980

939 fullscreenSDK5644: CreateSwapChainForHwnd did not return within12s. Paired
SDK/DWM1236 snapshots then only SDK killed. SDK104871B SHAab7d3d8b41580f44e149d431e2eb804514d9c03ce021af291a7ba73202658565;
DWM141374B SHA3cb29463d285c32a83883d944ca1015346d64fe3c780e691d3d16de441853fd5.
Six files host verified, taskEXP939-FullscreenSdk removed.
Decode ROOT/.local/experiments/EXP939-paired/{sdk,dwm}-decoded.txt with matched939PDB.
DWM LPC now in private_escape -> AgxWin32AsahiBatchRelease, within FlushRetire /
D2D constant-buffer update. SDK waits DXGI proxy-window -> DWM ALPC. This is one
observed wait boundary, not proof of permanent deadlock. No repeated fullscreen.

939 unique-coordinate pattern PID1836: all4096000 pixels correct, firstff000000,
lastff3e7fff, pitch10240, deviceS_OK, exit0/no timeout9.32s. This excludes repeated
or permuted tiles for the tested GPU copy. Host stdout895B SHAc241245f64c1d8bb65e3757120b24641009f725fa9004999d190cbc28f265f45.
Pattern source0ab19bc4 (fullhashGit), binarye114a0ae91e86a272107ad839df615ad00f5e0256d9d3fa80f8c758a5d78c547,
ROOT/.local/experiments/EXP939-coordinate-pattern. TaskEXP939-PatternSdk removed.
No owned SDK processes/tasks remain. No physical-screen confirmation for939.

## Next — EXP940 software RELEASE with safe deferred reclamation
Source ec73437819d57a721694976855ded432b5ddfcc9 committed.
ROOT940=.local/experiments/EXP940-private-release-software.
Buildsource555 and all native/WDK/sign/hash/PDB gates PASS; no940 staged or installed.
Plan investigation/analysis/EXP940-private-release-software.md.

UMD private_escape sends Flags0 for RELEASE as already forACQUIRE/PREPARE.
KMD accepts0/legacy1, authenticates exact owner/context/generation, marks release
requested and acknowledges while Queued/Submitting/ownerJobInFlight/LeaseToken.
Existing reaper unmaps/revokes/zeros/frees only after holds end. Direct release
also refuses Submitting. No graph/broker protection, fence or quarantine weakened.
Existing queued RELEASE was already asynchronous; success is an ownership-transfer
acknowledgment, not a render-completion signal or promise of immediate reuse.
Primary m1n1 publish_entry checks jobs of the same owner and invalidates only its
slots; revoke rejects referenced backing. Asahi scene lifetime inspected.

Evidence/tests: old RELEASE0 rejected (RED); flag-only intermediate returnsBUSY
when another real owner job is active (second RED). Final real KMD/graph/wire/m1n1
replay16/64 keeps sentinel bytes/mapping while owner job runs and reclaims after
completion. Failed-notify holds/stale generations, failed-revoke quarantine and
legacyFlags1 replay PASS. Actual UMD request builder oldreleaseFlag1 RED/new0GREEN.
Seven targeted request/private/storage/pool/FrameArm tests PASS, plus softwarecopy
bothprofiles and ledger checks. Tests are committed with source.

940 first hardware discriminator: SAME ce544e78 defaultfullscreen SDK,12s then
paired stacks if pending. Expect creation/Present to advance. If not, collect and
stop this hypothesis; do not repeat. Retain pixel correction and939 FrameArm fix.
NO_REDIRECTION, capabilities, AuxFB dimensions, firmware and power remain unchanged.

## Proven changes and remaining boundaries
-935 sourcea1d58340 fixed clearing an unbound RTV; hardware clear nowexecutes.
-936 source4e7bba96 fixed direct-primary rotation identity; rotationS_OK/no deviceerror.
-937 source2c66edbb corrected RunFragment headermerge/tilecount offsets
  0x60/64/70 ->0x68/6c/78. RealABI defect, but did not remove pixel corruption.
-938 source8e947fc4 corrected ISP_MTILE_SIZE y/x offsets0x3e0/e2 ->0x3e8/ea.
  Template4x4 became correct20x16 for2560x1600/utile32. Identical fullcopy changed
  from204800 bad pixels to0/4096000. SharedGPUclear/local and cross-process green
  readback also0bad;939 coordinate-pattern confirms spatial identity.
-939 FrameArm metadata request no longer asks Windows level-two GPU-idle entry;
  actual UMD/KMD/CPUwriter tests RED/GREEN with ownerguards. Fullscreen stillpending;
  next paired stack moved to RELEASE. Do not claim939 alone fixes the desktop.

One938 shared-clear run7392 exited7 with CompleteS_OK and unlogged device reason.
Receipt-only appchange86f02b00 subsequently showed deviceS_OK and0bad. Earlier
failure remains unexplained; no TDR captured. Do not silently erase it.

938 windowedSDK6264: device/create/clearS_OK but Present1=087a0001 OCCLUDED.
936 readiness already excluded minimized/background/initial-message-pump causes;
WARP producer on same output presentedS_OK (notAGX/physical proof).
936 live CDB traced NtGdiDdDDIPresent=c01e0006, flags3081 legacyBlt, beforeKMDPresent.
NO_REDIRECTION inherited software path disables DWM shared-resource presentation.
Microsoft docs include legacyD3D9 interoperability requirements; D3D11 sharingPASS
is not proof ofD3D9 support. Do not blindly toggle returnstatus orcaps. ROS sample
is RENDER_ONLY; use only commonUMD semantics, no admission/scheduler assumptions.

AuxFBInfo dimensions still16x16 atwork0xb8/bc and0x6e0/e4: separateknownomission,
not changed. Commit2aaf9e38 was unbuilt and reverted90db80be before938. Do notbundle.
Other size2/utile concerns unproven for current32x32 input; no speculative edits.

## Recovery / control / build
Air pavel@192.168.1.37, key /Users/pavel/.ssh/air; use LogLevel=ERROR, BatchMode=yes,
ConnectTimeout=5, ConnectionAttempts=1, known-hosts
ROOT/.local/experiments/EXP641-standard-present/air_known_hosts.
Builder pauls@192.168.1.24, key /Users/pavel/.ssh/windows_builder.
USB /dev/cu.usbmodemC02HDNCCQ6L41 and...L43. Before physical requests orreset, check
boundedSSH plus expectedUSB and exact active launcher. No rawproxy while ownerlives.
SIGINT to exactrun_uefi PID snapshots+continues; SIGTERM snapshots+resets. Never issue
nested proxy reads inside the interrupt callback. Prefer ordered Windows restart
with registryflush. Unreachability can be delayedNIC/DHCP: sameRTL MAC00:e0:4c:68:12:b1
was seen at169.254.241.29 before normal192.168.1.37 returned. Do not infer GPUcrash.

Normal immutable GPU-visible broker-disabled recovery:
ROOT/.local/experiments/EXP810-g4-package817/recovery/m1n1-exp377.macho
SHAfae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a
and J313_EFI-exp392.fd SHA16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06.
Hidden385 emergencyonly when normalguest cannot be recovered; none used this turn.
Normal sequence: freezeoriginal ->377392 Code43 exactoldpackage ->exactcleanup ->
ordered durable restart ->377392 Code28/no package/service/signer/files/RegFlush0/
Cnotdirty ->stageonlynewhashverifiedpackage+arm ->sealmanifest ->orderedfullboot.
939 normalrecovery/exactcleanup scripts prepared asdescribed above.

Fullm1n1 ROOT/.local/experiments/EXP933-hvc-return-contract/m1n1-exp933.macho
SHAbdcf87154043535f4bcfcba0aa04e30c97d4877dd3ba9c87e18b6af287158395.
MuR143 ROOT/.local/experiments/EXP928-selected-primary-lifetime/firmware/J313_EFI-r143.fd
SHAe54c009847e64a4b2b327f54385eedb94f5a9e5fd3b459fd6101b07af4c023fc.
Pinned signerE9BE15BD2A184BFABA0C8035B3C620C58037A241,
certSHA97145866a1530003077eacd8457f1a7a644d662423278fd94e450f903c85cbda.
Air signtool C:\Users\pavel\J313-tools\signing\signtool-arm64.exe
SHA097bdc4805f0cdcb4c1689a1533b0eb9a6143c3751c421b7ecc26b5c8cd5f0b1.
Builder signtoolSDK26100x64 temporaryRoot import/restoration verified eachpackage.
PDBs internal /Users/pavel/J313-evidence-archive/2026-10-03/EXP{936..939}-build/symbols.
Use /tmp/j313-hvc-abi-test/bin/python(pefile), /opt/homebrew/opt/llvm/bin/llvm-pdbutil.

Source build export scripts/g3_build_source_manifest.py --root WORKTREE --commit
HASH --manifest ROOTNEW/source-manifest.json --archive ROOTNEW/source.zip.
Current555 inputs. Clone previous build-kmd.ps1 with exact newhashes/commit/build.
Store largeETL/PDB internally, verifyhash before preserving original-path symlink.
Externalpwdev~5GiB free (full-launch gate>=5GiB); internal~80GiB. Preserve latest50
guestexperiments. No more spacecleanup needed unless gatefails.

938 final hostgate1309dc278d0fd2149a5cbe1639f51850e36fd3c570027de5a5ed53f3bfa5756b;
ROOT938/original-final symlinkinternalEXP938-original. Allrecent NTFS flush/clean
checks pass after931 recovery; do not re-enter oldNTFS repair orreplacehives.
One937 normalrecovery selfPSCIreset hadKernel41/no1001/newdump; retryhealthy, reason
unknown. No hostforcedreset. All other transitions ordered.

Unrelated pre-existing test limits: frontendprepare localreference path/dirtyMesa;
virtual-submit replay harness missingAdmissionMemoryRuntimeLocalView/Inner prototypes.
Do not claim whole-suiteGREEN orfixunrelatedharness. Relevantfocused suites above pass.
