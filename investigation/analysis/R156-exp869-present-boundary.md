# R156 — EXP869 after R155: stable, but no presentation (offline triage, operator Claude)

## Hardware facts (EXP869, package 869 = cfeeab28 = EXP868 + 6d0815f4)

- 600 s checkpoint passed; same boot stable to 948 s; no bugcheck, no current dump;
  `Wom1G3PagingStatus = 0`. R155 removes the EXP868 0x10E/0xB.
- Scanout: no nonzero frame evidence.
- Original-boot UMD log (`.local/experiments/EXP869-r155-pagingwait/original-evidence/original-umd.log`,
  capped per DLL load for non-refusal records; refusals always logged):
  - `reject-seterror fn=DrawIndexedInstanced line=297 hr=E_FAIL` ×1667
  - `reject-seterror fn=ResourceCopyRegion line=1321 hr=E_FAIL` ×959 (Explorer 706, DWM 88, others)
  - `g4-native-map-va-cb hr=0x8000000a` ×12325 — this is E_PENDING, the documented success-pending
    result of MapGpuVirtualAddressCb; `AgxWin32GpuvaBind` waits the returned paging fence
    (`agx_win32_gpuva.c:54-55`). **Not a defect.**
  - `presentation-import` / `presentation-allocate` ×1 each; present-related records 2.

## Attribution so far

- `Resource.cpp:1321` (projected native source) is `if (FAILED(result)) SetError(...)` after the
  blit + `pipe->flush`: the failure is `AgxD3d10WindowsFlushStatus` — i.e. the R149 error
  propagation (38308fa4) reporting a rejected GPUVA batch. The copy itself passed D3D validation.
  `DrawIndexedInstanced line=297` is expected to be the same flush-status path (not yet read).
- KMD `Wom1G4SubmitFailure` v2: first rejection branch 7 (EnvelopeState),
  `STATUS_INVALID_PARAMETER`, submit flags `0x80` (Resubmission), total KMD submit failures **6**.
  The first KMD rejection is the queued-preemption resubmission case that R154 `4ab20a46` fixes
  (not yet in any package). 6 KMD failures cannot explain ~2600 UMD flush failures.
- `Wom1G3CopyQueryFailure` v3 (first only): predicate 53, `leaf-absent`, VA `0x1100540000`
  (USC execution window) len 64 KiB, level 2 index 336, graph root = context root = last SetRoot
  (17 process SetRoots), mapping generation 43047, range validated. So most rejected batches are
  most likely UMD-side copy/staging refusals (QUERY) on USC-window BOs, rolled back before any KMD
  submit.

## Open questions (next offline step)

1. Why is the native leaf for a mapped USC-window BO absent at QUERY time although
   `AgxWin32GpuvaBind` waited the MapGpuVirtualAddress paging fence? Candidates: the canonical
   BO was evicted/re-placed (GPU_PHYSICAL update to a non-representable system placement,
   unpublished → absent), the USC window range is not covered by the local graph path after R149,
   or the QUERY targets the staging VA rather than the canonical one. The first-only receipt cannot
   distinguish them; a per-predicate counter + last-failure record (not first-only) would.
2. Presentation: only 2 present records; determine whether DWM ever reaches Present/Blt with a
   rendered primary, after the batch refusals are removed.
3. Collector: `collect.ps1` treats an already-stopped `EXP801DxgBoot` session (512 MiB ETL full
   at ~2.5 min) as a stop failure; accept "not found" as stopped and raise the ETL size so it
   covers the whole window.

## Suggested next experiments (one variable each)

- EXP870: `4ab20a46` (queued preemption resubmission) — addresses the first KMD rejection.
- Offline R157: QUERY failure histogram receipt + USC leaf-absent root cause.
