# G4 recovery: why every TDR is a 0x116 today (2026-10-13)

Sources: EXP1241/EXP1242 kernel dumps (cdb, private PDBs), Asahi `drivers/gpu/drm/asahi/gpu.rs`
(`handle_fault`, `handle_timeout`, `mark_pending_events`, `recover`, RTKit `crashed`), Microsoft
"TDR in Windows 8 and later", this KMD (`scheduler_windows.c` engine reset,
`backend_platform_windows.c` `AdmissionPlatformRuntimeReset`/`AdmissionTransportRecover`,
`apple_agx_g13_queue_runtime.c` deadline).

## Observed

- EXP1242 dump: `FirmwareRecoveries` 178, `RecoveredFaults` 0xb2, FW status `halt_count` 0xb2,
  `halted` 0, `resume` 0, last recovered event kind 4 (Timeout) at fence 0x4e97. The firmware
  detected and reported 178 job timeouts (system-memory BOs on unpublished UAT leaves) and the
  EXP1075 Asahi-style path resumed it each time.
- Fence 0x4e99 then hung without a firmware Timeout event: the KMD deadline (500 ms,
  `ADMISSION_PLATFORM_QUEUE_TIMEOUT_MS`) expired, `AppleAgxG13QueueRuntimeReset` could not quiesce
  (ResetFailed), the worker set `SchedulerFaulted` (backend_platform_windows.c:3819), the fence
  never completed and dxgkrnl's TDR called `DxgkDdiResetEngine` / `DxgkDdiResetFromTimeout`.
- Engine reset with an active packet calls `AdmissionPlatformRuntimeReset`, which stops and
  restarts the firmware. With retained firmware (`RetainedPrepared` 1 in the EXP1241 dump) this
  path returns STATUS_DEVICE_HARDWARE_ERROR by design, so every TDR with an active packet is a
  bugcheck 0x116 (P3 0xC0000483).

## Contracts

WINDOWS CONTRACT: engine-level TDR is mandatory for full-graphics drivers (SupportPerEngineTDR,
QueryDependentEngineGroup, QueryEngineStatus, ResetEngine; ResetFromTimeout/RestartFromTimeout
still required). ResetEngine must return LastAbortedFenceId within [last completed, last
submitted]; a failure promotes to adapter reset, and a failed adapter reset is 0x116.

AGX/ASAHI CONTRACT: the firmware reports Fault/Timeout events and halts itself; the driver fails
the pending work (`mark_error`) and resumes the firmware (`halted = 0`, `resume = 1`). Channel
errors are recovered with `RecoverChannel`. A firmware crash is terminal; Asahi never restarts the
firmware and has no recovery for a hang the firmware does not detect.

TRANSLATION:
- Firmware Timeout/Fault event -> resume (done, EXP1075) and complete the fence as faulted.
- KMD deadline expiry -> not a failure by itself: keep polling events until a hard limit below the
  Windows TDR delay, so the firmware's own detection wins.
- `DxgkDdiResetEngine` -> firmware-preserving reset: if the firmware halted, resume it; drop the
  active job, reset the KMD queue state to the firmware's done pointers, report the active fence
  as LastAbortedFenceId. Never restart retained firmware. Fail (adapter reset, then 0x116) only
  when the firmware neither completes nor halts.
- Prevention: refuse (device error, no GPU access) a submission whose referenced BO ranges are not
  published, instead of letting the GPU reach an invalid UAT leaf.

WHAT IS STILL UNKNOWN: the firmware's own job-timeout latency (how long after a stall it raises
Timeout), and whether a job the firmware never times out can be dropped at all (ring reset while
the firmware still holds it). Hardware checkpoint: a deterministic fault-injection submission
(a render that reads an unpublished VA) with receipts for event latency and halt state.
