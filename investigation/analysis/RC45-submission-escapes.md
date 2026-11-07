# RC45 — batch readiness and canonical CPU mappings

Offline candidate on `codex/rc45`, base
`116cc65d670c4fcd39e50c37ab91a8b6112c4f06`. Source, host regression, and saved-log
analysis only. Integration must validate the matched KMD/UMD package. No
hardware, guest, builder, package, firmware, recovery or other-worktree action
was performed. Integration owns experiment and change ledgers.

## Evidence and ownership

The supplied EXP1970 capture has 823 Execute/wait/query/path records and 812
Presents. Its SHA-256 is
`22ad787e26431c2c138631ff1dc0270a9909c1b669a654ad3c31f61715596af0`.
The old analyzer joined 819 Executes before the last Present but exported
nothing for the last four. Its 35 complete steady frames measured valid timing,
not receipt coverage: serials 43–818 (776 records) have flags 5, including the
invalid-scope/clock bit. The supplied 55.5 + 52.3 ms readiness, 37.3 ms copy,
and 13.7 ms render-fence medians are therefore hypotheses, not certified exact
timing. RC45 preserves this validity distinction.

The code independently proves repeated kernel transitions: the old
`native_held_ready` sent one 64-byte escape for every non-envelope CopyHeld BO;
`transfer_slot`/`heap_staging_transfer` sent QUERY plus each 64-KiB copy range.
`copy_escape` profiling includes QUERY, so 162 calls does not mean 162 data-copy
chunks. The log lacks token/class copy receipts. Whole-Execute heap-staging
counters for records 201–823 show median 1,032,192 uploaded and 1,179,648
downloaded bytes, each nonzero in 618/623 records. Both directions matter.

Primary sources inspected (repository-relative unless under `.local/reference`):

- WDK26100 `d3dukmdt.h:1475`: MakeResident supplies a paging fence to wait
  before use; `:1466` retains maximum-budget refusal. `d3dumddi.h:3838` defines
  synchronous CPU waits, `:3480` the existing Lock/Unlock callbacks, and
  `:3931` Lock2. `d3dukmdt.h:567` has only reserved Lock2 flag bits; no new
  CPU-cache or ignore-sync flag is invented.
- Mesa `src/gallium/drivers/asahi/agx_pipe.c:765,790,980,1031`: CPU read/write
  hazards, GPU staging blit and transfer lifetime. `agx_query.c:166` allocates
  query BOs as WRITEBACK. `src/asahi/lib/agx_device.h:205` caches BO maps.
- Asahi Linux `drivers/gpu/drm/asahi/gem.rs:160,195`: kernel cached mappings
  versus user WC/WRITEBACK choice. No external implementation was copied.
- `mesa/winsys/agx_win32_gpuva.c:216` constructs the complete referenced set,
  makes it resident, waits paging and retains it through fence retirement.
  UMD `umd_gpuva_windows.c:684` owns CopyHeld/SubmissionHolds;
  `umd_residency_trim.h:34` excludes held/transitioning/canonically mapped slots.
- KMD `gpuva_g3_windows.c:1405` authenticates allocation ownership and whole
  native coverage; `:3162` BeginJob separately revalidates parsed work and pins
  the current graph. Submit parsing does not prove every descriptor-reachable
  allocation's whole backing, so deleting readiness checks is unjustified.

UMD owns CPU pointer lifetime, submission references and staging policy; VidMm
owns residency and placement; KMD owns authenticated graph publication and
validation; native Mesa owns GPU hazards. The existing m1n1/Mu pool-window,
interrupt, power, DMA and recovery contracts are unchanged. Saved EXP1970 is
the workload reference; no new assisted/standalone launch claim is made, and
no live machine-state inspection is needed for this transport/policy change.

## Whole-set native proof

`shared/include/apple_agx_g3_copy_abi.h:41` adds a separate batch magic with a
32-byte fixed header followed by exactly Count existing 64-byte queries.
There are no user pointers or physical addresses. Count is 1..4096, matching
the UMD slot capacity; the maximum payload is 262,176 bytes. Counts, exact
lengths, version, reserved fields and input output-fields are checked before
walking entries. Empty UMD sets need no escape. Old KMDs fail closed because
there is no legacy per-BO fallback or assumed Ready result.

`render-admission/src/callbacks.c:332` routes to
`gpuva_g3_windows.c:1521`. The handler invokes the unchanged single-query
proof for each entry: acquire the runtime handle; authenticate process,
context, device, allocation and write permission; expand logical input extent
to the full KMD allocation extent; inspect every native leaf and every 4-KiB
logical PTE's allocation, offset, permission, original/native IPA and translated
address. Each entry retains the existing handle reference and graph mutex.
Only complete success sets aggregate Ready. A not-ready entry returns its
index/reason; an invalid request or callback identity returns failure.

`umd_gpuva_windows.c:1848` snapshots all non-envelope held slots under the
slot lock into bounded heap storage, after uploads at `:1997`. It sends one
escape. A successful callback reporting not-ready permits exactly one existing
CPU-only touch, then rebuilds/reproves the entire set, including earlier
entries. Callback failure never authorizes the touch retry. No per-allocation
readiness cache, process-epoch inference, stale physical pointer or new
residency assumption is introduced.

| Safety case | Retained proof or exclusion |
|---|---|
| Trim/eviction | SubmissionHolds and CopyHeld exclude UMD trim through retirement; MakeResident/paging precede this proof. Unmapped idle allocations remain trimmable. |
| Move/page-in/deferred translation | Every submission walks current graph/PTE state; the sole scheduling retry reproves all entries. A stale NativeIpa or missing interior leaf fails. |
| Transition | Snapshot refuses Transition, inactive or missing-owner slots before sending anything. |
| Deallocation/foreign handle | Submission holds exclude ordinary destruction; KMD acquires/authenticates the exact runtime allocation and device. |
| Canonical replacement | Upload/replacement finishes before the snapshot. New handles are read each time; stale Ready is never retained. |
| GPU writes | Write permission comes from allocation access flags and is checked on opened allocation, logical pages and native leaves. Data is not read by the metadata query, so it need not wait for unrelated GPU work. |
| DMA envelope | Excluded from native proof as before: it is CPU-parsed. Existing logical envelope checks remain in Submit/BeginJob. |
| Failure | No SubmitCommand callback on failed proof; leaf-absent/not-published remains OOM, other identity/translation/callback failures preserve failure classification. |

The graph mutex is acquired per entry, exactly as with separate escapes.
Batching does not turn this into an atomic graph snapshot or weaken its
existing residency/hold contract. The native page-walk CPU cost remains O(total
allocation pages); only kernel-transition count becomes O(1) per submission
(one ordinary call, at most two with the bounded retry).

## CPU copies: change the storage policy

`mesa/winsys/agx_d3d12_windows.cpp:548` selects existing RC25 DirectCpuPool by
default. Exact `APPLE_AGX_D3D12_CPU_POOL_MAP=0`, with normal HEAP_STAGING policy,
restores private staging. This is fixed at owner creation, never changed under
an already-published pointer. No allocation flag/capability/signer change.

Previously `umd_win32_screen.c:790` overrode SystemDirect for ordinary D3D12
CPU-facing BOs and `:854` allocated separate VirtualAlloc/write-watch memory;
canonical memory was CpuVisible=0. QUERY/upload/download escapes were necessary
to keep those two copies coherent. Lock2 cannot simply map that GPU-only copy.

The selected path uses the same canonical CPU/GPU allocation. The existing
`AdmissionUmdScreenMapBuffer` at `:1161` reserves Transition, restores residency,
waits paging, takes the existing runtime Lock, runs a waited CPU-only touch
and full pool-only proof (`umd_gpuva_windows.c:1654`), then publishes Mapped
and the pointer. Native pointers remain locked and resident until their real
unmap, including cached BO/persistent API mappings. Failed Unlock retains
its exact handle and terminal ownership. Unsupported aperture placement fails
before pointer publication. SystemDirect canonical replacement is refused.

`umd_gpuva_windows.c:1470,1495,1507` excludes SystemDirect from both staging
upload and download. CPU and GPU now address the same bytes, so there is no
staging merge to perform. Existing Mesa read/write hazards and owner retirement
remain. Descriptor/encoder cached CPU shadows still bulk-copy into the mapped
canonical allocation (`agx_win32_asahi_bo.c:99`); that memcpy has no escape.
Tiled/compressed texture staging continues to use existing GPU blits.

Explicit GPU-only BOs remain CPU-map-ineligible; applications upload through
CPU-facing staging and GPU copies. Borrowed/shared/imported presentation
shadows retain their existing per-range transfer path. They cannot be converted
to a private process canonical map without changing cross-owner ownership.
This candidate does not batch that compatibility path or the explicit
CPU_POOL_MAP=0 fallback. It targets ordinary unshared D3D12 frame BOs; it is
not a claim that every application now has zero copy escapes. The provided log
cannot prove which copy calls belong to those exceptional paths. If residual
copy escapes dominate the next run, their identities are needed before a
bounded gather/prevalidate-all/copy transaction is justified.

Tradeoffs must be measured: private mapped staging could survive canonical
trim, whereas mapped canonical storage is pinned; unchanged native BO caching
can extend that pin past API Unmap. Clean OOM/placement refusal remains possible.
Pool views are WC (`pool_windows.c:279,888,986`), so CPU reads can be slower.
Fresh maps still incur residency/Lock/pool-query/touch cost. No cached alias,
new raw mapping, pressure-adaptive migration or automatic private fallback is
claimed. Exact-0 rollback remains available without weakening the new proof.

## Render-fence audit

The only profiler leaf is `umd_gpuva_windows.c:796` (`wait_object`), selecting
render_fence for the render object and paging_fence for the paging object.
Every render-object caller is listed here:

| Line / caller | Why it exists; disposition |
|---|---|
| 1004 / touch_device | Wait for scheduled paging publication; required for copy/pool-map/native-proof retry. |
| 1281 / transfer_heap_slot | Before overwriting canonical bytes still read by an older submission; direct mappings avoid this copy path. |
| 1321 / transfer_slot | Same overwrite hazard for legacy/shared staging; retained. |
| 1785 / complete_render | Finish deferred fence and visible GPU-written staging copy-out; retained. |
| 1823 / finish_submission | Wait before immediate CPU-visible/shared download and later fence signal; naturally avoided when SystemDirect work has no download. |
| 2095 / wait_render | CPU completion and held-set retirement/reuse; retained, including required pending-completion work. |

Higher-level consumers include finite in-flight capacity
(`agx_win32_gpuva_batch.c:486`), native fence finish/poll (`:1276,1282`),
first-map BO retirement (`agx_win32_asahi_bo.c:975`), and owner retirement
(`agx_d3d12_windows.cpp:4075`). Mesa resource hazards are described above.
An already-signalled-fence fast path may avoid redundant callbacks, but the
22-call top-N receipt does not identify which callers qualify. No fence wait
was removed, and no 13.7 ms saving is claimed.

## Analyzer and deterministic checks

`tools/agx_submission_analyze.py:227` now accounts for all Executes, and
`--execute-csv` exports the four trailing records with no invented Present or
frame duration. Coverage reports 823 joined, 819 framed, 4 unpresented, 47
valid, 776 invalid, 818 top-N-censored, no missing/orphan/duplicate companions;
steady-frame counts are 811 joined and 35 valid. Existing exact/lower-bound,
duplicate and missing-data rules remain. Invalid profiler source is not
identified by the shared bit; RC45 does not repair or suppress that flag.

Tests execute real extracted KMD/UMD/allocator/transfer-dispatch code with
ASan/UBSan, not a second implementation of the page proof. Regressions cover
batch bounds/auth/full extent, whole-set retry, replacement/transition/failure,
default and opt-out policy, allocation wire validation, no private allocation
or copy dispatcher for direct storage, persistent map/trim and failed cleanup,
and compact original EXP1970 receipts plus optional full-capture accounting.
Targeted and prescribed full baseline results are in the handoff REPORT.md.
Native WDK/MSVC compile, link and stack gates remain integration work.

## Expected effect and integration-only checkpoint

The deterministic expectation is one native_ready_escape per nonempty held set,
not one per BO; zero staging copy escapes for ordinary direct CPU-facing BOs.
This is a reduction in call count, not a promise that native_ready becomes zero
or that all its previous wall time disappears. The task's nominal ~107.8 ms
readiness + ~37.3 ms copy = ~145.1 ms is an unvalidated upper opportunity bound.
Native page walks, fresh-map cost, remaining shared transfers, WC readback and
residency pressure subtract from it. No score prediction or >=3000 result is
claimed; whole-frame Score/FPS and valid receipts must decide.

WHY THIS HYPOTHESIS:
1. The pre-change UMD emits an escape for every held BO; transport batching
   removes those repeated transitions while running the identical KMD proof.
2. EXP1970 records nonzero upload and download bytes in 618/623 later Executes;
   canonical CPU mappings eliminate the separate storage requiring those copies.
3. RC25's existing full-placement and lifetime checks provide a concrete mapping
   path without inventing Windows or hardware interfaces.

Integration should build a matched KMD/UMD package (including ARM64/x64/x86 /TP
and stack gates) and preserve its current recovery artifact, firmware/caps,
expanded guest RAM and full pool settings. First run persistent upload -> GPU
copy -> readback, placed-range/disjoint CPU+GPU writes, native unmap/remap and
pressure/trim correctness checks. Require exact bytes, no mapped eviction,
clean placement/budget refusal, and no native-coverage/device/DWM failure.
Then run the same GravityMark D3D12 2k workload, followed by 10k if correct.
Compare default with exact CPU_POOL_MAP=0 to isolate mapping cost while keeping
batched readiness in both. Record Score/FPS/pixels, memory pressure, map refusal,
per-Execute completeness, native_ready/copy/pool-map/fence counts, and heap-staging
bytes. Resolve invalid profiler flags before quoting exact per-site savings.
Any regression triggers integration's ordinary evidence-first exact-package
cleanup and stable recovery. This document authorizes no hardware run by RC45.
