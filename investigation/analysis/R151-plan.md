# R151 queue-lifetime correction plan

Goal: identify EXP865's first lost TA progress, fix a deterministic lifetime defect offline, and propose one-variable EXP866.
Scope: NEXT_TASK_R151.md authorizes verified fixes and execution on integration/ad04-windows-compiler. No package or Air. Existing linked worktree retained; unrelated dirty submodules excluded.

Sources inspected in order: saved EXP865 kernel/provider/scene output and EXP864 R150 evidence; Asahi 77cb8f24 event.rs, workqueue.rs and queue/render.rs; current m1n1 agx/render.py and hv_agx_retained_platform.c; current Mu J313AppleAgxAbiAdmission.asl.inc; official WDDM completion/reset documentation (separate R151-prequeue-contract assessment). Prior launch is unchanged EXP865/R143 and ordinary Code28 recovery. No live state acquisition in this offline task.

Observed contract: first private149 succeeds; current220 uses persistent TA/D3 queues with ring done3/4 but Image.Sequence1 and first-job stamps again. G4 Bind and Release call Prepare, zeroing the image sequence independently of queue lifetime. KMD owns image/stamp identity, DMA admission, polling and WDDM retirement; broker owns process leases/UAT grant and power, firmware owns execution and stamp/event publication, Mu owns surviving resource exposure. Asahi and m1n1 increment stamps across submissions; Windows must not report completion until the actual TA/D3 pair completes. No external code copied.

One implementation change: preserve Sequence across G4 template refresh at Bind and Release. Initial Prepare and explicit RestartQueueLifetime retain reset behavior. Template bytes and private scene storage remain reconstructed as before; no reset/caps/firmware/timing changes.

- [x] Extend real render_backend_image_test G4 path through two jobs. Assert release and rebind retain1; second stage emits stamps+0x200, prior+0x100, matching barrier/finalize/event count; reject stale stamp using real completion predicate. Include mixed legacy/G4 and explicit restart/fail-closed limit coverage.
- [x] Run targeted test RED before source change.
- [x] Preserve queue sequence around the two successful Prepare calls, run targeted GREEN and existing queue/shared-memory tests.
- [x] Compare saved dump process/history and firmware queue fields; document unavailable original ETL/MMIO and avoid pixel/70-render claims.
- [x] Compile affected TU ARM64 /W4 /WX /analyze on persistent builder, transfer only changed sources and verify input hashes. No package/link/sign.
- [x] Run host suite once; compare identities with R150 15F/38E/2S; hash final source tree. Independent review before implementation commit, then CHANGES.csv implemented row referencing resulting commit, and ledger-only commit.

Smallest hardware checkpoint (proposal only): on exact EXP865+this fix and unchanged R143, second private render must stage sequence2/stamps0x7a000200/0x3d000200 with no InitBM and both matching completion observations. Capture first fault plus expected/actual stamps and event identity if it fails; real TA stall remains fail-closed. Exact package/firmware/manifest hashes and separate authorization required. Evidence-first exact cleanup and ordinary Code28, hidden only for unrecoverable guest, no retry/rearm. Sequence reuse is proven offline; being the cause of EXP865's TA stall is a hypothesis until this discriminator.

Execution record: image RED then GREEN; final mixed and active memory checks GREEN; ARM64 compile0/0; full suite exact53 baseline identities; review accepted. Implementation and change-ledger commits follow. Prequeue handling deferred by explicit task condition because truthful contract is incomplete.
