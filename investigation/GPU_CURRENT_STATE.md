# GPU current state

Updated 2026-09-08T16:12Z. Main process only; no agents.

## Current machine / next action

EXP639 failed package is removed exactly; emergency shutdown and ordinary377/392
restore completed. Ordinary is verified Code28, packages0, no AppleAgx service
or module,8CPU,NVMe2/USB5/keyboard1. CrashDumpEnabled is original3.

EXP640 exact30.0.640.0 is built/sign-verified, not staged. Transfer ZIP,
producer, workflow and collector; remote parser/hash gates; Stage from clean
ordinary; controlled shutdown; release EXP584 m1n1 + Mu406 full-owner natural
bind; exact Preflight; one unchanged16-frame producer. Save complete frame/HOLD
evidence before retirement signal. Require owned-primary retirement and all
destroy statuses0. Failed package cleanup and ordinary restore after verdict.

## Hardware proof retained

- EXP475/477/478: retained-root/context0/RTKit/firmware/native initdata,
  BackendRuntimeStart and arena/context/queues.
- EXP581/585/586/588: Windows-originated physical TA/3D, exact output,
  completion and Windows fences. Completion ingress remains polling.
- EXP591 physical scanout photo. EXP631/632 two full2560x1600 outputs, exact
  latches and15s HOLD. EXP632 proves owned-primary retirement and teardown.
- EXP634 four exact full frames/latches with alternating two exact owners.
- EXP636 thirteen complete frames; frame14 TA/D3/fence270 and all pixel
  comparisons completed, hash interrupted by0x101. Kernel dump
  A36F8D0E3B04C53E88548067879B35F1285B527DCAB7C025E6D786EBECB21ACB
  resolves CPU4 inside output byte hash, IRQL0/SPSR.I clear.
- EXP637 same verifier on user noncached16MiB fixed CPU4: sixteen9.4s scans
  PASS; Code28/8CPU/no fresh events. It does not reproduce the watchdog.
- EXP638 monitor: eight full frames; frame9 output then same0x101. Kernel dump
  893E4DDDFD2D4340EEC3F3A42D82BD021CBC36AA6494F1564758854EDFE02A4E
  again resolves the byte loop at IRQL0/SPSR.I clear. Aggregate EL2 telemetry
  advances FIQ/tick and balanced NVMe/xHCI IAR/EOI before failure; no exact
  per-CPU failure snapshot. Monitor did not establish a timer defect.
- EXP639 moved output off ExpWorkerThread to a driver-owned PASSIVE thread.
  Three complete frames passed, frame4 started, then0x101 CPU4. This rejects
  shared system-worker pool as the watchdog root. Small dump is truncated.
  Microsoft worker-context correction remains implemented.

Physical repeated color-change confirmation remains probable/user reported;
machine content/latch correlation is exact.

## Current causal candidate — EXP640

Production output lives in a DXGK contiguous physical memory object created
DXGK_MEMORY_CACHING_TYPE_NON_CACHED and mapped by DxgkCbMapPhysicalMemory.
Both full pixel and FNV passes read this mapping continuously; together they
take about9 seconds. User control used different pagefile-backed noncached
memory. After EXP639, the uninterrupted mapped-device read is the nearest
remaining difference.

Commit6a7e3aa4c976c7cc66077aa2099a77f27b18208f preserves full pixel, poison,
guard and FNV proof but limits each uninterrupted pass range to256KiB.
Between ranges, the dedicated PASSIVE thread performs a1ms nonalertable delay
and rejects stop/reset. The helper builds a local receipt and publishes output
fields only after complete success. No scan under spinlock/completion/DPC.
AGX, firmware, queues, fences, DCP and allocation/display ownership unchanged.
The dedicated thread commit b17e53a5fe7ae2b52481117080be7c559e79a5b4 stays.

Progress helper test was RED before API and is GREEN for literal boundary count,
hand-derived pixels/hash and atomic abort. Full render suite122 GREEN.
Pinned WDK/SDK26100 KMD/UMD/producer, analysis, Universal, Inf2Cat/TestSign and
version gates PASS; inherited C28251 only. Exact hashes:
overlay f2a29bac9b23f76be16ec41306d90560deec55fbae314abb3fe48ccb559afa58;
ZIP 2de1a31d762773134824b40e48554d093b935d2ece6dc4f9095f28444f96113f;
SYS 2f2cbcf88ecefb5eed4af365a50d84eb50b9ab9a04f0311505d8100ccb33ac67;
INF ba16ad7e204f1269cb0a4503065ca1606e80feb6d10a37441bb7985eb701a13b;
CAT cc0a6a3dbf6a432122de76c25bae82f68e643248fa8a51e2788692ad3a4b2f82;
UMD cc7485f12480773541eee6f83a21c0af0cacb5b7519d2f5c9b25fed7aade6638;
producer ac0b236e9d8571fe0a836c76a696eed04b383403842f58ccc2721227d8929af5.

## Constraints / final goal

No speculative timer/vGIC, AGX/PBE/UAT/RTKit/DCP/capability change. Preserve
unrelated dirty tree and native-ANS. Event129 remains telemetry without causal
proof. EXP640 PASS requires16 outputs/latches, monotonic fences/sequences,
HOLD, retirement and clean teardown; absence of0x101 alone is insufficient.
If it fails, do not stack another timing change without new evidence.
Final mission remains standard Windows Present, accelerated OpenGL and CS1.6
through real AGX; leave final known-good package installed. Detailed history
and hashes remain in EXPERIMENTS.md and experiment-local evidence.
