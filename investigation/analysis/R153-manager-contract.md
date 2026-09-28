# R153 — manager state and queue state have separate lifetimes

The R152 receipt-only EXP867 proposal is rejected by the reviewer. This change
implements the derived ownership contract offline; EXP867's proposed single
variable is the correction itself. No package or Air action is authorized here.

## Primary contract

Asahi `queue/mod.rs::Queue::new` creates a `buffer::Buffer` using its VM's
allocators and retains it in QueueInner; vertex/fragment workqueues are separate.
Thus the native owner is a logical queue's TVB buffer tied to a VM, not a global
GPU manager object. `buffer.rs::BufferInner` owns Info, BlockControl, Counter,
Stats and the page/block lists/heap. `Buffer::new_scene` creates scene/user
buffer state, retains Buffer, and acquires a hardware manager slot. The slot is
released by `Scene::drop` after the last active scene; the buffer's mutable
state persists. `queue/render.rs` emits InitBuffer on slot rebind or TVB growth
before vertex work, carrying vm_slot, buffer_slot, block_count, Info pointer
and stamp. `Buffer::increment` advances the manager counter per committed job.
`workqueue.rs::signal` retires commands only after the corresponding event value
completes; TA/fragment commands retain their scene references. `fw/buffer.rs`
defines the Info/control/counter/stats and per-scene layouts.

m1n1 `proxyclient/m1n1/agx/context.py::GPUBufferManager` similarly owns Info,
control/counter and VM-backed lists; `agx/render.py::GPURenderer` keeps that
manager and an initialization flag separately from its workqueues. Its simpler
single-renderer lifetime does not justify keeping the first manager when
Windows changes process backing. The existing G13/V13_5 template relocations
and `fw/agx/{cmdqueue,microsequence}.py` supply the field layout; no external
implementation code was copied.

Windows uses R137's `ADMISSION_G3_PROCESS.PrivateManager`: a process-owned
manager shared by contexts naming its generation, with per-scene scratch.
That is a valid serialized pooling choice even though Asahi creates one buffer
per logical queue. WDDM contexts execute within their process GPUVA space;
see Microsoft's [per-process GPUVA contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/per-process-gpu-virtual-address-spaces).
`AdmissionGpuvaG3BeginJob` validates the exact scene/manager generation and
mappings, pins them and leases VM slot1. Joined completion precedes JOB_END,
lease release, Windows notification and private-scene reclamation. A fetched
job or one completed queue alone is not permission to reuse manager state.

## Translation implemented

Each process keeps a nonpaged CPU snapshot for its manager's mutable firmware
objects: Info1, BlockControl20, Counter21, Stats22. The existing serialized
context0 slot remains the GPU-visible storage. After joined completion, the
platform synchronizes all four objects for CPU access and saves them before
`AdmissionGpuvaG3CompleteJob` releases the process lease. Failed synchronization
or a wrong fence leaves the snapshot pending; real GPU-stop failure remains
quarantined. No new persistent GPU mappings or firmware allocation policy.

The next job restores its manager's snapshot, including A→B→A counters, or
initializes a fresh manager for a new owner/generation/backing. The binding key
includes process owner, manager generation, root and page/block/heap ranges.
Root changes require rebind while preserving manager state. Identical VA
values in different processes do not identify one manager. Scene13 is refreshed
for every render pass, including a changed user buffer within the same manager.

`RequireInitBm` accepts only an idle provider/runtime with no pending fence or
completion. It invalidates the manager binding without resetting queue state.
The next plan separately says whether to initialize queues and whether to
publish InitBM; its extra TA ring entry is included in the expected head.
R151's sequence, queue pointers/rings/job list, events and NewQueue flags persist.
The dynamic scalar staging layer formerly rejected InitBM after sequence1;
that obsolete guard is removed. A real StageJob regression proves sequence2
can rebind with its own fresh stamps, and the InitBM stamp follows that sequence.
The process-owned snapshot is cleared with final private-manager reclamation;
the exact scene hold pins its pointer through completion. Legacy/B1 continue
through their existing materialization path.

## ATOMIC CONTRACT

Owner/backing selection, restoration of mutable manager state, refreshed list
pointers and scene, InitBM selection, extra TA-entry accounting, and preservation
of queue state are one binding transaction. Changing just the pointers or just
BufferManagerInitialized is invalid. Snapshot save, joined completion and lease
release have an equally ordered retirement contract. This coupling is why the
portable contract and its Windows wiring are separate reviewable commits but
one hardware variable.

## Verification and next gate

The real-materializer regression is RED on R152's source and GREEN with this
path. It checks distinct backing, A→B→A mutable-state restoration, identical
VA/different owner, generation and root changes, fresh scenes, counters,
queue/EventControl preservation, busy/stale save refusal and repeat completion.
The provider regression proves idle rebind publishes InitBM at the next ring
position without NewQueue reset. Real BeginJob replay checks the pinned snapshot
identity; extracted production callbacks test join, flush failures and ordering.
Exact results, ARM64 logs, source hashes, full-suite failure identities and
independent review are indexed in `investigation/evidence/R153/summary.json`.
Final full suite:1164 tests,15 failures/38 errors/2 skips; baseline1162 has the
same failure identities and counts. All eight final focused staging/manager/
callback/BeginJob tests pass. Six affected ARM64 TUs compile with
`/W4 /WX /analyze`, zero compiler warnings/errors. The persistent builder's
307 KMD/shared inputs were hash-checked; only changed inputs were synchronized.
Unrelated pre-existing Mesa/UMD builder differences were preserved.

Independent review found no Important/Critical issue in its scope; its optional
Stats/second-queue sentinel expansion is deferred (the production copy list and
queue exclusion were inspected). Root integration subsequently found the
sequence1-only staging guard and standalone-header dependency; both are fixed
and covered by the final tests/compile. Review limits and this coverage correction
are explicit in `R153-independent-review.md`.

Derivable ownership unknowns: **none**. Hardware-only unknowns: whether the fix
removes EXP866's timeout and what boundary follows the first newly completed
cross-manager render. No new claim of pixels, sustained DWM or GPU recovery.

WHY THIS HYPOTHESIS: EXP866 fetched job5 and then timed out; its active manager
named absent process mappings; the real materializer reproduces that mismatch
and the corrected path restores the selected backing without queue reset.

Proposed EXP867 = R153 binding correction on exact EXP866 source17ee8894,
unchanged R143, Flush excluded. Require fresh TA+3D completion across manager
change, then the existing600s/nonzero-scanout checkpoints. Build/sign/hash and
preregistration remain separate future work. Recovery stays EXP866's dump-first
hiddenCode45 exact cleanup then durable ordinary Code28. No receipt-only run,
package, rearm, firmware change or Air probe was performed.
