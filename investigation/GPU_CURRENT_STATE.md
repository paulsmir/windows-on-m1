# GPU current boundary — 2026-09-21

## Working mode
One executor in the current task. Routine build/test/SSH/launch/recovery steps are
not delegated. This supersedes the earlier mandatory Astra/Terra handoff loop at
the user's request to reduce time and tokens. No new agents without a concrete
independent need and user direction. Fix command/harness errors locally; they are
not architectural REDs. Read this file first; do not load the historical ledger.
The previous 2875-line state remains in Git at f4016d36 (same path).
Use source/logs as evidence, not an agent's completion claim.

## Objective and fixed architecture
Stable, visibly correct accelerated Windows desktop on Air M1 remains UNPROVEN.
Physical/patch-list WDDM is fixed; GPUVA is CLOSED/NO. Existing real Asahi graph ->
typed capture -> request-scoped materialization -> UMD composer/pfnRenderCb ->
KMD Render/Patch/Submit -> AGX. Do not redesign these layers or expand features.
OpenGL/CS1.6 follow desktop acceptance. Ordinary setup errors are not GPU verdicts.

## Current source / candidate
Integration worktree: /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler.
Compiled fix boundary: 5eaa15da2ecafa7d540f25e31059f7a7f6231d9a (package742).
Diagnostic 34e2498581855cbfe168bf4b64197ab8a4607f90; WDK fix fbd96fae;
original compute done predicate restored749e7154, regression tests27bd1369 PASS.
Latest prior bookkeeping HEAD f4016d36cedb1e6e9312b502112b10a65d74ab04.
Package742 is current;741 is the preserved pre-fix reference. All artifact paths below are rooted at
/Users/pavel/public_windows/.local/experiments (NOT the worktree's .local).
Candidate: EXP730-initialized-compute-742/builder/package. Unchanged client/cert
remain in EXP726-compute-identity-package741/{qualification,pinned.cer}.
Fresh742 source archive EXP730-initialized-compute-742/source.tar from clean
f5adda26, SHA29be05f0700ab6c231f06a1ea1dba643f262816e07779b1714d46f3aa39de1bd.
Build/analysis0warnings0errors; frozen native runtime props reused unchanged.
SYS 1758a4927ce4cf322a30703404f572675115625b927da7446feacf0415dda644
UMD bcb17e54233083500d2354db6b23d8e148a6a93fd9edcecb10567629db3ccf0e
INF 2c26b7e5cc6a3518df4f080893e78f2136b63f83e20509101004e867b45ee6bc
CAT 3697d73de96bc83263d01bfe66a5540296343e4acdc8f49e67a345de28fb453c
Client ec0adf38e1963d9eb8ec13ec9b998e15bdb570c6add4d8087a77a05afce9bf15
Cert 97145866a1530003077eacd8457f1a7a644d662423278fd94e450f903c85cbda
Signer E9BE15BD2A184BFABA0C8035B3C620C58037A241. Existing TESTSIGNING and exact
Root/TrustedPublisher trust reproduce native catalog PASS on Air. /kp Microsoft
root rejection and extra osslsigncode keyUsage rejection are recorded limitations,
not newly added gates. Do not change signer/security or repeat signing archaeology.

## Proven boundary and recent outcomes
EXP725/739: full16384000-byte primary copy status0/interrupt1/DPC1; subsequent
result8 phase5 fence304 compute guard6/subguard3. Event mismatch is measured,
but its operands were not captured at the failing decision. Source-backed next
discriminator is741's paired Build/RunTa job/provider/event/fence/stamp/done/work.
Evidence: EXP725-hardware/{evidence.json,client.txt}.
EXP726/740: invalid overlapping installation after failed cleanup; no compute verdict.
EXP727/741: successful install in FULL-OWNER584/406, then orderly fresh boot:
Code0, matching binaries and signer persistence. Client ran in SSH Session0,
CreateDevicePASS then CreateSwapChain887a0022/reason0; no user/Explorer and no
compute diagnostic. This is invalid desktop-session setup, not a GPU defect.
Evidence: EXP727-compute-identity; clean recovery receipt finalbaseline.json.
EXP728: no console workload. A prematurely invoked runner had no executable;
no client process. Corrected runner now requires nonzero ExpectedSession.
Installing741 in ordinary377/392 broker-OFF guest preceded a reset; ordinary
recovery also reset before SSH. Cause remains UNPROVEN (41/6008 only; historical
0x133 dump is not correlated). Immutable hidden377/385 recovered Windows;
exact741 was removed and ordinary377/392 returned. Evidence:
EXP728-console-compute/recovery/evidence/{EXP728-System.evtx,EXP728-092126-38078-01.dmp}.
Independent live baseline at17:15:23Z: Code28/nullINF, no package/service/module,
SYS/UMD or signer;8CPU; AutoAdminLogon0 and no DefaultPassword. Receipt under
the evidence directory: independent-baseline-20260921T171523Z.json. No login armed.

## Current causal result / next action
EXP729 valid Session1 run exposed garbage compute fields already in Build,
unchanged at RunTa (same job/provider, command4/MixedBranch0/fence335).
AppleAgxExp208BuildJob copied an uninitialized candidate. Fix5eaa15da initializes
it; pattern regression RED->GREEN,6 related tests PASS. EXP730 candidate742 is
hardware-validated for this fix: fence298 BackendSubmitResult0, CompletionStatus0,
TA/3D observed stamps match expected0x7a000100/0x3d000100 and done2/2;
NotifyInterrupt/Dpc1/1. No compute rejection. Standard client PID4876/Session1
still returns Present887a0005/reason887a0020 after CreateDevice/SwapChain0.
Do not call this stable desktop or diagnose firmware from normal fault-snapshot.
Evidence EXP730-initialized-compute-742/{EXP730-client.json,EXP730-client.stdout.txt}.
Clean ordinary baseline17:59:48Z: Code28/no package/service/module/files/signer,
8CPU; autologon0/no password. Candidate742 package remains immutable.
EXP731 process-local UMD trace: render-callback/native-dispatch/flush-state S_OK,
no present-callback; make-residentE_PENDING followed by waitS_OK (not a failure).
Source client Present(0,0) conflicts with native _Present accepting onlyONE.
Real Mesa DDI IMMEDIATE regression RED -> GREEN with705738eedf7692e45010649b22c9014ee5974cfb.
Native/generic guards now accept IMMEDIATE orONE; invalid intervals/other args
remain rejected. Existing opt-in native-present-entry records live args next run.
x64 actual producer/2placements/retirement + DDI suite exit0. Fresh x64/ARM64
native libraries built; ONLY DxgiFns.cpp differs in prepared source comparisons.
Candidate743 package/link/sign/hash next; no hardware verdict on interval fix.
Evidence EXP731-present-trace/EXP731-umd-trace.txt and EXP732-immediate-present.
Clean ordinary baseline18:18:26Z: Code28/no residues/signer,8CPU, no autologon.
Reuse executed full-owner/install/autologon/capture/cleanup recipes; install ONLY
in driver-absent full-owner584/406, orderly exit before chainload, one console
Interactive-token client. Keep process-local trace to measure actual interval.
## Fixed execution recipe — do not rediscover
Git: /opt/homebrew/bin/git (system Git hits unaccepted Xcode license).
Host cwd /Users/pavel/public_windows; Python proxyenv/bin/python.
Both chainload AND run_uefi need LLDDIR=/tmp/agx-lld-dir/.
Frozen launches need command-scoped WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT=1;
contract checkpoints unavailable is NOT PASS. Full-owner also needs
WOM1_AGX_G2_POWER_BROKER=1; ordinary/emergency must leave it unset.
USB /dev/cu.usbmodemC02HDNCCQ6L41 (proxy), ...L43 (vUART).
Chainload m1n1_windows/proxyclient/tools/chainload.py with M1N1DEVICE set;
then run_uefi.py <FD> --device <L41> --display-mode physical --debug-mode off
--low-mem --contract-output <experiment-local-path>.
Keep foreground exec session + durable log, never a detached background PID.
Windows shutdown must complete BEFORE chainload; confirm guest exit and proxy
re-enumeration, not merely SSH loss. Probe SSH+USB+launcher before operator request.
Full: EXP584-kmd-output/m1n1.macho + EXP-20260904-406-coherent-abi-admission/J313_EFI-exp406.fd.
Ordinary: EXP-20260903-377-secondary-cpu-receipt/assisted-boot/m1n1.macho +
EXP-20260903-392-current-gpu-mu-publication/assisted-boot/J313_EFI.fd.
Emergency only if ordinary unreachable: same377 +
EXP-20260903-385-hvc-single-page/recovery/J313_EFI-no-agx-autoboot.fd.
Air pavel@192.168.1.37 key /Users/pavel/.ssh/air;
knownhosts EXP641-standard-present/air_known_hosts. Builder pauls@192.168.1.24
key /Users/pavel/.ssh/windows_builder. Use uploaded literal PS files, not nested
shell quoting. Parse changed PS once; ordinary typos need no architecture review.
Installer /add-driver requires /install for existing devnode binding.
Enumerate actual AppleAgxRenderAdmission.inf/fullpath, not ^AppleAgx.inf$.
CM_PROB_FAILED_INSTALL is expected Code28 when the recovery devnode is inert.
Receipt Wom1ComputeIdentityDiagnostic: device Device Parameters and SERVICE ROOT
HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission (not Parameters).
Collect exact bytes + normal receipts before cleanup. Exact package cleanup,
hash-matched residues/signer, ordinary recovery remain required after experiments.

## Context and gate budget
Keep this file <=150 lines; replace current state instead of appending history.
One preregistration and one actual ledger update per experiment; raw logs stay
in artifacts. Reuse passed gates for unchanged hashes; add none without a real
contract defect. Batch independent reads, report only first meaningful failure.
Do not count planning, transfers or an empty output as completed work. Verify
on-disk artifacts/tool exits directly. No repeated "continue" handoff loops.

HARDWARE ROADMAP
[PASS] Native stack has prior execution evidence;741 paired diagnostic/offline gates.
[NOW] Candidate743 immediate-Present correction package gates.
[NEXT] Same traced console client verifies live args and reaches Present callback.
[HW] Standard Present and stable complete physical image remain unproven.
POST-HARDWARE: sustained correct desktop, longer acceptance; OpenGL/CS1.6 later.
