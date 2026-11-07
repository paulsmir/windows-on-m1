# RC38: EXP1966 stream timeout and invalid completed fence

Offline work in `codex/rc38`, base
`c7091b4fe7cb060fafdc980ada5252b1829ec13f`. The handoff task authorizes source,
regressions and commits, with no guest, hardware, package or other-worktree use.
Integration owns EXPERIMENTS.md and CHANGES.csv. The stream stays opt-in through
`APPLE_AGX_D3D12_GPU_INDIRECT_STREAM=1`; default reason 7/native fallback remains.

## Evidence and what it does not establish

Saved evidence root is shared `.local/experiments/EXP1966-stream-statistics/`.
The analysis outputs listed below are in shared `.local/handoff/codex/rc38/`.

- `evidence/rec1966h/app-logs/gm1966a-umd.log:12862`: the sole counted row is
  path 3, MaxCount 0x960, total 0x88108 us (557320), native 0x1f0e2 us (127202),
  reason 0. These are CPU call times, not GPU execution times. PID 10176.
- `evidence/rec1966h/kmd-receipts-pre-disable.txt:187-188`, decoded using the
  repository's `tools/decode_wom1_gpu_fault.py`: one firmware **Timeout**, fence
  **0x161e**, PID **10176**, 12 ms from submission, halt count 1, resumed once.
  FAULT_INFO was read, raw **0x569faae3117cf7c4**, but VALID is clear. There are
  **zero valid faults**. Neither Wom1GpuFaultMapping nor its Last variant is
  present. `decoded-1966.json` preserves the full decoded receipts.
- Therefore **no valid fault VA, access type or unit can be assigned**, nor can
  the event be attributed to the USC window, output pool, or an unmapped CDM read.
  EXP1964/1965's valid 0x11ac9a0000 fault is not this event.
- The ARM64 PAGEDU64 header independently confirms 8 CPUs, bugcheck **0x119**, P1
  **1**, P2 **0x161d**, P3 **0x161e**, P4 **ffffc808c8158000**. Dump SHA-256 is
  `7a1740f2ce2e02c7487b9365fe811d11471cb80f7f047fb501d89d4998c6ac1c`.
  `dump-header.json` records header-only extraction; no cdb was run.
- `evidence/EXP1966/full.log:3558-3572` reaches the guest's NVMe reset and PSCI
  reset; its tail has no decoded AGX fault or interrupt notification identifying
  a different cause. The later host serial error follows reset, not a proven
  cause of the GPU timeout.

The hardware manifest records source `915f9d7d5f668273424913aa2b55758a49f78610`,
package 30.0.1966.0, m1n1 macho SHA
`452ab29fd81b10dc026605ea6f64830dd1963a1ec2de299dbd1eb3fe97a171fd`;
Mu SHA `1c7d87d6fd4321feee605b5f9440dab6a350bc7a3964fb2c62b34602f565f3e5`.
Use the original manifest for package identity. `experiment-source-comparison.json`
and `completion-source-comparison.json` establish that the scheduler, kernel and
projection match the task base. The notification callback is byte-identical;
backend completion differs only by a later telemetry call, not fence accounting.

## Layer B: demonstrated WDDM contract defect

Microsoft documents 0x119/P1=1 as an invalid fence ID, but does not document P2/P3
semantics on that page. The matching prior/faulted values support the following
source diagnosis; they do not independently identify the exact crashing caller.
[Bugcheck 0x119](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x119---video-scheduler-internal-error).

A known-fence DMA_PAGE_FAULTED (FENCE_INVALID clear) requires the driver to
advance its completed boundary to FaultedFenceId. Error disposition and the
completed scheduling boundary are separate concepts.
[DXGK_PAGE_FAULT_FLAGS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmdt/ne-d3dkmdt-_dxgk_page_fault_flags).

The pre-fix sequence is deterministic:

1. `drivers/apple-agx/render-admission/src/backend_platform_windows.c:3370`
   called `AppleAgxSchedulerAbortActiveFence` for firmware-dropped work. Generic
   abort stores LastAbortedFence and clears ActiveFence but deliberately leaves
   CompletedFence unchanged (`shared/src/apple_agx_scheduler.c:227`).
2. `backend_platform_windows.c:3198-3199` reports DMA_PAGE_FAULTED with the actual
   packet fence. Its flags are zero except a decoded write bit; FENCE_INVALID
   is clear even for a non-MMU firmware error with VA zero. The same callback's
   success arm is DMA_COMPLETED with SubmissionFenceId at `:3237-3238`.
3. `render-admission/src/scheduler_windows.c:318` queries the old CompletedFence
   via `shared/src/apple_agx_scheduler.c:132`. Pending/new preemptions snapshot it
   at shared `:312,352`; Windows serializes that snapshot as
   DmaPreempted.LastCompletedFenceId at `scheduler_windows.c:60`.

With prior 0x161d and faulted 0x161e, the driver can thus report 0x161d **after**
Windows already advanced to 0x161e. This is a confirmed software violation and a
strong explanation of the observed invalid-fence pair. There is no evidence here
of a duplicate DMA_COMPLETED; the transaction already suppresses repeat reports.
The exact runtime stack/query-versus-preemption caller remains unconfirmed.
Optional `cdb-commands-1.txt` is one bounded attribution round for the hardware
owner, with no pool walk. No dump-debugger round has been consumed here.

The fix introduces `AppleAgxSchedulerFaultActiveFence` at shared `:240`: retain
abort metadata, then advance CompletedFence to the known faulted packet. Only
the firmware-dropped completion call switches to this API. Ordinary reset aborts
keep their former semantics. The error notification and context poisoning stay;
the change never turns a dropped draw into DMA_COMPLETED.

WDK26100 `d3dkmddi.h:1063-1078` supplies the page-fault structure and unknown-VA
contract; `winsdk26100/shared/d3dkmdt.h:2309-2329` supplies the flags. Completion
and preemption field meanings are also documented in
[DXGKARGCB_NOTIFY_INTERRUPT_DATA](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_notify_interrupt_data).

The existing backend transaction commits the scheduler once, then local release,
then notification (`backend_platform_windows.c:3368-3510`). Retry after a failed
synchronization does not reapply the fault transition or notify twice. Firmware
recovery still requires every published queue endpoint to retire
(`shared/src/apple_agx_g13_queue_runtime.c:582-614`, especially `done != write`
at `:609`); the backend then carries CompletionDropped (`apple_agx_backend_runtime.c:399`).
No synthetic queue pointer, early release, firmware reset or recovery change is
introduced. RC37's PLAN.md was read; its ChannelError handshake remains its scope.
`RC37-INTEGRATION.md` records the merge boundary.

## Layer A: strongest source discrepancy, candidate and remaining uncertainty

All external paths here are relative to `.local/reference/mesa/` unless stated.
One primary-source pass found these relevant distinctions:

| Question | Evidence and disposition |
|---|---|
| VDM words | `src/asahi/libagx/libagx_dgc.h:273-354` and `src/asahi/genxml/cmdbuf.xml:772-804,910-925` match the existing 20-byte nonindexed / 28-byte indexed records. Production kernel tests use these encoders. No deterministic record-layout error found. |
| Call/return | `cmdbuf.xml:915-917` explicitly calls function-call support tentative. `libagx_dgc.h:419,496` defines return/call helpers, but a search of the complete pinned Asahi/Gallium sources finds **no caller** of either. This is weaker evidence than a deployed path. |
| Proven native linkage | `agx_state.c:3787-3823` and `hk_cmd_buffer.c:838-874` use ordinary stream links; each reserves 0x800 mapped padding and uses 256-byte chunk allocation alignment. `hk_cmd_buffer.h:448` computes the current GPU address from the active chunk's CPU/GPU pair. |
| Honeykrisp GPU writes | `hk_cmd_draw.c:3615-3629` generates robust-index VDM into space reserved by `hk_cs_alloc_for_indirect` (`hk_cmd_buffer.h:650`): inline space in the normal linked stream, **not a call/return side stream**. |
| USC clone | The actual USC bytes and PARAMS position are captured by `native-asahi-indirect-stream.py:99-116`; the kernel clones the template and patches only the native four-halfword load. Input uniform ABI remains `agx_state.c:4420-4451` and `agx_nir_lower_sysvals.c:221-231,405-411`. No CPU-visible shared parameter overwrite was introduced. |
| Output mapping | GPU_ONLY plus LOW_VA selects writable General; low VA is a placement constraint (`mesa/winsys/agx_win32_asahi_bo.c:405-422`). Output BOs remain explicit batch references and are not CPU shadow-published. EXP1966 has no valid fault mapping with which to reject that contract. |
| Coherency/order | `agx_state.c:2998-3049` emits a full CDM barrier after the generator, including USC invalidation (`libagx_dgc.h:372-416`). Windows `agx_win32_gpuva_batch.c:961-974` places compute before render with a dependency. Linux Asahi `drivers/gpu/drm/asahi/queue/mod.rs:787-824` translates the same dependencies to firmware stamp barriers. Honeykrisp `hk_cmd_buffer.c:757-781` selects pre-gfx CDM and `:793-803` begins graphics with USC invalidation. These support ordering, not a proof of VDM fetch-cache behavior. No guessed cache register/bit is added. |

**WHY THIS HYPOTHESIS:** the only path-3 call immediately precedes a Timeout with
no valid memory fault; call/return has only tentative encoding and no native
caller, while both native implementations use plain links. This makes control
flow the strongest concrete discrepancy to remove before another large workload.
Missing mapped tail is also a deterministic source-contract discrepancy, though
not established as the trigger at MaxCount 2400. It is not proof of a cache fault.

The candidate uses an ordinary link into GPU output; the work-item at
`min(count, MaxCount)` writes an eight-byte link back to the instruction after
the outgoing link (`agx_win32_indirect_stream.cl:28-34`, emitter `:118-129,172-175,210`).
Count zero links back immediately. No VDM return stack is assumed. The output
extent is `MaxCount * record_bytes + 8 + 0x800`, aligned to 256 bytes (emitter
`:158-161`), with overflow guards before allocation. Output is still GPU-only;
the padding is mapped, not CPU initialized. No per-record CPU work is added.

The projected encoder retains `windows_chunk` at initial allocation and each
rollover (`native-asahi-indirect-stream.py:57-59,73-75,120-122`). Continuation is
computed from that current chunk, not `batch->vdm.bo`, which remains the original
BO after rollover. Bounds and the 40-bit link address are checked before emission.
Tests execute the production projected allocation and rollover functions.

This removes two source discrepancies, **not a proven hardware root cause**.
A passing combined candidate would not uniquely attribute the old failure to
call/return versus fetch padding. G13 command-fetch/USC visibility, generator
execution, actual residency and application-specific state remain live questions.
The default fallback must remain until those checkpoints pass. No safe hardware
claim or performance improvement follows from host byte tests alone.

The original kernel and integration edits remain MIT. Existing Mesa encoders are
included from the reference, not copied into repository source; the generated
bundle retains its existing MIT notices. Local mesa_clc/asahi_clc generated both
G13 targets; independent regeneration was byte-identical. `kernel-reproducibility.json`
records the exact hashes and `manifest.json` pins input/output dependencies.

## Ownership and launch contract

UMD/Asahi owns stream generation, graphics state, hazards and transient BO
references. KMD/VidMm owns DMA mappings, queue submission, retirement and Windows
fence accounting. Firmware owns execution; the RC37 recovery contract still
requires real retirement. m1n1/Mu own the retained launch/resources/interrupt/
power contract, none of which changes here. No live machine state was read.

Local submodule directories are empty and the exact m1n1 075e3980/Mu361a2134
objects are absent from their local Git databases. No network fetch or access to
another worktree was used to fill that gap. The saved EXP1966 manifest supplies
the launch comparison; no substitute historical source is claimed current.
The source decisions above do not depend on new firmware/register assumptions.

## Smallest next checkpoint (hardware owner only; not run)

First perform native Windows ARM64/x64/WOW64 compile/link and WARP debug-layer
validation of the probe. Record exact candidate/package hashes and preserve the
established ordinary GPU-visible recovery artifact. Keep the EXP1966 expanded-RAM,
m1n1, Mu and full-pool settings; do not introduce a firmware or recovery variable.
The required new named steps are `RC38Z`, `RC38N`, `RC38I`, outside the default suite.

1. A fresh-process native-fallback control: stream variable 0, `only RC38Z RC38N RC38I`.
2. Same candidate/settings, stream variable 1, first **`only RC38Z`**. MaxCount 2,
   count zero: generator writes only the terminal link; an ordinary triangle must
   execute afterward. Require pixels and `RC38Z_COUNT` PASS, and the counted
   receipt **path 3 / reason 0**. This is the smallest discriminator: with count zero correctly consumed, it
   bypasses generated per-draw USC cloning, INDEX_LIST and index-buffer data.
   The ordinary follow-up still consumes native graphics state; incorrect GPU
   count visibility is itself an unexcluded failure cause.
3. Only if that passes, fresh processes **`only RC38N`** and **`only RC38I`**:
   count one, MaxCount two, followed by the same ordinary draw. Require image PASS
   and `_COUNT` PASS: PS atomics must equal twice final drawn pixel coverage.
   An extra inactive draw or missing indirect/direct draw fails the count.
   Require the path-3 receipt; API success or a native fallback is insufficient.

`agx_d3d12_probe.c:1090-1129,1201-1208,1357-1380,1490-1522` implements those steps.
Count storage at 2112 is outside both indexed records. Existing run_list uses a
bounded fence wait. The steps reuse the checked-in C6/C13 shaders; no new shader
compiler/package operation was run offline. The probe covers return-to-caller
control flow and multiplicity; identical zero-base parameters **do not prove
parameter-sensitive USC restoration**, pipeline statistics, or high-count cases.
Those need the RC33/36 semantic checkpoint before claiming general stream safety.

Reject any timeout, device removal, wrong count/pixels, new valid fault or bugcheck.
If zero succeeds and one fails, focus the next experiment on per-draw USC/VDM
consumption and visibility; if zero fails, focus on generator/link visibility.
Do not add speculative flushes solely because a timeout occurred. A later cache
experiment must change one source-supported barrier on this bounded probe.

After the bounded and RC33/36 semantic checkpoints pass, run GravityMark 2k, then
10k in separate processes, comparing path3 receipts, frame metrics, device/DWM
health and fault records. Collect logs/receipts/dump before exact-package cleanup.
A timeout with retained queue ownership still follows rc37's fail-closed rule;
this fence fix is not permission to fabricate retirement. No hardware run was
performed or preregistered by RC38.

## Offline validation and unknowns

The production completion replay was RED at the old completed-boundary assertion
and GREEN after the fix: 66 combinations of preemption timing, metadata, retry
and fence seeds, including wrap; generic reset abort remains unchanged.
The stream replay was RED on CALL rather than ordinary LINK. Final tests cover
literal terminal target words, count clamps, guard bytes, USC patching, statistics,
allocation/ref/launch failures, current-chunk rollover and source pinning. The
probe host replay checks actual record/command/verdict fragments and C6D retention.
Independent code reviews found no blocking defect and record the probe limit.

Affected run: 73 tests, OK. Required baseline:

```text
ran 565 tests in 245 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

WHAT IS STILL UNKNOWN: exact dynamic caller behind 0x119; actual cause of the
12 ms firmware Timeout; G13 execution/cache visibility of the new linked stream;
parameter-sensitive pixels/statistics; native Windows compilation/ABI; high-count
performance/residency; and recovery of work whose queue endpoints never retire.
