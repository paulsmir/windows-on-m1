# RC25 canonical CPU mappings — implementation plan

Spec: `.local/handoff/codex/rc25/TASK.md` (shared handoff).
Execution: inline, under the user's end-to-end offline authorization.

Goal: remove T13 private staging for CPU-facing D3D12 BOs without exposing
scattered backing or allowing trim to invalidate a live CPU pointer.

## Contract and source inspection

T13 (`umd_win32_screen.c:787,849`) selects native CPU-invisible canonical
storage plus VirtualAlloc/write-watch. It protects native placement, cached
CPU access, persistent pointers and dirty-range synchronization; idle private
maps may survive canonical eviction (`umd_residency_trim.h:25`). It is not an
intrinsic UMA requirement. RC22 saved accounting identifies 443.625 MiB of
staging, including 428.125 MiB mapped. C70/C80 staging textures have separate
format/layout roles and remain necessary.

The existing SystemDirect path restores residency and waits before LockCb
(`umd_gpuva_windows.c:1559,1605`, `umd_win32_screen.c:1217`); Transition protects
the interval until Mapped pins it through successful Unlock. Direct transfers
are skipped (`umd_gpuva_windows.c:1476`). The winsys binds GPUVA before mapping
(`agx_win32_asahi_bo.c:470`). D3D12 MapHeap retains its native pointer.

KMD `allocation_windows.c:515-551` records hardware refusals for CPU-visible
local-only allocations. Keep the admitted pool-preferred/aperture-capable
shape. A direct CPU pointer is usable only if every page of the canonical
allocation is proven in segment 3 after Lock; fail a map on aperture placement,
without inventing native mappings. This is bounded admission, not a promise
that every working set fits the pool. The existing native query authenticates
process/device/context/allocation, full extent and original/native identity.

Pinned WDK `d3dukmdt.h:545-577`, `d3dumddi.h:3931-3941` supply Lock/Lock2 ABI;
`d3dkmddi.h:3804-3830` supplies CPU/cache allocation flags. No reserved field
or unsupported local-only allocation policy is introduced. Pool mappings are
WC (`pool_windows.c:279,887,985`). Readback remains WC: there is no coherent
cached-only pool here, so no conflicting cached alias will be created.
Asahi `gem.rs:160-195` separately chooses kernel cached/user WC or WRITEBACK;
Mesa `agx_pipe.c:983,1026,1031` maps the BO and retains it across transfer unmap.
That does not establish Windows pool cache coherence for a cached alias.

RC24's broker/firmware analysis establishes the pool-only constraint; this
change neither grants raw sysmem nor changes m1n1/Mu. Current submodules are
not initialized here and no other worktree or live hardware will be read.
UMD owns CPU pointer/lock and residency references; VidMm owns placement and
paging; KMD owns authenticated pool proof. Initialization, interrupts, DMA,
power, broker and recovery ownership remain unchanged. No external code copied.

## Steps and review focus

- [x] Add a v2 pool-only native query regression; retain v1 compatibility and
  reject missing/interior-aperture/foreign/stale backing and malformed versions.
  Extend the existing 64-byte ABI using v2 plus a required pool-only input flag.
- [x] Add the D3D12 `APPLE_AGX_D3D12_CPU_POOL_MAP=1` opt-in before screen creation.
  Leave T13 as default pending hardware admission/pressure validation. With
  opt-in, unshared CPU-facing class BOs allocate one SystemDirect canonical BO.
  Preserve GPU-only/shared/borrowed/C75c policies and all protected lines.
- [x] Reuse protected restore/wait/Lock, then issue the v2 proof before pointer
  publication. Use one existing CPU-only scheduling touch after every Lock, including an
  apparently ready old graph, to publish any Lock-induced placement change.
  Failed proof unlocks without exposing a pointer; failed cleanup retains lock,
  residency and owner and makes the device terminal. No successful null/renamed
  lock may be forgotten. Test callback failure/uncertain touch separately.
- [x] Execute actual-source tests for policy, mapping, pool query, trim/restore,
  direct no-copy submission, fallback staging, persistent heap bookkeeping and
  KMD headers. Observe RED then GREEN, run the full prescribed baseline to
  BASELINE_OK. Obtain final code review, commit small changes, write REPORT.

Hardware checkpoint (integration only): exact HEAD plus fix, pinned WDK build,
Wom1GpuFaultInfoRead=1 and Wom1PoolFraction=3, CPU_POOL_MAP=1. First persistent
UPLOAD -> GPU copy -> READBACK plus trim/unmap/remap and disjoint placed ranges.
Then GravityMark 2k, then 10k. Compare private bytes, staging bytes, available
physical RAM, longest paging wait and scene presents/min. Refusal is not success;
record it with native reason and retained residency. Preserve known-good recovery,
collect evidence, ordinary exact-package GPU-visible cleanup. No run here.

Unknown: native compilation, Windows lock/placement under pressure, WC readback
performance, actual retained pool demand, net RAM saving and scene throughput.
Pinned CPU maps reduce available trim targets; they must remain honestly charged.


## Implemented path and limits

Driver-relative evidence in the final source:

- `mesa/winsys/agx_d3d12_windows.cpp:514` reads the strict exact-1 switch before
  native screen initialization. Unset/0/other values preserve T13; setting the
  new switch overrides T13's staging selection, including when the old switch
  is 0. This new setting is per D3D12 owner, not a global D3D11 change.
- `render-admission/umd/src/umd_win32_screen.c:784,790,851` selects SystemDirect
  and bypasses the actual VirtualAlloc block. CPU-facing General/Shader/Encoder
  BOs retain the existing admitted allocation request; GPU-only and borrowed
  paths stay separate. Map reserves Transition at :1213, restores residency
  at :1222, takes Lock, checks identity and the full native pool at :1269, then
  publishes Mapped at :1319. Successful Unmap clears the lock at :1382.
- `render-admission/umd/src/umd_gpuva_windows.c:1623` validates current ownership
  under the slot lock. At :1652 it waits one existing CPU-only touch to publish
  deferred PTEs before the v2 query. A prior graph is not accepted merely because
  it still names pool pages. Clean not-pool/absent-leaf refusals preserve OOM;
  callback failures preserve HRESULT; uncertain scheduling is terminal.
- `render-admission/src/gpuva_g3_windows.c:1391,1435,1454` accepts v1 unchanged,
  requires exact v2 POOL_ONLY, authenticates the whole real description extent,
  and requires segment 3 on every original/native page. No page-table mutation
  or raw-page authorization is introduced. The 64-byte ABI and native coverage,
  rights, allocation-offset and translation checks are preserved.
- `render-admission/umd/src/umd_win32_screen.c:23,1291,1382` retains the exact
  callback lock handle when cleanup fails and clears it only after successful
  unlock. This includes an unexpected renamed handle. Terminal state blocks
  canonical destruction (:1430) and future work; retained maps remain ineligible
  for trim (`umd_residency_trim.h:38`).
- `render-admission/umd/src/umd_gpuva_windows.c:1445,1476` excludes SystemDirect
  from upload/download; writes already target canonical bytes. The ordinary
  consumer BO reference set, submission holds and rc6 query/rc7 spill dependencies
  remain mandatory. Unmapped slots can still be trimmed and restored. rc11's
  clean residency refusal versus uncertain accepted-work distinction remains.

API UnmapHeap retains the native pointer by existing design. Therefore pool
pin lifetime may exceed API map depth: a cached owner pointer is still live.
The winsys cache also retains maps; its 64-MiB storage cap and existing pressure
reclaim remain intact (`agx_win32_asahi_bo.c:277-365`). This is honest residency
accounting, not an assertion that mapped storage is pageable.

T13 also accelerated CPU reads: the source records about 80 MB/s for old VidMm
maps versus 2.5 GB/s private cached memory (`umd_internal.h:77`). RC25 does not
fabricate a cached pool alias. WC upload is appropriate for the existing pool;
readback may be slower. Descriptor heaps consequently keep their existing
separate cached shadow (RC22's private-map reuse query returns false), restoring
up to 60.75 MiB in the saved EXP1957 creation set. Removing 443.625 MiB staging
would therefore yield about 382.875 MiB net process commitment reduction for
that same live set before other lifetime/placement changes. This arithmetic is
not a measured saving or a prediction of physical headroom. C70 upload/C80
readback texture resources and command-encoder shadows serve other purposes
and are not removed. The 10k working set may still exceed pool capacity.

The old source fails the full allocation replay because it allocates private
memory in the selected direct mode. Pool-proof, publication-before-map and exact
failed-unlock regressions likewise have saved RED evidence. Independent review
found the renamed-lock retention issue; its fix was replayed RED then GREEN.
The complete baseline and exact commits are recorded in the handoff REPORT.
No hardware, native package build, experiment/change ledger edit, or default
promotion is part of this result. Current GPU_CURRENT_STATE contains no T13
section; its stale compact state was supplemented with the explicit T13 design
and CHANGES rows requested by this task, not a full-ledger reread.


Final host verification: 77 focused tests pass. The required full baseline is:

```text
ran 532 tests in 229 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The six selected pre-existing failures are unchanged; no exception was added.
Both source and callback/graph replays were independently reviewed; final
hardware and native-build gates remain as listed above. Implementation commits
are `7ec3fdfd` (KMD v2 pool proof) and `ae823f2c` (UMD opt-in and regressions).
