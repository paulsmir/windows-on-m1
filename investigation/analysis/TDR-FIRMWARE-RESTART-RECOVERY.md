# TDR recovery by firmware restart (timeout-only stall) -- design specification

2026-11-09, offline design. No driver code, no hardware, no ledger edits. Closes the design
half of GPU_DRIVER_ARCHITECTURE.md 2.1 "BUILD (C) timeout-only TDR recovery".

Path prefixes: `K/` = `drivers/apple-agx/render-admission/`, `S/` = `drivers/apple-agx/shared/`,
`A/` = `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/` (HEAD 77cb8f24),
`R/` = Asahi `drivers/soc/apple/rtkit.c` read from the git object at that HEAD (sparse checkout),
`M/` = `m1n1_windows/` **as checked out in this worktree (0d3a75b3)**, `W/` = `.local/reference/wdk26100`,
`L:` = `https://learn.microsoft.com/en-us/windows-hardware/drivers`.

Evidence grades used below: **[HW]** ledger experiment, **[SRC]** source read at the cited line,
**[DOC]** Microsoft documentation, **[HYP]** hypothesis.

## 0. Findings that change the task statement

| # | Finding | Evidence |
|---|---|---|
| 0.1 | **Asahi has no firmware restart.** A firmware crash latches `crashed`, fails all jobs with ENODEV and is never cleared; a job that never retires without a firmware event only hits a 100 s scheduler timeout whose handler is a FIXME. The reference for a restart is macOS, not Asahi. | [SRC] `A/gpu.rs:378-396`, `A/queue/mod.rs:403-411`, `A/queue/mod.rs:479`, `A/object.rs:9` |
| 0.2 | **"The retained firmware cannot be restarted" has never been tested on hardware.** The three KMD comments cite EXP1241/1242/1278/1296; in all of them the reset path refused by design after an RTKit stop. The only restart candidate (EXP589) was built and never run. | [SRC] `K/src/backend_platform_windows.c:9150-9157`, `K/src/scheduler_windows.c:626-636`, `S/src/apple_agx_g13_queue_runtime.c:1326-1333`; [HW] EXPERIMENTS.md EXP1241 ACTUAL ("reset unsupported"), EXP1278 ACTUAL, EXP589 BUILD ("HARDWARE DEFERRED ... no hardware verdict"); commit 590ae1dcc message |
| 0.3 | The KMD, not m1n1, owns the ASC CPU and the whole RTKit session; start and stop state machines already exist and accept a second start. What blocks a restart today is (a) the reset path never calls them, (b) the m1n1 broker refuses CLOSE/power-off while any GPU process is alive. | [SRC] `S/src/apple_agx_firmware.c:86-103,137-227,246-324`, `S/src/apple_agx_rtkit_session.c:93-135,361-413`; `M/src/hv_agx_retained_platform.c:149-157,404-418,562-565` |
| 0.4 | macOS restarts the same iBoot-loaded image at runtime (symbols/strings only, not disassembled). | [SRC] `.local/reference/macos-kernelcache-j313-15.7.8/extracted/kcscripts/agx.dm:1432-1435` (`stopFirmwareForRecoveryInspection`, `stopFirmware`, `bootFirmware`), `agx.dm:294-296` (`AGXAccelerator::restart`, `restartWorkQueue`), `agx.str:415` ("Failed to reboot the firmware"), `iogpu.str:267` ("Trying to restart GPU"), `fwk.str:59` ("GFX firmware was not loaded by iBoot") |

Caveat on `M/`: the worktree submodule checkout (0d3a75b3) is neither the pinned gitlink (882985fb) nor
the bench m1n1 (brokerlock-cppc3 line, MEMORY "Working GPU profile"). Every `M/` line below must be
re-pinned against the bench source before stage 4; KMD ops QUERY_CAPS/NEGOTIATE_LIVE have no case in this
checkout (`M/src/hv_agx_retained_platform.c:259-261`), so it is older than the bench.

## 1. Evidence

### 1.1 Experiments

All bugchecks are 0x116 VIDEO_TDR_FAILURE with P3 = 0xC0000483 (STATUS_DEVICE_HARDWARE_ERROR), P4 = 3;
P2 points into AppleAgxRenderAdmission.sys (`AdmissionDdiResetFromTimeoutStatus`, EXP2100 cdb).

| EXP | Job / trigger | Firmware state at the reset | Failing step | P1 / P2 | Class |
|---|---|---|---|---|---|
| 1964 | GravityMark 10k, combined render+CDM, PID 720 fence 0x3fb8; unmapped CDM read at 0x11ac9a0000 (FAULT_INFO 0x11ac9a001303cc11) 70 ms after submit | Timeout/Fault event, halt count 1, **resumed**; done/write TA 149/151, 3D 1006/1008, compute 904/905 never converge | engine, engine, adapter reset quiescence each timed out | ffffe701e9a02050 / fffff803eff51ec0 | F1 |
| 1965 | same fault, PID 7248 fence 0x3cbb, 71 ms | resumed; TA 697/699, 3D 1060/1062, compute 739/740 | same | ffffd98ea01272e0 / fffff802e3b71ec0 (sys+0x1ec0) | F1 |
| 2005/2007 | rc37 GPU fault drill (injected CDM read fault) | resumed, compute done 1 / write 2 | same | P2 sys+0x1ec0 | F1 |
| 2100 | GM FHD depth two: render-with-prelude 0x182a -> compute 0x182b | **RTKit crash** (session result 8 FirmwareCrashed); TA 0x107/0x109, 3D 0x226/0x228, compute 0x2d/0x30 | quiesce failed at the RTKit drain, WaitMs 0, Stage 2, three resets | ffffc20e81146010 / fffff800ae611ec0 | F2 |
| 2214 | GM FHD, OV5-waived renders (Waived 3) | one firmware recovery published (fence 14278) + GpuFault, then no retirement | quiesce | ffffb40cc1cf13a0 / fffff802fe1d1ec0 | F1 |
| 2283b, 2285a | GM start, waiver on, diagnostics off | unknown (no dump on the ANS profile, receipts are published only after completions) | -- | PSCI reset only | F3? |
| 2286a | same | unknown; "firmware did not retire a job at GM start" | ResetFromTimeout | ffffdd8efb9f7050 / fffff8005ce01ec0 | F3? |
| 1242 (history) | fence 0x4e99 after 178 recovered timeouts | **no firmware event**, halted 0 | KMD deadline, then reset refused | -- | F3 |

Sources: EXPERIMENTS.md EXP1964/1965/2005/2007/2100/2214/2283/2285/2286 ACTUAL and "EXP2286 ARCHITECT
READING"; `investigation/analysis/RC37-firmware-channel-recovery.md` "Machine-state evidence";
`Q13-gm-depth2-tdr.md` table; `G4-recovery.md` "Observed". EXP2291/2293 later showed 2283-2286 to be a
kick-timing race (waived successor doorbell before the compute started) and removed the trigger
(6f18cfb8); the recovery gap itself is untouched.

Failure classes: **F1** firmware halted, was resumed, queue never retires. **F2** RTKit crash message.
**F3** silent: no event, no halt, done pointer does not move. The KMD cannot tell F3 from "long job"
except by time. Controls that do recover: EXP2032/2034 and EXP2098/2099 (KMD-side TDR drill: the job
runs to its end at the firmware; only the KMD stops polling) -- these never exercised a firmware
that does not retire.

### 1.2 What the reset DDIs do today and where they give up

| DDI | Path | Gives up at |
|---|---|---|
| `DxgkDdiResetEngine` node 0 | `AdmissionResetEngineInternal` (`K/src/scheduler_windows.c:684-868`): fault the owner, freeze dispatch, `AdmissionResetQuiesceActive` (`:637-680`) -> `AdmissionPlatformRuntimeQuiesceActive` (`K/src/backend_platform_windows.c:9345-9524`) polls RTKit + provider for `ADMISSION_RESET_QUIESCE_TIMEOUT_MS` = 1000 ms (`K/include/render_admission.h:2129`); optional RecoverChannel after 100 ms (`:9431-9450`, opt-in) | deadline -> STATUS_IO_TIMEOUT (`:9426-9430`); RTKit drain not Ok, e.g. crashed -> hardware error (`:9456-9464`); provider poll failure (`:9466-9476`). Caller maps every failure to STATUS_DEVICE_HARDWARE_ERROR (`scheduler_windows.c:728-729`). Also `AdmissionGpuvaG3PrivateReset` returns FALSE for any started private scene: "No GPU stop/TLB proof exists" (`K/src/gpuva_g3_windows.c:1012-1034`) -> `scheduler_windows.c:750`. |
| `DxgkDdiResetFromTimeout` | pause the scanout timeline, then the **same** engine reset for node 0 and node 1, then CompletedFence = LastSubmittedFence (`scheduler_windows.c:956-995`) | the same quiescence wait; nothing stronger is attempted. A failure status is the 0x116. |
| `DxgkDdiRestartFromTimeout` | refuses when `SchedulerFaulted`, else resumes the scanout timeline (`scheduler_windows.c:997-1004`) | touches no GPU state. |
| worker | an expired 500 ms job deadline is counted once and the job stays pending (`S/src/apple_agx_g13_queue_runtime.c:1326-1338`); dxgkrnl's TDR is the only hang detector | by design. |

Existing drills: `Wom1TdrDrillAllowed` (worker leaves its poll loop, firmware completes;
`backend_platform_windows.c:6106-6124,7517-7523,6216-6237`); depth-two drill (`:6067-6101`);
`Wom1GpuFaultDrillAllowed` (one injected CDM fetch fault; `K/include/render_gpu_fault.h:280-287`).
**No drill makes the firmware not retire a job.** The compute-start gate that hides the EXP2283-2286
race has no switch (`backend_platform_windows.c:6864-6875`).

## 2. WINDOWS CONTRACT

Header facts are from the pinned WDK; behaviour is from Microsoft Learn (pages fetched 2026-11-09).
Class: RO = RENDER_ONLY, FG = FULL GRAPHICS, DO = DISPLAY_ONLY. AppleAgx is FG with
`SupportPerEngineTDR = TRUE` (`K/src/lifecycle.c:813`).

| # | Requirement | Source | Class |
|---|---|---|---|
| W1 | TDR = a preemption that does not complete within TdrDelay (2 s). TdrDdiDelay 5 s for threads to leave the driver; more than TdrLimitCount (5) TDRs in TdrLimitTime (60 s) is a 0x116 regardless of recovery. | [DOC] `L:/display/timeout-detection-and-recovery`, `L:/display/tdr-registry-keys` | RO, FG (TdrDod* values: DO) |
| W2 | With SupportPerEngineTDR all of QueryDependentEngineGroup, QueryEngineStatus, ResetEngine are required, and ResetFromTimeout/RestartFromTimeout stay mandatory. | [DOC] `L:/display/tdr-changes-in-windows-8`; [SRC] `W/d3dkmddi.h:2440,8189-8260` | RO, FG |
| W3 | ResetEngine returns only when the node's hardware queue is empty and it accepts new packets; LastAbortedFenceId must lie in [last completed, last submitted] (else 0x119 P1 0xA). A failure promotes to the adapter-wide reset (TdrReason 9). An interrupt from the engine after the reset is 0x119 P1 0x10. | [DOC] same, `L:/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetengine`, `L:/debugger/bug-check-0x119---video-scheduler-internal-error`; [SRC] `W/d3dkmddi.h:8242-8247` | RO, FG |
| W4 | ResetFromTimeout: PASSIVE_LEVEL; no other KMD thread runs except power-component DDIs; interrupts and DPCs may still fire and the driver synchronises with them itself. On return the GPU is reset and **neither reads nor writes any memory**; the driver does not touch the hardware until restart. Only STATUS_SUCCESS is survivable. | [DOC] `L:/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetfromtimeout`, `L:/display/thread-synchronization-and-tdr`; [SRC] `W/d3dkmddi.h:8332-8340` | RO, FG |
| W5 | Between Reset and Restart the OS releases system-managed resources (VidMm purge; "most" DDIs may be called); the driver avoids GPU access in them. | [DOC] `L:/display/thread-synchronization-and-tdr` | RO, FG |
| W6 | RestartFromTimeout: PASSIVE_LEVEL; the driver may touch the GPU again; failure bugchecks. | [DOC] `L:/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_restartfromtimeout`; [SRC] `W/d3dkmddi.h:8342-8350` | RO, FG |
| W7 | Adapter reset fences: the driver advances last completed to last submitted on every node; no per-fence DMA_COMPLETED/FAULTED is required. | [DOC] `L:/display/tdr-changes-in-windows-8` | RO, FG |
| W8 | Device loss: every device on the adapter goes to an error state; the culprit sees DXGI_ERROR_DEVICE_HUNG (0x887A0006), bystanders DEVICE_RESET (0x887A0007); applications re-create device and objects. | [DOC] `L:/display/timeout-detection-and-recovery`, `L:/ddi/d3dkmthk/ne-d3dkmthk-_d3dkmt_deviceexecution_state`; [SRC] `W/d3dkmthk.h:4353-4364` | RO, FG |
| W9 | 0x116 parameters: P1 TDR recovery context, P2 pointer into the responsible driver, P3 NTSTATUS of the last failed operation, P4 internal context (undocumented). | [DOC] `L:/debugger/bug-check-0x116---video-tdr-failure` | all |
| W10 | No watchdog on the reset/restart DDIs themselves is documented. | [DOC] DDI pages (absence) | RO, FG |
| W11 | Display: the only documented visible effect is a redraw. | [DOC] `L:/display/timeout-detection-and-recovery` | FG, DO |

Not documented in anything read (see section 5): which VidPn/source-address DDIs dxgkrnl replays after
an adapter reset; whether GpuMmu page-table state is replayed by VidMm or assumed to persist; whether
contexts are re-created by dxgkrnl or only by the applications.

The Microsoft render-only sample is no behavioural reference: its ResetFromTimeout and
RestartFromTimeout return STATUS_NOT_IMPLEMENTED
(`.local/reference/microsoft-graphics-driver-samples/render-only-sample/roskmd/RosKmdAdapter.cpp:1386-1391,1498-1503`).

## 3. AGX/ASAHI CONTRACT

### 3.1 What Asahi does (and does not)

| Situation | Asahi behaviour | Source |
|---|---|---|
| Firmware Timeout (event 4) / Fault (event 0) | read FAULT_INFO (SGX 0x17030, read-only), mark the pending stamps' owners (culprit Timeout/Fault, others Killed), clear the 0x100-entry pending-stamp table, wait <= 100 ms for `halted`, then `halted = 0`, `resume = 1`. If not halted: "Cannot recover.", nothing else. | `A/gpu.rs:1422-1457,1005-1041,1059-1089,139`; `A/regs.rs:374-393`; `A/fw/initdata.rs:38-53` |
| ChannelError (event 8) | mark error, DeviceControl RecoverChannel{pipe_type, work_queue info pointer, event_value}, doorbell 0x83<<48 \| 0x11, wait 1000 ms; no halt/resume | `A/gpu.rs:1459-1542`; `A/fw/channels.rs:187-192,281-286` |
| RTKit crash (ep 1 type 1) | `crashed = true` (never cleared), fail all jobs NoDevice, every later submit/op returns ENODEV | `A/gpu.rs:378-396`; `A/event.rs:242-256`; `A/queue/mod.rs:634-641`; `R/rtkit.c:363-399` |
| Job never retires, no event | 100 s scheduler timeout, handler logs and returns NoDevice; no firmware action | `A/queue/mod.rs:403-411,479` |
| Suspend/resume, runtime PM, second boot | none; the Rust RTKit binding has no shutdown/quiesce/reinit | `A/driver.rs:152-237`; `A/gpu.rs:1257` (only `rtk.boot()` call) |

Completion rule that matters for recovery: errors only *mark* commands; a command completes when the
firmware advances the stamp (`A/workqueue.rs:854-934,945-973`). Asahi never frees memory of work the
firmware has not retired -- except in the crash path, where it fails everything while the GPU state is
unknown and the system "must be rebooted" (`A/object.rs:9`).

### 3.2 Boot sequence a restart has to replay (Asahi order; KMD equivalent)

| # | Asahi | Source | KMD today | Source |
|---|---|---|---|---|
| 1 | power domain `ps_gfx` on (DT) | `arch/arm64/boot/dts/apple/t8103.dtsi:483` | broker power ON: pmgr enable `/arm-io/gfx-asc` then `/arm-io/sgx` | `S/src/apple_agx_power.c:23-46`; `M/src/hv_agx_power_broker.c:45-59`, `M/src/hv_agx_power_platform.c:16-38` |
| 2 | ASC `CPU_CONTROL` (0x44) \|= bit 4 | `A/regs.rs:30-31,213-221`; `A/driver.rs:175` | `AppleAgxAscSetRun` + wait CPU_STATUS running | `S/src/apple_agx_asc_transport.c:64-82`; `S/src/apple_agx_rtkit_session.c:118-126` |
| 3 | UAT handoff: magic_ap = 0x4b1d000000000002, cur_slot 0, wait <= 1000 ms for magic_fw, clear flush slots | `A/mmu.rs:932-967` | `AppleAgxGfxHandoffInitialize` after CPU ready | `S/src/apple_agx_rtkit_session.c:127-134` |
| 4 | TTB slot 0 = kernel ttb0/ttb1 \| valid, slots 1..63 zero | `A/mmu.rs:1621-1637` | broker RR PREPARE (CPU stopped) + ACTIVATE (CPU running) | `K/src/backend_platform_windows.c:3327-3398`; `M/src/hv_agx_retained_platform.c:323-364` |
| 5 | event manager: 128 stamps, `stamp[slot] = slot<<24` | `A/event.rs:31,149-164` | notifier flights / stamps inside the initdata memory graph | `S/src/apple_agx_render_shared_memory.c` (owner), rebuilt by `AppleAgxInitdataMemoryPrepareBroker` |
| 6 | initdata, channel rings (0x100 entries each), ring pointers published | `A/gpu.rs:490-576,690-753` | initdata memory graph + channel memory | `S/src/apple_agx_initdata_memory.c`, `S/src/apple_agx_channel_memory.c` |
| 7 | queue DC Initialize, `rtk.boot()` (EPMAP, IOP ack, AP power 0x20) | `A/gpu.rs:1249-1257`; `R/rtkit.c:817-839` | IOP 0x220, HELLO, EPMAP, system endpoints, AP 0x20 | `S/src/apple_agx_rtkit_boot.c:60,86-132` |
| 8 | start ep 0x20, 0x21 | `A/gpu.rs:1258-1259` | StartEndpoint x2 | `S/src/apple_agx_firmware.c:160-184` |
| 9 | ep 0x20: 0x81<<48 \| initdata VA | `A/gpu.rs:1260-1261` | PublishInitdata + SendInitdata | `S/src/apple_agx_firmware.c:186-215` |
| 10 | ep 0x21: kick device control (0x11), kick firmware (0x10) | `A/gpu.rs:1262-1266` | DeviceControlInit, UpdateIdleTimestamp | `S/src/apple_agx_firmware.c:217-224` |

### 3.3 RTKit power states and stop/wake

| Fact | Source |
|---|---|
| OFF 0x00 "cannot be restarted", SLEEP 0x01 "can be restarted", QUIESCED 0x10, ON 0x20, INIT 0x220 | `R/rtkit.c:8-15`; `M/src/rtkit.c:80-84` |
| Linux quiesce = AP 0x10, IOP 0x10, reinit; wake = IOP 0x220 then boot; shutdown = AP 0x10, IOP 0x01 ("if OFF is used here the co-processor will not wake up again") | `R/rtkit.c:842-857,896-941` |
| m1n1 `rtkit_sleep` = AP 0x10, IOP 0x01, then `asc_cpu_stop` (clear bit 4 of 0x44) | `M/src/rtkit.c:919-984`; `M/src/asc.c:10-11,73-81` |
| KMD stop = AP 0x10 (wait ack), IOP 0x10 (wait ack), clear RUN; forced variant clears RUN only | `S/src/apple_agx_rtkit_session.c:361-413,44-56` |
| KMD start refuses unless StopPhase is Idle or Complete: a stop that timed out between the two acks leaves the session unstartable | `S/src/apple_agx_rtkit_session.c:105-108` |
| The image is loaded by iBoot; nothing in m1n1 or the KMD loads one | `fwk.str:59`; no loader found in `M/src`, `S/src` |

All of these stop sequences need a firmware that answers. For F2/F3 only the forced stop (CPU RUN
off) and a power-domain cycle are available; neither Asahi nor m1n1 exercises them on the GFX ASC.

### 3.4 m1n1 (0d3a75b3 checkout) ownership and gates

| Resource | Owner | Rule relevant to a restart | Source |
|---|---|---|---|
| ASC MMIO incl. CPU_CONTROL, SGX registers, pmgr | passthrough to the guest | the KMD writes CPU_CONTROL directly; m1n1 only reads bit 4 as a gate | `M/src/hv_autonomous_runtime.c:182-202,328`; `M/src/hv_agx_retained_platform.c:305-308` |
| Power domains | broker power window (QUERY 0 / ON 1 / OFF 2) | OFF = sgx then gfx-asc; refused (BUSY) while the retained root is Prepared or Active; a failed transition latches FAILED with no way out | `M/src/hv_agx_power_broker.c:40-84`; `M/src/hv_agx_retained_platform.c:562-565`; `M/src/hv_agx_power_mmio.c:657-661` |
| UAT: ttbat slot 0, kernel TTBR0 root, firmware TTBR1 prefix | broker retained root | PREPARE needs CPU stopped and no existing root, takes a new epoch; ACTIVATE needs CPU running, re-initialises GPUVA v5 with the new epoch; CLOSE needs CPU stopped **and** no live GPUVA process and no occupied slot 1..63; Tainted blocks PREPARE and CLOSE for good | `M/src/hv_agx_retained_platform.c:149-157,323-364,404-418`; `M/src/hv_agx_retained_root.c:246,682` |
| Per-process tables, slots 1..63, TLB flush | broker GPUVA v5 (LEASE/JOB_BEGIN/JOB_END/RELEASE per job) | every op is epoch-checked (STALE otherwise) | `S/include/apple_agx_gpuva_broker_v5.h:34-61`; `M/src/hv_agx_gpuva_v5.c:211` |
| DART | not used for the GPU | only DCP scanout | `M/src/hv_agx_power_mmio.c:326-337` |
| Display (DCP/IOMFB) | m1n1, broker scanout window 0x400 | independent of the GFX ASC | `M/src/hv_agx_power_mmio.c:638` |
| Guest reset | PSCI SYSTEM_RESET -> `reboot()` | nothing touches GPU/ASC | `M/src/hv_psci.c:1603-1609` |
| Missing ops | -- | no ASC stop/start, no pmgr reset of gfx-asc/sgx (`pmgr_reset` exists, used for DCP/ANS only), no forced lease release, no way to clear Tainted/FAILED | `M/src/pmgr.c:327`, `M/src/dcp.c:958`, `M/src/nvme.c:418` |

Consequence: with any GPU process alive (DWM always is), today's broker allows neither CLOSE nor power
OFF, so the only restart reachable without an m1n1 change is a **warm restart**: CPU stop, CPU start,
retained root left Active, power domains left on.

## 4. TRANSLATION

### 4.1 Decision

The adapter-wide reset is a firmware restart split across the two DDIs exactly as Microsoft splits them:
**ResetFromTimeout = stop and prove the GPU is silent; RestartFromTimeout = boot and re-publish.**
Engine reset stays what it is (bounded wait for the firmware's own end, RecoverChannel) and promotes by
returning an error, which is the documented path (W3).

Why not restart inside ResetEngine: a firmware restart ends every process's GPU state and both nodes'
in-flight work; that is the adapter-wide semantic (W7, W8), and W5 gives the window in which VidMm may
purge with the GPU off.

### 4.2 Callback -> AGX sequence

| Step | DDI | Action | Proof / bound | Source of the mechanism |
|---|---|---|---|---|
| T0 | ResetEngine | unchanged: wait <= 1 s for the firmware to end the job (F1 after resume, long job); RecoverChannel once; on timeout / RTKit crash return error and record class F1/F2/F3 in the reset receipt | existing | `scheduler_windows.c:637-680`; `backend_platform_windows.c:9345-9524` |
| T1 | ResetFromTimeout | pause scanout timeline; own the runtime (`Resetting`), worker idle; disarm the mailbox interrupt path and flush DPCs; skip the 1 s wait when T0 already timed out for the same fence | `KeFlushQueuedDpcs` precedent `K/src/scanout_windows.c:1650-1651` | W4 |
| T2 | ResetFromTimeout | stop the firmware: orderly (AP 0x10, IOP 0x10) only when class F1 and RTKit answers within a short deadline (100 ms, the Asahi halt bound), else **forced**: clear CPU_CONTROL bit 4 and read it back; session StopPhase forced to Complete | RUN bit reads 0 | `S/src/apple_agx_rtkit_session.c:44-56,361-413`; `A/gpu.rs:139` |
| T3 | ResetFromTimeout | prove the SGX block is silent. Warm path (stages 2-3): allowed only when the stalled job provably never reached the GPU (withheld-kick drill) or the firmware reported power-off. Cold path (stage 4): new broker op power-cycles sgx then gfx-asc with the CPU stopped | pmgr state off | `M/src/hv_agx_power_platform.c:16-38` (existing ops), new op section 4.4 |
| T4 | ResetFromTimeout | fail the work: active job, kicked successor, prebound successors, G4 pending all aborted with the engine; `CompletedFence = LastSubmittedFence` on both nodes; no per-fence interrupt | W7; existing code `scheduler_windows.c:977-990`, `:830-837` |
| T5 | ResetFromTimeout | per-process state: with the stop proof of T2/T3, started private scenes are **released, not quarantined**; retained slots released; no process is poisoned for the stall itself | replaces "No GPU stop/TLB proof" (`gpuva_g3_windows.c:1012-1034`) |
| T6 | ResetFromTimeout | tear down firmware-derived KMD state: backend runtime stop, provider destroy, initdata memory destroy; return STATUS_SUCCESS. From here to T7 no MMIO to ASC/SGX | sequence of the removed `AdmissionPlatformRuntimeReset` (`git show 590ae1dcc`, deleted lines 108-135 of the diff) |
| -- | (OS) | VidMm purge; page-table DDIs reach only the broker's table ops (CPU-side) | W5 |
| T7 | RestartFromTimeout | rebuild initdata graph + channels (`AppleAgxInitdataMemoryPrepareBroker`), restart image queue lifetime (first job per queue lifetime carries InitBM, local sequence 1), then the 3.2 boot sequence from step 2 (warm) or step 1 (cold) with the existing deadlines | `AppleAgxFirmwareStart` phases; EXP589 preregistration text for the sequence rule | `S/src/apple_agx_firmware.c:137-227` |
| T8 | RestartFromTimeout | provider/queues re-created, backend start, mailbox interrupt re-armed, `SchedulerFaulted` cleared, scanout timeline resumed, receipts published (`Wom1GpuReset` v5: class, stop kind, boot time) | `scheduler_windows.c:997-1004` extended |
| T9 | RestartFromTimeout failure | do **not** return failure for a firmware that does not boot: enter render-dead mode (display alive, every submission completes as faulted at once, no further TDR) and return success; 0x116 only if the display path itself is unsafe | W6 (failure = bugcheck); [HYP] that dxgkrnl tolerates a permanently faulting engine without TdrLimit, see U6 |

Time budget: no documented limit on the DDIs (W10). Target <= 300 ms stop + existing boot deadlines
(`J313_AGX_G2_ASC_BOOT_TIMEOUT_MS`, `..._ENDPOINT_TIMEOUT_MS`, `..._INITDATA_TIMEOUT_MS`).

### 4.3 Rebuild versus keep

| State | After restart | Why |
|---|---|---|
| RTKit session, endpoint map, crash latch | rebuilt | new firmware lifetime (`AppleAgxRtkitBootInitialize`, `apple_agx_rtkit_session.c:93-117`) |
| Handoff (magic, flush slots) | rebuilt | firmware waits for it after CPU ready (`A/mmu.rs:932-967`) |
| Initdata, RegionC (pending stamps, fault_control 0xb), firmware status (halt flags), channel rings, FW RX rings | rebuilt, zero ring pointers | firmware-visible, hold stale indices (`S/src/apple_agx_regionc.c:25-44`) |
| Event stamps / notifier flights, job-word sets | rebuilt (`slot<<24` base) | `A/event.rs:149-164` |
| Queues, GpuContext-equivalents, buffer managers, TVB state | re-created lazily per process on its next job (InitBM) | firmware-private objects died with the firmware; Asahi's DestroyContext needs a live firmware (`A/gpu.rs:1284-1298`) and is not sent |
| Private scenes | released to the pool (stop proof) | T5 |
| VM slots 1..63 (ttbat) | all unbound; leases dropped | slot table is hardware/broker state: warm path needs the KMD to RELEASE every lease (possible only if JOB_END is accepted for a job the firmware never ended -- U4); cold path: the new broker op drops them |
| Per-process page tables and backings registered with the broker | **kept** (variant K, section 4.4) | guest memory validated by the broker, independent of the firmware |
| Kernel TTBR0 root / retained root epoch | kept on the warm path; kept by the new op on the cold path | avoids the epoch change that makes every GPUVA record STALE (`M/src/hv_agx_gpuva_v5.c:211`) |
| Windows fence identity, scheduler nodes, VidMm segments, memory pool, allocations | kept | Windows-owned (EXP589 contract: fence identity is global) |
| Scanout runtime, DCP surfaces, VSync timer | kept, paused T1..T8 | m1n1 DCP path, no GFX ASC dependency |
| GPU id / core mask, config snapshot | kept | read once (`backend_platform_windows.c:3176-3190`) |

### 4.4 Ownership per layer

| Concern | m1n1 / broker | Mu / ACPI | KMD | UMD |
|---|---|---|---|---|
| Initialization | power domains, retained root, DCP | APPL0002 resources, interrupt lines | ASC CPU, RTKit, handoff, initdata, queues | device/owner creation |
| Runtime | slot leases, table validation, TLB | -- | submission, completion, paging translation | command building |
| Interrupts | fixed physical->guest routes (`M/src/hv_agx_g2.generated.h:46-55`) | line resources | ISR/DPC; disarm T1, re-arm T8; the level mailbox line must be dismissed in every state (KMD-IRQ-COMPLETION.md default-on plan) | -- |
| DMA / memory | UAT writes; **new: forced lease release + sgx/asc power cycle with the CPU stopped** | -- | owns the "GPU is silent" proof before any memory is released (W4) | -- |
| Power | pmgr enable/disable | -- | requests ON/OFF; never writes pmgr | -- |
| Recovery | new op `AGX_RR_RECOVER` (stage 4): preconditions CPU stopped; effects: unbind slots 1..63, drop leases and job state, keep processes/tables/backings and the root epoch, sgx off, gfx-asc off, gfx-asc on, sgx on, root back to "prepared, not active" so ACTIVATE runs after the next CPU start without re-initialising GPUVA; clears a power FAILED latch only through a complete successful cycle | none | T0-T9; class and stop-kind receipts; drills | sees device removed; re-creates the device (D3D12: DXGI_ERROR_DEVICE_HUNG/RESET; GL ICD: context loss) |

The m1n1 change is owned by the layer whose contract is violated (the broker assumes a firmware that
always returns its leases); it is not papered over in the KMD. Class C. The known-good recovery m1n1
stays unchanged.

### 4.5 Display during a render-side reset

The scanout path is m1n1's DCP (broker window 0x400) with a software VSync timeline; it has no
dependency on the GFX ASC. T1 pauses the timeline (`AdmissionScanoutSetTimelinePaused`,
`K/src/scanout_windows.c:1624-1653`), T8 resumes it; the last latched surface stays on the panel.
Presents that need a GPU blit fail between T1 and T8 with the rest of the engine. Classification:
DISPLAY_ONLY behaviour is unchanged by this design; no KMDOD assumption is imported.

## 5. WHAT IS STILL UNKNOWN

| # | Unknown | Why sources do not settle it | Smallest experiment |
|---|---|---|---|
| U1 | Does the iBoot-loaded GFX image boot a second time after quiesce + CPU RUN off + RUN on, with the retained root left Active and power on? | Asahi/m1n1 never do it; macOS symbols say a restart exists but not how; RTKit QUIESCED->INIT is supported for other coprocessors only | H1 (section 6): idle warm restart, healthy firmware |
| U2 | Same after a **forced** stop (no AP/IOP acks) | forced stop is used nowhere on a booted GFX firmware | H1b: same drill with the orderly stop skipped |
| U3 | Does a pmgr power cycle of sgx + gfx-asc with the CPU stopped mid-job leave the image bootable and the SGX clean? (`R/rtkit.c` says power state OFF "cannot be restarted"; that is the RTKit message, not the pmgr gate) | no source exercises it; macOS path not disassembled | H3 after the broker op exists; cheaper pre-check from the m1n1 proxy without Windows: python `agx_boot` then `pmgr` disable/enable then boot again |
| U4 | Does the broker accept JOB_END/RELEASE for a job the firmware never ended (warm path lease cleanup)? | bench m1n1 source not pinned in this worktree | read the bench m1n1 GPUVA v5 job state machine (offline, first task of stage 2) |
| U5 | What dxgkrnl replays after an adapter reset on a GpuMmu driver: VidPn/source address, root page tables, UpdatePageTable; whether contexts are destroyed by dxgkrnl | not in the fetched Microsoft pages or the header | H2 with DxgKrnl ETW (by GUID) around the reset + the KMD DDI call receipt |
| U6 | Does dxgkrnl accept an engine that faults every submission after a failed boot (render-dead mode) without TdrLimit -> 0x116? | undocumented | only if H1/H3 show boots can fail; drill "boot refused" in the restart escape |
| U7 | Firmware state in a real F3 stall (EXP2283-2286): halted? RTKit alive? RX pending? | no receipts exist for those boots | reset receipt v5 (stage 1) + reproducer (waiver on, diagnostics off, start gate disabled by a new opt-in switch) in H3 |
| U8 | macOS stop sequence (which power state it requests, whether it cycles the domain) | strings only | bounded RE of `AGXArmFirmware::stopFirmware/bootFirmware` and RTBuddy power state, one pass, behaviour notes only |

## 6. Implementation plan

| Stage | Class | Content | Files | Offline test: what real bug or invariant it catches |
|---|---|---|---|---|
| 1 | D | Reset receipt v5: failure class F1/F2/F3 from halted/halt_count, RTKit crash latch, per-pipe done/write, mailbox control words; flushed before a failing return. Correct the three "cannot be restarted" comments to "not tested (EXP589 never ran)". | `K/include/render_gpu_fault.h`, `K/src/scheduler_windows.c`, `K/src/backend_platform_windows.c`, `S/src/apple_agx_g13_queue_runtime.c` (comment), receipt decoder in `tools/` | decoder round-trip of v4 and v5 records: a v5 field read at a v4 offset silently misreports the class. No new behaviour test. |
| 2 | I | Restart mechanism, off by default: forced-stop primitive that leaves the session startable; `AppleAgxFirmwareRestartWarm` (rollback without UAT destroy / power off, then start from BootAsc); KMD `AdmissionPlatformRuntimeRestart` re-introducing the 590ae1dcc teardown/rebuild order; G3 release-with-stop-proof; escape `ADMISSION_FW_RESTART_DRILL` gated by `Wom1FwRestartDrillAllowed`, runs only with no job in flight. Resolve U4 from source first. | `S/src/apple_agx_rtkit_session.c`, `S/src/apple_agx_firmware.c`, `S/src/apple_agx_firmware_provider.c`, `K/src/backend_platform_windows.c`, `K/src/gpuva_g3_windows.c`, `K/src/callbacks.c`, `windows/icd/agx_tdr_drill.c`, shared tests | (a) a stop whose IOP ack times out leaves StopPhase mid-way and the next start returns InvalidState (`apple_agx_rtkit_session.c:105-108`) -- deterministic state machine; (b) after restart the first job of every queue lifetime must be InitBM/sequence 1 and no stamp, flight or ring index of the old lifetime may be reused (EXP589 invariant); (c) restart with a lease or started scene outstanding must not release its memory without the stop proof. |
| H1 | -- | first hardware stage, below | -- | -- |
| 3 | C | Wire T1-T8 into ResetFromTimeout/RestartFromTimeout for the warm path; withheld-kick stall drill (`Wom1FwStallDrillAllowed`: the drilled job is published but its pipe doorbell is not sent, depth-two kick refused while armed) so the firmware provably never starts it. Hardware H2: drill -> TDR -> restart -> desktop and a new D3D12 device work; ETW answers U5. | `K/src/scheduler_windows.c`, `K/src/backend_platform_windows.c`, `K/src/interrupt.c`, drill tool | reset replay harness (the tests 590ae1dcc removed): ResetFromTimeout returns success only after RUN reads 0 and all firmware-derived state is torn down; fences end at LastSubmitted on both nodes; a mailbox interrupt between T1 and T8 is dismissed and reports nothing (W3 0x119 0x10 class). |
| 4 | C | Cold path: broker `AGX_RR_RECOVER` (4.4) in m1n1 + KMD use; opt-in switch disabling the OV5 compute-start gate as the real-stall reproducer. Hardware H3: EXP2283-2286 configuration and the rc37 fault drill end in a recovered TDR. | `m1n1_windows/src/hv_agx_retained_platform.c`, `hv_agx_retained_root.c`, `hv_agx_power_broker.c`, `S/include/apple_agx_retained_root_abi.h`, `S/src/apple_agx_power.c`, KMD callers | m1n1 host tests of the broker state machine: RECOVER refused with the CPU running; after RECOVER no slot 1..63 is bound and every process record is still valid under the same epoch; a failed power step cannot leave the root Active. |
| 5 | C | Default on: remove the drill gates from the production path, render-dead mode (T9), repeated-TDR policy, perf gate (`post-exp-gate.sh`), suite 119. | KMD | state-machine test for T9 only if U6 requires the mode. |

Stage 1 has no hardware of its own (it rides on H1's package).

### First hardware stage H1 -- idle warm restart of a healthy firmware

- **WHY THIS HYPOTHESIS:** (1) every recovery design below this point depends on U1, and no experiment
  has ever restarted the GFX firmware in the same boot (EXP589 never ran; EXP1241/1278/1296 refused by
  design); (2) the KMD start/stop state machines and Linux RTKit quiesce -> INIT wake show the
  protocol supports it for other coprocessors (`R/rtkit.c:896-941`), and macOS has
  `stopFirmware`/`bootFirmware`; (3) testing it at idle with a healthy firmware removes the stalled-GPU
  and lease unknowns (U3, U4, U7), so a failure can only mean "this image does not boot twice this way".
- **Why not the stall drill first:** a stall drill followed by a restart changes two unknowns at once
  (does the stop proof hold; does the image reboot). H2 is that experiment and needs H1's answer to be
  interpretable.
- **WINDOWS CONTRACT:** none exercised; the restart runs from a drill escape with no job in flight, no
  TDR, no fence aborted. **AGX/ASAHI CONTRACT:** section 3.2 steps 2-10 replayed on the same image.
  **TRANSLATION:** stage 2's `AdmissionPlatformRuntimeRestart`, warm. **WHAT IS STILL UNKNOWN:** U1.
- **Single variable:** one orderly warm restart (AP 0x10, IOP 0x10, RUN off, RUN on, boot, initdata,
  queues) issued by the drill tool at an idle desktop; power domains and the retained root untouched.
- **Expected checkpoint:** drill receipt shows stop Complete, CPU running, HELLO/EPMAP, both endpoints,
  initdata sent, DC init acknowledged; boot time recorded; then `agx_d3d12` smoke renders correct
  pixels in a new process (first job InitBM/sequence 1) and DWM keeps composing; no TDR, no 0x116;
  three cycles in one boot.
- **Failure criterion:** no HELLO within `J313_AGX_G2_ASC_BOOT_TIMEOUT_MS`, handoff magic_fw never
  appears, any m1n1 exception, or renders wrong after the restart. A failure rejects the warm path and
  moves the design to the cold path (U3) before any TDR wiring.
- **Recovery path:** the display does not depend on the GFX ASC, so SSH stays up: collect receipts,
  then `recXXXXh/recover.sh` (AT_PROXY variant if the guest resets); recovery m1n1 and the GPU-visible
  inert profile unchanged. H1b (forced stop, U2) only after H1 passes, as its own run.

## 7. Sources inspected

KMD: `K/src/scheduler_windows.c`, `backend_platform_windows.c`, `lifecycle.c`, `gpuva_g3_windows.c`,
`scanout_windows.c`, `K/include/render_gpu_fault.h`, `render_admission.h`; `S/src/apple_agx_firmware.c`,
`apple_agx_rtkit_session.c`, `apple_agx_asc_transport.c`, `apple_agx_power.c`, `apple_agx_regionc.c`,
`apple_agx_g13_queue_runtime.c`. Asahi: `gpu.rs`, `queue/mod.rs`, `object.rs`, `rtkit.c` (direct);
`channel.rs`, `workqueue.rs`, `event.rs`, `mmu.rs`, `regs.rs`, `fw/*.rs`, `t8103.dtsi` (through a
research pass, key lines spot-checked). m1n1 0d3a75b3: `hv_agx_retained_platform.c`,
`hv_agx_power_broker.c`, `hv_agx_power_platform.c` (direct); the rest through a research pass. WDK
26100 `d3dkmddi.h` (direct for the TDR prototypes) and Microsoft Learn pages listed in section 2. Prior
notes: `G4-recovery.md`, `RC37-firmware-channel-recovery.md`, `T10-fw-timeout-reset.md`,
`T14-survivable-fault-recovery.md`, `Q13-gm-depth2-tdr.md`. Mu/ACPI was not re-read: no change to it is
proposed and it exposes no recovery service (`T10-fw-timeout-reset.md`, AGX/ASAHI CONTRACT). No external
code was copied.
