# RC50: canonical CPU mapping does not pin native placement

The saved EXP1974 evidence identifies a real non-native republish, not a stale
readiness token. This branch corrects the scheduling retry's ownership/error
handling. **It does not implement a permanent native-backing guarantee for
canonical CPU maps and does not solve the observed GravityMark removal.**

Work was offline in `codex/rc50`, starting at `ebccdbd8`. No hardware, guest,
builder, package, presentation, capability, firmware, pool-sizing or recovery
operation was performed. Integration owns merge-time experiment/change ledgers.

## Saved observation and causal decision

Paths in this section are relative to the shared
`.local/experiments/EXP1974-unified-copy/evidence/EXP1974/` directory.
The reproducible decoder and exact SHA-256 identities are in
`.local/handoff/codex/rc50/decode_evidence.py` and `evidence.json`.

| Observation | Evidence |
| --- | --- |
| Arm A initially exposed a mapped, locked SystemDirect 128 MiB canonical allocation, handle `0x40003380`, VA `0x11873a0000`, PID 10124. | `gm1974a-umd.log:3792,3796`; CPU-access flags `0x22f` include Mapped, LockedBase, active lock, owner pointer, SystemDirect and NativeBo. |
| A scheduling touch completed, but the same allocation was rejected at its first native leaf. | `gm1974a-umd.log:21758,21759`, reason 3, followed by rejected batches and first fault at `:21760-21762`. |
| Arm B independently exposes another mapped, locked SystemDirect 128 MiB allocation, handle `0x400035c0`, VA `0x1192c10000`, PID 6472. | `gm1974b-umd.log:3712,3716`; successful map QPC 4,231,288,036. |
| The KMD later accepts five updates covering that exact allocation in segment 0, omitting every one of its 8,192 native groups. | `kmd-receipts-probe-end.txt:118`, Wom1G4OmittedLeaves sequences 34-38, QPC 4,509,448,792 through 4,513,288,129. The 19.9375 MiB prefix, three 32 MiB spans and 12.0625 MiB suffix total 128 MiB. |
| Arm B then rejects the same handle/VA/extent with LeafAbsent. | `gm1974b-umd.log:22217`, QPC 4,944,862,234. |
| The captured segment-0 census has 198 native groups, 17,252 incomplete groups and zero broker-refused groups; no 64 KiB system groups. | `kmd-receipts-probe-end.txt:120`. This includes other processes; it is not a per-allocation PFN dump. |
| Pool allocation failures do not support the proposed chunk-hole cause. | `kmd-receipts-probe-end.txt:159`: zero commit failures, new-slab failures, reserve failures, unaligned placements and reference underflows; 185 chunked slabs succeeded. |

The finite omitted ring no longer contains arm A's updates. Arm B supplies the
stronger exact PID/VA/extent join. Reason 3 in arm A and EXP1971 arm B is consistent
with the same failure class, but the precise earlier mutation in each is unknown.
The EXP1971 prior decode is `.local/handoff/codex/rc46/failure-umd-b.json`, with
the original `gm1971b-umd.log:12080` rejection of a 64 KiB allocation at VA 0x20000.

The recorded system geometry was unsuitable for native publication. It was not
just an unflushed WC write: WC stores do not supply the KMD's logical page-table
updates. Exact PFNs and the reason VidMm selected this placement were not captured.
Do not promote the aggregate census into an exact fragmentation or budget trigger.

## Source contract and ownership

All driver paths below are relative to `drivers/apple-agx/`; line numbers identify
the inspected base unless described as the correction.

1. `render-admission/src/allocation_windows.c:515-554` prefers native pool segment
   3 but admits aperture fallback for CPU-visible allocations. The comments link
   the pool-only CPU-visible admission failure and earlier incompatible system
   placement observations. `:258-302` of `pool_windows.c` verifies each 64 KiB
   physical chunk; a whole large allocation need not be physically contiguous.
2. `render-admission/umd/src/umd_win32_screen.c:1227-1334` restores residency,
   locks, proves the complete current pool placement and then publishes the CPU
   pointer. `umd_residency_trim.h:38` excludes mapped SystemDirect storage from
   **this UMD's** trim. Neither rule constrains all later VidMm placement.
3. The actual lock implementation is `windows/icd/agx_kmt_gpuva_bridge.c:453-470`:
   legacy LockCb syntax is translated to `D3DKMTLock2`. Only LockEntire,
   ReadOnly and WriteOnly hints are accepted; other flags are rejected. Pinned
   WDK `d3dukmdt.h:545-577` defines DonotEvict only for legacy Lock flags and
   reserves every Lock2 bit. Setting DonotEvict here is not a supported fix.
4. `render-admission/umd/src/umd_gpuva_windows.c:732-748` skips MakeResident when
   the persistent reference is already held. WDK `d3dukmdt.h:1475-1490` gives
   MakeResident handles, priorities, budget flags and a paging fence; it has no
   requested-segment or native-geometry field. Adding and dropping another
   reference is not evidence of a placement repair. `map_canonical_as` republishes
   the allocation's current backing; remapping does not manufacture native PFNs.
5. `render-admission/src/gpuva_g3_paging_windows.c:723-768` derives a native leaf
   only from four valid, equally permissioned, same-segment, consecutive 4 KiB
   PTEs with a 16 KiB-aligned base. Otherwise it retires any prior native leaf
   while retaining logical CPU paging metadata. `:826-837` clears native identity
   and invalidates affected readiness entries; `:853-858` commits and records it.
6. `render-admission/src/gpuva_g3_windows.c:1475-1553` authenticates live handles
   and context/process ownership before a cache hit or full proof. The graph
   checks native coverage and the KMD checks every logical owner, offset, access
   and original/native translation. `shared/src/apple_agx_gpuva_g3_graph.c:497`
   invalidates before changing a native leaf. Refusing the migrated allocation
   is correct. A stale cache success would conceal the missing mapping.
7. `shared/include/apple_agx_g3_copy_query_receipt.h:16-24` defines the reasons.
   Graph `:681-690` collapses an interior missing component into TailShort;
   `:712-723` distinguishes missing root edge/table/leaf and unpublished or
   insufficiently writable leaf. Therefore all five topology reasons can get
   one scheduling retry, followed by the same full permission/identity proof.

UMD owns CPU pointer and residency/submission lifetime; VidMm owns placement and
paging; KMD owns the logical/native mapping projection and authenticated proof;
m1n1 owns native grants, publication and TLB acknowledgement. Initialization,
interrupt, DMA, power and recovery owners are unchanged.

Primary implementation comparison: pinned Asahi Linux
`drivers/gpu/drm/asahi/gem.rs:160-211` creates native-sized GEM storage and
`mmu.rs:1191-1216` retains its DMA SG backing. Windows can instead supply arbitrary
4 KiB system PTE geometry. The saved exact m1n1 `075e3980` export in the RC46
handoff, `src/hv_agx_gpuva_v5.c:459-521`, already admits eligible raw guest RAM
but requires a valid aligned native page. Its existence does not convert four
scattered pages to one native leaf. Saved Mu `361a2134` exports/contract in the
RC30/RC46 handoff preserve guest RAM and AGX reserve publication; no firmware
change addresses the Windows placement boundary. No external code was copied.

No new live ADT/register/interrupt or assisted/standalone launch claim is made.
The task forbids hardware and the defect can be evaluated at the saved
placement/publication boundary. Older ledgers were not reread.

## Implemented correction and safety argument

The confirmed retry defect was independent of the unsupported placement:
`touch_device` accepted SubmitCommandCb, then could fail to signal/wait its fence
without marking uncertainty. `native_held_ready` returned false, `submit` returned
0, and `mesa/winsys/agx_win32_gpuva.c:307-310` released the held set. The existing
return-2 path at `:300-305` instead retains ownership when work was accepted.

The correction in `umd_gpuva_windows.c` does the following:

- A refused touch preserves the callback HRESULT. After accepted work, failed
  signal/wait sets DrawTerminal, leaves NextRenderFence unchanged and prevents
  further touches. The Boolean signal/wait helpers do not expose their exact
  HRESULT here, so this case records E_FAIL, not invented memory pressure.
- Submission returns 2 for terminal readiness failure. Its held allocations
  follow the existing uncertain-work retention path. Uploads also stop when
  their prerequisite touch fails; the old caller ignored that result.
- CPU-map proof retains the touch helper's classification. A clean pre-accept
  refusal can successfully Unlock and return an error with no published pointer;
  a later map can retry. Accepted uncertainty and failed Unlock remain terminal.
- A successful, well-formed negative proof may retry exactly once for NoRoot,
  NoTable, LeafAbsent, LeafNotPublished or TailShort with an in-range failed VA.
  Other reasons, callback failure and malformed responses are not scheduling
  opportunities. Both attempts reconstruct and prove the whole held set.
- The prior final-error classification is preserved: only LeafAbsent and
  LeafNotPublished map to E_OUTOFMEMORY. That value does not prove physical OOM.

The code does not add residency references, change backing, migrate a live CPU
pointer, weaken the native proof, fabricate completion or drop a command. A
permanent missing leaf still fails. D3D12 Execute cannot silently return success
after omitting work; the batch-fault path at `agx_win32_gpuva_batch.c:1103-1113`
and `:1220-1226` remains necessary. A one-touch retry was already present in the
failing runs and is not a solution to non-native geometry.

There are no new exported symbols or wire structures. Existing C declarations
and C++ /TP linkage are unchanged. No native WDK/MSVC compile is claimed.

## Verification and remaining gate

Handoff RED logs reproduce accepted-touch misclassification, lost clean-refusal
status, unnecessary identity retry, ignored upload prerequisite and poisoned
clean CPU-map refusal. The topology review regression catches overly narrow
retry policy. GREEN tests execute production touch, native readiness, submit,
CPU-map and restore functions with controlled callback boundaries, plus the
real graph/cache and KMD proof. The latter proves warm pool token -> omitted
system backing -> stale-token rejection -> complete native republish/reproof.
Existing real UpdateLeaf projector tests cover omission and invalidation too.

These are offline ownership and admission tests. They do not execute VidMm's
placement policy, hardware CPU/GPU coherence or a native Windows build. Exact
test counts and the required BASELINE line are recorded in the handoff REPORT.

The unresolved design gate is a supported native-compatible backing owner over
the lifetime of an exposed canonical CPU pointer, or a complete coherent
re-admission architecture. A second private staging copy would change the
task's single-copy requirement. Pool sizing, priorities and retries alone do
not prove the required invariant. No speculative replacement is committed.

## Smallest future hardware checkpoint, owned by integration

WHY THIS HYPOTHESIS: arm B proves a full-range non-native republish after a
successful map; the host replay shows a stale token must reject it; the real
bridge proves Lock2 supplies no requested physical-pinning flag.

First complete the backing-lifetime design and native build gates. With exact
package hashes and preserved known-good recovery, test persistent CPU upload ->
GPU copy/write -> CPU readback under pressure that previously triggered migration.
Require exact bytes and native-ready coverage on every referenced range, with
no stale-token acceptance or pointer substitution. Separately inject a deferred
publication and a refused/accepted-but-uncompleted scheduling touch to verify
the corrected retry/ownership contract. This branch alone does not predict
that the persistent-placement checkpoint will pass.

After that gate, run GravityMark D3D12 FHD 1920x1080, 100k then 200k, canonical
maps enabled/unset, holding firmware, caps, signer, pool and workload policy
constant. Compare 100k with EXP1973's CPU_POOL_MAP=0 result of 1275 / 7.6 FPS,
and the supplied macOS references of 7,700 (100k) and 5,500 (200k). Capture score,
FPS, pixels, memory and full mapping/refusal evidence. Performance references
are comparison targets, not predictions. Collect evidence before integration's
normal exact-package GPU-visible cleanup; emergency GPU-hidden recovery remains
only the existing fallback. Nothing was installed or run by RC50.
