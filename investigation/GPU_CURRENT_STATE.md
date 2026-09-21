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
Integration HEAD before this evidence checkpoint:59aa1672d3e30d4085dc0d19275b5e0af8c978f2.
Implementation75bc5baff3b638e5d5f483686153cca523738117 accepts exact Blt1 orFlip2.
EXP736 package745 under /Users/pavel/public_windows/.local/experiments/EXP736-blt-present/package-build/package.
SYS d352215b5d641c527481f16bb0d985803e6a8791d3dadec3674b2f30ee6e72a5
UMD c620fad0950a6ff7e110e6df2e3a1cf245a68b79692ae01e3528be953b029528
INF fc3fb2417b29498ee9961da14ad17cb7d891447a583a81cca3282f71d46c2366
CAT 2421a016a37e9f80b91d974a19e596759f5ff522ae115b5c19de45aaa5317c39
Client ec0adf38e1963d9eb8ec13ec9b998e15bdb570c6add4d8087a77a05afce9bf15
Signer E9BE15BD2A184BFABA0C8035B3C620C58037A241; preserve existing TESTSIGNING.
Artifacts are rooted at main repository .local, NOT worktree .local.

## Proven boundary / current causal target
EXP729 paired Build/RunTa exposed already-uninitialized compute fields in a TA3D
job. Constructor fix5eaa15da initializes candidate={0}; deterministic pattern
regression RED->GREEN,6 related testsPASS. EXP730 physically validated completion,
matching TA/3D stamps and done2/2, interrupt/DPC1/1. Do not reopen compute dispatch.
EXP734 ETW exposed app-local stale741 UMD in EXP728 client directory; it was
hash-checked and quarantined as EXP734-shadow-UMD-741.dll.saved. EXP732/733
absence-of-new-callback conclusions invalid. Always isolate EXE without DLLs,
System32 workingdirectory, ETW actual loaded-module path/checksum/timestamp proof.
EXP735 coherent744 measured flags1/Blt, interval0, sourceindex0,destination0,
valid source/context rejected by Flip-only guard. Fix75bc5baf realDDI RED->GREEN,
x64 producer/materializer/retirementPASS; both native archives; onlyDxgiFns diff.
IMMEDIATE fix705738ee also retained; no newcaps/queue/allocator/composer.
EXP736 client1028 Session1 with exact loaded System32745 checksum12328883,
timestamp1790021801 returned STANDARD_RUNTIME_PASS create=PASS draw=PASS present=PASS.
Trace native-render-callback/native-dispatch/flush-state/_Present/present-callback
allS_OK; observedBlt1/interval0. One client only; no replay allowed.
Service GDI receipt fence300 stage7 status0 submit/completion0 interrupt/DPC1/1;
TA/3D expected/observed match and done2/2. PresentTransfer fence253 is earlier:
do NOT assert that receipt belongs to client1028 without causal correlation.
User explicitly reports physical black/white corruption PERSISTS on745.
Current Application events prove repeated dwm.exe/dwmcore.dll26100.9278 crashes,
exception889800b0 offset27bde4 since boot; Explorer5236 remainsSession1, DWM PIDs
change. ETW records DWM exits/restarts. Desktop acceptance remainsFAILED.
Evidence EXP736-blt-present/{EXP736-dwm-state.json,dwm-etw-selected.json,
dwm-runtime-events.json,physical-observation.json,loaded-umd-proof.json,
EXP736-umd-trace.txt,EXP736-client.json,EXP736-runtime.etl}.
WER Temp current dump disappeared before copy. Preserved dump SHA6874779fff450e759d21604c8de570f77cf67ea3b31061fdae6d9f0b06ef4203
predates EXP736 boot: historical ONLY; dump-validity.json explicitly rejects
current attribution. Its matching-code stack points to CreateD3D11Device;
this is a lead, not current causal proof. Current Report.wer/Application.evtx saved.
NEXT: source-first DWM device-creation admission diagnosis; obtain current first
HRESULT/DDI failure before a semantic fix. No evidence justifies AGX queue changes.
Exact745 oem5.inf uninstall/delete succeeded20:32:31Z. Orderly shutdown completed;
ordinary377/392 recovery verified20:34:01Z: Code28/nullINF, no packages/service/
modules/SYS/UMD/signer,8CPU,autologon0/no password; session71264 active.
EXP737 passive trace completed on unchanged745, no qualification client. Direct3D11
journal1820 reports887A0004 "Failed to find DDI to drive requested feature levels"
in DWM5292/8580/8644 and other shell processes. DWM HWDEVICE409 identifies only
Microsoft Basic Render Driver/FLc100; DWM continues crashing889800b0. Loaded
System32745 image checksum12328883/timestamp1790021801 verified. ETW60s/182698events,
eventsLost0. This is a DWM runtime-DDI negotiation boundary, not a queue fault.
Evidence EXP737-dwm-creation/{causal-result.json,device-events.json,
EXP737-dwm-evidence/runtime.etl,EXP737-dwm-evidence/state.json}.
Both attempted current WER copies were zero-length and INVALID; do not analyze.
Read-only static same-version dwmcore shows FL array includes a000/10_0 and lower;
D3D11CreateDevice flags0xa9 plus priority privilege0x1000. Exact current failed
request args remain unmeasured. Never infer DWM requires higher FL from fallbackc100.
Native Adapter.cpp projection advertises only D3D10_0_DDI_SUPPORTED (Vista).
Pinned WDK confirms x/7 interface differences; Microsoft docs distinguish DDI from
feature level. Next narrow source decision: why D3D11 cannot select advertisedDDI;
do not blindly enable higherDDI/caps without companion implementation.
745 exactcleanup20:47:27Z succeeded. Ordinary377/392 finalbaseline20:49:20Z:
Code28/nullINF,no package/service/module/SYS/UMD/signer,8CPU,autologon0/no password.
Recovery launcher session55457 active. No new package or client currently installed.

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
[PASS] EXP736 exact745 standard runtime Create/Draw/Present and native callbackS_OK.
[NOW] EXP737 DWM Direct3D11 negotiation887A0004; Basic Render fallback and crashes.
[NEXT] Derive minimum truthful runtime-DDI admission and required companion contract.
[HW] Only a justified DWM discriminator/fix run; original first-runtime-test target met.
POST-HARDWARE: correct stable accelerated desktop; OpenGL/CS1.6 only afterward.
