# EXP1935: preserve fault-time paging provenance

EXP1935 did not eliminate GravityMark's GPU fault. The saved evidence supports
one diagnostic discriminator before another behavior change. The full ACTUAL
draft, decoded receipts/dump, test logs and next package contract are in
`.local/handoff/codex/exp1935/REPORT.md` in shared local storage.

## Observed boundary

Evidence root: `.local/experiments/EXP1935-sampler-hold` in shared storage.
The pre-disable receipt file is
`evidence/rec1935h/kmd-receipts-pre-disable.txt` (K below), byte-identical to
`evidence/EXP1935/kmd-receipts-probe-end.txt`.

- BEFORE (`investigation/EXPERIMENTS.md:62983`) adds graphics-only sampler
  residency/holds. Package30.0.1935.0, source06f1a6f7, full-owner m1n1
  0d3a75b3; Wom1GpuFaultInfoRead=1 and Wom1PoolHalfSize=1.
- K:188: one valid unmapped read at **0x1113b90000**, UL1C(1), VM1,
  PID10816, fence0x18fc2,40ms after submission. First/FirstValid/Last are
  three views of this same event.
- K:189–190: first/latest locked snapshots agree: graph process0x14,
  root0x9defdc000, table0x9def6c000, generation0xe11b9, lease0xd3b,
  active job, no native leaf, four zero logical PTEs. They cannot identify
  the former allocation or explain why the group is absent.
- K:187,192: a later job0x19305 fails quiescence in two engine resets and
  one adapter reset; all return0xc0000483. Dump
  `evidence/dump/102426-16343-01.dmp` confirms0x116, not0x101.
- The10K run lost SSH; its per-app files are missing locally. The2K attempt
  reports no D3D12 devices and is not a second GPU result. The global trace
  has no PID10816 records. Nine native-allocation refusals in other processes
  do not identify the faulting reference. No benchmark completion or image
  correctness was established. Saved recovery ends PackageAbsent/eight CPUs.

**Verdict:** sampler inclusion is not a sufficient fix. Retain its proven
reference correction; the experiment does not prove that either fault was a
sampler heap. Do not infer another missing BO or alter placement from this
evidence alone. The next question is what paging operations preceded the
zero-logical-PTE state.

## Contract and source evidence

The KMD owns native translation and its fault capture. VidMm supplies the
Windows paging operations; the UMD owns explicit residency/holds. Native16KiB
leaves cover four logical4KiB PTEs, so an adjacent-page change within the group
can matter. Initialization, interrupts, DMA, power and recovery ownership stay
unchanged. This diagnostic does not alter admission or mapping policy.

- Official local WDK26100 `d3dkmddi.h:2081,4693`: UpdatePageTable flags,
  process/allocation/table, index/count and FirstPteVirtualAddress.
- Local Asahi `drivers/gpu/drm/asahi/gpu.rs:1422,1443`: record fault information
  and mark pending work before recovery. No external code is copied.
- `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c:13`: authenticate
  active fence/context/process/VM, try the existing lock and capture the graph.
- `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:38,106,142`:
  existing table clear/retire records have a table and8192-entry range, no VA.
- Same file `:818`: leaf records include process, allocation token, VA,
  table/index/count, flags/status and generation.64KiB entries scale to4KiB
  counts; Repeat validity is counted from one source entry.
- `drivers/apple-agx/render-admission/include/render_admission.h:389`: the
  existing8192-entry ring. `gpuva_g3_private.h:25` owns it under the G3 lock.
- `drivers/apple-agx/render-admission/src/backend_platform_windows.c:2228,3734`:
  first/latest mapping capture precedes firmware resume; publication uses
  `sizeof`, not a hardcoded256-byte length.

m1n1/Mu are uninitialized in this isolated worktree and remain untouched.
The saved EXP1935 launch and rec1935h recovery are the platform reference;
no fresh machine-state measurement or hardware action was performed.

## Delivered diagnostic

`Wom1GpuFaultMapping` and `Wom1GpuFaultMappingLast` become v2/912bytes:
the complete256-byte prefix plus16bytes of counters and eight80-byte events.
The decoder still accepts v1/256bytes and reports its history as unavailable.

After existing authentication/state checks, scan the existing ring under its
existing lock. Match only the authenticated graph process and either an
overlapping VA range or the current table/index range, covering the whole
faulting16KiB group. Retain the newest eight matches, with total scanned,
matched and retained counts, sequence and match basis. Capture historical VA
matches even when the current table is absent. Unknown/busy state stays unknown.

No new allocation, wait, MMIO, handle dereference, paging decision or resource
pin is introduced. The scan is bounded to8192 records and runs only on valid
fault capture. Both first/latest retain the existing publication behavior.

Historical tokens are not current allocation ownership. Table identity can be
reused; VA matches can name older tables. Correlate generation/sequence/QPC.
ValidCount is aggregate across the source update, not the target group's
validity. Recorded status precedes possible MirrorTable failure at
`gpuva_g3_paging_windows.c:1683`, so success is not final publication proof.
No match and truncation are explicitly inconclusive. The existing ULONG
counter's2^32 rollover can make the available history appear partial; no
lifetime-complete-history claim is made.

## Verification

The regression first failed on missing history in v1. The implementation and
related receipt/native-guard/KMD-CRT tests then passed (12 tests). A temporary
mutation retaining the v2 ABI but removing the capture block failed the real
capture's first history assertion; repository source was not changed by it.

The executable fixture exercises production capture/publication with real
graph lookup under ASan/UBSan: identity rejection, adjacent versus disjoint
groups, table-only clear/retire, replaced/missing table, failed update,
invalid index, empty ring, physical ring wrap, newest-first truncation,
first/latest preservation and v1/malformed-input decoding.

Required full command, `cd tests && python3 run_against_baseline.py`, exit0:

```
ran 477 tests in 199 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The six baseline failures are retained and listed in the handoff. No baseline
or protected invariant was changed. Independent read-only review found no
blocking issue; its interpretation/counter limits are recorded above.
WDK/native package compilation and fault-time overhead remain untested.

## Next checkpoint (Claude owns hardware)

WHY THIS HYPOTHESIS:

1. Two successive authenticated fault captures have zero logical PTEs, and
   the behavioral sampler-reference correction did not remove the fault.
2. Existing paging history can distinguish recent invalidation/table clear
   or failed update from absence of retained mapping evidence, but recovery
   can overwrite that history before collection.
3. Freezing the existing records at fault time is smaller and better grounded
   than another guessed residency or placement change.

Use a new KMD package with this diagnostic, normal qualification/native build
and stack gates, updated decoder, and recorded new hashes. Keep the EXP1935
UMD/platform/caps/signer/pool behavior. Preserve m1n1 0d3a75b3 and
`AGX_DEVICE_SWITCHES="Wom1GpuFaultInfoRead Wom1PoolHalfSize"`, both DWORD1
before the ordered full-owner start. No new switch or hot-swap path is needed.

One short GravityMark D3D12 ARM64/10000 asteroids/windowed/90s run suffices.
Do not continue to2K after device failure/reboot merely because SSH returns.
Collect app/global traces, process identity, first/latest mapping, fault,
reset/recovery, omission/pool/allocation-lifetime receipts and dump before
exact-package GPU-visible recovery to Code28/package absent.

Diagnostic success means matching authenticated v2 history is preserved;
it does not mean workload success. A recent all-invalid update or table event
supports the paging/lifetime path; a failed leaf update supports that path.
Use aggregate validity/status with the limits above. Missing/ambiguous history
remains inconclusive; a native-present fault moves the boundary toward
publication/TLB/address reuse. The allocation token is for offline correlation
only. Valid MMU fault/device removal/0x116/hang/corruption still fails the
workload; a healthy90s timeout establishes only that interval.

WHAT IS STILL UNKNOWN: allocation identity/lifetime, why mappings are absent,
complete GPU references, shader addressing, hardware-PTE/TLB state, later
reset quiescence failure, exact application timing, native build/runtime cost,
benchmark stability, correctness and performance. No causal behavior fix is
claimed. This is the one receipt-only run allowed after EXP1935's behavior
change; the following iteration must act on evidence or state precisely why
no correct behavior change is established.
