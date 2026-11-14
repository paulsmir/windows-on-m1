WP A2+A3 design note: GPU hang / firmware crash ends in device-removed for the app, not 0x116

Read-only work. No files were written and the scratch directory $HOME/public_windows/.local/night-20261110/wp-a2a3/ is empty. Paths: K = drivers/apple-agx/render-admission, S = drivers/apple-agx/shared, A = asahi-linux-asahi drivers/gpu/drm/asahi, M = .local/m1n1-wt-s2-broker-v3 (63c13618).

1. CURRENT STATE (arch-integrate 4d0d4ca23)

Where the audit is right:
- K/src/scheduler_windows.c:963-1006 (ResetFromTimeout) pauses the scanout timeline. It then runs the same engine reset for node 0 and node 1, and on success sets CompletedFence = LastSubmittedFence.
- The engine reset (:687-871) only waits up to 1000 ms (render_admission.h:2512) for the firmware to retire the job. Any failure becomes 0xC0000483 (:731-732).
- RestartFromTimeout (:1008-1015) only resumes the timeline.
- The crash latch (S/src/apple_agx_rtkit_session.c:146,292) is only written into receipts (K/src/backend_platform_windows.c:9995, 10296, 10409).

Corrections to the audit:
- **"Any GPU hang is a 0x116" is wrong.** A render hang that the firmware detects already ends per application. In EXP1277 the firmware's Timeout event led to a resume, then DXGK_INTERRUPT_DMA_PAGE_FAULTED for that fence (backend_platform_windows.c:5680-5726). The app got DXGI_ERROR_DEVICE_HUNG; DWM kept the same PID; no TDR, no bugcheck.
- The 0x116 cases fall into three classes:
  - **F1:** the firmware halts and is resumed, but the job never retires (CDM fault: EXP1964/1965/2214).
  - **F2:** the RTKit firmware crashes (EXP2100). Today nothing is reported to Windows: the worker exits with PollFailure (:2274) and the 2 s TDR then fails at the RTKit drain.
  - **F3:** a compute loop the firmware never detects (EXP2324/EXP2340: compute done 362 / write 363 for 3 s, no event).
- The tree already contains:
  - stage 2a/2b warm restart, reachable only from a drill: AdmissionPlatformRuntimeRestart :10181, RestartOwned :9970, gated by Wom1FwRestartDrillAllowed :9681;
  - the H0 halt observation (:10734, called at scheduler_windows.c:972).
- The tree does not contain TDR-3H-R1/R1B (b16bf46c1, 00f4386ae on arch/tdr-3h-r1). Both are offline only, not MSVC-built.
- EXP2340 confirmed that firmware 13.5 acknowledges a host halt (0x85<<48|N on ep 0x21, halt_count N+1 after 2 ms).

2. WINDOWS CONTRACT (driver model: WDDM 3.0/3.2, software-scheduled, SupportPerEngineTDR = TRUE at lifecycle.c:837)

Read in headers (WDK 26100):
- ResetEngine: d3dkmddi.h:8243-8260, PASSIVE level, returns LastAbortedFenceId.
- ResetFromTimeout / RestartFromTimeout: :8334-8350.
- QueryEngineStatus Responsive bit: :8209-8220.
- DXGK_INTERRUPT_TYPE: :687-733. DMA_FAULTED is 4; DMA_PAGE_FAULTED is 9; GPU_ENGINE_TIMEOUT is 16 (WDDM 2.4, sits in the hardware-scheduling group, so it does not apply to us).
- DmaPageFaulted payload: :1063-1080.
- Page-fault flags (winsdk shared/d3dkmdt.h:2309-2327): FENCE_INVALID 0x2, ADAPTER_RESET_REQUIRED 0x4, ENGINE_RESET_REQUIRED 0x8, FATAL_HARDWARE_ERROR 0x10 (bugcheck, never used).
- Device states: d3dkmthk.h:4353-4364.

From Microsoft documentation (pages fetched 2026-11-09, cited in TDR-FIRMWARE-RESTART-RECOVERY.md §2, W1-W11; not re-fetched today):
- TDR fires after TdrDelay of 2 s. TdrDdiDelay is 5 s. More than 5 TDRs in 60 s is a 0x116 whatever we do.
- With per-engine TDR, dxgkrnl calls ResetEngine first. On return the node's queue must be empty, LastAbortedFenceId must be between completed and submitted, and no interrupt may come from the old work (otherwise 0x119). A failure escalates to the adapter-wide ResetFromTimeout.
- ResetFromTimeout must return with the GPU neither reading nor writing memory. Only success survives.
- Between Reset and Restart, VidMm purges and the driver must not touch the GPU. A failed RestartFromTimeout is a bugcheck.
- Adapter reset: last completed fence = last submitted fence on every node.
- Per-engine reset puts only the culprit's device in error. Adapter reset gives the culprit DEVICE_HUNG and every other device DEVICE_RESET.
- Recalled from documentation, needs re-verifying before step 4: a DMA_PAGE_FAULTED notification with ADAPTER_RESET_REQUIRED or ENGINE_RESET_REQUIRED makes the OS start that reset at once, without waiting for TdrDelay.

3. AGX/ASAHI CONTRACT

Asahi:
- It never restarts the coprocessor. A crash latches `crashed`, fails all jobs with NoDevice and the latch is never cleared (A/gpu.rs:378-396). object.rs:9 says the machine needs a reboot.
- Its job timeout handler is a FIXME (A/queue/mod.rs:403-410).
- On a firmware Timeout or Fault event it marks the jobs, waits for halted, then writes halted = 0, resume = 1 (A/gpu.rs:1059-1089, 1422-1457).
- ChannelError is answered with RecoverChannel (:1459-1542).
- Compute firmware limits: cl_context_switch_timeout_ms 40 and cl_kill_timeout_ms 50 (initdata.rs:796-798); progress_check_interval_cl 250 (:784).

macOS 15 (U10, static read, not 13.5):
- The host halts the firmware, error-completes the stamps, rebuilds channel state and resumes in phases 2/3/0.
- It never stops the ASC and never power-cycles SGX for a hang. It boots the firmware again only at system wake.

m1n1 (M):
- pmgr on/off for /arm-io/gfx-asc and /arm-io/sgx exists (hv_agx_power_platform.c:19-37).
- pmgr_reset is used only for DCP and ANS (dcp.c:1019, nvme.c:390), never for the GPU.
- The power broker refuses during a transition (hv_agx_power_broker.c:94-96). CLOSE needs the CPU stopped and GPUVA idle (hv_agx_retained_platform.c:442-444).
- So nothing can power-cycle the GPU while DWM lives.
- DCP is a separate ASC with its own pmgr device; scanout does not depend on the GFX ASC.

4. TRANSLATION (design)

Decision: recover at engine level first by halting and resuming the firmware. Restart the coprocessor only for a dead firmware.

Recovery ladder:

| Class | Who detects | Action | Windows signal |
|---|---|---|---|
| Firmware-detected render hang or fault | firmware | resume (existing) | DMA_PAGE_FAULTED with the fence, no flags (proven in EXP1277) |
| F3 / F1 | dxgkrnl TDR, then ResetEngine | host halt, acknowledgement wait, queue-runtime RecoveryPending plus resume (R1B entry), then the existing quiesce. Success means the culprit fence is LastAbortedFenceId, queued packets are released for VidSch to resubmit (scheduler_windows.c:821-841), and only the culprit device is in error | ResetEngine success |
| Halt not acknowledged, or the entry never retires | escalate | ResetFromTimeout runs the restart (T1-T8 of the design note §4.2/§10.2), force-stops when needed. Fences: CompletedFence = LastSubmitted on both nodes (existing) | adapter reset (DEVICE_HUNG / DEVICE_RESET) |
| F2 crash latch | worker poll | report DMA_PAGE_FAULTED with ADAPTER_RESET_REQUIRED and FENCE_INVALID once, freeze dispatch; ResetFromTimeout then does a forced stop plus the cold path; RestartFromTimeout does a warm boot | adapter reset without the 2 s wait |
| Restart fails | -- | render-dead mode: display alive, every submission faults immediately; Restart returns success | -- |

Why per-engine reset first: it is exactly the macOS mechanism (firmware kept alive). It is the only way bystanders (DWM) see nothing at all. A firmware restart ends every process's GPU state, which is an adapter-level meaning.

R1 currently sits at ResetFromTimeout entry. Keep it there for the first boot: the hardware question (does the firmware drop the entry) is the same in either DDI. Move it into ResetEngine node 0 afterwards.

Ownership by layer:
- **m1n1:** power domains, the retained root, the DCP scanout. Owns the new cold op AGX_RR_RECOVER: CPU stopped, broker lock held, unmap the ASC/SGX stage-2 range, sgx off, gfx-asc off, gfx-asc on, sgx on, slot 0 and the TTBR1 prefix unchanged, a way out of the power FAILED latch.
- **Mu/ACPI:** none.
- **KMD:** detection, class, the halt, stop proof, fences, restart, interrupt mask/re-arm, receipts.
- **UMD:** already turns a faulted context into DEVICE_HUNG (mesa/winsys/agx_d3d12_windows.cpp:321-327, 3602) and calls pfnSetErrorCb (d3d12/agx_d3d12_ddi.cpp:7547).
- **DMA:** no memory is released before the stop proof: RUN reads 0, plus a power cycle on the cold path.

Display during recovery: the timeline is paused between T1 and T8 and the DCP keeps the last latched surface. Any MPO slot 1 must be retired before Restart returns (S2-FULL F-9).

Offline tests:
- R1B tests exist.
- New: ResetEngine with a host halt either succeeds (fence aborted, queue empty) or escalates without side effects.
- An F2 latch produces exactly one ADAPTER_RESET_REQUIRED notification, and no completion interrupt after it.
- m1n1 host test for AGX_RR_RECOVER: refused while the CPU is running; slots and epoch survive; a failed step never leaves the root Active.
- Render-dead mode: TDR count bounded.

5. WHAT IS STILL UNKNOWN

| # | Unknown | Smallest checkpoint |
|---|---|---|
| U-A | After a host halt plus one-step resume on 13.5, does the firmware drop the looping CDM entry (done == write), report an event, run it again, or stay silent? | R1+R1B boot, k4128 arm |
| U-B | Why a CDM loop raises no firmware event (CL progress 250 ms). Hypothesis: the kill fires only on a context-switch request. | same boot, receipt; no extra run |
| U1/U2 | Does the image boot again after an orderly stop, and after a forced stop? | H1 / H1b idle drill; SSH stays up; recover.sh |
| U3 | Is the image bootable after a pmgr cycle of gfx-asc and sgx? | Proxy-only experiment at "Running proxy", no Windows: boot, cycle, boot. Recovery is a proxy reboot. |
| U5 | What dxgkrnl replays after an adapter reset: VidPn, page tables, contexts | DxgKrnl ETW in the first adapter-reset drill |
| U6 | Does dxgkrnl tolerate render-dead mode without hitting the TDR limit? | only if boots can fail |
| U-C | Is the primary surface stable during the VidMm purge? | Photo during the drill |

6. STEPS

1. MSVC-build and merge R1+R1B; boot: k4128 arm with Wom1TdrHaltRecover=1. Small. This decides U-A.
2. Move the halt into ResetEngine node 0, with escalation on failure; boot: k4128 three times in one boot, check DWM and a second D3D12 app. Medium.
3. F2 reporting (ADAPTER_RESET_REQUIRED) plus a software crash-latch drill switch; boot: drill, then check the ETW order. Medium.
4. H1, then H1b warm restart (code exists); two boots. Small.
5. Wire the adapter restart into ResetFromTimeout/RestartFromTimeout with a withheld-kick stall drill (H2). Large.
6. U3 proxy test, then AGX_RR_RECOVER in m1n1 plus KMD use; boot: real crash drill (EXP2100 depth-two prelude config, gate opened by a drill switch). Large.
7. Render-dead mode, default-on, remove drill gates from production (F2); acceptance: three drills in one boot plus the perf gate. Medium.

7. RISKS

- **SError if the guest touches SGX/ASC MMIO while powered off.** m1n1 unmaps that stage-2 range for the duration of the cycle; the KMD issues no MMIO between T6 and T7.
- **GPU DMA into freed memory.** Nothing is released before RUN reads 0 or the power cycle completes; uncertain graphs stay quarantined.
- **Broker Tainted / power FAILED latch.** Only a reboot clears it today. Every new op checks its post-conditions; the known-good recovery m1n1 stays unchanged.
- **TDR limit → 0x116.** Drills stay at most three per boot.
- **Disk/data:** ANS is independent of the GPU; a dump is written as today. Nothing here touches iBoot or NVRAM, so the machine always stays bootable.
