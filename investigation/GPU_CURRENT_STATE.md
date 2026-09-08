# GPU current state

Updated 2026-09-08T16:03Z. Main process only; no agents.

## Current machine / executable action

EXP638 monitor evidence and kernel dump are preserved. Exact test package and
stale service/SYS/UMD were removed in emergency; CrashDumpEnabled restored from
temporary7 to original3. Emergency shut down. Ordinary377/392 restored and
verified: APPL0002 Code28, packages0, no service/module,8CPU,NVMe2/USB5/
keyboard1. Live ordinary launcher session38469.

EXP639 exact30.0.639.0 is built/sign-verified, not staged. Next: transfer
EXP639.zip/Producer/workflow/collector; remote parser/hash gates; Stage from
clean ordinary; controlled shutdown; release EXP584 m1n1 + Mu406 full-owner
natural bind; Preflight; one unchanged16-frame producer. Save HOLD evidence,
then signal explicit owned-primary retirement and require clean destruction.

## Hardware proof retained

- EXP475/477/478: retained-root/context0/RTKit/firmware/native initdata,
  BackendRuntimeStart and arena/context/queues.
- EXP581/585/586/588: Windows-originated physical TA/3D, exact hardware output,
  completion and Windows fences. Completion ingress remains polling.
- EXP591: physical panel scanout photo. EXP631/632: two full2560x1600 outputs,
  exact latches and15s HOLD. EXP632 proves owned-primary retirement and all
  producer destroy statuses0 with no reset.
- EXP634: four exact full frames, monotonic fences/sequences and latches,
  alternating two exact owners/offsets/PAs and full4096000 pixels.
- EXP636:13 complete frames; frame14 physical TA/D3/fence270 and all pixel
  comparisons completed, hash interrupted by0x101. Kernel dump
  A36F8D0E3B04C53E88548067879B35F1285B527DCAB7C025E6D786EBECB21ACB
  resolves CPU4 at AdmissionOutputWorker byte-hash load, IRQL0/SPSR.I clear.
- EXP637: same verifier on user noncached16MiB, fixed CPU4, sixteen9.4s scans
  PASS; clean Code28/8CPU host and no fresh events. GPU work is not required
  for the computation, but mapping/execution context differences remain.
- EXP638: existing non-verbose monitor passed8 full frames; frame9 output then
  same0x101. Required pages in kernel dump
  893E4DDDFD2D4340EEC3F3A42D82BD021CBC36AA6494F1564758854EDFE02A4E
  again resolve CPU4 in the same byte loop, IRQL0/SPSR.I clear. Pre-failure
  aggregate telemetry advances FIQ/tick and balanced NVMe/xHCI IAR/EOI; no
  exact per-CPU failure sample. Monitor did not fix or identify timer policy.

Physical repeated color-change confirmation remains probable/user-dependent,
not instrumented proof. Machine latches and content identity are exact.

## Current causal correction — EXP639

Microsoft limits IoQueueWorkItem to short work because it uses a shared finite
pool; long processing belongs on a driver-created thread. Both kernel dumps
place the approximately9-second verification on ExpWorkerThread, while the
CPU-only normal-thread control passes. Commit
b17e53a5fe7ae2b52481117080be7c559e79a5b4 moves only the long output
verification/presentation to one per-runtime PsCreateSystemThread at
PASSIVE_LEVEL. Existing AGX work item, output functions and ownership are
unchanged.

Completion schedules the same output generation and signals OutputWake. Stop
rejects new schedules, drains an active generation, marks thread exited,
signals OutputExited, self-terminates, then destroy closes its kernel handle.
Executable state test was RED before start/stop API; now covers unstarted and
post-stop rejection, drain-before-exit and exact exit. Render suite122 GREEN.

Pinned WDK/SDK26100 KMD/UMD/producer, analysis, Universal, Inf2Cat/TestSign and
version gates PASS; inherited C28251 only. Exact hashes:
overlay6b7975147d38cc8aacbf783e34425860491ddba0875a53aa5297b2dc22b9fb90;
ZIP363ade63bc8a50719a278058a3c3d70c24756de1c304d9ea334b7ddfabc0d3bd;
SYS91c7814a0f4358dc179f242b4cc775549822f92130ddef71421637dc0dc79cb0;
INF889eaf0d44e457796e557366741c14b3e71386f1435472c65e5468f941ac00d1;
CAT792b1debf8f4b1f12113a3c5f41ef291ac6125ec84de057b8bfcd6abc6848481;
UMD368a031daa2ff9629e46b5937ef8c3f96225fbb69a3b3fea1b9abc129d35b660;
producer9bfabd6ff266f767ab916e0bd0fa80cc8ac452a21566f52b10f63ac51c1f2039.

## Constraints / final goal

No speculative timer/vGIC, AGX/PBE/UAT/RTKit/DCP/capability changes. Preserve
unrelated dirty tree and native-ANS. Event129 remains telemetry without causal
proof. EXP639 PASS requires16 outputs/latches, monotonic fences/sequences,
HOLD, explicit retirement and clean teardown; absence of0x101 alone is not PASS.
Failed package cleanup before next run. Final mission remains standard Windows
Present, accelerated OpenGL and CS1.6 through real AGX; final good package stays
installed. Historical details remain in EXPERIMENTS.md and experiment evidence.
