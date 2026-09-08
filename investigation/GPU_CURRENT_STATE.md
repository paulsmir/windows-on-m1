# GPU current state

Current boundary: EXP634 frame5 PrivateVirgin guard. EXP635 build in progress. Main process,
no agents. Historical state preserved in
`.local/experiments/EXP633-repeated-ping-pong/current-state-before-compaction.md`.
Consult only relevant EXP631–633 evidence before the next experiment.

## Current machine

EXP634 raw evidence saved and exact package/devnode/stale cleanup succeeded
in the live guest. Controlled shutdown succeeded; ordinary377/392 restoration
is running in session66395. Verify clean health before staging EXP635.

## Hardware proven

- EXP475/477/478: retained root, context0, RTKit, native initdata, firmware,
  BackendRuntimeStart, arena/context/queues.
- EXP581/585/586/588: Windows-originated physical TA/3D, correct offscreen
  output and exact Windows fences. Completion ingress remains polling.
- EXP591: physical-panel photo proves scanout.
- EXP631: two complete2560x1600 outputs, exact query/latch identities,15s HOLD.
  User reported probable visual color change, not instrumented confirmation.
- EXP632: two full frames, HOLD, explicit replacement by retained Windows
  primary at offset0, exact newer latch, all allocation/context/device destroy
  statuses0. ACTIVE, no fresh41/1001/129 through uptime189s.
  Source36168a9f2802e8dd6e2c5f1fe5875161192f7722.

## EXP633 actual result

Frozen source eb249e6439b2faf2d6cd1c86bc2f66eb9b3b4832, package30.0.633.0.
One16-frame attempt reached two Render/Submit/worker/Notify/DPC calls,
fences256/257. Correlation build633/boot354696813, generation18, durable1,
export status0; binary SHA256
`b8e3563dfdb034a1f867f591c905f3940100175b2a0e60b23b1cf36eebf42d91`.
Host has initial swap8, Windows-primary swap9, frame1 swap10; no saved frame2
latch. Live frame1 query passed, but recovered stdout ends earlier: stdout
disk persistence is incomplete. Missing stdout does not prove absent calls.

Bugcheck0x101 (0x18,0,ffffdb805de5a980,4), uptime81.697s. Dump
090826-21875-01.dmp SHA256
`a734514abdf06038032cca40e376b04059416d5ee878516d7f268888489d45b1`.
Private633 PDB matches. CPU4 is an ExpWorkerThread; truncated KiSwapContext
stack does not identify an AppleAgx callback. No attribution to cleanup,
DCP, output hashing or storage is proven. Frame3 not reached; repeated
acceptance INCONCLUSIVE, two render completions confirmed.

Evidence: `.local/experiments/EXP633-repeated-ping-pong/` contains
hardware.log, correlation.json and evidence with raw binary, stdout, events,
dump, kd-analyze.txt and kd-cpu-progress.txt.

## Active source work / next gate

Source16f7e308154781dccfed4936d8bd3e68fc85d844 appends
four version3 per-fence output records: entry, verification, presentation entry
and exit, each with validity/status/CPU/IRQL/time. Decoder retains version2
support. Existing asynchronous device-key exporter is reused; no functional
render result changes.122 render tests pass, including production C records
decoded by the actual decoder. Pinned634 build/sign/Universal/version gates
PASS; exact manifest and build logs in EXP634-output-progress. Staged only;
hardware outcome pending.

Next: finish composition review/tests, freeze only explicit overlay over
immutable633, build/sign/hash and preregister bounded live collection.
Missing asynchronous export alone cannot prove callback absence. Keep
captured/exported/durable distinct. Select causal fix from exact output stage.

## Latest EXP634 verdict / EXP635 next

EXP634 completed FOUR full frames: fences256/257/258/260, presentation
sequences3/4/5/6, latches10/11/12/13. Every frame4096000/4096000 correct
pixels, alternating exact two owners/offsets/PAs/hashes. Both targeted v3
output records show entry/verification/presentation success at sampled IRQL0.
No watchdog in this run; no claim the diagnostic change cured0x101. Physical
color-change question remains pending. Event129x5 occurred before producer.

Frame5 Render returned0xc00000e8; saved Wom1UmdRenderGuard18 names
PrivateVirgin. Microsoft's DXGK_DEVICEINFO guarantees zeroed private DMA
storage at creation only. Our per-Render zero requirement rejects reuse.
Source653e4d85bdea4262d6e71cdcae9144979c0c1ebf removes that precondition;
existing Initialize/Append reconstructs new request, preserves other guards.
Source9c1dbfd8f6c4041ea2cdabf30d81667fe87cd141 fixes producer's common
cleanup label: direct Render failure cannot bypass retirement. EXP634 had
called DestroyAllocation while CleanupAllowed0; safe retirement is NOT proven
by that run. EXP632 remains the validated normal retirement reference.

Executable sixteen-request/four-recycled-block shadow test and producer
failure cleanup state test pass; render122 tests pass. Exact635 overlay
024aff141e0e2215a6bb0b84ca26616c10e44bdef252b203d88b9a15be11f152 over
immutable FRYZZING EXP634. Build/sign/hash in progress; no635 hardware yet.
Next execute635 one unchanged16-frame workload after health and build gates;
save every record, HOLD, explicit retirement then health/cleanup. Evidence
and exact634 hashes are in EXPERIMENTS.md and EXP634-output-progress.

## Constraints

No speculative AGX/PBE/UAT/RTKit/DCP/capability changes. No ANS modifications.
Event129 remains telemetry without causal evidence. No blind candidate retry.
Remove failed packages after evidence; retain final working accelerated driver.
Desktop/standard Present/OpenGL/CS1.6 acceptance remains unproven.
