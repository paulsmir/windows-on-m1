# EXP1934: graphics sampler heap missing from the submission set

## Saved boundary

`investigation/EXPERIMENTS.md:62821` preregistered native coverage of each
held allocation, with `Wom1GpuFaultInfoRead=1` and `Wom1PoolHalfSize=1`.
Despite the handoff task's receipt-only label, that BEFORE includes the
behavioral guard from `238076bd`. Preserve that correction when recording ACTUAL.

Saved evidence root:
`.local/experiments/EXP1934-native-coverage` (shared repository-local storage).
`evidence/rec1934h/kmd-receipts-pre-disable.txt:189` records one valid unmapped
read, PID9688, fence0x153bc, VA0x1112790000, UL1C(1), VM1, 45ms after submit.
The first/latest mapping receipts at lines190–191 agree: captured, graph
process20, JobInFlight1, Uncertain0, CpuOnly0, absent native leaf and four
zero logical PTEs. This differs from EXP1933's valid scattered PTEs.

The dump `evidence/dump/102426-13828-01.dmp` confirms ARM64/eight CPUs,
VIDEO_TDR_FAILURE0x116, parameters `ffffe4884b4642a0`, `fffff8011c48a5c0`,
`ffffffffc0000483`, `3`. The reset receipt at line193 records three failed
quiescence attempts on a later fence0x15700. No saved per-app trace identifies
PID9688's executable or the faulting allocation. **Do not label that VA a
sampler heap.** Eight `reject-native-allocation` rows in the global UMD log
show the guard ran for other processes; they do not explain this fault.

## Source evidence and ownership

- Reference Mesa `src/gallium/drivers/asahi/agx_state.c:2773` allocates the
  sampler heap separately from both pools and the batch handle list.
  `agx_uniforms.c:95` uses it for bindless sampling in shader stages.
  `agx_pipe.c:1395` publishes its VA/count in a render command independently
  of the optional compute command; `agx_batch.c:140` releases the batch heap.
- Reference Asahi Linux `drivers/gpu/drm/asahi/queue/render.rs:888` and `:1332`
  carry that address/count in fragment and vertex parameters. GPU execution
  needs the backing throughout the job. These sources were inspected for
  behavior; no external implementation was copied.
- `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:3038` calls the sampler
  bridge for both graphics and compute. `agx_win32_asahi_scene.c:468` fills
  the native batch heap without adding its BO to the handle bitset.
- Before the fix, `agx_win32_gpuva_batch.c:789` added the sampler BO only
  inside `compute && (...)`, although the render command could name it
  without a CDM command. The compute-only path at line940 already adds it.
- WDK26100 `d3dukmdt.h:1475` defines the explicit allocation list and paging
  fence returned by MakeResident; `d3dumddi.h:3999` defines SubmitCommand,
  which has no full resource allocation list. Embedding a VA does not add
  that allocation to the UMD residency transaction.
- `agx_win32_gpuva.c:192` makes the reference set resident, waits at line209,
  submits at215 and retains it until retirement; failed submit releases it
  at228. `render-admission/umd/src/umd_gpuva_windows.c:616` gives each BO
  CopyHeld/SubmissionHolds. Its native query at1619 checks CopyHeld BOs and
  the submit path invokes it at1736 after uploads. The omitted sampler
  therefore misses this protection, regardless of whether creation-time
  residency happens to leave it accessible.
- `render-admission/umd/include/umd_residency_trim.h:37` excludes submission
  holds from trimming. Mesa owns BO lifetime, the winsys owns the reference
  set, UMD owns residency/copies/holds, VidMm owns placement and paging order,
  KMD owns native publication and runtime execution. No initialization,
  interrupt, power, firmware, hypervisor, or recovery contract is changed.

## Fix and regression

Move the existing sampler `add_bo` into the common render reference set.
Null heaps remain valid, allocation identity deduplicates repeated references,
and the existing `fixed_refs=19` already budgets for the larger mixed batch.
No size/address/content exception or process-wide residency scan is added.

The production-body replay in `tests/g4_mesa_pool_residency_replay.c:274`
places a distinct sampler BO outside both pools and `bo_list`. Before the
fix, the real parser returns Unmapped at the sampler VA, ordinal13, and the
expected successful submission asserts. Afterward it checks exactly one new
resident BO, upload, retention until completion, duplicate identity, no heap,
empty pools, bad identity and submit-failure cleanup. ASan/UBSan are enabled.
The callbacks model VidMm and upload; the host does not execute Windows trim
or GPU reads. Existing native-query and mixed/compute regressions also pass.

Validation: 15 targeted tests passed; required baseline:

```
ran 477 tests in 199 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

## Next hardware checkpoint (Claude only)

WHY THIS HYPOTHESIS:
1. EXP1934's real fault has no logical/native backing despite per-held-BO
   validation, making an omitted reference a concrete candidate.
2. Graphics publishes this independently owned BO but omitted it from exactly
   that validation/hold set. The production replay proves the defect.
3. Compute already uses the correct set; the correction changes only the
   graphics sampler admission/lifetime and preserves firmware/paging policy.

Build the usual package including all UMD ABIs from this branch, retaining
EXP1934's KMD behavior, full-owner/m1n1 profile, signer, caps and recovery.
Keep `Wom1GpuFaultInfoRead=1 Wom1PoolHalfSize=1`; no new switch is needed.
Preregister the new hashes before launching. Run the existing short
GravityMark D3D12 ARM64/windowed1280x720/10K/90s profile; 2K only if the first
run leaves the adapter healthy. Collect app logs, native refusals, first/latest
fault mappings, reset/pool receipts and any dump before exact package cleanup.
Use the established GPU-visible recovery contract, not live restart or an
ordinary armed boot.

Short-checkpoint success: correct output through90s (or earlier natural
completion), no MMU fault/TDR/device removal, DWM and SSH remain alive. A
controlled90s wrapper timeout proves only that interval; benchmark completion
requires a natural completed run and score. A clean native-coverage refusal is containment, not benchmark
success. Any valid unmapped fault or0x116 rejects the fix as sufficient for
this workload; attribute its address anew rather than suppressing admission.

WHAT IS STILL UNKNOWN: whether0x1112790000 was this sampler BO, another
untracked BO, an out-of-range shader access or a stale descriptor; the app
removal time and PID identity; hardware PTE/TLB state; the later reset failure's
cause; native WDK/package compilation, real trim timing, image correctness,
performance and the remaining scattered-page behavior. Host success is not
hardware validation. m1n1/Mu checkouts are uninitialized here and were not
modified or populated; the saved launch contract is the offline reference.
