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
Current implementation056377f3; last hardware package745 remains immutable.
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
Constructor5eaa15da hardware validated EXP730: TA3D completion, matching stamps,
done2/2, interrupt/DPC1/1. Do not reopen spurious compute dispatch.
EXP734 ETW exposed app-local stale741 UMD in EXP728 client directory; it was
hash-checked and quarantined as EXP734-shadow-UMD-741.dll.saved. EXP732/733
absence-of-new-callback conclusions invalid. Always isolate EXE without DLLs,
System32 workingdirectory, ETW actual loaded-module path/checksum/timestamp proof.
EXP735 coherent744 measured Blt1/interval0 rejection. Fix75bc5baf hardwarevalidated
EXP736: exactSystem32745 checksum12328883/timestamp1790021801, client1028Session1
STANDARD_RUNTIME_PASS create/draw/present; native render/flush/present callbacksS_OK.
Service GDI fence300 status0 interrupt/DPC1/1; TA3D stamps/done2match. Earlier
PresentTransfer fence253 is NOT correlated to client1028. No replay allowed.
User explicitly reports physical black/white corruption PERSISTS on745.
Current Application events prove repeated dwm.exe/dwmcore.dll26100.9278 crashes,
exception889800b0 offset27bde4 since boot; Explorer5236 remainsSession1, DWM PIDs
change. ETW records DWM exits/restarts. Desktop acceptance remainsFAILED.
Evidence EXP736-blt-present/{EXP736-dwm-state.json,dwm-etw-selected.json,
dwm-runtime-events.json,physical-observation.json,loaded-umd-proof.json,
EXP736-umd-trace.txt,EXP736-client.json,EXP736-runtime.etl}.
EXP736 historical WER dump is INVALID for current attribution; static same-version
binary inspection only. EXP737 empty WER copies alsoINVALID.
EXP737 passive trace completed on unchanged745, no qualification client. Direct3D11
journal1820 reports887A0004 "Failed to find DDI to drive requested feature levels"
in DWM5292/8580/8644 and other shell processes. DWM HWDEVICE409 identifies only
Microsoft Basic Render Driver/FLc100; DWM continues crashing889800b0. Loaded
System32745 image checksum12328883/timestamp1790021801 verified. ETW60s/182698events,
eventsLost0. This is a DWM runtime-DDI negotiation boundary, not a queue fault.
Evidence EXP737-dwm-creation/{causal-result.json,device-events.json,
EXP737-dwm-evidence/runtime.etl,EXP737-dwm-evidence/state.json}.
Both attempted current WER copies were zero-length and INVALID; do not analyze.
Source decision: current D3D11.dll ETW checksum5006223/timestamp2013979966 matches
static inspected binary10.0.26100.9457. FillAPIVersions tests createflags bit5/BGRA:
skips VistaDDI a0001/build4, retains a0006(D3D10_0_x) and a0009(Win7).
Dwmcore same-version call uses0xa9(+priority1000), includesBGRA; FLlist includes10_0.
This explains empty runtime/UMD DDI intersection; no higherFL requirement inferred.
PinnedWDK minor6/build0 is authority; older Microsoft page macro examples differ.
Microsoft extended-format-aware requirements mandate BGR families, typed backbuffer
casting and BGRA/sRGB scanout. Must close companions before advertising0_x.
EXP738-extended-bgra/causal-contract.json records primarysources and ownership.
Implementation642baea78033df502947b399e39a8e8762a1d433 closes typeless BGRA/BGRX
sampled views through actual producer. REDs: view-family rejection, compressed
initial-upload draw, BGRX texture capture whitelist. Fix: same-family views,
native TILED modifier via existing allocator, bounded BGRX texture reference.
Complete source85a5c10332b16055eb6d8c0efd8ae3be711936c530c20b459d840a056afcb8df.
Actual x64 executable suite exit0/fourUNORM+SRGB cases, initialupload/context clean,
materializer2placements/retirementPASS; build0warnings0errors; ARM64archivePASS.
EXE660097af5b93ab4666a7073bf5d63b2f7fcab8a7b4184ccdc109a7b2b0e37098.
Only preparedFormat.cpp/Resource.cpp/agx_win32_graph.inc changed; no diagnostics
left in corebatch. Evidence EXP738-extended-bgra/{complete/result.json,
complete/test.log,projection-comparison.json,causal-contract.json}.
EXP739 implementation57b30b555e4bbf8f0bc728c733b351803dfbb2b4 closes typedBGR targets:
BGRA_SRGB/BGRX_UNORM/BGRX_SRGB clear/draw/flush; distinct nativeformat10/11/12,
4byte ABI bounds; existing capture/materializer/retirement. RED creation -> GREEN.
Source81236b2afa3b642b474ab2d0c1718a62f3669f724cdb979f59465d7b70a32a25;
x64suite0,0warnings/errors,ARM64archivePASS,hostABI PASS; EXP739-bgr-targets/green.
EXP740 implementation056377f3cb88de0f0f73795e2449fef71ab42ba5 admitsBGRA_SRGB
primary with existingBGRA byte layout, plus same-family RTV/SRV casting scoped
to created BIND_PRESENT resources. Ordinary typed/cross-family casts stillreject.
RED primarycreate -> fullx64suiteGREEN,0warnings/errors; bothnativearchivesPASS.
Existing SetDisplayMode and nativeBlt/Present/retirement now exercise SRGBprimary.
Sourcee11c78b6892f63708c10d362a16eb12d8ef8af4d46a6254ef3877279893fadda;
EXEdf59dd254cff05cdecd4a732514023039ea0573c194a1ba1ecb2fcdbb1ee6707.
Evidence EXP740-srgb-backbuffer/{contract.json,green/result.json,green/test.log}.
NEXT: truthful format-query and remaining mandatory BGR resource/scanout operations;
current tests do not prove arbitrary stretch/rotation/resolve or full dimensions.
D3D10_0_x advertisement remainsCLOSED until all mandatory companions pass.
Operator followup: black afterlogon; elements only appear withartifacts after
Windows key; no spontaneous correctdesktop. Exact additional run/time unspecified.
Evidence EXP736 physical-observation-followup.json. Does not prove Flush/scanout.
745 exactcleanup20:47:27Z succeeded. Ordinary377/392 finalbaseline20:49:20Z:
Code28/nullINF,no package/service/module/SYS/UMD/signer,8CPU,autologon0/no password.
Operator poweredAir off; nowRunningproxy. Recheckedproxy/vUART USB present,
WindowsSSH timeout, noactive launcher. Do not relaunch stale55457. Lastcleanbaseline
above remains lastverifieddiskstate; recheck ordinaryguest before futureinstall.

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
[NOW] BGRA_SUPPORT excludes VistaDDI; closing mandatory0_x extended-format companions.
[NEXT] Derive minimum truthful runtime-DDI admission and required companion contract.
[HW] Only a justified DWM discriminator/fix run; original first-runtime-test target met.
POST-HARDWARE: correct stable accelerated desktop; OpenGL/CS1.6 only afterward.
