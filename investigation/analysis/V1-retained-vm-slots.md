# V1 -- retained per-process VM slots (design, offline)

Status: design only, 2026-11-04. No code changed. m1n1 is fixed at the checked-in
gitlink `0d3a75b3` (the EXP2163 broker); nothing below requires an m1n1 change.

Motivation (EXP2163, `investigation/EXPERIMENTS.md` ~68948): `AppleAgxGpuvaG3GraphBeginJob`
(V5 LEASE + JOB_BEGIN) costs 160-272 us on every process-switch begin and 8-19 us otherwise.
The EXP2163 mid-arm -> after receipt delta (40.0 s, `Wom1G3PagingProfile`, decoded offline) shows
LEASE 5527 calls at 54.7 us mean (max 2.67 ms), JOB_BEGIN/JOB_END/RELEASE 5527 each at 5-6 us,
**UPDATE_LEAF 190,652 calls at 2.9 us mean**, REVOKE_BACKING 188,932, UPDATE_PARENT 123. The KMD
saw 845 DXGK UpdatePageTable operations (972k entries, about 1150 per operation, 1.43 ms each) and
725 FlushTlb operations, so a paging batch arrives about once every two frames.

## 1. AGX/ASAHI CONTRACT -- m1n1 broker (`m1n1_windows/src/hv_agx_gpuva_v5.c`)

- **Several owners at once: yes.** There is one `slots[64]` table (`hv_agx_gpuva_v5.h:25,111`).
  `lease` (`:729-760`) refuses only slot 0, slot >= 64, an occupied slot (BUSY), slot 63 while
  the legacy root owner is active (`:740`), and a nonzero prior TTBR (OWNERSHIP, `:743`). One
  owner may hold several slots (`m1n1_windows/tests/hv_agx_gpuva_v5_test.c:112-124`, slots 1 and 2).
  Process capacity is 128 (`hv_agx_gpuva_v5.h:10`).
- **Refused while the owner holds a slot with jobs == 0:**
  RELOCATE_ROOT (`:720-722`, any occupied slot of the owner -> BUSY) and DESTROY (`:846-848`, BUSY).
  RR_CLOSE in the platform also needs `gpuva_idle()`, meaning no occupied slot
  (`hv_agx_retained_platform.c:149-157,408`).
- **Allowed while leased and idle; refused only when jobs > 0:** UPDATE_LEAF and UPDATE_PARENT
  (both go through `publish_entry`, `:300-322`), and FLUSH_TLB (`:821-823`).
- **No slot check at all:** REGISTER_TABLE, REGISTER_(SHARED_)BACKING, REVOKE_BACKING (`:621-650`;
  it only needs the leaf to be unmapped), REVOKE_TABLE (`:652-670`) and CREATE.
- **Invalidation:** `publish_entry` does `sync_tables` and then calls `invalidate(slot)` on
  **every occupied slot of that owner on every leaf or parent update** (`:310-312`). FLUSH_TLB
  invalidates every occupied slot of the owner (`:828-834`). LEASE and RELEASE invalidate their own
  slot (`:747-748`, `:795-796`). `invalidate` is `TLBI ASIDE1OS(slot<<48); DSB OSH; ISB`
  (`hv_agx_retained_platform.c:115-123`), and the DSB waits for the GPU's DVM acknowledgement.
  With no lease (today's idle state), `publish_entry` only syncs, which is why UPDATE_LEAF is 2.9 us.
- **Stale-TLB safety with a persistent lease: preserved by m1n1, not by the KMD.** Every descriptor
  change of a leased owner is invalidated before it is acknowledged, and VidMm's FlushTlb becomes
  one more invalidation. **Cost hazard:** with a lease kept across the paging batch, each of the
  ~225 UPDATE_LEAF calls per batch pays a TLBI. Idle-GPU RELEASE costs 5 us and a TLBI while the
  other process runs costs 160-270 us, so one batch would cost about 1-60 ms instead of 0.65 ms. Keeping
  the lease across paging is therefore unsafe for performance, although it stays memory-safe.
- `check()` (`:170-198`) reads back every occupied slot on every call, at two MMIO reads per retained
  slot. This cost is small and is measured by the per-command means.
- **Slot limits:** m1n1 accepts 1..63, with slot 63 only while no legacy root is active. The KMD
  graph accepts 1..62 (`apple_agx_gpuva_g3_graph.c:954`). The G13 compute builders refuse
  `VmSlot >= 0x10` (`shared/src/apple_agx_g13_compute_work.c:25,124`, `apple_agx_render_shared_memory.c:908`),
  so **K <= 15 (slots 1..15)**.

### Asahi (`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi`)
- Asahi has the same retained-slot model. `Uat::bind` (`mmu.rs:1471-1525`) prefers the VM's last slot through
  `slotalloc::get(bind_token)` (`slotalloc.rs:203-283`; LRU by `drop_time` over free slots, blocks
  if none). Only when `slot.changed()` does it write TTB0/TTB1 under the handoff lock and then
  `tlbi_asid(idx)` (`mmu.rs:1490-1515`). When the last `VmBind` user drops, the slot returns to the
  allocator **without clearing the TTB** (`mmu.rs:578-593`). Unmaps invalidate the last slot by range even
  while unbound (`mmu.rs:360-362,414-416,459-477`). A dropped VM clears the TTB and does `tlbi_asid` (`mmu.rs:1357-1400`).
  User slots are 1..63 (`mmu.rs:93-97`). `robust_isolation` limits this to one slot.
- `vm_slot` is a per-command field (TA/3D/compute/InitBuffer/barrier: `fw/vertex.rs:123`,
  `fw/fragment.rs:216`, `fw/compute.rs:68`, `fw/buffer.rs:117`, `workqueue.rs:146,317-346`). Faults
  carry `vm_slot`, and a fault in another slot marks the job Killed (`workqueue.rs:215`).
- **Firmware evidence:** overlapping work with *different* vm slots on *different* queues is routine in
  Asahi. Consecutive jobs with different slots on *one* queue happen in Asahi only after a slot steal.
  **Overlapping (TA N+1 during 3D N) with different slots on one queue is never exercised:** a queue
  has one VM (`queue/mod.rs:661`), and job N holds its `VmBind` (`queue/mod.rs:188,720`), so N+1 gets
  the same slot. Asahi issues InitBuffer (which carries `vm_slot`) only on a scene rebind or a TVB
  grow (`queue/render.rs:954-973`).

## 2. WINDOWS CONTRACT

VidMm serializes UpdatePageTable and FlushTlb on the paging engine and expects them to complete
without waiting on unrelated processes. DestroyProcess must eventually succeed. Engine reset/TDR
expects the adapter to accept new work afterwards. Nothing in WDDM names a GPU ASID, so the slot is
private to the KMD. The FlushTlb DDI requires that no translation older than the preceding
UpdatePageTable remains usable by later GPU work.

## 3. KMD sites that assume slot 1 or "no lease while idle"

Today `Graph.LeaseToken != 0` is equivalent to `Graph.JobInFlight` outside `state->Lock`, because both
change only inside BeginJob/EndJob under the lock. A BeginJob failure between LEASE and JOB_BEGIN sets
`Uncertain`. Every `JobInFlight || LeaseToken` test therefore means "GPU busy", and every one of them
deadlocks or refuses once the lease persists.

| Site | Today | Required change |
|---|---|---|
| `backend_platform_windows.c:3352` (compute), `:3642` (prelude compute) `input.VmSlot = ADMISSION_GPUVA_G3_JOB_VM_SLOT` | slot 1 | `process->Graph.Slot` |
| `render_backend_image.c:346-347` `AppleAgxG4BuildTa3dEx(..., 1u, ...)` (render/present template, seven `context_id` fields `apple_agx_render_template_vm_slot.c:9-17`) | literal 1 | pass the process's slot. Re-Prepare restores 63, so a per-job slot works |
| `lifecycle.c:374` start-time `SelectVmSlot(1)` | slot 1 | keep (it is overwritten per job) or drop |
| `gpuva_g3_windows.c:3942-3944` BeginJobAs -> `GraphBeginJob(slot 1)` | LEASE every job | allocator (section 4) |
| `apple_agx_gpuva_g3_graph.c:949-1007` Begin/EndJob | EndJob = JOB_END + RELEASE | `EndJob` keeps the lease; new `GraphReleaseIdle` |
| `gpuva_g3_windows.c:51-72` fault attribution (`Graph.Slot == FaultInfo[22:17]`, needs `JobInFlight && LeaseToken`) | slot 1 | unchanged semantics. Per-process slots make the slot match a real identity |
| `gpuva_g3_windows.c:3767` fault drill `Slot != ADMISSION_GPUVA_G3_JOB_VM_SLOT` | slot 1 | `Slot == 0` |
| Paging waits `gpuva_g3_paging_windows.c:1884,1909` (FlushTlb), `:2058,2094,2113` (UpdatePageTable), `:773` (leaf refusal), `paging_windows.c:382` (live mutation hold) | wait while `JobInFlight || LeaseToken` | wait on `JobInFlight` only, then release the idle lease (rule R) |
| FlushTlb branch 5 `gpuva_g3_paging_windows.c:1964` (skip if `Slot == 0`) | skip when idle | correct as is. An idle-leased process gets a real FLUSH_TLB (1 TLBI) |
| `gpuva_g3_paging_windows.c:214` table reuse/root parking (`!Slot`), `:244,263,2197`, `gpuva_g3_windows.c:3411` -> `GraphBindRoot` = RELOCATE_ROOT | KMD checks `JobInFlight` only (`graph.c:449`) | **must release first** (m1n1 BUSY -> poison) |
| `graph.c:876` AttachPrivate, `:905` CanDetachPrivateRootSet, `:1015` Destroy require `!LeaseToken` | | release first |
| DestroyProcess `gpuva_g3_windows.c:3096-3103` -> `GraphDestroy` | BUSY if leased | release first (otherwise the process leaks forever) |
| Private storage `gpuva_g3_windows.c:664,758,879` (steal from *other* owners), `:1000,1097,2312,2458,2500,2643,2669,2693,2716,3200` | `JobInFlight || LeaseToken` | `JobInFlight`, plus release before any `PrivateMapExtent` UPDATE_LEAF |
| Copy gate `gpuva_g3_windows.c:1931,1973,1985` (refusal 44) | lease = busy | `JobInFlight` (copies move data, not translations) |
| Timestamp `gpuva_g3_windows.c:4213` (`JobInFlight && LeaseToken`) | | unchanged (true during a job) |
| Completion/abort `gpuva_g3_windows.c:4315,4365` | EndJob releases | EndJob keeps the lease. Successor logic unchanged (same process, same slot) |
| Live-additive `graph.c:118-129` (`NegotiateLive` needs `!JobInFlight`) | at CreateProcess only | unchanged. Not supported by m1n1 0d3a75b3 (`validate_envelope` range, `hv_agx_gpuva_v5.c:205-208`) |
| Stop/RR_CLOSE `backend_platform_windows.c:2570` | close needs `gpuva_idle()` | release all leases first |
| Reset/TDR (`AdmissionGpuvaG3AbortJob` `:4350-4390`, `PrivateReset` `:1006`) | lease gone with the job | release every idle lease after reset (return to the validated baseline) |
| Depth-two window `render_job_window.c:120` refusal 55 (`m->Process != n->Process`) | | unchanged in step 1 |

## 4. TRANSLATION (design)

**State.** `ADMISSION_G3_STATE` gets `SlotOwner[16]` and `SlotLastUse[16]` (indexes 1..K, K <= 15,
first run K = 4). Each `ADMISSION_G3_PROCESS` gets `PreferredSlot` (like Asahi's bind token).
`Graph.LeaseToken/Slot` now mean "this process holds slot S", and `Graph.JobInFlight` means "GPU busy".

**Take (BeginJobAs, non-successor, under `state->Lock`; `ActiveProcess == NULL` is already required,
`gpuva_g3_windows.c:3913-3915`, so every other process is idle):**
1. If the process already holds a lease, send JOB_BEGIN only (about 6 us).
2. Otherwise pick `PreferredSlot` if free, then any free slot, then the LRU idle-leased victim.
   Send RELEASE for the victim (its `LeaseToken = Slot = 0`), then LEASE + JOB_BEGIN. A slot whose owner
   has `JobInFlight` is never stolen. A slot outside 1..K is never used.
3. Store the slot in the process and in `SlotOwner`. Pass it to the compute, prelude and render builders.

**Keep (EndJob):** send JOB_END only. Set `JobInFlight = 0`, keep `LeaseToken`, and stamp `SlotLastUse`.

**Release (rule R, "release before mutate"):** `AdmissionG3ReleaseIdleLease(p)` (needs `!JobInFlight`,
under the lock) sends RELEASE (one TLBI) before the first
UPDATE_LEAF/UPDATE_PARENT/RELOCATE_ROOT/DESTROY/Attach/Detach issued for `p` by UpdatePageTable,
BindRoot, table reuse, private storage map/unmap, DestroyProcess, reset and Stop. FlushTlb on a
still-leased process issues FLUSH_TLB (one TLBI, keeps the lease). Registration and revoke calls need
no release.
Effect: every translation change of process P happens with P unleased, exactly as in the hardware-validated
baseline. The next LEASE writes the TTBR and does a TLBI (`hv_agx_gpuva_v5.c:747-748`). No GPU access
can use a stale translation:
(a) m1n1 refuses any update while jobs > 0;
(b) RELEASE zeroes the TTBR and invalidates;
(c) a missed release site would still be invalidated per entry by m1n1, so it costs time, not safety.
The exceptions are RELOCATE_ROOT and DESTROY, which fail closed (BUSY -> poison). The tests cover them.

**Manager key.** Add `Slot` to `G4ManagerKey` (`gpuva_g3_windows.c:3949-3957`) so that a process whose slot changed
(after a steal or rule R) re-issues InitBM with the new `context_id` (the template field
`{16,0x004}`). This is a conservative reading of Asahi's InitBuffer carrying `vm_slot`.

**Later, refusal 55.** With two leased slots, N (slot a) and N+1 (slot b) can both be JOB_BEGIN'd, because
m1n1 counts jobs per slot. The KMD would need `ActiveProcess` split into per-flight entries, paging gated on
the owning process's `JobInFlight` only, and the window allowing `m->Process != n->Process` when both
processes are leased and neither has a paging packet between them (54). This stays refused until the
firmware question below is answered.

**Switch.** Add `Wom1G3RetainedSlots` (default 0 = today's EndJob + RELEASE, which keeps the production
and diagnostic profiles equivalent). The experiment sets 1 as the single variable.

## 5. WHAT IS STILL UNKNOWN

1. Whether AGX firmware accepts consecutive jobs on **one shared queue** with different `context_id`.
   Asahi does this only after a steal, and the hardware state is not validated here. This is the
   single hardware question of step 1.
2. Overlap with different slots on one queue (cross-process depth-two). Not exercised anywhere.
   Deferred to a separate experiment after step 1.
3. The per-process paging rate between that process's jobs. 845 UPT per 40 s across all processes is known;
   the split between GravityMark and DWM is not. This decides how many re-leases rule R leaves. Expected
   bound: LEASE calls fall from 5527 to at most about 845 + steals. The gain is at most about
   2 x 200 us per frame (~1.5 % FPS at 39 FPS); the larger value is enabling the cross-process pairs.
4. Whether InitBM needs a re-issue when only the slot changes (handled conservatively by the manager key).

## 6. Smallest first step and hardware checkpoint

**Step 0 (offline, behaviour-neutral):** introduce `AdmissionG3GpuBusy(p)` (= `JobInFlight`) and replace
every `JobInFlight || LeaseToken` in the table above. Equivalence argument: section 3, first paragraph.
The existing replay suites must stay GREEN.

**Step 1:** add the allocator (K = 4), EndJob-keeps-lease, rule R at the listed mutation sites,
release-all on DestroyProcess/reset/Stop, the process slot in the three builders, and the manager key
slot, all gated by `Wom1G3RetainedSlots`.

Offline tests (`WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?`: stale translation after
release/steal, per-leaf TLBI regression, paging deadlock, DESTROY/RELOCATE poison):
- **Real-broker replay** (extend `tests/g4_real_broker_builder_replay.c`, linking the real
  `hv_agx_gpuva_v5.c` with counting ops as in `m1n1_windows/tests/hv_agx_gpuva_v5_test.c:62-67`).
  Two processes alternate 100 Begin/End pairs, which must produce exactly two LEASE calls and two
  invalidations. A 225-leaf UPT batch for idle-leased A must produce exactly one RELEASE invalidation
  and 0 per-leaf invalidations. A's next job must re-lease slot A and invalidate before JOB_BEGIN.
  BindRoot, DestroyProcess and PrepareTableReuse on an idle-leased process must succeed (no BUSY).
  Invariant: for every slot, no JOB_BEGIN follows a descriptor change of its owner without an
  intervening invalidate of that slot.
- **Allocator unit test:** preferred slot, free slot, LRU steal of an idle lease only, never steal
  `JobInFlight`, never 0/63/>K, K <= 15.
- **Graph test** (`shared/tests/apple_agx_gpuva_g3_graph_test.c`): EndJob keeps the token,
  `ReleaseIdle` refuses while `JobInFlight`, BeginJob with a held token sends JOB_BEGIN only, nested
  successor unchanged.
- **VidMm replay** (`tests/g3_vidmm_replay_shim.h` cases): UpdatePageTable/FlushTlb for an idle-leased
  process do not wait (iterations 0). FlushTlb on a leased process sends FLUSH_TLB once.
- **Builder test:** the render template's seven `context_id` fields and the compute `VmSlot` equal the
  process slot (update `tests/g4_submit_virtual_replay.c:1178`, `tests/test_g4_fault_drill.py:96`).

**Hardware checkpoint (one variable: `Wom1G3RetainedSlots=1`, EXP2163 package otherwise, m1n1 0d3a75b3,
`Wom1JobDiagnostics=1 Wom1GpuFaultInfoRead=1`, D3D12 suite + GravityMark D3D12 100k -temporal 0 HD PERF):**
- Pass:
  - Wom1G3BeginSplit lease stage on process-switch begins drops from 160-272 us to <= 20 us
    (JOB_BEGIN only) on begins that follow no paging batch for that process.
  - `Wom1G3PagingProfile` LEASE calls per 40 s fall from 5527 to <= ~1000.
  - **UPDATE_LEAF mean stays about 3 us.** This proves rule R: no per-leaf TLBI.
  - Wom1G3PagingWait shows no new timeouts.
  - Suite 119/0, no GPU fault receipts, no TDR, FPS >= EXP2163 (39.29) - 2 %.
- Fail and stop: any GPU fault (above all a fault in slot != 1), UPDATE_LEAF mean > 10 us, a paging wait
  timeout, DestroyProcess BUSY in the receipts, RR_CLOSE failure at rollback, or a firmware assertion.
- Recovery: `Wom1G3RetainedSlots=0` (restores today's behaviour) plus the standard recXXXXh visible recovery.
