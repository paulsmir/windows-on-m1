# EXP926 heartbeat result implementation plan

Goal: resolve the exact pre-submit heartbeat failure behind EXP925's unretired
fence before changing runtime behavior. Implement inline under the user's standing
instruction to continue until stable physical output; no subagents, no reapproval.
This is a bounded diagnostic addition to the existing backend worker, not a new
render/scheduler/power architecture. Project source-first/experiment rules govern.

WHY THIS HYPOTHESIS:
1. Matched925 PDB/fullkernel SchedulerFaulted0x40bb0 maps exactly to the heartbeat
failure branch in backend_platform_windows.c2992; submitted7966 remains active
behind completed7961, before queue enqueue. This is closer than allocation disposal.
2. KMDdestroy577entries/577exits/drop0; firmware BootReady/Running/CpuReady and
lastASCep20/type42 is already allowed. These observations exclude a persistent KMD
free body and that exact event being a protocol violation.
3. Actual frozenFwlog6 state blocks are allzero, eventread/write both0x6f. No
fulllog/event ring in this capture. ExactHeartbeatResult and timing are absent:
RenderCorrelation.Count0, optionalregistryheartbeat receiptsabsent, initialASCtrace64
saturated. One in-memory invocation receipt distinguishes timeout/clock/transport.

WINDOWS CONTRACT: FULL GRAPHICS unchanged. A dispatched fence must complete or
enter supported recovery; a failed pre-submit health operation is not successful
execution. Microsoft TDR synchronizes KMD calls, captures fences and requests reset.
No capability/DDI/admission/GPUVA/retirement/reset-status changes. KeQueryInterruptTime
is the same existing monotonic Windows time source; extra final read observes only.
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/thread-synchronization-and-tdr
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8

AGX/ASAHI CONTRACT: ep0 managementPing/Pong is distinct from ep20 Event42. Asahi
GpuManager recv_message and m1n1 FirmwareEP route42 to channel polls. ExistingWindows
heartbeat accepts42 but no channel pump insideit. Frozenemptyrings make fullness
unsupported here. LinuxRTKit handles management/app messages through one receiver;
currentWindows ASC consumer search found only session boot/heartbeat/stop, no second
runtime reader. m1n1 retained powerplatform owns power gates only; Mu APPL0002 ACPI
exposes mappedwindows/inertrecovery, not mailbox consumer. Firmwarekick0x10 exists
inprimary sources aswake aid; absencebeforeheartbeat is a hypothesis, not a fixyet.

TRANSLATION: same existing heartbeat call/500ms deadline, record one coherent
invocation in ADMISSION_PLATFORM_RUNTIME: Sequence/Calls/Fence/Result,
StartMs/EndMs/DeadlineMs/RxBefore/RxAfter/LastRxPayload/LastRxEndpoint. Oddsequence
meansinprogress; even means complete. One platform worker owns it. No extra MMIO,
wait, registry, allocation, callbacks, retry, wake or return-normalization.

WHAT IS STILL UNKNOWN: exact heartbeat result and elapsedWindows interrupt time
under originalhardware; whether Pong waitexpires or actual time regresses. The
runtime/firmware powerstate and management-response behavior may require hardware
measurement after this discriminator. Do not infer hardwarefailurefromc0000483.

Inspected primary/currentfiles: backend_platform_windows.c worker/ASC wrappers,
shared apple_agx_rtkit_session.c/asc_transport.c/rtkit.c and tests; render correlation
and receipts; pinnedAsahi gpu.rs recv_message/kick and channel handling; m1n1
fw/agx FirmwareEP/EventMsg, agx.poll_channels, src/rtkit.c deferredreceiver and
hv_agx_power_platform.c; Mu J313AppleAgxAbiAdmission.asl.inc; Microsoftdocs above.
Currentreference LinuxRTKit source read asbehavior only, noexternal code copied.
Runtime ownership: KMD backendowns ASC/session/channel operation; firmwareownsPong
andGPU processing; Windowsowns fences/TDR; m1n1owns retainedhardwarelaunch/stage2/
power broker and Mu ownsACPI; same validated R143/full/377-392 recovery artifacts.

## Tasks
- [ ] Add64byteinternalheartbeatreceipt and bracket onlyexistingworkerheartbeat.
  Preserve deadline calculation and all branches/statuses. No externalABIchange.
  WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? This is receipt-only hardware
  instrumentation, so no artificial RED test. ExistingASC/RTKit/providersuites GREEN.
- [ ] Commit implementation, append CHANGES.csv40hash statusimplemented, validate
  ledger; pinnedARM64/native/WDK zero-warning build926/sign/CAT/PDB/hash/sourcegates.
  NativeUMD andSDK behavior unchanged; package version follows experiment926.
- [ ] Preregister hardware/artifacts/manifests after sealing (before install).
  Fresh ordinaryCode28/CPU8/oneAPPL0002/zero priorpackage; installonlyexact926.
  FirstCode0 readiness ->one originalSDK ->Presentcheck/evidence immediately.
  Snapshot receipt through matchedPDB ifTDR; no longPresent0 stabilitywait.
  CollectallclosedfileshostsizeSHAbefore Code43 exactcleanup, normalrecoveryCode28twice.
  Smallest checkpoint: evenheartbeatreceipt withexactResultandtiming correlatedto
  activefence. Incomplete/missingreceipt=inconclusive, never guessedstatus.
- [ ] Verdict selects nextowninglayer anddeterministicreproducer/fix. Only after
  realPresentandphysicalcorrectframe start a separatelyrecorded stabilitycheck.

Pre-flight: no shared interfaces; one internalrecord consumed by matchedPDB only.
Review focus: interruptedoddsequence; source/PDB mismatch; optionalmacroreceipts
missing; staleoriginalboot; interpretingaWake42 asPong. Existing hardware/hashgates
and exactevenreceipt check address these withoutmanufacturedsourceassertiontests.
