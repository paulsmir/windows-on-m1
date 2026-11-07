# RC63: the GravityMark stream hang and why no TDR recovers it

Offline branch `codex/rc63`, base `143924a2` (integration after EXP1985).
No hardware, guest, cdb, other worktree or experiment directory was written.
Evidence (read only): `.local/experiments/EXP1982-paging-containment/evidence/`
`EXP1982/{gm1982b-umd.log, gm1982b-out.txt, gm1982b-done.txt, umd.log,`
`kmd-receipts-probe-end.txt, full.log, procs-final.txt}` and `rec1982h/`.

## 1. What the EXP1982b rows and receipts say

### The reject rows are not GravityMark's

GravityMark is PID 5688 (`gm1982b-umd.log`, every row). Its own log has no
`reject-native-envelope` and no `reject-seterror` row. All 19 rows are in the
merged `EXP1982/umd.log` and belong to other processes (`procs-final.txt`:
1224 dwm, 5196 explorer, 2768 PhoneExperienceHost):

| umd.log line | pid | row | meaning |
|---|---|---|---|
| 5908, 7635, 7917, 8950, 9963, 10488 | 5196, 6156 | `reject-seterror fn=ResourceMap line=1330 hr=0x800700aa` | ERROR_BUSY map refusal, qpc 0.50e9-0.63e9, minutes before GravityMark started (qpc 8.216e9) |
| 7855 | 1224 | `OpenResource line=1144 hr=E_INVALIDARG` | same, before GravityMark |
| 13914 ... 21655 (7 rows) | 1224 | `ResourceCopyRegion line=1792 hr=E_INVALIDARG` | same, before GravityMark (qpc 1.5e9-2.1e9) |
| 43034 | 5196 explorer | `reject-native-envelope ... c0000184 0...` | qpc 10.006e9 = 47 s after GravityMark's last row |
| 43056 | 2768 Phone | same | qpc 15.427e9 = 300 s, the harness timeout |
| 43143, 43150 | 1224 dwm (two threads) | same | qpc 15.767e9 = 314 s, when the harness killed GravityMark (`gm1982b-done.txt`: TIMEOUT KILLED 316 s) |
| 43155 | 1224 dwm | `reject-seterror fn=Flush line=627 hr=E_FAIL` | DWM's Flush failing because of the refused submission above |

(24 MHz QPC: GravityMark's 657 M ticks of log are 27 s, matching its own
16.5 s "Ready" plus the first frame.)

The 14 early `reject-seterror` rows are pre-existing desktop behaviour,
unrelated to the stream. The four `reject-native-envelope` rows are the rc57
preflight (`render-admission/umd/src/umd_gpuva_windows.c:2006-2040`): the
escape itself succeeded (`hr` logged is the UMD's `E_FAIL`), and the KMD put
`STATUS_INVALID_DEVICE_STATE` into `q.Status` with every parse/failure field
zero. That is the early-out of `AdmissionGpuvaG4PreflightEscape`
(`render-admission/src/gpuva_g3_windows.c:5107-5112`): no process, a poisoned
process, CPU-only mappings, no/uncertain graph, **or `adapter->SchedulerFaulted`**,
before any parsing. Three unrelated processes failing the same way at 47, 300
and 314 s is the adapter-wide predicate: **the scheduler stayed faulted from
the GravityMark hang until the end**, and every native submission after it was
refused.

### KMD/firmware receipts for the hung job

Decoded with `tools/decode_wom1_gpu_fault.py` from
`kmd-receipts-probe-end.txt:187-188` (identical keys in
`rec1982h/kmd-receipts-pre-disable.txt`):

- `Wom1GpuFault`: one event, **Timeout**, fence **0xb957**, PID **5688**,
  **25 ms** after submission, halt_count 1, flags resumed+info_read+job_known,
  FAULT_INFO `0x46dfaae319fcf794` **not valid**, valid_faults 0. Same signature
  as EXP1966/1976/1978 (12-26 ms, no valid fault).
- `Wom1FirmwareRecovery`: 1 recovery, kind timeout, fence 0xb957, 25 ms;
  `longest_job_ms` 54 at fence 0x2a2e. The worker publishes this receipt only
  when it leaves (`backend_platform_windows.c:4716-4725`; the only other
  publisher is the reset quiesce at `:5404`), and it updates LongestJobMs just
  before. So the platform worker of 0xb957 **left within 54 ms of the
  submission**: the firmware dropped the job after `AdmissionTransportRecover`
  (`:2293`) and the queue runtime saw it retire.
- **No `Wom1GpuReset`** in either receipt set. `AdmissionResetEngineRecorded`
  writes and flushes it synchronously on every DxgkDdiResetEngine /
  ResetFromTimeout (`scheduler_windows.c:694`, `receipts.c:3099`). So **no TDR
  reset ran in the 300 s of the hang nor during the attempted reboot.**
- `Wom1JobTiming971` is throttled to one export per 2 s; its newest slot is
  fence 0xb829 (all normal, ~0.7 ms jobs), so it does not cover 0xb957.
  `Wom1G4OmittedLeaves` only shows the usual 4 KiB dummy page at 0x10000 per
  process. `Wom1M2PoolDemand`: 3.14 of 3.29 GB committed, no commit failure.

### GravityMark's last rows

`gm1982b-umd.log:15628`: `measure-d3d12-counted-path 3 0xea60 0x675ec 0x2b158 0`
= path 3 (GPU stream), MaxCount 60000, 423 ms in draw_vbo, 176 ms native
encode, reason 0, during replay of the first of five counted calls
(recorded at `:10486-10522`). `:15627` creates a 7 MiB BO in 31.7 ms just
before (60000 x 128-byte USC copies: the VS USC template is 65-128 bytes).
After it: a successful `measure-touch` at `:15635` (qpc 8871584670; the touch
packet completes in fence order, so every earlier GravityMark job had
retired), a `measure-native-ready-cache` at `:15636` (the escape before a
submission), then `measure-make-resident-trimmed hr=E_PENDING` at `:15639`
(qpc 8873937116) and nothing for 290 s. Inference (no per-fence receipt
covers it): the submission after the touch is the render batch that carries
the first stream draw, flushed by the 160x90 compute dispatch recorded between
the first and second counted calls (`:10496`); it is fence 0xb957 and timed out
25 ms later. GravityMark's thread then blocked in the paging-fence wait of
its next MakeResident and never returned.

### What the stream job does that the probes did not

The generated stream (`mesa/winsys/agx_win32_indirect_stream.cl`,
`agx_win32_indirect_stream_emit.h`) is the same code for the probes and for
GravityMark; the differences are the inputs:

| aspect | RC43/RC57/RC59G | GravityMark (EXP1976-1982 logs) | stream code that depends on it |
|---|---|---|---|
| live records | 0-2400 | up to 60000 (count GPU-written, 50k asteroids) | threads `i < count` copy USC, write VDM, add statistics (`.cl:24,44,54`) |
| BaseVertex/StartInstance | 0 in every record (`agx_d3d12_probe.c:1222`) | per asteroid | per-record PARAMS uniform repoint (`.cl:61`, captured at `native-asahi-indirect-stream.py:108`) |
| VS USC template | trivial VS, no bindings | instance data, more bindings: 65-128 B | `usc_bytes` copy, 64 B stride (`emit.h:154,180`) |
| PS state | colour or UAV counter | many textures | unchanged per record (template PPP state) |
| depth | none | D32 + DSV | unchanged per record |
| statistics query | RC57 only, 2400 records | open around the counted draws | 5 atomics per live record on 5 shared addresses (`.cl:39-48`) |
| calls per batch | 1 (RC57M: 7) | 5 counted + ordinary draws, compute between | one CDM launch each (`emit.h:223`) |

No static check, encoding limit or allocation in this path depends on the
record values: the field widths (VDM pipeline 26 bits shr 6, USC uniform 38
bits shr 2, index-list size) and the bounds checks
(`emit.h:146-186`) hold at 60000 x 128 B. The firmware's FAULT_INFO is invalid
in every stream Timeout, so no address or unit names the stalled stage, and
nothing offline proves which of the GravityMark-only inputs above stalls the
GPU. The one class of stream defect that the probes structurally cannot see is
the per-record PARAMS repoint (wrong base vertex/instance only changes which
memory the VS prolog reads, which with zero values is invisible); a wrong
instance base in GravityMark's asteroid VS can feed a data-dependent shader
garbage, which is consistent with a hang without an MMU fault. That is a
labelled hypothesis, not a finding.

## 2. Probe RC63G (commit e3d9932d)

`agx_d3d12_probe.c:7749-8060` (`only RC63G`, `warp RC63G`): one R32 indexed
counted ExecuteIndirect, stride 20, MaxCount 60000 with all 60000 records
live, records and count written by GPU copies into a DEFAULT buffer behind an
INDIRECT_ARGUMENT barrier, inside an open pipeline-statistics query, D32
depth LESS with writes, a PS texture SRV table and static sampler, per-vertex
positions selected by BaseVertexLocation and per-instance texture coordinates
selected by StartInstanceLocation (instance-rate COLOR, R8G8B8A8_UNORM). Record
r draws quad r % 4096 over pixel r % 4096 with instance r. Verdict: every
pixel equals the texel its first record addresses (`AGX_D3D12_RC63G`), and
IA/VS/clip counters equal 6/2 x 60000 with PS nonzero and GS/HS/DS/CS zero
(`AGX_D3D12_RC63G_STATS`). `tests/test_d3d12_rc63_gravitymark_probe.py` runs
the probe's own record/uv/quad/verdict code against a reference rasterizer:
correct D3D12 semantics pass; a dropped base vertex (4095 bad pixels), a
dropped base instance (4095), reversed records (4096) fail, and a lost
2048-record tail fails the statistics. The probe still uses the precompiled
`vs_tex`/`ps_tex` pair: the VS has no resource bindings of its own (no fxc
offline), so a GravityMark-sized VS USC template remains uncovered.

## 3. Why the hang wedges the machine and the TDR never comes

**Proven from source, matching every EXP1982b observation:**

1. After the dropped stream job, `adapter->SchedulerFaulted` is set (shown by
   the preflight refusals above) while **no render job is active** (the worker
   left; Wom1FirmwareRecovery). The flag is sticky: only SchedulerStart
   (`scheduler_windows.c:193`) and a successful engine reset (`:674`) clear it.
2. While faulted, `AdmissionDispatchQueuedWork` returns at once
   (`work_queue_windows.c:107`), so nothing queued ever runs. Since rc59
   `AdmissionCpuQueueSubmit` still **accepts and holds every paging packet**
   (`work_queue_windows.c:35-46`), relying on "the engine reset that dxgkrnl's
   TDR issues for the unfinished fence".
3. Base `AdmissionDdiPreemptCommand` (`scheduler_windows.c:399-501`) moves the
   held fences to the preempted list, finds no active fence, and
   `AdmissionSchedulerTryNotifyPreemption` reported **DMA_PREEMPTED
   immediately** (LastCompletedFence = 0xb957). VidSch then resubmits the
   held paging packets into the same frozen queue, and each later preemption
   succeeds again.
4. Microsoft's TDR (Windows 8 and later): the scheduler detects a task that
   runs too long, **tries to preempt it, and declares a timeout only when the
   preemption does not complete within TdrDelay**. Here every preemption
   completes, so DxgkDdiResetEngine is never called (no Wom1GpuReset), the flag
   is never cleared, and every paging fence waits forever: GravityMark's
   MakeResident wait, DWM/explorer (refused at preflight), and at shutdown the
   allocation teardown of every process. The ordered reboot never reached
   PSCI reset (the owned launcher stayed alive, `rec1982h/recover-run-1.log`:
   `OWNED_LAUNCHER_STILL_RUNNING 61797`), which needed the operator power cycle.

**Fix (commit b3b5b134):** while SchedulerFaulted is set,
`AdmissionSchedulerTryNotifyPreemption` leaves the ready boundary unreported
and returns success (`scheduler_windows.c:106-120`); the PreemptCommand, DPC
and worker-exit callers therefore record no new fault. TdrDelay then expires,
dxgkrnl calls DxgkDdiResetEngine; `AdmissionResetEngineInternal` finds no
active packet (quiescence trivially proven), `AppleAgxSchedulerResetEngine`
clears the pending preemption and keeps the held fences resubmittable, and the
reset clears SchedulerFaulted. An unfaulted scheduler is unchanged.
`tests/test_g4_faulted_preemption_tdr.py` (real PreemptCommand, notifier,
worker exit and shared scheduler): RED on base (one DMA_PREEMPTED while
faulted, phase Idle), GREEN with the change, plus the after-reset and healthy
controls. The engine reset also writes Wom1GpuReset with the full
SchedulerFaulted code (`scheduler_windows.c:694`; tag << 16 | line), so the
next run names the predicate that faulted the scheduler.

**Not proven:** which `SchedulerFaulted` setter fired after the drop (the
code is not in any EXP1982 receipt; candidates are the worker's poll-guard
exits `backend_platform_windows.c:4518,4667,4674` after a Busy dropped
completion, and `AdmissionSchedulerRecordCompletion`/notification paths), and
whether the fault was reported to Windows as DMA_PAGE_FAULTED before that.
Neither changes the fix: in every case the engine can only make progress
again through a reset, and a faulted scheduler must not hide that.

## WHAT IS STILL UNKNOWN

1. The GPU-side cause of the 14-26 ms firmware Timeout of the GravityMark
   stream job (no valid FAULT_INFO in five runs).
2. The exact SchedulerFaulted setter after the drop (the Wom1GpuReset of the
   next run will carry it).
3. Whether the engine reset of a faulted, idle engine is accepted by dxgkrnl
   as a per-engine TDR (expected: DWM recovers, GravityMark's device is
   removed) or escalates to ResetFromTimeout (same code path, then
   RestartFromTimeout).
4. Native Windows build/link of the KMD and the probe (only host replays and
   SDK-header syntax checks for ARM64/x64/x86 ran).
