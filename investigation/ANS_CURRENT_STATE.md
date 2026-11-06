# ANS current state — DMA blocker reduced to one read-only census; WAIT FOR USER

Updated 2026-11-05T23:20Z (UTC). No hardware was accessed in this phase. No
Air permission has been given. The single J313 bench belongs to the GPU
sessions; every ANS run needs explicit user approval and an agreed slot.

## Read first after a reset

1. Original checkout `investigation/GPU_CURRENT_STATE.md` (GPU owns the bench).
2. This file, then `investigation/ans/ANS_DMA_ADMISSION_CONTRACT.md` (sources,
   DART/SART + ANS protocol contract, layer ownership, admission design).
3. History before 2026-11: `git show 9c55b6c79:investigation/ANS_CURRENT_STATE.md`
   (ANS001 packet, collector qualification, recovery baseline) — still valid
   as record, not as fresh hardware state.

## Branch

`feature/j313-ans-native-offline` in `/Users/pavel/public_windows/.worktrees/ans-native`
now contains `feature/j313-gpu-acceleration` 8786f51f7 (merge 803593d5e; GPU
branches untouched). m1n1_windows ANS branch merged GPU m1n1 c6d10e04 → ba1cfa1
(no overlapping files). mu ANS branch baea359242 (one clang-21 header fix).
Offline gate `tools/test_ans_offline.py` GREEN after the merge, receipt
`.local/ans-native/tests/receipts/89b20c294c0cf992db2763035ecf5917b89ff44fc171c6c79461e8f99054bf3c.json`
(103 Python cases + ANS/common/Windows/Mu sanitizer suites + hypervisor checks).
Fixes needed only for the new toolchain/volume: clang 21
`-Wunterminated-string-initialization` (test fixture, Mu magic) and a hardlink
test now creating its temp dir on the sealed input's filesystem.

## Main blocker, re-stated from sources

`DART_TABLES=UNOBSERVED` is **not** an unknown ANS IOMMU: t8103 ANS has no DART
(m1n1 `nvme.c` PRP/TCB = physical addresses; only `sart-ans` filters RTKit
buffers). It is the unbounded set of *other* DARTs' table pages that a
mis-authorized ANS write could hit. Two populations:

- m1n1-owned (heapblock/dlmalloc, incl. late xHCI/DCP DARTs): below the guest,
  but heapblock has **no ceiling** → fix offline (ANS005A, design §3A).
- iBoot-retained (TTBRs valid at m1n1 entry, `pt-region-N`): PAs only measurable
  → read-only census ANSDART001 (`tools/ans_dart_census.py`, committed, tested).

Admission rule (design §3): ANS DMA authority ⊆ guest CPU stage-2 write
authority ∩ GUEST_RAM \ reservations, plus "no retained table guest-writable".
No flag is cleared by assertion; the policy still rejects any flagged region.

## Next steps (in order)

1. ANSDART001 — user approved; first slot 2026-11-06T00:12Z FAILED in ADT parsing
   (raw-byte iommu-parent) before any register read; fixed c31f47fcf. Banner of
   that boot already shows disp0 L1 0 at 0x9fff34000 (above mem_top 0x9df708000)
   and L1 1-3 in the m1n1 heap; dart-dcp is locked (unobserved). Needs one new slot
   from fork 6 (bench owner); never start run_uefi from the ANS session.
2. ANS005A offline — heapblock ceiling + exact DART_TABLES region publication
   (assisted + standalone), `run_uefi.py` assertion `phys_base >= natural`,
   RED/GREEN in `tests/hv_ans_handoff_memory_test.py` and an allocator test.
3. Encode census result (§3C). If CONFLICT: m1n1 stage-2 unmap + Mu reserved
   memory for those pages first.
4. ANS001 (sealed 2026-09 packet, WinPE-only handoff) is still a valid record
   but its m1n1 is pre-merge 2d869eb and its rollback assumptions predate the
   current GPU recovery (expanded RAM, broker-lock m1n1). Rebuild/reseal on the
   merged line before running it; then ANS002 QUERY/CLAIM, ANS003 admin queue,
   ANS004 Identify, READ — each needs the §3 admission GREEN first.
5. Asahi apple.c/sart.c/t8103.dtsi cross-check done (contract §1, §4); re-pin before ANS003.
6. macOS kext RE not completed (contract §6); not required by the current design.

## Unchanged constraints

Never edit/reset/stash the original GPU checkout; commit only with explicit
paths; no push/PR without permission; ledgers use `date -u`; reports in Russian.
