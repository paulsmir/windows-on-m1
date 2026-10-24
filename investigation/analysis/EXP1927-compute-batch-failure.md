# EXP1927: failed compute-batch admission after residency OOM

Offline analysis of the saved EXP1927 run, package 30.0.1927.0, source
`2e3a76faf60186e9ef181cf222e8ed247c5b1d2e`. No hardware, guest, builder,
package build/install, firmware edit, or other-worktree action was performed.
The complete draft ACTUAL and decoded receipts are in the task handoff:
`.local/handoff/codex/exp1927/REPORT.md` under the repository's shared local root.

## Observed boundary

The BEFORE entry at `investigation/EXPERIMENTS.md:61991` tested the opt-in
1344 MiB resident segment with MEM1 BO-cache reclaim. The v8 pool receipt
confirms SegmentBytes=1409286144. Live reservation peak reached
3039625216 bytes (2898.8125 MiB), while committed peak stayed at 21488
64 KiB granules (1343 MiB). ReserveFailures, FloorRefusals, NewSlabFailures,
CommitFailures, ReleaseFailures, BusyRetries and RefUnderflows are all zero.
This passed the earlier allocation-reservation refusal, but not residency
admission or benchmark completion.

Evidence root: `.local/experiments/EXP1927-pool-half/evidence/EXP1927/` in the
shared local tree. These are exact saved lines, not a new live observation:

| File:line | Observation |
| --- | --- |
| `gm1927a-out.txt:32` | 10k benchmark starts at 52.821 s; resize follows at 52.831 s. |
| `gm1927b-out.txt:32` | 2k benchmark starts at 25.740 s; resize follows at 25.753 s. |
| `gm1927a-umd.log:15574` | MakeResident E_OUTOFMEMORY, 51 handles, two trim rounds, 29 idle slots trimmed (0x22ed0000 bytes); remaining NumBytesToTrim=0x23ae999c. |
| `gm1927b-umd.log:15442` | Same boundary, 52 handles, two rounds, 29 slots (0x227e0000 bytes); remaining NumBytesToTrim=0x182b6665. |
| `gm1927a-umd.log:15575` / `gm1927b-umd.log:15443` | Batch Finish refusal, then backend failure, then first context fault 0x10297. |
| `gm1927a-umd.log:15578` / `gm1927b-umd.log:15446` | Another BatchBegin refusal (kind 1, site 0x116) after the context fault. |
| `gm1927a-done.txt:1` / `gm1927b-done.txt:1` (UTF-16) | 92 s TIMEOUT CLOSED / 71 s EXIT, WER=1 each; c0000005 in ARM64 UMD at RVA 0x48184. |

The app output contains no clean resource OOM report this time. There is no
saved CPU minidump/cdb stack or explicit device-removal timestamp. A harness
end time is not a device-removal time.

## CPU failure mechanism and owning layer

The exact ARM64 DLL SHA-256 is
`38e92c2f273e859da8e5fdeea60ca7adeaa751a12457f7b6c5de2f1f3f154a13`.
Disassembly at ImageBase 0x180000000 + RVA 0x48184 shows `str w9, [x8]`.
The enclosing routine tests initialized at batch+0x160, selects compute by
framebuffer width 0xffff, loads the CDM cursor from batch+0x1c10, and builds
0x60000168 or 0x60000178 according to cluster count / GPU generation 13.
This matches the initial CDM barrier in the pinned Mesa
`src/gallium/drivers/asahi/agx_state.c:3273` (especially 3283-3296).
This is instruction/source triangulation, not PDB symbolization. Without
registers/stack, the precise caller and faulting pointer value remain unknown.

The source explains the unsafe continuation:

- `mesa/scripts/build-native-asahi-state.py:6041` already makes an encoder
  allocation failure return a zero encoder. In the saved pre-fix projected
  `agx_batch.c:87-90`, that means a missing CDM BO/cursor.
- `mesa/scripts/native-asahi-batch-lifecycle.py:126` projects batch setup
  so BatchBegin failure faults the context; setup's return type is void.
  The old compute getter still returned the batch unconditionally.
- `mesa/winsys/agx_win32_gpuva_batch.c:272` refuses Begin on a failed backend
  or missing compute encoder. This is the saved final refusal at line 278.
- The saved pre-fix projected `agx_batch.c:663` marks the first context fault
  after rejected submission: `(1 << 16) | 663 == 0x10297`, matching both logs.
- The descriptor-heap bridge and Gallium compute consumers initialized or
  used that unusable batch without checking the acquisition outcome.

The fix belongs in UMD batch acquisition and its consumers. A new KMD policy,
GPU mapping, firmware initialization, IRQ/DMA/power change cannot make the CPU
store through a failed encoder valid.

## Contract and implementation

**WINDOWS CONTRACT:** pinned SDK `shared/d3dukmdt.h:1465-1489` explicitly permits
MakeResident to fail over budget, reports NumBytesToTrim and a paging fence.
The existing retry in `render-admission/umd/src/umd_gpuva_windows.c:338-370`
trims safe idle residency and propagates terminal refusal. The UMD must
propagate that error without subsequent CPU command encoding. No change to
residency priority, caps, page size, signer, or headroom policy is made.

**AGX/ASAHI CONTRACT:** `agx_batch_init_state` needs a valid encoder;
`agx_batch_writes_internal` requires initialized state. Linux GEM ownership
is in reference `asahi-linux-asahi/drivers/gpu/drm/asahi/gem.rs:80-109`, with
fallible mapping. Windows has its own allocation/residency lifecycle and
already replaces the Linux batch lifecycle. The pinned upstream MIT notices
remain in generated sources; this change adds local failure handling.

**TRANSLATION:**

- `native-asahi-batch-lifecycle.py:183`: checked compute getter refuses an
  already-faulted context before acquisition; checks fault state again after
  acquisition; rejects a missing batch, encoder BO or CPU cursor. It leaves
  acquired batch/active ownership available to normal abort/cleanup.
- `native-asahi-batch-lifecycle.py:838`: all five projected call sites stop
  before first batch use (buffer copy, two clears, query copy, dispatch).
  A GPU query failure with a faulted context cannot fall back to CPU access.
- `agx_win32_asahi_scene.c:447`: heap read/write/sampler consumers share a
  checked acquisition, including sampler reacquisition after a flush.
  Read/write loops stop when hazard tracking faults the context. Healthy
  initialization order, resource filtering, mip coverage and sampler
  deduplication remain unchanged. First-fault kind 4 identifies the scene
  bridge; kinds 1-3 retain their existing projected source meanings.

m1n1/Mu submodules are empty in this worktree. Their current implementations
cannot be inspected here without changing the offline environment. The saved
m1n1 contract is 0d3a75b3; no new assertion about its live state is needed for
this CPU defect. KMD retains paging/interrupt ownership and integration retains
exact-package recovery. No workaround is added for either firmware layer.

## Separate GPU failure and verdict

Probe-end receipts contain 246 resumed firmware timeout events, 244 with valid
FAULT_INFO; pre-disable contains 798 / 796. First is DWM PID 1228,
UL1C(2) unmapped read at 0x11024d0100, fence 0x26ee9; last is Explorer PID
5024, VCE(0) unmapped write at 0x11ffe88000, fence 0x2bec6 at probe end and
0x31da6 before disable. No Wom1GpuReset receipt is present. DWM PID changed
1228 -> 10188; Explorer stayed 5024. SSH survived the recorded runs and cleanup.

Wom1G4OmittedLeaves reports 1506 updates / 21614 omitted groups, retained
sequences 1499-1506 with segment 0. Source
`render-admission/src/gpuva_g3_paging_windows.c:748-766` counts incomplete
native groups containing valid PTEs, not ordinary all-invalid unmaps. These
are historical events. None of the retained omission ranges contains either
retained fault address in its recorded process context; allocation base
0x11024d0000 in Explorer is not proof of the earlier DWM fault's mapping.
The snapshot cannot establish which paging operation caused the GPU faults.

**Verdict: REJECTED** for a usable half-size-pool workload. Confirmed only:
smaller cap consumed and lifetime reservation growth bounded early enough to
avoid the old CreateAllocation refusal. MakeResident still exhausted its
trim retry, both apps crashed, and DWM/GPU failures appeared. The pool switch
remains experimental; no further size sweep is justified by these receipts.
MEM1 and the pool switch were bundled, so progress cannot be attributed to
one independently.

## Next checkpoint (integration only)

**WHY THIS HYPOTHESIS:** both logs place the same residency/context refusal
immediately before failed Begin; both WER records name the same CDM barrier
store; compiled pre-fix code reproduces publication/use of an unusable batch.
The smallest change is to stop that failed acquisition and its consumers.

Build all UMD ABIs from this fix and its projection script. Keep the EXP1927
KMD, firmware, caps, signing and full-owner/recovery contracts. For one bounded
2k / 1280x720 discriminator, retain device DWORDs
`Wom1GpuFaultInfoRead=1` and `Wom1PoolHalfSize=1`; verify v8/360-byte pool receipt
SegmentBytes=1409286144 before interpreting the result. Do not change the pool
default or extend the run into a performance benchmark after failure.

Expected: the same residency refusal, if reached, is followed by clean
context/device error and collection/cleanup, with no attempt to encode another
compute batch, no new post-fault Begin, and no CPU access violation. Success
of this guard does not require residency OOM to disappear. New AV/hang or
failure to reach a clean terminal path rejects the fix; absence of a triggered
refusal is inconclusive for the guard. Any GPU fault/DWM restart still rejects
whole-workload stability even if the CPU fix passes.

Save start/failure/pre-disable receipts and UMD/WER; collect a CPU dump if an AV
persists. Stop at the first failure and use ordinary exact-package GPU-visible
recovery. If integration chooses the established UMD hot-swap series instead,
require exact package/UMD identity and its existing restore/cleanup contract;
this offline task did not authorize or run a different recovery procedure.

**WHAT IS STILL UNKNOWN:** the failing CPU address/call stack; hardware
confirmation of the guard and runtime device-error propagation; why the
residency set cannot converge at the half-size cap; precise DWM/Explorer
faulting mapping history; native Windows compilation, other OOM paths such as
sampler-heap growth, rendered output and sustained benchmark performance.
