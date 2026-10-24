# EXP1930: allocation refusal after the copy-ownership census

The saved EXP1930 evidence confirms the v1 measurement checkpoint, but does
not justify an allocation-policy fix. The first 10K/2K refusals are General
GPU-only AllocateCb requests for 129/128 MiB, HRESULT 0x8007000e. The app reports
OOM at 53.269/16.708 s; the runner kills each at 106 s. The 10K WER event is
PhoneExperienceHost/Xaml c000027b, not an AGX/GravityMark AV.

Evidence root: `.local/experiments/EXP1930-oom-snapshot` (shared local store).
Full ACTUAL draft, decoded receipts and artifact hashes:
`.local/handoff/codex/exp1930/REPORT.md` (shared local store).

## Measured boundary

`evidence/EXP1930/gm1930a-umd.log:4250` and `gm1930b-umd.log:3863`
identify the first refusal before rollback and cache reclamation:

| Logical storage, MiB | 10K first | 10K retry | 2K first | 2K retry |
|---|---:|---:|---:|---:|
| Canonical | 2354.984 | 2324.859 | 2140.734 | 2117.859 |
| Private staging | 675.125 | 645.000 | 588.938 | 566.062 |
| Mapped private staging | 570.750 | 552.438 | 570.750 | 552.438 |
| Unmapped private staging | 104.375 | 92.562 | 18.188 | 13.625 |
| Merge baseline | 0 | 0 | 0 | 0 |

Each snapshot excludes just the transitional request; no copy-held or
unknown-size slot is omitted. The other two 2K snapshots also have no merge
baseline. These are logical spans, not resident physical bytes or process
commit. Do not add them to pool reservations or OS counters.

Both `kmd-receipts-probe-end.txt:160` and
`../rec1930h/kmd-receipts-pre-disable.txt:160` decode as pool v8:
20 floor/reserve refusals, zero commit/new-slab failures, full 2688 MiB segment.
Last failure: 7.25 MiB request, 2339.8125 MiB reserved/committed, 2340 MiB held,
HighMemory clear and LowMemory clear. It is a later failure, not the first
128/129 MiB refusal. No saved GPU fault/reset/recovery receipt or kernel dump.

WHY THIS HYPOTHESIS:

1. Policy refusals, rather than failed slab construction, identify the
   headroom boundary (`render_pool_slab.c:82`, `pool_windows.c:395`).
2. Merge-baseline storage is absent, while most private storage is mapped.
   Revoking those pointers is not a supported memory-pressure remedy.
3. Cache reclamation lowers owned spans but the retry still fails; OS/process
   headroom was not sampled, so a logical-copy sum cannot select a safe fix.

The next measurement distinguishes physical pressure from process commit and
working-set growth beyond the reported private spans. It does not promise a
benchmark memory reduction.

## Source and ownership contract

Paths below are within `drivers/apple-agx/` unless prefixed `.local`.

- `mesa/winsys/agx_win32_asahi_bo.c:342`: safe cache reclamation; allocation
  retries only after actual release. Reference Mesa
  `.local/reference/mesa/src/asahi/lib/agx_bo.c:354` also retries after cache
  eviction. Existing implementation and licenses remain unchanged.
- `render-admission/umd/src/umd_win32_screen.c:794`: AllocateCb; `:831`:
  receipt before cleanup. `.local/reference/winsdk26100/um/d3dumddi.h:4303`
  defines its HRESULT callback contract. Preserve original result and handles.
- `render-admission/umd/src/umd_gpuva_windows.c:1027`: mapped GPU-written
  staging may require a merge baseline; logical allocation alone does not
  establish whether one exists.
- `.local/reference/winsdk26100/um/sysinfoapi.h:66` and `:86`: the memory
  status structure and API. `.local/reference/winsdk26100/um/psapi.h:482`,
  `:522`, `:120`: process counters, API declaration and K32 alias.
- `render-admission/umd/src/umd_runtime_device.c:213`: diagnostics preserve
  LastError, accept at most 16 words, and retain measure-* rows.

UMD owns CPU copies, pointer lifetimes and these CPU queries; KMD/VidMm owns
canonical backing/headroom/residency. Existing DMA, interrupts, power,
initialization, firmware and recovery ownership is unchanged. No new hardware
protocol is proposed, so no live machine or m1n1/Mu change is needed. Windows
and Asahi cache/retry behavior remains intact; no external code was copied.

## V2 wire contract

The existing bounded collector at `umd_win32_screen.c:603` emits v2, retaining
all v1 header/storage fields. V2 adds two mandatory 16-dword rows. Identity
words 0–5 are version, OwnerCookie low/high, token low/high and sequence.
For both new rows, word 6 is Boolean validity, word 7 is the Win32 error on
failure (zero on success). Words 8–15 are four little-endian 64-bit values:

| Row | Values in order |
|---|---|
| measure-memory-system | ullAvailPhys, ullTotalPhys, ullAvailPageFile, ullTotalPageFile |
| measure-memory-process | PrivateUsage, WorkingSetSize, PeakWorkingSetSize, PeakPagefileUsage |

Failed queries emit zero payload words but decode as unavailable, never as
zero headroom. Queries are independent, happen after slot unlock, before
receipt I/O/rollback/retry, and preserve caller LastError. Four snapshots per
device bound queries to eight and rows to 24. No allocation or GPU call is
added. Stack storage captures values before diagnostics can perturb them.
These are adjacent observations, not an atomic snapshot with the KMD event.

GlobalMemoryStatusEx page-file fields describe available/limit commit for the
calling process, not disk page-file size. PrivateUsage is process commit;
working set includes shared pages; peaks are historical. Do not derive an
exact other-owner footprint by subtracting overlapping counters.

`tools/decode_umd_memory.py` accepts complete v1 and v2 groups; v1 returns
null OS samples. It rejects incomplete v2, mixed versions, duplicate rows
and inconsistent query validity. Decode saved traces offline with:

```
python3 tools/decode_umd_memory.py saved-umd.log
```

## Verification and smallest checkpoint

The actual collector/rollback host harness fails on the old four-row
collector, then passes on v2. It covers >4 GiB values, query failure with
poisoned output, LastError, budget/lock/error cleanup, legacy decoding and
reordered/interleaved records. Twelve focused tests pass. Required baseline:

```
cd tests && python3 run_against_baseline.py
ran 473 tests in 196 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Claude must compile/link the native ARM64/x64/x86 UMDs and record exact new
package hashes. Host tests are not Windows ABI execution. No new device
switch: retain Wom1GpuFaultInfoRead=1; Wom1PoolHalfSize absent/0; same full-owner
profile, firmware, signer, caps and ordinary UMD trace as EXP1930.

Smallest run: same bounded ARM64 GravityMark D3D12 1280x720 10K workload.
Expected: at the first OOM and retry, six complete v2 rows per snapshot with
both OS queries valid. Existing OOM is expected for a receipt-only candidate.
Missing/malformed rows, stale v1, query failure or no reproduced refusal are
an incomplete/inconclusive measurement. New AV/removal/reset/hang fails
behavioral equivalence. Collect traces/KMD receipts first, then use unchanged
exact-package GPU-visible recovery and verify package absence. No hardware
was launched by this offline task.

WHAT IS STILL UNKNOWN: physical occupancy of private spans; OS/process
headroom at the original failures; pressure from other processes; a safe
memory reduction sufficient for GravityMark; native 32-bit execution and
hardware behavior of the new queries; benchmark completion and correctness.
