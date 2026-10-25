# GravityMark rc1: allocation pressure and the unresolved compute read

Offline analysis on `codex/rc1`, base
`4d239e23fa7d6a4a37261dff74d8f7293278dd3e`, 2026-10-25.

**A: the immediate allocation failure is PROVEN to be the KMD pool's physical
headroom refusal. Dropping Wom1PoolHalfSize is the strongest LIKELY explanation
for the regression from execution faults to load-time OOM. B: the faulting
resource and the defect that produced its address remain UNKNOWN.**

The actionable correction is to restore the previous half-size experiment
configuration, using an existing exact package, before considering a driver
policy change. This note does not change the default pool policy or claim to fix
GravityMark. No new production defect was established sufficiently to justify
a driver patch/RED-to-GREEN regression. Existing executable regressions were
run; adding an unrelated fix or another receipt-only change would not resolve
the requested cause. The user-visible end-to-end failure remains unresolved.

## Evidence locations

All paths below are local saved files. No guest, hardware, builder or other
worktree was accessed. Let `L=/Volumes/pwdev/public_windows/.local`:

- `E38=L/experiments/EXP1938-retire-evidence`
- `E39=L/experiments/EXP1939-heap-after-hazard`
- `E40=L/experiments/EXP1940-validate-refs`
- `H=L/handoff/codex/rc1`
- `K38=E38/evidence/rec1938h/kmd-receipts-pre-disable.txt`
- `K39=E39/evidence/rec1939h/kmd-receipts-pre-disable.txt`
- `K40=E40/evidence/rec1940h/kmd-receipts-pre-disable.txt`
- `P40=E40/evidence/EXP1940/kmd-receipts-probe-end.txt`

The handoff README and TASK were read completely, as were the referenced
exp1940 report and the relevant exp1936–1939 findings. Historical comparison
was limited to the cited experiments and `2e1fdfcb..02c1393e` driver delta.
`H/pool-comparison.json` contains newly decoded raw pool receipts, source paths,
line numbers and SHA-256s. `H/fault1939.json` is a fresh decode with the current
production decoder. This note supplements, rather than rewrites, older results.

## A: why allocation fails before the segment fills

The actual creation chain is:

1. `drivers/apple-agx/render-admission/src/allocation_windows.c:519` reserves
   backing at allocation creation; refusal returns STATUS_NO_MEMORY. It is
   independent of whether that allocation has become resident yet.
2. `render_pool_slab.c:24` in the same directory defines the backing promise
   as **min(total live reserved granules, ReserveCap)**. `:78` grows this
   physical promise and rolls back the attempted reservation on failure.
3. `pool_windows.c:389` permits new locked slabs only with HighMemory set and
   LowMemory clear. `:527` records the failed reservation before reclaim.
4. `pool_windows.c:695` halves the fixed-at-start size when opted in;
   `:786` uses that size for ReserveCap and `:800` publishes PoolBytes.
   The policy is read once, not changed by setting a registry value mid-run.
5. Destruction unreserves at `allocation_windows.c:667`. Eviction/trim alone
   does not remove this lifetime promise. `render_pool_slab.c:30` preserves
   promised pages even when a slab is otherwise empty.

Thus “VidMm evicts earlier” is an incomplete account of the half-size effect.
It directly caps locked physical backing at 1344 MiB while allowing live
allocation reservations to grow past that size. The larger cap allows the
creation-time promise to grow until physical headroom stops it, even below
the advertised 2688 MiB segment ceiling. Merely evicting idle allocations or
raising the segment ceiling does not solve that creation boundary.

Fresh decoding (MiB = 1048576 bytes; adapter-wide totals, not per-process):

| Receipt | Segment | Peak reserved | Peak committed | Reserve/floor refusals | Held at collection |
|---|---:|---:|---:|---:|---:|
| EXP1938, K38:159 | 1344 | 2892.5 | 1334.0625 | 0 / 0 | 1344 |
| EXP1939, K39:159 | 1344 | 2880.25 | 1344 | 0 / 0 | 1344 |
| EXP1940, P40:160 | 2688 | 2362.1875 | 2363.5 | 17 / 17 | 256 |
| EXP1940, K40:160 | 2688 | 2362.1875 | 2363.5 | 17 / 17 | 146 |

K38:2 and K39:2 record Wom1PoolHalfSize=1. The explicit removal is
`L/scratchpad-tools-backup/before1940.txt:3`, citing EXP1927's rejection.
P40:160 records last refusal with HighMemory=clear, LowMemory=clear,
request 7.25 MiB and reserved/committed 2355.5625 MiB. No new-slab or commit
failures occurred. EXP1940's first 10k refusal was AllocateCb E_OUTOFMEMORY
for 16 MiB with 738.773 MiB available physical RAM; 2k's first was 128 MiB
with 744.547 MiB available (`E40/evidence/EXP1940/gm1940a-umd.log:747`,
`gm1940b-umd.log:3985`). Pagefile/commit availability does not override a
locked-physical-page headroom gate.

**Correction to the exp1940 report:** probe-end and pre-disable pool data are
not identical in the current saved files. Reserved/committed fall from 225.5
to 93.6875 MiB; held falls from 256 to 146 MiB. Peaks, 17 refusals and the last
failure tuple agree. This establishes some subsequent release, not absence
of a transient leak or identification of the first run's RAM consumer.

### Ranked A candidates

1. **LIKELY: experiment configuration regression, full-size backing promise.**
   Same RAM size, double cap, zero versus 17 floor refusals, and the exact
   source mechanism above support this over a new unconditional UMD pin.
   A controlled same-package comparison is still required: EXP1940 also
   enabled validation and changed UMD code, and starting OS pressure was not
   held constant. The 10k run failing at only 444 MiB of canonical spans is
   not explained by the combined two-run peak alone.
2. **POSSIBLE: validation CPU storage/timing or different starting pressure.**
   `agx_win32_gpuva.c:8` grows value-only reference arrays; cleanup is at
   `agx_win32_gpuva_batch.c:1312` (both in `drivers/apple-agx/mesa/winsys`).
   This is not an agx_bo_reference or a residency hold. Validators can still
   allocate CPU memory and use batch getters; zero BO pins does not mean zero
   overhead. No failure-time validator-capacity census proves its magnitude.
3. **LOWER: new exceptional-paging retention caused normal-path OOM.**
   The `838058f9`, `8e3e1b92`, `8d80641c` delta retains ownership only on
   uncertain MakeResident/WaitPaging/restore outcomes. Current
   `agx_win32_gpuva.c:267,281` selects those terminal paths; `:328` still
   retires normal submissions. `umd_gpuva_windows.c:676,1462` guards the
   exceptional accepted/restore paths. Normal queueing clears CopyHeld but
   retains SubmissionHolds until retirement (`:1822`, release at `:194`).
   `umd_residency_trim.h:36` excludes those holds from trim. Saved refusal
   evidence does not establish one of the new terminal branches was taken.
   Reverting these safety fixes is not justified.

`9d00d55a` rebinds each indirect dispatch record after possible CDM rollover;
it does not add a permanent BO owner. `13da5553`/`29c199e8` add validation;
`affa508e` records historical owners rather than retaining them. The C-linkage
fix changes linkage. No unconditional normal-retirement/destruction leak was
found in this bounded delta inspection.

### What EXP1927 rejected

`investigation/EXPERIMENTS.md:61991` preregistered half-size plus BO-cache
reclaim. ACTUAL at `:62020` records consumed 1344 MiB cap, peak reservations
2898.8125 MiB, **zero reserve/floor failures**, followed by MakeResident OOM,
UMD access violations and GPU faults. The rejection was of a usable workload,
not of the specific mechanism that prevented allocation reservation failure.
It also bundled cache reclaim, so it was not a clean size-only comparison.
EXP1928 BEFORE at `:62097` explicitly retained half-size to reproduce the
later failure. EXP1938/1939 still used it, with zero floor refusals and GPU
execution faults. Later failure-handling fixes do not turn any of these into
a successful benchmark, but the old rejection does not justify removing the
switch while trying to study the later first-frame boundary.

## B: what the compute fault actually proves

K39:187–191 and `H/fault1939.json` identify a single valid fault:
fence 0x151a9, OS PID 8400, context 0xffff85838cdf56c0, graph process 0x14,
VM1, read of VA **0x1113ba0000**, UL1C(1), level0, 39 ms after submit.
Mapping capture succeeded, job in flight, Uncertain=0, native leaf absent,
four zero logical PTEs. This is a locked CPU graph/shadow observation, not
a direct hardware page-table/TLB read.

The matching history includes successful all-invalid records:

| Sequence | Relevant range | Interpretation |
|---|---|---|
| 16589 | 0x1113ba0000, 16 logical 4 KiB pages | Repeat invalidation, allocation token 0 |
| 19365 | 0x1112000000, 8192 pages | Repeat + NotifyEviction on old table |
| 20160 | 0x1112000000, 7088 pages | Repeat + InitialUpdate on replacement table |

No retained original valid mapping/owner or subsequent valid publication of
the fault range is present. The bounded history cannot prove “never mapped.”
Old table/index-only matches at other VAs do not identify this allocation.
EXP1936 and EXP1937 have similar all-zero matching snapshots; EXP1938 instead
faulted in DWM and its mapping capture was busy. These are not interchangeable
samples of one authenticated resource.

### Ranked B candidates

1. **LIKELY class, not a proven defect: stale/freed GPU address or missing
   submission residency reference.** It explains why a running command reads
   a range invalidated earlier and not restored in the available history.
   Whether this is a descriptor, root argument, upload/command BO, heap or
   committed/placed resource cannot be selected. The successful invalidation
   does not prove eviction occurred while the GPU was using a properly held BO.
2. **POSSIBLE: incorrectly encoded/out-of-range address or wrong logical VM
   association.** Zero PTEs also fit a command reading a VA that did not belong
   to its submitted resource. The snapshot authenticates the active context
   and process, not the shader's intended owner. There is no evidence to change
   heap-offset arithmetic or VM selection to a guessed value.
3. **LOWER for this exact snapshot: 4 KiB aperture backing cannot form a
   16 KiB native leaf.** That defect normally has valid logical subpages but
   cannot publish a native group; here all four logical entries are zero.
   Historical omitted leaves are not ownership proof: K39:118's retained
   events concern DWM PID1220, not PID8400. Earlier aperture events might be
   missing from the bounded ring, so this is a ranking, not universal exclusion.
4. **LOWER: failed native publication of a currently valid logical mapping.**
   Current logical state is zero and successful invalidation/initialization
   records exist. A lost valid mapping earlier in history is still unexcluded.

The requested “WHICH resource” cannot be answered from this evidence:
`E39/evidence/EXP1939` has no gm1939a UMD/stdout/done artifact, and even PID8400's
executable is not authenticated. The post-reboot process list does not fix that.
The fresh EXP1938 `agx_gm_alloc_join.py` run (`H/alloc-join1938.log`) finds 87 GM
heaps and three 128 MiB CPU-access slots, but no G4 refusal join; its DWM fault
must not be joined numerically to a GM VA. The tool joins G4 submit refusals,
not arbitrary MMU faults, and does not establish cross-process ownership.

An independent source lead was checked and set aside: heap binding precedes
Asahi's compute render-condition check (`agx_d3d12_windows.cpp:3429`, reference
Mesa `agx_state.c:5353`; `agx_query.c:609` can synchronize a predicate writer).
Unlike D3D11, this D3D12 owner does not install a Gallium render condition.
Neither the D3D12 call path nor saved workload evidence establishes that trigger.
Patching that adjacent path would not be an evidence-backed fix for this task.

**Correction to the brief's reset summary:** EXP1938's later job fails the RTKit
notification poll. EXP1939's reset instead times out on the recovered compute
fence itself: three 1000 ms waits, incomplete stamp/consumer, RecoveryPending=1,
and scheduler reset-quiesce tagging after the first failure. It is not evidence
of an RTKit notification-poll failure in EXP1939. Neither TDR symptom identifies
the original bad GPU address. Resume is not retirement and may not release DMA
owners. EXP1941's aborted-job correction is a distinct later-boundary candidate.

## Contract and ownership

WDK26100 `d3dukmdt.h:1475,1484` specifies accepted allocation count and the
paging fence required before GPU use. `d3dkmddi.h:2081,4693` defines paging
flags and incoming allocation/range fields. Windows/VidMm owns residency and
paging; the KMD owns the backing promise and logical/native page tables; the
UMD owns the complete reference set and holds through retirement. Mesa Asahi
`agx_batch.c:519,537` attaches resource BOs to batches and `:206,255,262`
releases their references and pools at cleanup. Asahi Linux `gem.rs:56,84,97`
uses shmem objects/mapping lifetimes, not this Windows pool promise;
`regs.rs:374` supplies the G13 fault decode. No external implementation copied.

No firmware, hypervisor, initialization, interrupt, DMA protocol, power or
recovery behavior is being changed. Live ADT/register collection is explicitly
forbidden by this task. m1n1/Mu are uninitialized gitlinks here; no claim is made
to have revalidated their implementation or their saved binaries. The reference
experiments used m1n1 0d3a75b3 and their recorded Mu/recovery artifacts. An offline
finding about reservation arithmetic does not authorize modifying those owners.

## Smallest next hardware checkpoint — for Claude, not executed here

**Yes: re-enable Wom1PoolHalfSize=1 on the existing exact EXP1940 or EXP1941
package, with validation explicitly off. No rebuild is needed.** Use one chosen
package throughout a comparison; 1940 and 1941 are not equivalent for TDR.
Keep Wom1GpuFaultInfoRead=1, caps, signer, firmware and recovery unchanged.
This is an opt-in experiment, not promotion of half-size to the default.

WHY THIS HYPOTHESIS:

1. Removing the switch doubled the backing cap; the only measured allocation
   failures are 17 physical-floor decisions, below that larger cap.
2. EXP1938/1939 carried ~2.8 GiB live reservations behind only 1.344 GiB backing
   with no reservation failures. Source explicitly permits that distinction.
3. The new retention paths are exceptional; no supplied evidence selects them
   over the changed cap as the immediate load blocker.

Concrete procedure for the authorized hardware owner:

1. Preregister the selected package/manifest/SYS/UMD hashes and preserved
   recovery artifact. Prefer the already available 1940 package for comparison
   against 1940; a 1941-only comparison is acceptable but must be labelled.
   Set device DWORDs before the normal ordered start. Confirm the decoded
   SegmentBytes is **1409286144** (not 2818572288); otherwise stop as a profile
   mismatch. A live registry write without restart does not consume the switch.
2. In a fresh child process explicitly set
   `APPLE_AGX_UMD_VALIDATE_REFS=0`,
   `APPLE_AGX_UMD_VALIDATE_REFS_REFUSE=0`,
   `APPLE_AGX_UMD_REFUSALS_ONLY=1`, `APPLE_AGX_UMD_TRACE_SYNC=1`.
   Invoke the existing `C:\Users\pavel\gravitymark\gm-run.ps1` with
   `-Tag <unique> -Api d3d12 -Asteroids 10000 -Fullscreen 0 -Timeout 90
   -ValidateRefs 0` (1280x720 established workload). The saved runner does
   not clear inherited validation when passed zero: set the environment,
   rather than relying on that argument alone.
3. Record original boot identity, workload PID, available RAM and pool receipt
   before launch; collect receipts and all app logs immediately after this
   one workload. Do not run the 2k follow-up after device loss. Hash exact files.
   Require previous process exit and reservations/headroom back to measured
   baseline before another comparable run.
4. The cheap half-size/off run changes two settings relative to EXP1940's
   full-size/on run, so crossing load is useful but not a size-only causal
   proof. For strict attribution use a **full-size/off versus half-size/off**
   same-package pair with comparable starting pressure. Alternatively, after
   half-size/off crosses load, run half-size/on to address B using the existing
   validator and owner-history instrumentation. Do not add another diagnostic
   build. Refuse mode remains 0 to preserve the intended record-only contract.

| Observation | Decision |
|---|---|
| Half-size/off crosses load, no new floor refusals | Supports the configuration remedy; later GPU fault is B, not failure to cross A |
| Full-size/off fails at floor, half-size/off crosses, comparable starts | Confirms the size policy as causal for A in that workload/profile |
| Full-size/off also crosses | Validation overhead/start pressure remains plausible; size-only attribution unproven |
| Half-size still gets floor refusal before it is fully backed | OS headroom is insufficient even for the smaller promise; preserve baseline/ownership evidence, do not weaken the floor |
| Half-size reaches MakeResident OOM | A has moved to the working-set/residency boundary; EXP1927-style failure is possible, not benchmark success |
| Allocation succeeds, zero-PTE fault recurs | A workaround is useful; B remains. Collect validator violation and same-process historical owner/range plus full UMD log |
| Validation names an absent/wrong-owner reference | Follow that exact command/resource path; do not infer all other references are valid |
| Fault with no violation or no owner-history match | Existing validator coverage/history is insufficient to identify the resource; do not claim a mapping fix or schedule a census-only build |

Successful load is not successful rendering. Full product success still needs
natural benchmark completion, correct image/score/time, no device removal or
0x116, and stable desktop/SSH. Collect evidence before exact package cleanup;
use the established GPU-visible recovery, emergency GPU-hidden only if needed.
No authorization or physical action is requested or exercised in this task.

## Verification and deliverable limit

Existing targeted regressions: pool residency limit, pool headroom, slab
allocator, persistent/partial residency, failed restore paging, reference
validator, indirect heap rollover and fault mapping: **14 tests, OK**
(`H/targeted.log`). These include executable production-code checks of the
half-size backing promise and preservation of submission ownership. They are
not a new RED/GREEN demonstration of a production fix.

Required command, from this worktree, `cd tests && python3 run_against_baseline.py`:

```text
ran 496 tests in 214 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

`H/baseline-first.log` is the original output. No known-failure entry changed.
The six existing failures are heap-registry-threading import, rejected-device
callback fixture, primary-import alignment, USC-window replay, R105 open
normalization, and win32-screen fail-closed fixture. No new fresh-worktree
environment failure was reported. `.local/reference` was linked to the shared
read-only reference tree; no reference content was edited. Tests do not prove
native WDK compilation or hardware behavior. No package was built or installed.

The only tracked deliverable is this analysis. The handoff REPORT records its
commit and supplemental evidence. No new driver fix, receipt change, fabricated
regression failure, hardware verdict, experiment/change ledger edit, or claim
of GravityMark completion is made. Tandem OPEN dispositions are in
`H/review-responses.txt`; protected invariants remain intact.

**WHAT IS STILL UNKNOWN:** the fault resource/creator/address source; whether
it was destroyed, trimmed without a required hold, wrongly addressed or never
mapped; the fault PID's executable; failure-time validator storage and other
RAM consumers; the effect of half-size on the current package's working set;
whether the existing 1940 behavior fixes resolve B once A is bypassed; and
whether 1941 changes the later abort/reset outcome. These gaps prevent an
honest source-proven end-to-end fix from this saved evidence.
