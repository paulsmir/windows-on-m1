# RC47 — generation-checked native readiness

Offline candidate on `codex/rc47`, based on `dc32d95e`. Scope comes from the
RC47 handoff: retain the complete authenticated native proof, amortize its page
walk over unchanged mappings, audit render-fence waits, and expose all steady
Execute observations. No hardware, guest, builder, firmware/package or other
worktree action is part of this change. Integration owns experiment/change
ledger entries and native Windows validation.

## Observed boundary and sources

EXP1972 arm a (supplied): GravityMark D3D12 FHD, 100k asteroids, CPU_POOL_MAP=0,
ValidateRefs=0; score 543 / 3.2 FPS, median frame about 272 ms, estimated GPU
busy 31 ms. The saved capture is under the shared `.local/experiments/` evidence
root. RC45 reduced readiness transition count but still walked every native
leaf and every 4-KiB logical page of each held allocation on every submission.
The full log has 614 joined Executes, 613 after the first Present, including
four unpresented tails. Of the 613, 543 carry invalid profiler data. A reproducible
`serial > 150` selection has 464 Executes and observed medians of 71.056 ms
(draw readiness), 69.221 ms (query readiness), 58.046 ms (draw copy), and
37.552 ms (draw render fence). These are raw top-N observations, not certified
exact timing; no invalid flag is erased.

Primary source contracts inspected:

- Local WDK26100 `d3dkmddi.h:4693` supplies UpdatePageTable's process, allocation,
  offset, flags, table and VA provenance. `:2081` supplies NotifyEviction and
  paging flags. `:4227` requires PASSIVE_LEVEL for DestroyAllocation, permitting
  the existing G3 mutex in the destruction invalidator.
- WDK `d3dukmdt.h:1475` makes MakeResident's paging fence a prerequisite to use;
  `:1869` defines the monitored fence's CPU mapping as read-only. WinSDK
  `um/winnt.h:6091` implements ARM64 ReadAcquire64; `:8829` provides x64's
  load. WDK `d3dumddi.h:3838` defines synchronous CPU waits.
- Pinned Asahi Linux `drivers/gpu/drm/asahi/mmu.rs:348` unmaps pages and orders
  TLB invalidation before unlinking their lifetime. Pinned Mesa
  `src/gallium/drivers/asahi/agx_pipe.c:651,714,765,790,980` supplies shared/
  persistent rename restrictions and real CPU/GPU hazards; `agx_batch.c:278`
  polls completed slots before blocking. No external implementation was copied.
- Repository `mesa/winsys/agx_win32_gpuva.c:216` assembles the complete held BO
  set, waits residency, submits, then retains references through retirement.
  `render-admission/umd/include/umd_residency_trim.h:38` excludes held,
  transitional and canonically mapped storage from trim.
- `render-admission/src/gpuva_g3_windows.c:1435` authenticates the query and
  expands it to the full KMD description extent before any cache lookup.
  `:1518` retains the existing native-range and logical-page proof on misses.

Paths below are relative to `drivers/apple-agx/` unless qualified. VidMm owns
placement/paging; KMD owns authenticated logical/native translations and
invalidation; UMD owns residency holds and token transport; Mesa owns GPU/CPU
hazards. m1n1, Mu/ACPI, initialization, DMA, interrupts, power and recovery are
unchanged. The saved EXP1972/RC45 contract is the reference; this offline work
makes no new live-state or launch-contract claim and requires no ADT/register
measurement.

## Cache and transport design

`shared/include/apple_agx_g3_native_cache.h` defines a four-way, 1024-bucket
cache per G3 process (163,856 bytes with the supported ABI). An entry contains
an allocation identity key, VA, full extent, access mode and monotonically
issued token. No key is dereferenced. Each lookup performs at most four entry
comparisons; no allocation or page walk occurs on a hit. Replacement/collision
is only a miss. Zero tokens always demand a full proof. Exhausted 64-bit token
space stops issuing hints instead of wrapping. Topology invalidation advances
the invalid-before boundary to the current token clock without scanning pages
or cache entries; new successful proofs receive later tokens.

`shared/include/apple_agx_g3_copy_abi.h:30` introduces single-query version 3,
retaining its 64-byte size: Reserved2 is the input/output proof token and
Reserved may select POOL_ONLY. Legacy v1/v2 retain their always-walk semantics
and zero Reserved2 requirement. Batch version 2 keeps the 32-byte header and
returns hit/full-proof counts in Reserved[0]/Reserved[1]; both must be zero on
input. Version 1 remains available and rejects v3 entries. An older KMD refuses
the new transport; the UMD does not fall back to an assumed Ready result.

`gpuva_g3_windows.c:1475` acquires the live runtime allocation reference;
`:1481` takes the G3 mutex and authenticates the process, graph, context, device,
opened allocation, description extent and requested write permission. Only
then does `:1510` compare the token against KMD-owned state. Pool-only and write
proofs are distinct keys. A miss clears the output token, walks the same native
range and every logical page, and only complete success issues a token at
`:1550`. Not-ready and uncertain state never create a proof. A guessed token
cannot manufacture a matching authenticated current entry.

The UMD stores `NativeReadyToken` in each screen slot under ScreenBufferLock.
`umd_gpuva_windows.c:1861` always sends the held set through the KMD, including
warm hints. The snapshot occurs after upload/canonical replacement. A separate
bounded index array associates results with the original slots; results are
stored only on successful complete batches with matching live handle/VA/hold
state. The KMD full-extent/access key handles any subsequent size or flags
change. A callback failure blocks submission; not-ready retains the existing
single scheduling-touch retry, rebuilding the entire set. This adds no new
shared lock spanning the escape and submission: the existing per-entry mutex
and held-residency lifetime contract remain essential.

For B held allocations and P pages whose proofs are cold/invalidated, ordinary
page-proof work is O(B + P), with bounded comparisons per allocation, instead of
O(all resident pages). Collisions and alternate VA/access proofs can also cause
misses; near-zero page walking is an expectation to measure, not a universal
latency guarantee. One batched kernel transition remains.

## Invalidation safety argument

| Event | Owning hook or exclusion |
|---|---|
| Native IPA, leaf presence or write-permission change | `shared/src/apple_agx_gpuva_g3_graph.c:497` calls the optional invalidator before any mutation/publication. `gpuva_g3_windows.c:1104` finds the exact table shadow and invalidates the four old logical owner keys of that 16-KiB leaf. Idempotent native updates can retain the proof. |
| Logical owner, offset, flags, segment or NativeIpa change with identical native bytes | `gpuva_g3_paging_windows.c:834` invalidates both old and candidate owner identities for the entire affected quartets before swapping ResidentPtes. `gpuva_g3_private.h:178` is the common helper. This catches cases where the native graph's idempotent fast path correctly issues no callback. |
| Evict/trim, page-in, paging move | VidMm updates flow through the same native/logical hooks, including invalid PTEs and hAllocation=NULL. Old owner provenance is taken from ResidentPtes, not the incoming handle. One changed page invalidates neighbouring owners if their native quartet changes. An Evict request that does not alter mapping metadata does not invalidate a still-true proof; residency is reestablished and waited before each query regardless. |
| Parent unlink/relink, alias retarget, root move | Graph `:432` and `:404` invalidate the entire process cache before parent/root mutation. This covers every alias and rollback, without attempting to infer one VA from an aliased table. |
| Table reuse/reset/retirement | Graph `:364` invalidates topology before a registered table changes level. Paging `:110,146` invalidates affected ResidentPte owners before retiring/resetting their shadows, including same-level reset. Parent detach/root relocation callbacks cover topology changes in reuse preparation. |
| Partial publication failure/rollback | Native callbacks invalidate before attempted mutation; successful rollback cannot restore old tokens. Uncertain/poisoned graphs fail before lookup. An acknowledged no-mutation failure may retain an unaffected proof; it cannot publish candidate logical metadata. |
| Canonical allocation replacement, free/address reuse | New identities miss. `allocation_windows.c:693` calls `AdmissionGpuvaG3InvalidateAllocation` before storage release/free; `gpuva_g3_windows.c:1119` invalidates that identity across every process cache under the G3 mutex. Token keys are never used as pointers. |
| UMD Transition or destruction | Snapshot rejects Transition/inactive/unheld/missing-owner slots. Existing holds exclude ordinary destruction. A stale token is only a hint and cannot skip those checks or KMD authentication. |
| Open/close and write rights | Every query reacquires the exact live runtime handle and rechecks opened Device/RuntimeAllocation/ReadOnly and allocation magic. A cache is process-mapping state, not a cached handle authorization. Closing an opener does not invalidate another opener's true mapping proof; reusing its handle cannot bypass authentication. |
| Context teardown | Every hit still requires membership in the process context list and nonclosing/nonpoisoned transport. Detach removes membership. Any private/native teardown mutation also passes the graph callbacks. An unchanged user mapping may remain proven for a different authenticated context of the same process. |
| Process teardown | Process-local cache is freed with the process; graph destruction retires leaves/topology and sets Created false. Future queries must find a live process and created certain graph. No numeric OS PID is used as a cache owner. |
| CPU-only paging process | Existing query rejects it before lookup; its logical-only updates cannot seed readiness tokens. |

The callback is installed once at process creation (`gpuva_g3_windows.c:2493`).
Private native leaves without VidMm shadows cannot carry a successful user
allocation proof; their ordinary scene reuse does not invalidate unrelated user
BOs. Root/parent operations still invalidate globally. The native broker
protocol, grant ownership and publication/TLB ordering are unchanged.

Moving readiness completely into submit was considered. The present G4 private
command ABI carries parsed render/compute metadata and CPU envelope, but not
the entire UMD held list of arbitrary descriptor-reachable BOs. Existing
Submit/BeginJob checks are mandatory but cannot replace this proof. Removing
the escape safely would require a separately specified complete-list ABI and
lifetime protocol; no such assumption is introduced here.

## Render-fence audit and behavior change

The common `wait_object` leaf at `umd_gpuva_windows.c:796` now checks a live,
aligned, read-only render-fence mapping with ReadAcquire64 on `_WIN64` before
profiling or calling Windows. A completed target returns success; missing or
unaligned mapping, terminal device, all-ones value, other/paging object or x86
uses the existing callback. Original argument/callback validation remains.
No interlocked read-modify-write is used on the read-only fence page.

| Current call site | Why the dependency remains |
|---|---|
| `:1017`, touch_device | Wait for scheduled paging/PTE publication before pool/copy/readiness retry. |
| `:1294`, transfer_heap_slot | Protect canonical bytes still read by an older submission before CPU overwrite. |
| `:1334`, transfer_slot | The corresponding legacy/shared staging overwrite hazard. |
| `:1798`, complete_render | Finish pending fence plus GPU-written staging download exactly once. |
| `:1836`, finish_submission | Complete immediate CPU-visible/shared download before later completion signal. |
| `:2127`, wait_render | CPU/API completion and held-set retirement/free/reuse. Its complete_render call can already satisfy the same target; the second callback is redundant. |

Higher callers include completed batch cleanup (`agx_win32_gpuva_batch.c:513`),
finite in-flight slots (`:486`), native poll/wait (`:1276,1282`), first CPU map
(`agx_win32_asahi_bo.c:975`) and owner retirement
(`agx_d3d12_windows.cpp:4043`). Pending completion/download processing remains
before the shortcut, so a signalled fence never skips CPU-visible copy-out.
The x86 path deliberately retains callbacks because a plain 64-bit source load
is not an atomicity proof there. No new exported UMD C/C++ function is added.

Renaming was considered: a ring of older GPU-produced indirect counts cannot
supply the current count; shared/persistent mapped storage cannot be renamed
without preserving externally visible identity and pointers; finite in-flight
slots own GPU VA-bearing resources and scene leases. Those actual dependencies
remain. Only already-satisfied callbacks are removed; there is no evidence that
all 22 waits or all 37.6 ms are recoverable.

## Analyzer and verification

`tools/agx_submission_analyze.py:103` retains raw observed site values separately
from exact/lower-bound values. `:335` reports observed per-Execute medians and
invalid/saturated/censored/absent/missing counts alongside the unchanged exact
frame statistics. Every Execute after a process's first Present participates,
including tails without an invented frame duration. Absent top-N sites and
missing companions are never turned into observed zeroes. Duplicate/orphan
protections remain. The old 51 valid steady frames describe timing validity,
not complete receipt coverage.

Regressions execute the production query, graph mutation, logical UpdateLeaf,
cache helper, UMD transport and completion functions. ASan/UBSan cases cover
cold/hot/stale/forged tokens; 1000 repeated proofs with exactly 16 initial logical
page visits; changed/unchanged native leaves; write and access separation;
parent/root/table changes; old/new/neighbor logical owners; NULL unmap;
allocation reuse across processes; context teardown; bounded collisions and
token exhaustion. Generated-only mutations removing old-owner, new-owner or
quartet invalidation each fail. Fence tests assert actual callback counts,
pending downloads and the read-only page behavior. Analyzer tests cover invalid
flags, all-Execute populations and the saved EXP1972 log.

The prescribed full baseline, targeted logs, review and exact commits are in
handoff `rc47/REPORT.md`. Clang MSVC-target C++ syntax/layout checks pass for
x86, x64 and ARM64 (64-byte query, token offset 56, 32-byte batch). This is not
native WDK/MSVC compilation. The broader G3 broker replay requires the missing
`m1n1_windows` source in this fresh worktree; its environmental failure is
reported separately, never added to known_failures.txt.

## Expected saving, uncertainty and integration checkpoint

The task's readiness medians total approximately 140.3 ms of raw observed CPU
elapsed time per selected Execute/frame population. Removing repeated page
walks targets that dominant component. This is an opportunity estimate, not an
additive certified per-frame saving: cold/changed proofs, collisions, handle
acquisition, locks, transition, scheduling and trace cost remain. The separate
37.6 ms fence bucket contains required waits; only redundant completed-target
callbacks are expected to disappear. No score, FPS or pixel improvement has
been measured by RC47.

WHAT IS STILL UNKNOWN: native Windows compile/link/stack checks; steady-state
hit ratio and invalidation churn at 100k/50k; memory cost under pressure;
placement/coherency and real paging races; actual callback versus GPU-wait cost;
the producer's invalid profiling flag; net frame/score and white-asteroid pixels.

WHY THIS HYPOTHESIS:
1. RC45 and the current proof show per-page native/identity walks despite batched
   transitions; unchanged-generation regression removes those exact visits.
2. EXP1972's direct raw parse places roughly 140 ms in the two readiness sites;
   the new receipt separates hits from proofs to test the mechanism directly.
3. Existing completion bookkeeping can wait the same target twice; the real
   function replay demonstrates that the mapped completed value removes the
   second callback without skipping download or releasing live work.

Integration should first build a matched KMD/UMD with native ARM64/x64/x86 /TP,
link and stack checks and pin hashes. Preserve firmware, caps, signer, expanded
RAM, pool, counted policy and recovery artifact. Verify upload -> GPU copy ->
readback and exact bytes, then force trim/evict/page-in, remap/permission changes,
canonical replacement, context/process recreation. Require cold/changed misses,
warm hits, no stale-content or native-ready refusal, and no device/DWM fault.

Then run GravityMark D3D12 **FHD 1920x1080, 100k, then 50k**, initially retaining
EXP1972 CPU_POOL_MAP=0 and ValidateRefs=0 to isolate readiness. Compare supplied
macOS scores **7,700 / 10,400** and record the actual Windows score/FPS/pixels.
`measure-native-ready-cache` words are `{entry_count, cache_hits, full_proofs}`;
sum them over the same selected Executes and require hits+proofs=count. Collect
all wait/query/path/completion receipts and inspect flags/coverage, memory
pressure, readiness failures and fault evidence. With stable hits, native_ready
wall time should collapse while escape count remains about one per nonempty
submission. If misses dominate, identify invalidation/collision/alias churn
before claiming improvement. Preserve the current integration recovery path;
on regression collect evidence then perform exact-package rollback. RC47 runs
no hardware and does not preregister or claim that checkpoint complete.
