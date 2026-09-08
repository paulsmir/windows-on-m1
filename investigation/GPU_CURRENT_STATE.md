# GPU current state

## Active experiment and machine

EXP636: diagnose recurring CPU4 watchdog with Automatic Kernel Dump, reusing
identical signed EXP635 KMD/UMD/producer. No rebuild. Exact oem5.inf is now
staged from verified ordinary377/392 Code28/no-package baseline. Next controlled
shutdown and one full-owner natural bind, config/hash Preflight, live collector,
single16-frame producer. Evidence root: .local/experiments/EXP636-kernel-dump.

CrashDumpEnabled temporarily7, original3 backed up at
C:\Users\pavel\EXP636-crashcontrol-before.json and locally. Restore3 after
diagnostic evidence. AutoReboot/LogEvent/DumpFile unchanged; system-managed
pagefile1600MiB, free C:80GB. Script32c804bb4e38e3d8e2cfa93350cbab9dde566f10.
If reset: collect full MEMORY.DMP before cleanup; verify dump completeness and
matching635 symbols before attributing owner. Kernel dump is not guaranteed.

## Hardware proof retained

- EXP475/477/478: retained-root/context0/RTKit/firmware/native initdata,
  BackendRuntimeStart, arena/context/queues.
- EXP581/585/586/588: physical Windows-originated TA/3D and exact fences/output.
  Completion ingress remains polling.
- EXP591 photo: physical scanout. EXP631/632: two full2560x1600 outputs,
  exact latches and15s HOLD. EXP632 additionally proves explicit owned-primary
  replacement/retirement then all producer destruction statuses0, no reset.
- EXP634: FOUR exact full frames, fences256/257/258/260, sequences3/4/5/6,
  latches10/11/12/13, alternating same owners and offsetsfa0000/1f40000,
  PAs9bcf90000/9bdf30000, colorsff112233/ffcc8844, full4096000 pixels,
  hashes27592755b9c32325/ad1245c8bf762325. Both targeted output stages succeed
  at sampled IRQL0. No watchdog in634; not proof diagnostics cured0x101.
  Physical color-change confirmation remains pending/probable only.

## Latest causal fix and limits

EXP634 frame5 returned0xc00000e8 with device ACTIVE. Saved guard18 identifies
PrivateVirgin. Microsoft DXGK_DEVICEINFO promises zeroed private DMA data at
creation only. Commit653e4d85bdea4262d6e71cdcae9144979c0c1ebf removes the
per-Render zero precondition, retaining existing Initialize/Append and guards.
Commit9c1dbfd8f6c4041ea2cdabf30d81667fe87cd141 gates ALL producer cleanup
routes on existing retirement state;634 had incorrectly destroyed on error
with CleanupAllowed0. Executable shadow16 requests/four recycled blocks and
producer cleanup tests plus122 render tests pass. Pinned635 build/sign/
Universal/version pass. Corrected frame5 remains hardware unproven.

EXP635 passed two full frames then reset after frame3 Render0/queued1/ACTIVE.
No cleanup occurred. Dump090826-13171-01.dmp matches635, uptime126.872s,
0x101 CPU4 with truncated KiSwapContext. SHA256
9df2934e9cf6dc783e0a8f7de704cb19091895978c75ca549eef01314fabf532.
EXP633 had the same bucket. Neither small dump identifies the callback.
Two-slot v3 output correlation is complete for first two calls, explicit
overflow for third. Do not infer absent execution from missing third record.
Exact635 cleanup and ordinary recovery completed before636 staging.

## Exact artifacts / current discriminator

Actual package/query version stays30.0.635.0 in EXP636:
ZIP bd57291980575de8c029e38d5054c4eb3f900dd8462ffc7229c52eecbfc4e0e2;
SYS1056bc8d4cb2f8058ca1c619c0389489001fa21ff43df3eb122809d264299357;
producer7b44fabcf756c05d4aa2414e9158385300d743627b31476fc7d25898b2a40177.
All hashes/workflows in EXPERIMENTS.md and636 manifest. Collector records initial
same-build stale bytes as BASELINE_CORRELATION, excludes them from current-run
records. Preserve raw binary/decoded JSON and host producer output.
Only new diagnostic variable is CrashDumpEnabled3->7; hardware code unchanged.

## Constraints / remaining goal

Main process only, no agents. Preserve unrelated dirty tree and native-ANS.
No speculative AGX/PBE/UAT/RTKit/DCP/capability or timer changes.
Event129 remains telemetry without causal proof. One justified run then evidence,
exact cleanup/recovery. At final accelerated OpenGL/CS1.6 PASS retain driver.
Desktop/standard Present/OpenGL/CS1.6 and stable16-frame run remain unproven.
Historical expanded current state is archived under EXP633; consult only
relevant experiment-local evidence and ledger entries, not full archaeology.
