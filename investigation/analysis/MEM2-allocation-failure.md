# MEM2: offline allocation-failure containment and bounded memory reduction

This is a CPU failure-containment candidate, with two separately reviewable
memory reductions. It is not hardware validation or proof that GravityMark
fits. The handoff is `.local/handoff/codex/mem2/REPORT.md` in the shared local
root. No hardware, guest, builder, network, package, or other-worktree action
was performed. Integration owns the experiment and change ledgers.

## Evidence and contracts

The saved EXP1925/27/28 analyses identify three successive unchecked consumers:
resource transition, compute batch, then query BO construction. Those fixes
were already present at the starting commit `3eb39f69b41184469b5c008094a096e5f678fcd0`.
New work audits the consumer class instead of waiting for another crash RVA.

Primary sources inspected:

- Pinned Mesa `src/gallium/drivers/asahi/{agx_state,agx_pipe,agx_batch,agx_blit,
  agx_query,agx_streamout,agx_uniforms}.c`, `agx_state.h`; shared
  `src/asahi/lib/{pool,agx_scratch,agx_linker,agx_bg_eot}.c`; their actual
  prepare-only Windows projection, including `agx_win32_pipeline.inc`.
- Mesa `src/gallium/auxiliary/util/u_upload_mgr.c:331-345` establishes that
  `u_upload_data_ref` returns a reference and replaces the prior output ref.
  The indirect-upload callers must not pass a borrowed count-buffer pointer
  as that output or leak their new reference. Saved compute-blitter bindings
  also retain references; projected setters take a new reference on restore.
- Local SDK `winsdk26100/shared/d3dukmdt.h:1466-1489`: MakeResident can refuse
  even with CantTrimFurther, and its paging fence must complete before GPU use.
  `winsdk26100/um/d3d12umddi.h:2602,2626`: the runtime error callback exists.
  Existing owner/Execute propagation remains the Windows error boundary.
- `mesa/winsys/agx_win32_asahi_bo.c`, `agx_win32_asahi_scene.c`,
  `agx_win32_gpuva_batch.c`, `agx_d3d12_windows.cpp`, and
  `render-admission/umd/src/{umd_gpuva_windows,umd_win32_screen}.c`.
- Saved EXP1925–1928 `Wom1M2PoolDemand` receipts, decoded by the repository's
  v7/v8 decoder, and retained heap-staging streams. Raw roots and exact log
  lines are in `MEMORY_AUDIT.md` and `memory-evidence.json` in the handoff.

Mesa MIT notices and `licenses/MIT` were inspected: modification is permitted
with notice retention. External implementations stay in the prepared tree;
existing notices are preserved. The repository gains projection edits and
regressions, not vendored external implementation files.

UMD/Mesa owns CPU construction, command emission and CPU copy lifetimes. KMD
continues to own residency, paging, DMA and interrupts; m1n1/Mu retain their
existing initialization/power/exposure contracts. No live machine fact is
needed to establish the unchecked CPU uses. Firmware submodules are not
initialized here; no new firmware protocol claim or workaround is introduced.

## Consumer inventory and disposition

[MEM2-allocation-inventory.md](MEM2-allocation-inventory.md) enumerates 144 direct
runtime acquisition/use sites in 13 final projected files, with function and
file:line evidence. The shared handoff additionally contains exhaustive native
winsys, D3D12-owner and UMD-staging inventories (`WINSYS_AUDIT.md`,
`OWNER_AUDIT.md`, `MEMORY_AUDIT.md`). Generated-source SHA-256s are retained in
`projected-sources.json`. This inventory is of runtime allocation/residency
consumers; NIR/compiler allocator internals and unshipped Linux paths are not
represented as tested Windows runtime paths.

| Consumer group | Disposition |
|---|---|
| Pool CPU descriptors and GPU-address uploads | All directly enumerated state/uniform/render pool consumers check the result before use. Fault propagates up encoding calls; allocations stay batch-owned for abort. |
| Encoders and graphics/compute batch acquisition | Missing BO/map/cursor refuses acquisition; all graphics consumers stop. Finalization checks faults after Leave and before Finalize/submit. |
| State/sampler constructors and heaps | Constructors return NULL; sampler heap insertion returns an invalid index without advancing count; uniforms/owner reject it. |
| Scratch, linked programs, BG/EOT and precompiled programs | Failed BO/map/upload unwinds unpublished state; failed objects are not cached. Scratch metadata resets so later valid retry reallocates. |
| Metadata arrays and shader keys | BO-list reallocation preserves old pointer/ownership; writer/scissor/depth/query arrays check growth; shader constructors and cache insertions check allocation. |
| CPU transfer and shadow copy | Existing staging/map guards retained; failed preparation/sync stops CPU access; shadow copy checks both maps and preserves old storage on failure. |
| Geometry/indirect uploads | Resource and pool allocations checked; synthesized/count-buffer references have explicit cleanup, without releasing caller-borrowed references. |
| Compute blits | Callback/constructor/key/cache failures unbind and restore saved state; references are balanced on success and failure. |
| Query and stream output | Existing query OOM guards retained and swept; nested stream-output offset failure rejects the target; timestamp array growth checked. |
| Windows owner create/map/state/bind/draw/dispatch | Existing buffer/texture/PSO guards preserved; newly fixed sampler, heap, stream-output and transfer failures propagate HRESULT/context failure instead of silent success. |
| Native tile/batch construction | Existing allocation/map checks and unwind retained (see winsys inventory); encoder map/current now checked before shadow publication. |
| Disabled disk-cache readback | Unchecked upstream deserialization is unreachable because the Windows screen does not initialize disk_cache. Explicitly classified, not represented as repaired. |

An untouched preallocated shadow slab is a legitimate publish no-op. The audit
initially tightened this incorrectly; existing ShadowPools regression caught
it and that change was withdrawn. Encoding failures must also release matching
compute/emission scopes. Review found and corrected returns that would have
bypassed ComputeLeave or left ActiveEmission pointing at stack storage.

## Memory accounting and reductions

| Saved run | Segment MiB | Peak committed MiB | Peak reserved MiB | Reserve failures |
|---|---:|---:|---:|---:|
| EXP1925 | 2688 | 2505.25 | 2505.25 | 7 |
| EXP1926 | 2688 | 2548.4375 | 2547.875 | 7 |
| EXP1927 | 1344 | 1343 | 2898.8125 | 0 |
| EXP1928 | 1344 | 1344 | 2882.625 | 0 |

These are separate high-water marks. Held includes committed, and reserved
also overlaps committed. EXP1927/28 fail MakeResident at Execute 51/52; their
new-slab and floor-refusal counters are zero. Do not apply EXP1925's
creation-headroom conclusion to these half-segment runs. Retained uploads total
about 1.07 GiB per run, downloads zero: traffic is not retained memory.

Two safe reductions are implemented independently:

1. T13 only creates a merge baseline for a GPU-written slot with an exposed CPU
   mapping. An unmapped private slot has no CPU writer; PrepareCpuMap downloads
   pending GPU output before publishing its first pointer. The baseline becomes
   necessary on a later mapped submission and is then allocated normally.
   Saving: one slot-sized internal copy per qualifying slot. Existing baselines
   and published staging pointers remain intact.
2. The BO cache charges both canonical bytes and any allocated EXP1207 command
   shadow against its existing 64 MiB cap. Every add/remove/reclaim path uses
   the same charge; hit eligibility and in-flight rules stay the same. The
   all-shadow case previously retained 64 MiB canonical plus 64 MiB shadow;
   those two components now total at most 64 MiB within the reusable cache.
   This excludes UMD PrivateStaging, T13 merge shadows and failed-dispose objects.

Canonical plus private staging remains separate real memory, but is not a
second pool reservation. Release-on-D3D12-Unmap is not introduced: the owner
retains native mappings and cached pointers. The observed 128 MiB and 0x730000
failures are already 64 KiB aligned. No page-size change, capacity increase,
C75c disable, or T8-headroom bypass is justified. Protected invariants remain.

## Verification and limits

New tests compile actual projected or owned routines with ASan/UBSan, injecting
successive allocation/map failures. Query, sampler, pool, BO-list, scratch,
library admission, geometry heap, indirect upload, owner preparation, compute
blit and transfer scenarios include success controls and cleanup assertions.
RED controls use original source or a removed-guard mutant where appropriate.
Some owner/linker cases compile admission regions rather than entire Windows
translation units; the tests state those boundaries.

The aggregate result and exact baseline line are in REPORT.md. The full suite
uses the existing known-failure baseline without modifying it. No native
WDK/MSVC build, full-driver allocator sweep, GPU execution, rendered correctness
or workload-fit result is claimed. A k-th sweep proves its exercised paths,
not arbitrary allocation failure anywhere in the compiler or Windows runtime.

Additional source findings requiring their own ownership transaction design:
partial main/read-shadow virtual mapping can retain a new main mapping while
its new backing reference is dropped after the second map fails; canonical
replacement ignores some failed disposal/remap outcomes. The lower-level CPU
map retry can retain Resident after a paging-wait failure, although the ordinary
Asahi caller makes its backend terminal. Exact locations and containment are
recorded in the handoff audits. They are not repaired or hidden by this patch.

## Smallest integration checkpoint

WHY THIS HYPOTHESIS: saved runs repeatedly reach allocation/residency refusal
before a new CPU AV; the unchecked consumers reproduce under injected failure;
unmapped merge baselines and uncharged cache shadows are source-proven copies.

Integration alone should rebuild all native UMD ABIs from these commits and run
one bounded GravityMark D3D12 2k / 1280x720 profile through benchmark setup and
past the saved Execute 51/52 refusal. Use **Wom1GpuFaultInfoRead only** as the
requested diagnostic switch; do not add/retain Wom1PoolHalfSize for this task.
Record the actual segment receipt because EXP1928's half-size numbers are not
a matched comparison to the normal segment. Keep caps, signer, firmware,
C75c/T8/T13 and ordinary GPU-visible recovery contracts fixed. Package hashes,
identity, recovery artifact and exact command belong in integration's BEFORE.

Pass for failure containment: a triggered refusal produces a clean device error
without another CPU AV, encoding after terminal fault, or stale capture scope.
Absence of refusal is inconclusive for the guard. Full workload acceptance also
requires no GPU fault/reset, DWM failure, corrupt pixels or hang. Collect
failure-time live canonical/staging/shadow/cache bytes, private commit and OS
Budget/CurrentUsage where existing collection permits, plus UMD/WER/dump and
start/failure/pre-disable receipts. Stop on first failure and collect before
ordinary exact-package cleanup. Actual GravityMark peak savings and fit remain
unknown until that measurement.
