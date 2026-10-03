# J313 GPU — EXP941 first real windowed Present S_OK; physical proof pending

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

## CURRENT — EXP941 active; first real windowed Present S_OK
Source382a333d65368453879155a21895bbef280ba126; ROOT941=.local/experiments/EXP941-native-shared-presentation.
All555source/native/WDK0warnings/signatures/hosthash/PDBGUIDage gates PASS.
SYS23a792a3f17d9e23ebc5d49696da355ff9234402f447877efa7e3738afb2ebe5
UMDd98dab01b6dcaa636191506138b0b097afcd9ec116ed629827d065c27c5eb814
INFe84f516ca2112460e0c0d7f8e99fc42d65a975c2dcb6e5349a8cf6012378286b
CAT6f79d1a642f0b7d827e94df494825a6399cab138a0823b71b94f071274deea1a
NativeGPUVA+extendedDXGI1.1 success usesS_OK; software/base retainsNO_REDIRECTION.
CurrentWin11D3D11 consumer scope; legacyD3D9interop notclaimed. NoDDI/cap/fwchange.

940 original frozen/hostverified and exactoem5 cleanup complete. First normal
recovery selfPSCIreset; one retry recovered. No new1001/41 inselectedinterval or
newdump; reasonunknown. DurableCode28 baseline124297 passed;941only staged/armed.
Ordered fullrestart05:54:07.373Z, launchsamebdcf/MuR143. Current launcher65399,
session23072; do notreset orrawproxywhileowned. Original941 immutableevent124399,
Start2026-10-03T05:54:17.4508010Z, generation242524939; CPU8/Code0/Stage12/HVC1,
Armnull/exact941. Runtime bootclock differsfromimmutableevent; useeventidentity.

SAMEce544 --windowed SDK3940: create/clear/realPresent1/device-after ALL S_OK,
first improvement over940OCCLUDED. UMDactualPresent callback androtation S_OK.
stdout971B SHAdf80d167478f4b545f3ceb464b8112cc6c3d880ad207256716286f9545c93055.
Task267014 terminated before finalmanifest; later host-recovery-manifest validates
stdout/stderr hashes andSDKabsence, ownedtaskremoved. No boundedexit claim.
Postwindowed DWM1244:97submit95complete +2/2, Present0/Virtual0/TDR0.

Same941 fullscreenSDK3732 pendingCreateSwapChain12s; pairedsdk108978B
SHAa8c1becb088101f2035f5da928b5866ee8d153bd807fc50a91cab34c0706d985,
DWM117870B SHA48e276e615228e95ac98f8868ff1ba2cf95ce35c1dd61603101a2b46182b0824.
All7fileshostverified, SDKkilled~14.37s/taskremoved. Matching941PDB decodeEXP941-paired:
SDK DXGIproxywindow->DWM ALPC; DWM LPC privateRELEASE->ContextRetire->UpdateSubresourceUP.
Changing snapshots andcontinuedsubmissions are notpermanentdeadlockproof.
Do notrepeatfullscreen orblindly changeanotherescape flag.

Physical941question ispending (sentafterwindowedS_OK); lasthumanfeedback936artifacts.
Preserve941activeboot whileawaitingphysicalfeedback. No stable/workingdisplayclaim.
Nextcausalboundary: DWMcomposition/presentation -> actualscanout. A new experiment
must distinguish this boundary, not repeat now-provenwindowedadmission. Source
UpdateSubresourceUP unconditionallyFlushRetires wholecontext; resource-map upstream
has own hazards, but no new fix justified yet. Do not remove synchronization blindly.
Posttests SSHalive/RDPserviceRunning/storageUSBstatusOK; no currentSystem1001/Application1000/newdump. Physicalinputnotoperator-tested.
Nooriginal941freeze/ETLstop/rollback yet; collectfinalbeforeexact941normalcleanup.

ROOT940=.local/experiments/EXP940-private-release-software.
940SYSa8748efa750df0f3e2fba3cfbbf0b082477f1582d0293b0a0039c0e46d24096c
940UMDa89ecaa21aba054543da8433a632e542920a2d1b07b4836fdefc820017f85f80
940INF00f2ae4fe2782d670ce661cd0f4e515084e65658d0030725b83b5b6a29317a3b
940CATcd3f683e2ec5dc942c01f167ee0bf90c87dda0ef0249799b35811148912ad682

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

## EXP940 result and retained contract
Source ec73437819d57a721694976855ded432b5ddfcc9 changes private RELEASE to
software entry plus safe deferred ownership transfer. Queued/Submitting/owner
JobInFlight/LeaseToken retain mappings; existing reaper unmaps/revokes/zeros/frees
only after holds end. Authentication, generation, quarantine and fence guards
retained. Real16/64KMD/graph/m1n1 replay old RED/flag-only BUSY/final GREEN.
Current fullscreen still pending12s, paired stack moved to NtDestroyAllocation
inside agx_pool_cleanup/ContextRetire/D2D UpdateSubresource. A changing snapshot
is not proof of a permanent deadlock at each operation. No more blind flag edits.
941 native shared-route hypothesis is recorded in its plan; first windowed test.

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
