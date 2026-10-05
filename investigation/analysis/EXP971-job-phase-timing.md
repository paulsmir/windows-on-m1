# EXP971 per-job phase timing plan

## Contract and source record

EXP970 ETW measured Explorer render DMA at 33–48 ms and DWM at 65–326 ms
(median 131 ms), while paging packets took about 0.2 ms. The operator saw a
correct full desktop only after a roughly 30 s redraw. The current boundary is
render-job latency, not missing pixels, package admission, or GPUVA eviction.
REC-EXP970E restored the ordinary GPU-visible Code28/PackageAbsent guest.

Inspected: `gpuva_g3_windows.c` SubmitCommandVirtual and BeginJob/CompleteJob;
`backend_platform_windows.c` worker, queue delegates, progress poll, 1 ms wait,
completion notification, and existing TA timestamp objects; `render_call_correlation_windows.c`
registry export; `m1n1_windows/src/hv_agx_gpuva_v5.c` and
`hv_agx_retained_platform.c` JOB_BEGIN/JOB_END broker contract; Mu EXP392
ordinary profile and r143 full-owner launch contract (unchanged); Asahi Linux
DRM timestamp UAPI and existing render queue semantics; Microsoft WDK
`DXGKARG_SUBMITCOMMANDVIRTUAL`, `DxgkCbNotifyInterrupt`,
`KeQueryPerformanceCounter`, and `KeDelayExecutionThread` documentation.

Windows owns scheduling and DMA fence admission. The KMD owns job validation,
GPUVA lease, queue submission, completion reporting, and recovery on failure.
m1n1 owns the broker and stage-2/AGX power contract; Mu owns ACPI exposure.
Asahi's GPU timestamp values are GPU clock domain data, so do not subtract
them from Windows QPC ticks. Read raw TA/3D timestamp targets only when the
current object mapping proves an 8-byte target; zero means unavailable.

## Experiment

One observational KMD change: a 64-job ring with process/context/fence/DMA
bytes, QPC frequency, QPC stamps at DDI entry, worker activation, G3 BeginJob,
backend submit and firmware queue calls, first progress, G3 CompleteJob,
DMA_COMPLETED notification, and a count of 1 ms delay calls. Capture raw
firmware timestamp target values separately. Export a snapshot to the APPL0002
device registry after each completed job from the passive worker; include
export duration so its observer cost is distinguishable from GPU execution.
No scheduling, wait, capabilities, UMD, firmware, signer, or recovery change.

Host RED→GREEN: ring wrap and fence matching; missing phase remains zero;
monotonic QPC phase projection and duration calculation; 64th/65th job preserve
the newest 64. Build exact KMD under pinned WDK. Preregister package/manifest
SHA and one full-owner run after Code28 gate. Ask the operator to move the mouse
on Code0, collect the registry ring and ETW, then identify the longest phase.
If the candidate fails, collect first and run exact cleanup in ordinary Code43,
then durable Code28. The smallest checkpoint is one DWM/Explorer fence with
ETW DMA duration and matching ring stamps; absent correlation is inconclusive.
