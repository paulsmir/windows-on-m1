# J313 GPU — EXP964 callback fix and physical-pixel boundary

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

## Current — EXP964 running, callback refusal cleared, physical pixels unknown
No accepted visual desktop package. Operator reset REC-EXP958A and ordinary
recovery reached exact958 Code43/SSH. Cleanup958 succeeded, removed oem5 and
ordered reboot; one intermediate PSCI reset occurred. Identical ordinary
relaunch reached SSH, durable preflight PASS/Code28/CPU8 at11:30Z.

EXP959 source c9dacb1, package959 30.0.959.0 INF615859a9/SYS0851f9a9/
UMD34411206/CATa1900174 (receipt only) staged on that exact clean boot,
then full-owner m1n1-exp954/Mu-r143 booted Code0/CPU8/SSH/Explorer/disk/USB.
DCP exact latches through swap37; snapshots mostly zero (later1067/4096000
nonzero), no physical pixel success. DWM1220 crashed once, restarted1568.
UMD trace shows two Flush stage3 E_INVALIDARG (PID1220/1568). EXP959's new
umd-deallocate-failure receipt was silently quota-suppressed: both DWM PIDs
had exactly 128 non-measure records before the failures, and the diagnostic
function caps all non-measure/non-reject stages at 128. Therefore classification
is INCONCLUSIVE; neither local guard nor pfnDeallocateCb is excluded.
Dump dwm.exe.1220.dmp SHA061742cd analyzed on builder: AV reading
0x000000003c146258 in uDWM!CCachedBorderBrush destructor while x23 held
0x000001d03c146240 and x22 held its truncated low32. Earlier EXP958D full
dump showed analogous high32 loss in two uDWM visual vtable pointers. Writer
unknown; this repeated pattern is the strongest current corruption boundary.

REC-EXP959A stalled ordinary; documented host SIGTERM captured CPUs and reset
to proxy. One identical ordinary relaunch reached exact959 Code43/SSH. Exact959
cleanup and ordered reboot gave durable Code28 PASS at12:10:08Z; no package
carried into EXP960. Manual reset was not needed.

EXP960 quota correction commit f38a7f0a2bc514ab0115e67b58f5d60da2f7ae05
is verified offline (RED/GREEN focused test, UMD 11/11); pinned WDK ARM64
package960 built 0 warnings/errors, INF f8878e19, SYS7ed0377c,
UMD4400ca06, CAT0f51cd58, receipt SHAfa384ffc. Exact package staged after
clean Code28, full-owner booted Code0/CPU8/SSH. Corrected receipt proves
Windows pfnDeallocateCb returned E_INVALIDARG for full hResource
0x0000019ad19fe880 before DWM Flush stage3 E_INVALIDARG; local guard is
excluded for this first error. Later same handle received 0x88760870
(device removed). DWM1236 fail-fast dump SHAef18f82a is 0x889800d0
"DWM failed during present"; this run did not reproduce low32 pointer AV.
DCP latches/zero snapshots are not valid desktop pixels; no accepted screen.
Microsoft D3DDDICB_DEALLOCATE docs confirm hResource form/NumAllocations=0
we pass; callback invalid parameter cause remains to resolve from exact
Create/Open/Destroy provenance and runtime ownership. Keep independent low32
pointer corruption as separate writer-unknown issue.

EXP961 reused exact package960 after full rollback to query the existing KMD
DWM DDI probe. It did not reproduce UMD callback E_INVALIDARG in a short run;
the probe retained only latest successful DestroyAllocation among 756/4784
calls, so verdict INCONCLUSIVE. Exact961-local cleanup and durable Code28 PASS.

EXP962 source commit66565a2a adds only existing bounded G1b failure receipt
for KMD DestroyAllocation (ID6), package962 built0 warnings/errors. Full-owner
reproduced UMD stage2 E_INVALIDARG for full runtime hResource
0x000002bc95d26600 before Flush stage3. G1b registry receipt Count0 before,
during and after the error; KMD DDI probe observed 1788 DestroyAllocation
calls, latest status0. Thus no failing KMD DestroyAllocation return was
observed in this boot. Inference: Direct3D runtime/dxgkrnl rejects the
resource handle before KMD or after a successful KMD call; exact internal
ordering not proven. Do not modify KMD hResource logic on speculation.
New DWM dump SHA f6474f7c has another c0000005 low32 uDWM code-pointer
read (0x00000000fc5abf50) in CBaseObject::Release. It repeats EXP886/958/959
corruption but the writer remains unknown; do not claim callback refusal
caused it. DCP latches/cached-zero samples still do not prove physical pixels.

EXP963 source27a45271 added failure-only UMD retirement origin. Hardware
first failure was Origin1/CreateResource, Primary0, Shared0,
KernelResource0, KernelAllocation0x80001140, full runtime handle
0x000001f84e9f8120. Repeated failures have the same class. This strongly
points to wrong deallocation form for a device-associated nonshared
allocation, separately from the writer-unknown low32 DWM corruption.
EXP963 exact package was removed in Code43 and durable Code28 PASS on
boot14:06:00.8827080Z after one ordinary recovery PSCI reset.

EXP964 source commit6fc753fa uses the Microsoft documented allocation-list
`pfnDeallocateCb` form only for nonshared nonprimary CreateResource with
KernelResource0 and nonzero KernelAllocation. Shared/primary/resource-backed
forms and failure requeue are unchanged. RED/GREEN and UMD/R148 suites passed;
pinned WDK build964 0 warnings/errors, exact INF bdb6e604, SYS d3288aaa,
UMD184f78c9, CATcb05ad69. EXP964 full-owner is ACTIVE on Air, Code0,
CPU8/SSH/Explorer/DWM1232/disk/USB5. At14:44:02Z, >30min after boot
14:13:12Z, no DWM Application1000 errors, same DWM PID, 13154 UMD lines
with zero deallocation or Flush E_INVALIDARG errors, KMD DWM graph3
submit441/complete440/Present44 status0. DCP exact
latches continue; cached zero samples cannot establish physical pixels.
Console GDI CopyFromScreen timed out15s/result267014 without PNG; task was
removed. Operator text description of physical panel was requested
asynchronously but not yet received. Do NOT call this a stable desktop or
accepted package. The current EXP964 visual-verification run remains live;
cleanup964 exact scripts/manifest are staged and guest-hash verified, ready
for Code43 rollback if pixels are wrong. Do not start a new GPU package while
this one is installed. Evidence EXP964/evidence/30min-identity.json SHA44ba64de,
umd-30min.log SHA58bf98cc, dwm-ddi-30min.txt SHA3b2676d6,
desktop-capture/before.json and ledger.

Next causal decision depends on physical pixels. If desktop is correct, verify
motion/input and an additional bounded stability window, then decide package
acceptance. If artifacts persist, collect the observation, roll back exact964
to durable Code28, and focus on GPU-produced DWM surface versus DCP scanout
under synchronized cache observation; do not guess stride or format because
current m1n1 IOMFB descriptor matches its source reference. Keep the recurring
low32 DWM pointer writer as an independent unresolved cause.
WHY CLEAN RECONSTRUCTION: old admission comparisons do not explain current
DWM pointer truncation or separate platform recovery hang.

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
