# EXP919: early kernel failure; compositor experiment did not execute

The intended DWM reinitialization is **INCONCLUSIVE / NOT RUN**. Windows reset
before the first successful SSH sample; no invocation receipt exists. The
reinitialization runner was stopped without sending its command. Do not attribute
this crash to DWM termination, and do not repeat the unchanged boot as that test.

Exact package30.0.918.0/source d5c68559, same firmware as918. Original UART records
DCP sequence2/swap9 latch followed by NVMe dump-path initialization and Windows
PSCI reset. Host six-log gate:
`b4c7acca3f4a215ee49a0468cb0d3b74ff8ef887bb39adf8b69fa20d680faaa8`.
No original ETL or Code0 receipt was recovered before reset.

## Dump evidence

Kernel dump596888485 bytes, SHA
`f582779824a83c3ed2a160cd06f6973593ce5ae4fd01795430112a1ed84f04d9`,
independently verified on host and builder. CDB loaded the exact918 private PDB.
CPU5/System, uptime38.769s, `0x119/2/80000011`, on the dxgmms2 paging submit path.
Args atffff8c8bcab0da50: normalPagingflags1, fence735, DMA4100120/100bytes,
private780bytes (16 paging records), no resubmission.

The typed pointer chain is hContextffffb708f8e4c1c0 -> objectDevicefcf76490 ->
adapterfcd02000 (ObjectAdapter offset0). Scheduler offset7cb50:

- Completed731, LastSubmitted732, queue0, active0;
- PreemptedFenceQueue[0]=732/count1;
- preemptionIdle, pending0, cutoff0;
- CPUqueue0, dispatched0, PagingPending0, PagingStopping0;
- SchedulerInitialized1, **SchedulerFaulted1**;
- RenderPacketEmpty, BackendImageReady/no bound or job fence;
- backendReady, WorkScheduled0/WorkersActive0/Stopping0/Resetting0.

Thus the immediate rejection is the already-latched fault, not current queue
capacity or the previously repaired NotificationClaimed admission gate. The
first setter of the fault was not recorded; the dump cannot identify it uniquely.
Backend TerminalResultInvalidState with TerminalPending0 is the normal
ClearPending default in source, not evidence of a GPU error.

The reader reports96% completion and some absent pages; its last dump-stack line
says completed successfully. Only readable nonpaged fields above support these
conclusions. MMIO/firmware views absent from the bitmap were not interpreted.
Initial CDB command-file semicolons were consumed as part of .sympath; that log
was retained and analysis rerun using separate command lines.

## Reproduced source race and correction

PreemptCommand decides notifyNow under locks, releases them, then calls TryNotify.
Another legitimate WorkerFinished/DPC caller can notify and commit first. The
old TryNotify returnsFALSE for the lost claim, and the outer PreemptCommand
incorrectly sets SchedulerFaulted despite successful delivery. Actual production
Preempt/Try/Worker code with the real shared scheduler reproduced exactly
Idle/fault1/one notification/completed731/last732 before the fix.

Correction a5c2c1af treats already completed/owned/waiting notification attempts
as benign under lock, while preserving genuine interface/synchronization/commit
failures and invalid-phase failure. Existing worker-retirement deferral and
postcommit dispatch wake remain. Three actual ASan/UBSan replays pass.
Diagnostic fault tags record first file/line in the existing nonzero fault word,
with unchanged zero/nonzero behavior and normalized public Boolean receipts.
This source race is confirmed and matches the dump, but is not presented as the
uniquely proven first setter in this hardware run. EXP920 must verify the corrected
mapping; tags make another failure distinguishable. No DWM reinitialization in920.

## Evidence and recovery

Root `.local/experiments/EXP919-dwm-reinitialize/`:
`hardware-evidence-direct/` contains eight files plus inventory, host gate
`e0048b8094acf5923afc54f9fc81b83ae4d1402f01919ef17aa169a6282793ea`.
The ETL in it is recovery-only. CDB analysis is in kernel-analysis/context/objects/
device/scheduler/queue-state/backend-state/runtime text/log files. Original actual
race RED and final tagged GREEN logs are preserved. An initial extra negative
test incorrectly assumed opaquePreemptionFence0 was invalid; its correction and
overwritten intermediate GREEN log are explicitly recorded in the transcript note.

Ordinary377/392 recovery reached Code43/Stage2/Stopped/CPU8 at boot21:03:43.181521Z.
All host gates preceded diagnostic cleanup. The mixed direct inventory was aliased
for the existing hash-only cleanup guard, explicitly not an original ETL receipt.
Only the exact Windows MEMORY.DMP duplicate was removed after host+builder copies
were verified, restoring5025148928 free bytes/shadows0. Exact918/oem5 removal at
21:54:15 scheduled an ordinary restart. No hidden boot or forced signal in919.
Final clean recovery receipts are appended below when complete.

Final ordinary377/392 boot `2026-10-01T21:59:38.5512320Z` passed at22:01:54
and22:03:02: oneAPPL0002 Code28, no package/modules/service/signer/arms/diagnostics,
CPU8/disks2/USB5, SSH, RDPservice, autologon1, freeC5034749952/shadows0.
Cleanup three-file host gate
`13d4be526d0dcf33f52ef2dcc2cd7f1187e2e284f292f2b67f68ab31005219fc`.
EXP919 is closed; EXP920 is built/sealed but not staged at closure.
