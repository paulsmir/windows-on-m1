# GPU current boundary — 2026-09-23

## Working mode
One executor in the current task. Routine build/test/SSH/launch/recovery steps are
not delegated. This supersedes the earlier mandatory Astra/Terra handoff loop at
the user's request to reduce time and tokens. No new agents without a concrete
independent need and user direction. Fix command/harness errors locally; they are
not architectural REDs. Read this file first; do not load the historical ledger.

## Objective and fixed architecture
Stable, visibly correct accelerated Windows desktop on Air M1 remains UNPROVEN.
Physical/patch-list WDDM is fixed; GPUVA is CLOSED/NO. Existing real Asahi graph ->
typed capture -> request-scoped materialization -> UMD composer/pfnRenderCb ->
KMD Render/Patch/Submit -> AGX. Do not redesign these layers or expand features.
OpenGL/CS1.6 follow desktop acceptance. Ordinary setup errors are not GPU verdicts.

## Current source / candidate
Current implementationEXP747 Map/busy; last hardware package745 remains immutable.
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
Evidence EXP736-blt-present: dwm-state/loaded-umd-proof/physical-observation andETL.
EXP737 passive trace completed on unchanged745, no qualification client. Direct3D11
journal1820 reports887A0004 "Failed to find DDI to drive requested feature levels"
in DWM5292/8580/8644 and other shell processes. DWM HWDEVICE409 identifies only
Microsoft Basic Render Driver/FLc100; DWM continues crashing889800b0. Loaded
System32745 image checksum12328883/timestamp1790021801 verified. ETW60s/182698events,
eventsLost0. This is a DWM runtime-DDI negotiation boundary, not a queue fault.
Evidence EXP737-dwm-creation/{causal-result.json,device-events.json,
EXP737-dwm-evidence/runtime.etl,EXP737-dwm-evidence/state.json}.
Source decision: current D3D11.dll ETW checksum5006223/timestamp2013979966 matches
static inspected binary10.0.26100.9457. FillAPIVersions tests createflags bit5/BGRA:
skips VistaDDI a0001/build4, retains a0006(D3D10_0_x) and a0009(Win7).
Dwmcore same-version call uses0xa9(+priority1000), includesBGRA; FLlist includes10_0.
This explains empty runtime/UMD DDI intersection; no higherFL requirement inferred.
PinnedWDK minor6/build0 is authority; older Microsoft page macro examples differ.
Microsoft extended-format-aware requirements mandate BGR families, typed backbuffer
casting and BGRA/sRGB scanout. Must close companions before advertising0_x.
EXP738-extended-bgra/causal-contract.json records primarysources and ownership.
EXP738–742 offlinePASS BGRsampling/targets/primarycasting/formatbits/RT|SRV; referencedartifactdirs unchanged.
EXP743 implementationee605a52490e294d18143786a4c4dc517614feb7: 1024square4MB
native color output now passes UMD, materializer, KMD bind/root routing,retirement.
One shared native surface validator replaces16/fullscreen whitelists;8192axis,
1/2/4/8/16byte pixel pitch, backingfootprint and16MB arena bounds retained.
Fixture1MBclass limit corrected to realKMD16MB. Fullx64suite0/0warnings/errors,
3hosttestsPASS,ARM64testcrossbuildPASS; EXP743-bgr-geometry/routed evidence.
EXP744 offlineGREEN: sampling tests now use actual FXC ps_4_0 Sample/SampleLevel
Draw/Flush, notResourceCopy conversion. SV_Position is TGSI systemvalue; SAMPLE
keeps texture0/sampler3 inNIR. Texturedcapture usesexisting760reloc boundedstorage.
ResourceCopy now enforces equal2Dsize/castfamily and rawUNORM bytes independently
of boundSRV; wholeCopyRegion delegates. Crossfamily/size negativecasesPASS.
Finalsourcea90e1309f331c599cfaa24a3e862d156bc45fffc5c3b826d93a9c28c249de575.
x64fullsuite0;ARM64testcrossbuild0;5hosttestsPASS; EXP744-copy-contract/green.
DWM_SHARED_RUNTIME_CONTRACT.md/EXP746 supersedes shared/Blt POST-HARDWARE placement.
EXP737: API0xA02=SHARED|GDI_COMPATIBLE|SHARED_NTHANDLE;0x200=GDI_COMPATIBLE.
Same-runtime APIMiscFlagsToDDIMiscFlags:0xA02->DDI2,0x200->DDI0; NT ownedbyruntime/KMT.
WDK permitsDXGI1_1resolve with0_x+Versionlow0x177a; these flagsdoNOTforceD3D11DDI.
Shared/GDI/Blt pre-DWM acceptance: DWM_NEXT_GATES.json; independentA->B/private-data-only,measuredsizes;runtime-tableproof BEFOREslotwrite.
DirectFlip preregistration: DWM behavior withoutCheckDirectFlipSupport UNKNOWN; observe reject-*/ETW; retainmandatoryKMDcap.
KernelModeCommandBuffer: CLEAR in separatecapschange unlesscoherentaperture proven; nofakeCacheCoherent.
TDR: retainrequiredresetABI; softwareresetNOTAGXquiescence; timeoutfatal/reboot,ResetFromTimeoutfailurecanbugcheck.
EXP745 feb7f7a1 offlinePASS: sixBGRformats x4staging mip/array subresources exact
byteroundtrip; dynamicBGRAsRGB/BGRXsRGB Map->actualshaderDraw/Flush->retirement.
ExistingAsahitransfers andWindowsflush/retire; no newallocator/composer.
Sourcee72ca40b59a434694ae9cbd9af081e9217175a5bce32e213f5a551bbaba28a70;
x64suite0,ARM64archives/testcrossbuild0; EXP745-texture-map/verified.
EXP747 nonblockingMap+ResourceIsStagingBusy GREEN: active/no-flush,pending/timeout0,
completion/retry/holds,unrelatedresourceidle; x64suite0/ARM64build0,EXP747-nonblocking-map/final.
Map stayscontext-wide;WRITE_DISCARD serializes,norenameclaim. NEXT software-runtimeprobe.
EXP746 refusaltraceGREEN: all5DDIs observed,one recordperrejection; x64suite0/ARM64build0.
UseTRACE_FILE+APPLE_AGX_UMD_REFUSALS_ONLY=1; skipsoptionalresidencyqueries.
No0_xadvertisement ornewhardwarecandidate; donotclaim readiness.
Rotation can leave prehardware scope: Microsoft DXGI_DDI_BASE_FUNCTIONS exempts
nonidentityrotation whenprimarycreation neverusesDXGI_DDI_ERR_UNSUPPORTED; our
activecreatepath doesnot. MSAAresolve notadmitted (quality>1zero). No fakepass.
Operator followup: black afterlogon; elements only appear withartifacts after
Windows key; no spontaneous correctdesktop. Exact additional run/time unspecified.
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
[PASS] EXP736 standardruntimeCreate/Draw/Present;EXP744-746;EXP747 Map+busy.
[NOW] Software-runtimeprobe: actual Interface/Version/DXGI1_1 table; no slotwrite beforeproof.
[NEXT] 0_xcoverage thenlinearshared/private-data-onlyA->B; pitch>=width*bpp and%16=0;package gates.
[HW] DWM hardwaredeviceadmission/firstDDIfailure;physicaldesktop immediatelyafterlogin.
POST-HARDWARE: generalBlt deferredbyNO_REDIRECTION;preregister reject-blt=0,reopenonreject;stabledesktoplater.
