# GPU current boundary — 2026-09-23

## Working mode
One executor; routine build/test/SSH/launch/recovery steps are not delegated. This supersedes the earlier mandatory Astra/Terra handoff loop at
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
Current implementationEXP752/package747 fixes indexableTEMP; 746 remains rejected/removed.
EXP736 package745 under /Users/pavel/public_windows/.local/experiments/EXP736-blt-present/package-build/package.
SYS d352215b5d641c527481f16bb0d985803e6a8791d3dadec3674b2f30ee6e72a5
UMD c620fad0950a6ff7e110e6df2e3a1cf245a68b79692ae01e3528be953b029528
INF fc3fb2417b29498ee9961da14ad17cb7d891447a583a81cca3282f71d46c2366
CAT 2421a016a37e9f80b91d974a19e596759f5ff522ae115b5c19de45aaa5317c39
Client ec0adf38e1963d9eb8ec13ec9b998e15bdb570c6add4d8087a77a05afce9bf15
Signer E9BE15BD2A184BFABA0C8035B3C620C58037A241; preserve existing TESTSIGNING.
Artifacts are rooted at main repository .local, NOT worktree .local.

## Proven boundary / current causal target
EXP736 exact745 standardruntime client Create/Draw/Present PASS; physicaldesktop
corruption persisted. EXP737 DWM failed before DDIdispatch (BGRA0xa9 removeslegacy).
EXP747 Map/nonblocking/busy offlinePASS;EXP748 realSOFTWARE runtime selects0_x
interfacea0006/version177a and conditionalDXGI1_1 table PASS (builder9278 vsAir9457).
EXP749 BGR1D/2D/3D/Cube views,16GenMipschains,ld_ms->txf_ms offlinePASS;
MS_LOAD set for87/88/91/93,MS_RT0,quality>1zero. Shared256registry/constructionlimit.
EXP750 measuredsharedCreate/Open offlinePASS: private-onlyA->B,draw/fence/destroyA/
sampleB;6measuredsizes plus1366x768;invalidbytes/pitch/size reject. Linearstride and
borrowedlogicalsize fixed;existingallocator/capture/composer/retirement retained.
EXP751 advertiseslegacy+0_x (ebab00e3); fullx64suite0/ARM64testbuild0.
Original mandatory DWM inventory PASS; no broad expansion. SeeDWM_NEXT_GATES.json.

EXP751 candidate746 source209412e71f64b22752552c5788d88f120d190368b9ce4655829e381f5ecabe67.
Packagebuild/analysis0warnings0errors;KMDUniversal/Inf2Cat/signer/catalogmembershipPASS;
AirnativeCATPASS. SameTESTSIGNING/signercfg; installedonceoem5Code0, nowremoved.
Candidatehashes/launchprofiles/prereg: EXP751-dwm-extended-admission/manifest.json.
ActualboundDWM now reachesnativeCreateVertexShader, thenASSERT !indirect in
exactgeneratedtgsi_to_nir.c574 ->ucrtbaseabortc0000409/FAST_FAIL7. Callchain:
D3D11ClearGuard/BeginGuardRectangleSupport ->DirectComposition ->uDWMinit.
Root cause: ShaderTGSI.c DCL_INDEXABLE_TEMP emitted scalarureg_DECL_temporary;
TTN only permitsindirect access whenDeclaration.Array creates variable. EXP752
fix below preservesassert and doesnotreinterpret0_xasinherentlyunsuitable.
Noactiveuser/Explorerafter45s;autologonpasswordcleared. Post-logincheckpointINCONCLUSIVE.
403081ETWevents/lost0;1304System32UMDimageevents exact746 checksum12354734/time1790135148.
PriorEmptyDDIintersectionmessageabsent; journals80004001/887A0020 removal remain.
NoDWM Present orphysicalshaderpixelsproven. Emptyrejecttrace doesNOTprove5DDIsPASS;
reject-blt=0unexercised. Physicalobservationnotreceived; userpreviousblack/artefacts
remainhistoricalbaseline, notEXP751 observation. Noqualificationclient/retry.
Evidence17fileshashverifiedbeforecleanup;freshdumps/debug-current/debug-assert/
fault-source/etw-causal-summary/loaded-umd-proof/causal-result inEXP751artifactroot.

EXP751 exactcleanup0;ordinary377/392 finalbaseline04:11:28Z Code28/nullINF,
nopackage/service/module/SYS/UMD/signer,8CPU,ANS/USBhealthy,no41/1001/129,
autologon0/nopassword,traceenvrestoredabsent. Ordinarylauncher14804 currentlyactive;
WindowsSSH reachable. Don'tchainloaduntilorderlyshutdownandproxyreenumeration.
DirectFlip retained mandatoryWDDM1.2+bit;DWMwithoutCheckDirectFlipSupportUNKNOWN.
KernelModeCommandBuffer: separatefutureCLEAR unlesscoherentapertureproven.
TDRrequiredABI retained; softwareResetNOTfirmwarequiescence,timeoutfatal/reboot.
GeneralBlt deferredunderunchangedNO_REDIRECTION;reopenonactualreject-blt.
NoGPUVA orclosedlayerredesign. Newarraytranslationtask is justified only byEXP751
actualruntimeassertion, notoptionalcompleteness or inventory expansion.

EXP752 exactRED->GREEN: pinnedFXC VS declaresx0[4] and readsx0[r0.x]. Old source
exitsc0000409 atsameTTNassert; fix usesoneureg_DECL_array_temporary and propagates
ArrayID to elements. TGSI nowTEMP[1..4],ARRAY(1);NIRtranslation and actualAsahidraw
throughcapture/materializer/KMD2placements/retirementPASS. x64fullsuite0;ARM64
native/testbuild0;8hosttestsPASS. Source01e72146;seeEXP752-indexable-temp.
Nohardwareclaim. Nextcandidate must change onlythiscausalshadertranslation over746.
Package747 ARM64analysis0warnings/errors,Universal/Inf2Cat/version30.0.747.0,
existing signer/catalogmembership/localhashesPASS. Preregistered exact causal run
in EXPERIMENTS andEXP752 manifest; AirnativeCATPASS/installonce/cleanupcomplete.

EXP752 hardware proves priorTTN !indirect assertion GONE and sameClearGuard VS
advances intoAsahi compiler. Newassert agx_compile.c1446 on
nir_intrinsic_load_vertex_id_zero_base: `stage==MESA_SHADER_COMPUTE && only for SW VS`.
Stack iswassert->agx_compile_shader_nir->CreateVertexShader->D3D11ClearGuard->
DirectComposition->uDWM. ExactSystem32747 identity via1308ETWimageevents;407172
events/lost0. Journals80004001/887A0020;rejecttrace0bytes. Noactiveuser/Explorer;
post-login/physicalobservationINCONCLUSIVE. ElevenRaidPort0Event129 occurred during
boundboot; temporalonly, GPUcausalityNOTestablished. Evidence18fileshashverified.
Exact747cleanup0; ordinaryfinalbaseline08:07:57Z Code28/nullINF/noresidues,8CPU,
traceenvabsent. Nextoffline target: authoredVS combiningindexableTEMP+SV_VertexID;
derive correcthardwareVS vertex-id mapping withoutweakeningAsahiassertion.

EXP753 exactRED->GREEN: pinnedFXC VS combinesSV_VertexID andx0[4]/x0[r0.x].
Currentcode RED atsameagx_compile.c1446 afterarrayNIR. Windows-specific Asahi shader
prep now lowers zero-baseID to fullvertex_id-base_vertex before existing sysval
lowering; authoritative draw-params table preserved. Assertion unchanged. Actual
Asahidraw/capture/KMD2placements/retirementPASS; x64suite0;ARM64native/testbuild0;
8hosttestsPASS. Sourcec7f786f5;seevertexid-verified-contract.json. Nohardwareclaim.

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
Verify on-disk artifacts/tool exits; planning/transfers are not completed work.

HARDWARE ROADMAP
[PASS] Originalgates;EXP751TTNrootcause;EXP752offlinefix+hardwareadvance;747cleaned/recovered.
[NOW] EXP753 hardwareVS vertex-id exactRED/GREEN offlinePASS; commit/package gates.
[NEXT] Freshsingle-variable package/preregistered DWM run; no inventory expansion.
[HW] Nextunproven: ClearGuard shader compilation ->desktopdraw/completion/Present;post-loginobservation.
POST-HARDWARE: generalBlt onlyifmeasured;stableaccelerateddesktop acceptance remainsUNPROVEN.
