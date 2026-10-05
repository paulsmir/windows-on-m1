# EXP910 correction and EXP911 envelope discriminator

## Proven correction to the EXP910 report
All eleven preserved ETL inputs and the progressive UMD snapshots were rehashed against final-host-manifest.json. The full retained set, not just first/final windows, contains four later DWM1240 Present events on source handles 80005300 and 40000d80. UMD callbacks appear progressively 0,2,4 in same-original-boot snapshots. The claim that DWM had no UMD Present callback after Apple started is false. Do not erase the historical report; this is its explicit correction.

ETL four unique later ID184 events: 10:58:12.9239533 status0/source80005300; 10:58:37.3740848 statusC01E0200/source80005300; 11:00:41.9204199 and 11:00:55.9224828 status0/source40000d80. Flags8000 = RedirectedFlip in pinned WDK26100, not an ordinary display Flip. Context handles C00017C0 and C0002800 map through ETL context/device creation to Apple adapter ffffd48223ece000, LUID4207c. These are not the early BasicRender frames. Later DWM recreates BasicRender devices at11:03:18; sequence alone does not prove why.

KMD armed contexts retain branch7 failures9776/22407 and completed9213/21897. The source80005300 matches the second failed context; source40000d80 matches a later context completed35185. Global first branch7 Flags80 means Resubmission and PID4, not DWM. No exact per-DWM rejection predicate exists. RedirectedFlip does not itself prove a physical flip is requested, so KMD Present0 is not sufficient evidence of a missing mandatory MPO callback.

## Sources and ownership
FULL GRAPHICS. Inspected current Windows gpuva_g3_windows.c, scheduler_windows.c, dwm_ddi_probe_windows.c, umd_runtime_device.c, umd.c, native frontend generator build-native-asahi-state.py, agx_d3d10_windows.cpp; pinned d3dkmddi/d3dkmthk/dxgiddi26100. Asahi77cb8f24 dcp.c/iomfb_template.c distinguishes swap completion from timing; current m1n1 broker/service and Mu generated APPL0002/IRQ889/local-reserve exposure remain the EXP910 contract. No new physical register, DMA, IRQ, clock or firmware sequence is proposed. Existing EXP910 full.log/contract and immutable recovery establish the inherited memory/interrupt contract.

Microsoft SubmitCommandVirtual: C000000D means malformed input and puts the device into error; it is not normal scheduler backpressure. GPU Preemption: nonpaging resubmission gets a new fence. Current KMD accepts the Resubmission bit but requires a matching suspended private-scene lifetime. Exact failed field must be measured before relaxing it. Microsoft Supporting the DXGI DDI requires NO_REDIRECTION for drivers without the shared D3D9 hardware path; changing that return blindly is not a justified fix. Native negotiated InterfaceA0006/Version177A has no DXGI1_3 table, so writing Present1/MPO slots would corrupt the base table. MPO documentation makes its DDIs conditional on supporting that feature; no evidence makes MPO3 mandatory here.

References: https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual ; https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-preemption ; https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-the-dxgi-ddi ; https://learn.microsoft.com/en-us/windows-hardware/drivers/display/multiplane-overlay-support ; https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_presentflags .

## Experiment contract
WHY THIS HYPOTHESIS:
1. DWM really reaches Apple runtime Present, disproving the handoff premise; an exact redirected Present returns GPU_EXCEPTION_ON_DEVICE.
2. Two same-DWM KMD contexts have branch7 refusal; Microsoft says this status errors the device. The first global refusal is a resubmission, ranking suspended lease state above absent MPO.
3. Existing frame receipts do not distinguish outer guard from private lease/resubmission lookup. One bounded snapshot resolves that uncertainty without changing admission.

WINDOWS CONTRACT: retain truthful caps/DDI tables and SubmitCommandVirtual semantics; preserve context/device/fence namespaces and capture actual refusal state.
AGX/ASAHI CONTRACT: no native packet, UAT or hardware ownership changes; queued/preempted scene must retain correct owner/generation until resubmission or retirement.
TRANSLATION: frame ABI2 adds a first branch7 snapshot per armed context. Stage1 records outer guard scalars; Stage2 records context and exact lease/scanned scene while State.Lock is held. UMD records session and QPC for time correlation. Existing Present and device/context ETL provide adapter association. Runtime results and guards unchanged.
WHAT IS STILL UNKNOWN: exact DWM branch7 predicate and whether device-error transition prevents desktop composition; redirected Present history versus actual display queue progression. Firmware/caps changes prohibited without new evidence.

ContextState bits: closing1, poisoned2, cancelUncertain4, schedulerActive8, Win32Transport16. SceneState bits: found1, foreignContext2, quarantined4, queued8, submitting16, started32, releaseRequested64. Snapshot freezes first failure per context, preserving its fence even when frame-arm changes the tracked allocation. Frame Version2 rejects mismatched probe/UMD ABI rather than interpreting old bytes.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Wrong-context receipt attribution, overwrite of first failure, and a diagnostic observer mutating native scene state. Actual recorder and extracted observer tests check these. This is receipt-only work; no manufactured RED or claim of a black-screen fix. Existing high-local BeginJob replay must remain GREEN. Full baseline comparison uses exact failure identities.

## Execution and falsifier
Build/sign/hash package911 with unchanged EXP910 firmware/Mu/recovery. Instrumentation is the only variable. No synthetic producer, DWM restart or caps experiment. One original >=750s boot: correlate DWM session/context/allocated source/QPC and ETL Present/device events to frozen branch7 state and late current-DCP snapshots. If branch7 is a valid-resubmission contract error, derive minimal RED/GREEN owning correction next; if no branch7, follow actual redirected/display queue state. No claim of working desktop without normal changing image and current-surface proof. Keep original/periodic/recovery host size+SHA gates and exact package cleanup, >=4GiB+ShadowCount0 prelaunch.

## Control-plane correction
The requested standalone proxy NOP disturbed an ordinary guest with no host launcher; USER_INTERRUPT and later stage1 proxy were observed, same-boot continuation was not proven. No GPU experiment was run. Immutable377/392 recovery subsequently produced Code28/pkg0/CPU8/USB5/disks2/RDP/autologon1 at boot12:34:37.395251Z. Serial NOP must not be treated as passive when SSH is live; inspect owner/endpoints and use NOP only after determining the guest is not executing. Detailed BEFORE/AFTER in EXPERIMENTS.md.

Evidence: root .local/analysis/EXP910-present-boundary/{etl-inputs.json,*.etl.jsonl,correlated-events.json,diagnostic-replay.log,frame-test.log,suite.log,recovery-ordinary-first.json,recovery-ordinary-second.json}. No accepted package and no new hardware verdict at this checkpoint.
