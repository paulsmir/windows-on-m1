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
Compiled production boundary: 749e7154720688dfcf42fbf09ce81a7a1574808e.
Diagnostic 34e2498581855cbfe168bf4b64197ab8a4607f90; WDK fix fbd96fae;
original compute done predicate restored749e7154, regression tests27bd1369 PASS.
Latest prior bookkeeping HEAD f4016d36cedb1e6e9312b502112b10a65d74ab04.
Package741 is unchanged. All artifact paths below are rooted at
/Users/pavel/public_windows/.local/experiments (NOT the worktree's .local).
Candidate: EXP726-compute-identity-package741/{package,qualification,pinned.cer}.
Manifest/provenance-final.json plus builder-all-input-summary.json record335/336
matching files; the sole mismatch is an uncompiled host test. Original source
archive is unavailable; recorded integration archive is explicitly post-build.
SYS b0b4c86e53545853ba1b6827d1d2b4ab15db7014e1d87c2a8781379f35428203
UMD e8edb9423617497293da38e1f2dcb9216e212aa29c9b8618b8697bd5e40caa38
INF d9c004ac3785ff5fd5e621ba1e4bb9d7143d3b276c8b29bd4d691cff93c5b305
CAT c0c07ae50ded732cdb4e0ce8e0a363eb837163a89eec00901d29ac49646f5ed1
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

## Next causal action (already architect-reviewed)
Reproduce successful EXP727 installation environment, correcting only the console
workload setup: clean ordinary -> orderly exit -> full-owner584/406 with AGX
package absent -> exact741 explicit /install THERE -> orderly fresh full-owner
boot and actual physical-console login -> first DWM diagnostic or one client.
Do NOT reinstall741 in the ordinary broker-disabled guest. No new GPU code yet.
Use the previously authorized temporary autologon; clear DefaultPassword and
AutoAdminLogon immediately after login/failure. Never log a credential.
Require active physical console user, Explorer and DWM in the same nonzero
session. Interactive-token task only, never direct SSH/Session0. If login itself
produces the first compute failure, collect it and do not launch another client.
Scripts: EXP728-console-compute/{arm-console-autologon,clear-console-autologon,
run-console-client,EXP728-task-runner}.ps1. Parser-tested, runtime flow not yet
validated. Verify hashes and paths; never execute task-runner for preflight.
Client path C:\Users\pavel\EXP728\AppleAgxD3d10Standard.exe;
runner path C:\Users\pavel\EXP728-task-runner.ps1. Preserve one-shot result guard.

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
[NOW] Clean baseline and reproduce successful full-owner installation + console login.
[NEXT] First DWM/interactive-client receipt selects the exact compute owner.
[HW] Valid paired compute identity experiment; stable correct picture unproven.
POST-HARDWARE: sustained correct desktop, longer acceptance; OpenGL/CS1.6 later.
