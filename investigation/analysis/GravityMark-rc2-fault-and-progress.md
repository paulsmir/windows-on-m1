# GravityMark rc2: recovered ownership clues, paging progress and failed retirement

Offline on `codex/rc2`, base `ab7b13ea`, 2026-10-25. No hardware, guest,
builder, package operation or other worktree was accessed. This is an analysis
result, **not a driver fix or a claim that GravityMark works**. The new evidence
settles the t14 retirement question and corrects the interpretation of the
10k Execute log. It still does not identify the instruction/resource responsible
for the GPU read sufficiently to justify a production patch.

## Evidence and reproducibility

Let `L=/Volumes/pwdev/public_windows/.local`,
`E39=L/experiments/EXP1939-heap-after-hazard/evidence`,
`E41=L/experiments/EXP1941-fault-recovery/evidence`, and
`H=L/handoff/codex/rc2`. Log names below mean:

- `A39=E39/app-logs-late/gm1939a-umd.log`;
- `A41=E41/EXP1941/gm1941a-umd.log`;
- `B41=E41/rec1941h/app-logs/gm1941b-umd.log`;
- `K41=E41/rec1941h/kmd-receipts-pre-disable.txt`.

`H/receipts1941.json` is a fresh decode of K41's hex values through the current
`tools/decode_wom1_gpu_fault.py` DECODERS. `H/timelines.json` contains source line
numbers, complete rows, counts and largest consecutive QPC gaps for all three
logs. Times below use the task's 24 MHz QPC frequency. The README, TASK and rc1
REPORT were read in full. Only the referenced ledger tail was consulted.

## 1. B: what was read, and why was it unmapped?

**PROVEN: both VAs fall immediately after a large descriptor heap, not inside
its recorded allocation. This is an adjacency observation, not ownership.**

| Run | Authenticated UMD PID | Heap creation | Heap interval, end exclusive | Fault |
|---|---:|---|---|---|
| EXP1939 | 8400, A39:1 | A39:522 | `[0x11127a0000,0x1113ba0000)` | `0x1113ba0000`, fence `0x151a9` |
| EXP1941 2k | 1640, B41:1 | B41:441 | `[0x1111280000,0x1112680000)` | `0x1112680000`, fence `0x340e8` |

A39:523 and B41:442 identify a shader-visible CBV/SRV/UAV descriptor heap
with 0x20000 descriptors. Each entry is 160 bytes, so the size is exactly
0x1400000 (20 MiB). This is derived from the general receipt schema:
`drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:2130` records size/VA shifted by 16;
`:3302` computes descriptor bytes and `:3360` records type/count/flags.
`drivers/apple-agx/mesa/dxbc/dxbc_bindings.h:51` defines the stride.
The flags are 3, including SHADER_VISIBLE. Other successfully logged heaps do
not contain these addresses in the ordinary half-open interval join.

A39:530 and B41:449 contain successful `measure-touch` rows whose low VA words
are respectively 0x13ba0000 and 0x12680000. They occur near early staged uploads,
well before the first full frame. **They are not full-address owner records:**
`render-admission/umd/src/umd_gpuva_windows.c:893` emits only `(UINT)va` and
success. The full high bits, BO token, size, purpose and allocation handle are
absent. A numerical match cannot prove the touched object is the fault owner.
The 20 MiB descriptor heap must not be labelled the owner simply by using an
inclusive end in the join.

**EXP1941 improves the KMD history substantially.** K41:189-191 records a read
at 0x1112680000, PID1640, graph process0x16, VM1, active fence0x340e8. Capture
succeeded, graph was not uncertain, job was in flight, native leaf was absent,
and all four current logical PTEs were zero. The history contains:

- sequence47051, QPC5640623728: `invalidated-logical-owner`, allocation
  **0xffffe78e1b06da90**, VA0x1112680000, 16 previously valid logical pages,
  segment3;
- sequence47052, QPC5640623733: the same 64 KiB range becomes invalid, status0;
- sequence49204: containing 32 MiB range invalidated with NotifyEviction;
- sequence49942: replacement table initialized with invalid entries covering
  the fault address.

Thus a real logical allocation previously covered the fault address; it is
not merely an address with no known owner. **That historical kernel token is
not a UMD BO identity or proof of ownership at submission.** The history has
11 matches but stores eight, and scans only the last8192 events. It cannot
prove there was never an intervening mapping. The snapshot is CPU graph/shadow
metadata, not a hardware PTE/TLB read. Table-index-only matches at 0x1122000000
must not be joined to the fault VA.

EXP1939's retained history (rc1/fault1939.json; K39:190) still has only invalid
records for the exact VA and containing range. The recovered UMD log authenticates
the process and establishes heap adjacency, but does not add the missing full
allocation lifecycle or submitted reference list. Neither log contains a
`reject-va-history` dump for the fault. The UMD ring is recorded in memory at
`umd_gpuva_windows.c:135`, but `va_dump` is called only at `:1319` on a different
failure path. Likewise, successful submission does not print its complete BO
reference set (`mesa/winsys/agx_win32_gpuva_batch.c:1159` onwards).

**LIKELY:** a stale address or missing reference to a previously backed 64 KiB
object remains the strongest class. The old-owner invalidation and early touch
support it. The descriptor-heap-end relationship raises an independent
heap-relative indexing/pointer candidate. It does not select that candidate:
the G13 fault address has 64-byte granularity (`asahi-linux-asahi/.../regs.rs:483`),
while ordinary heap texture descriptors start at entry offset64
(`dxbc_to_nir.c:2004`). A simple read of the next texture descriptor would
normally point 64 bytes beyond this boundary, not exactly at it. Other reads
or address corruption remain possible. No exact size/index/content admission
rule should be introduced from these traces.

**Still unknown:** which BO/resource/descriptor the GPU intended to read;
its creator and destruction/reuse time; the relation between kernel token
0xffffe78e1b06da90 and UMD tokens; whether that object was in fence0x340e8's
reference set; and which encoded operand produced the address. No evidence
supports claiming a specific descriptor, upload staging texture or constant
uploader owns it. Consequently there is no source-proven B fix in this branch.

## 2. EXP1941 10k: slow progress, then a likely paging wait

**The last Execute receipt marks completion, not entry.**
`agx_d3d12_ddi.cpp:6643` emits it after replay and submit/wait;
`:6674` calls ReplayAndSubmit under the owner lock. Therefore “no execute after
Ready” does not mean the runtime never entered ExecuteCommandLists.
`measure-d3d12-copy-texture` is a recording receipt, whereas the owner receipts
`measure-d3d12-texture-copy` (`agx_d3d12_windows.cpp:2423`) and
`measure-d3d12-staged-upload` (`:2284`) show actual replay progress.

A41:3987 is the last completed Execute, QPC2788941024 =116.205876 s. Afterwards
there are 3732 completed texture-copy and1512 staged-upload receipts, along with
allocation and command recording. The last row, A41:15600, is at156.266305 s:
**40.060429 seconds after the completed Execute**. The workload is doing work,
not spending that entire interval in one stuck wait.

The long gaps are not evidence of pure CPU memcpy cost. For example A41:4069
through4070 costs9.658892 s. The ending `measure-bo-create` attributes
0x935ff5 microseconds (9.658357 s) to GPU VA binding and only459 microseconds
to allocation. The timing-field definition is in `agx_d3d12_windows.cpp:277`.
A41:4265-4266 has another9.056230 s gap. Memory residency/binding dominates
these individual stalls; thousands of copy rows alone do not measure CPU cost.

A41:15599 successfully trims197 slots /0x40620000 bytes (1030.125 MiB).
A41:15600 requests52 allocations, one trim/retry round, and returns
**E_PENDING (0x8000000a)**. `umd_gpuva_windows.c:340` retries OOM after trimming,
`:371` logs the resulting accepted request, `:690` returns the paging fence,
and `mesa/winsys/agx_win32_gpuva.c:279` waits it. `wait_paging_at` at
`umd_gpuva_windows.c:707` calls the blocking CPU synchronization callback;
it logs its completion only afterwards. The source-backed leading explanation
for the final silent interval is a paging-fence wait, possibly followed by
copy/restore/touch work. There is no captured stack or wait-entry fence value,
so the exact last blocked operation is **not proven**.

The 2k control is especially useful: B41:15434 has the same trim size and197
slots, with53 requested allocations. B41:15435 logs successful touch14.324270 s
later, then the first batch refusal arrives another7.876622 s later. EXP1939
also progresses from its final pending MakeResident to touch after14.026490 s.
These observations disprove treating E_PENDING itself as terminal failure and
weaken the indefinite-paging-hang hypothesis. They do not prove 10k would finish.
Its harness killed the process at106 s (UTF-16 done file), leaving roughly
11 s after the last UMD row when aligned to the first adapter-open QPC; the
exact process-start/QPC alignment is not recorded. The final wait could simply
have outlasted that remaining budget. Ready/resize are application milestones,
not completion of all deferred upload replay.

## 3. Why t14 still ends in 0x116 / C0000483

K41:192: three reset attempts, zero successes, all failing at quiesce after
1000/1001/1001 ms. Fence0x340e8 remains active; completed is0x340e7;
LastAborted=0. Before and after the last quiesce:

- compute GPU done pointer6, CPU write/expected done7;
- stamp0x25a76, expected0x340e8;
- event2 not observed, Complete=false, RecoveryPending=true;
- one firmware recovery/resume recorded.

**No firmware retirement was observed.** T14's new recovered completion path
requires queue consumption, not a success stamp: `shared/src/apple_agx_g13_queue_runtime.c:600`
reads done/write, and `:609` leaves retirement false while they differ. This
case never qualifies. Ignoring the stamp is insufficient because the consumer
also remains behind. Setting LastAborted or releasing DMA owners here would
invent completion while ownership is unresolved.

The first reset has no scheduler fault at entry. Later0x10223 is
`scheduler_windows.c:547` marking the failed reset quiesce; it is not evidence
of an RTKit notification-poll failure. Asahi `gpu.rs:1005` marks pending work
errors, clears pending bookkeeping and `:1059` resumes halted firmware; our
`backend_platform_windows.c:2260` follows the bookkeeping/resume sequence.
The receipts do not explain why this queue failed to advance after resume.
No firmware reset or recovery policy change is justified by that gap alone.

`H/cdb-commands-1.txt` is a bounded first round for
`E41/dump/102526-14671-01.dmp`: bugcheck, analysis/current stack, loaded AppleAgx
module names and secondary tags. It was **not executed**. It can authenticate
stack/module/blackbox context, but a kernel minidump is not expected to recover
all UMD resource/descriptor memory. No second round is proposed without a
specific useful result from round1. No whole-pool walk is included.

## Contract, owners and next checkpoint

WDK26100 `d3dukmdt.h:1475-1488` defines accepted allocation count, the paging
fence before GPU use and budget trimming. VidMm owns residency/paging; the UMD
owns complete references and holds until retirement; the KMD owns mappings,
firmware submission and truthful completion. Mesa `agx_batch.c:519` adds BOs
for resource reads; `agx_uniforms.c:14` does so for constant buffers. These
are source contracts, not proof the saved fault's intended operand followed
them. No external code was copied. No initialization, interrupt, DMA protocol,
power, m1n1 or Mu change is proposed; live collection is prohibited. The
submodules are not initialized here. Saved launch contracts remain unchanged.

**The smallest useful B checkpoint needs ownership evidence, not another
unchanged validator run.** There is no defensible production behavior change
to select between an incorrectly encoded address and an omitted residency
reference yet. Disabling trim, retaining every BO, changing heap sizes or
clearing completion stamps could mask the fault or move it back to OOM without
settling the cause. I therefore recommend the README's single receipt-only
exception, explicitly, rather than presenting such a perturbation as a fix.
This recommendation is a design for the hardware owner, not an implemented
candidate or authorization to run from this worktree.

WHY THIS HYPOTHESIS:

1. The 1941 historical owner proves the exact64 KiB range was backed before
   invalidation; the missing link is its UMD identity and submission membership.
2. The repeated descriptor-heap-end relation gives a competing address-generation
   explanation; current successful-submit logs cannot distinguish it.
3. Existing validation produced no violation and the kernel history already
   captured the invalidation. More of those same receipts cannot resolve the gap.

Exact next-package/profile plan:

1. Base the diagnostic package on EXP1941's code and preserve t14, caps, signer,
   firmware and normal GPU-visible recovery. Add only an opt-in, bounded durable
   lifecycle/submission capture: process/context, full VA/length, BO/token/generation,
   KMT allocation identity, create/cache-reuse/free/trim, and each submitted fence's
   full reference set. Record command/encoder bytes and the relevant uniform and
   descriptor snapshots at submission so an encoded address can be traced. Do not
   restrict capture to the two trace VAs or the20 MiB heap size. Capture must state
   truncation and flush before submit; if its bound is reached, the run is
   inconclusive, not an absence-of-reference verdict. Kernel allocation token must
   be joined through a corresponding KMT-handle receipt; do not equate those tokens.
2. This instrumentation is **not implemented or compiled in rc2**. Before a run,
   the owner must implement/test it, pin actual package/manifest hashes, and record
   one preregistered experiment. Reusing1941 unchanged cannot supply this evidence.
   No numeric package version or nonexistent artifact hash is invented here.
3. Keep device DWORDs `Wom1GpuFaultInfoRead=1`, `Wom1PoolHalfSize=1`. Keep
   `APPLE_AGX_UMD_VALIDATE_REFS=1`, `APPLE_AGX_UMD_VALIDATE_REFS_REFUSE=0`,
   `APPLE_AGX_UMD_REFUSALS_ONLY=1`, `APPLE_AGX_UMD_TRACE_SYNC=1`. Enable the new
   capture opt-in under a documented name when implemented; all other env/defaults
   unchanged. Run **2k only**, fresh process, existing gm-run.ps1 with
   `-Tag <new-id> -Api d3d12 -Asteroids 2000 -Fullscreen 0 -Timeout 180 -ValidateRefs 1`.
   The longer harness limit is for evidence collection, not a driver behavior fix.
   Do not precede it with the10k timeout or another load; record starting pool/RAM.
4. If the encoded address resolves to a live object absent from the submitted set,
   fix the reference-producing call path. If present/held but invalidated, follow
   its exact residency reference and paging history. If outside the intended object,
   fix address generation. If it belongs to a destroyed/reused generation, fix
   the descriptor/resource lifetime. These are different changes; choose only the
   one proved by the joined record. Stop receipt-only iterations after this run.
5. Collect app logs/capture, KMD receipts and dump before exact package cleanup;
   use the preserved normal recovery, emergency hidden only under its established
   gate. Success still requires real frames/benchmark completion, no GPU fault,
   no0x116 and a live desktop; merely reaching Ready is not success.

For A alone, extending the existing10k harness deadline could test whether it
progresses after the final wait, but that would not resolve B and is not the
recommended next hardware experiment. A live stack plus paging-fence
requested/current values would definitively classify that wait; current saved
logs cannot.

## Verification and deliverable limit

Existing targeted tests: `test_umd_residency_trim`,
`test_g4_survivable_fault_recovery`, `test_g4_reset_retirement_receipt`,
`test_g4_fault_mapping`, `test_wom1_gpu_fault_receipt`: **14 tests, OK**.
Required baseline (`cd tests && python3 run_against_baseline.py`):

```
ran 496 tests in 214 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The six existing failures: heap-registry-threading import, rejected-device
callback fixture, primary-import alignment, USC-window replay, R105 open
normalization, and win32-screen fail-closed fixture (exact IDs remain in
`tests/known_failures.txt`, unchanged). No additional environment failure.
Only this worktree's `.local/reference` symlink was created for offline tests.
These results do not prove a native WDK build or hardware behavior.

No implementation defect was established, so no production patch or artificial
RED/GREEN regression is claimed. The requested end-to-end root-cause fix remains
undelivered. This analysis and bounded next checkpoint are the task's conditional
no-proven-defect outcome. EXPERIMENTS/CHANGES remain for the integration owner as
the handoff requires. Tandem dispositions are in `H/review-responses.txt`.
