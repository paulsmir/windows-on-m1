# ANS current state — DART_TABLES lifted under HYP-ANS-CARVEOUT (offline); WAIT FOR USER

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

1. ANSDART001-R3 (2026-11-06T00:52Z) CONFIRMED with census v2 (9fb711e30): all 26
   L1/L2 pages of dart-dcp (iBoot-locked), dart-disp0, dart-usb0/1 lie outside both
   GUEST_RAM windows (iBoot tables at 0x9fff04000-0x9fff40000 above mem_top; m1n1
   tables in its heap). 18 other DART nodes remain unread (R2: blind reads abort).
   Side finding: RAM-HIGH stage-2 maps [mem_top, 0xa00000000), so the guest CPU can
   write those iBoot tables (m1n1 stage-2 follow-up, not an ANS DMA path).
   Bench: handed to the ANS work by fork 6 at the user's request; guest left running the
   inert recovery profile (launch-normal2.sh, run_uefi 47806).
2. DONE offline: heapblock ceiling primitive (m1n1 94d6a1262) and carveout DMA
   exclusion from the live ADT (m1n1 b89cce170; 13 iBoot carveouts lie inside
   GUEST_RAM); assisted heap sealed at descriptor publish (m1n1 0beb7b552).
   Open: standalone heap seal (its runtime allocates guest firmware from heapblock);
   ANS005B (2026-11-06T02:34Z) CONFIRMED: ANS m1n1 line boots the recovery guest
   with the heap seal, no ceiling panic. DART_TABLES lifted (root d8f6519ba, m1n1 470ef6872) under
   HYP-ANS-CARVEOUT, user decision relayed by fork 6. Old ANS005A text — heapblock ceiling + exact DART_TABLES region publication
   (assisted + standalone), `run_uefi.py` assertion `phys_base >= natural`,
   RED/GREEN in `tests/hv_ans_handoff_memory_test.py` and an allocator test.
3. Encode census result (§3C). If CONFLICT: m1n1 stage-2 unmap + Mu reserved
   memory for those pages first.
4. ANS001-R2 packet sealed offline (qualification 7a3ae8bb..., ANSBUILD006 = ANS005B
   m1n1, dry-run exit 0). Unattended blocker: WinPE evidence lives only in X: and needs
   an operator USB export. Next: host-observable evidence channel (guest NIC is a USB
   Realtek RTL8153, rtucx22arm64.sys; kdcom PL011 is DBG2-only), rebuild/requalify WinPE.
   Plan ANS001-PREP-E: WinPE has inbox RTL8153 + ftp.exe -> export results over FTP to a
   one-shot Mac receiver, then wpeutil reboot; requalify in VM (ANSCOLLECT005).
   Old text: ANS001 (sealed 2026-09 packet, WinPE-only handoff) is still a valid record
   but its m1n1 is pre-merge 2d869eb and its rollback assumptions predate the
   current GPU recovery (expanded RAM, broker-lock m1n1). Rebuild/reseal on the
   merged line before running it; then ANS002 QUERY/CLAIM, ANS003 admin queue,
   ANS004 Identify, READ — each needs the §3 admission GREEN first.
5. Asahi apple.c/sart.c/t8103.dtsi cross-check done (contract §1, §4); re-pin before ANS003.
6. macOS kext RE not completed (contract §6); not required by the current design.

## Unchanged constraints

Never edit/reset/stash the original GPU checkout; commit only with explicit
paths; no push/PR without permission; ledgers use `date -u`; reports in Russian.
