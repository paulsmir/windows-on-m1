# C1 audit: every place that names "the" process or VM slot of the depth-two window

Implementer note, 2026-11-10. Scope: mechanism C1 of DWM-COMPOSITION-LEVER.md (a compute job of process B kicked
behind a running plain render of process A). Offline source reading only; nothing here was run on hardware.
Verdicts: SAFE (correct as is for a cross-process pair), CHANGE (must change with the wiring), RISK (left open).

Sources read: render_job_window.c/.h, backend_platform_windows.c (kick, fallback, adopt, completion, lead Barrier,
compute build, InitBM), gpuva_g3_windows.c (BeginJobAs, CompleteJobTimestamp, AbortJob, PrivateReset, copy gate,
fault receipt), gpuva_g3_private.h, shared apple_agx_gpuva_g3_graph.c/.h (slot table), scheduler_windows.c
(ResetEngine), m1n1 src/hv_agx_gpuva_v5.c of the bench line (06616be7: per-slot `jobs` counter).

| # | Place | Finding | Verdict |
|---|---|---|---|
| 1 | `ADMISSION_G3_STATE.ActiveProcess` + `SuccessorContext/SuccessorFence` (gpuva_g3_private.h) | One process for both jobs: the successor is assumed to share ActiveProcess and its graph lease (nested job). A cross-process successor needs its own process recorded. | CHANGE: `SuccessorProcess` (NULL = same process, today's path) |
| 2 | `AdmissionGpuvaG3BeginJobAs`, successor branch | Requires `ActiveProcess == process` (refusal 1) and joins the lease with `GraphBeginNestedJob` (no broker call). A cross-process successor must begin a job on its own graph. | CHANGE: cross branch, only under the switch, only a compute envelope, only JOB_BEGIN on a lease the process already holds |
| 3 | Broker call that activates a slot | `begin_retained` may LEASE (pick a free slot, or steal an idle lease; never one with a job) before JOB_BEGIN. A LEASE while another slot's job runs has never happened on the bench (depth one begins only with no job in flight); a RELEASE has (rule R on idle graphs). The broker counts jobs per slot (`slots[slot].jobs`), JOB_BEGIN/JOB_END of slot B do not touch slot A, RELEASE refuses a slot with jobs. | CHANGE: new `AppleAgxGpuvaG3GraphBeginLeasedJob` refuses when no lease is held, so no LEASE and no steal happens while N runs; the kick then falls back to depth one |
| 4 | `AdmissionGpuvaG3CompleteJobTimestamp` | Ends the active graph's job and promotes Successor{Context,Fence}; ActiveProcess stays. With two graphs the active graph's EndJob is a real JOB_END (not the nested shortcut) and the successor's process must become ActiveProcess. Timestamp publication checks `context->GpuvaG3Process == ActiveProcess`: correct once promoted. | CHANGE: promote `SuccessorProcess` |
| 5 | `AdmissionGpuvaG3AbortJob` (kick fallback S1-S4a, ResetEngine unsubmitted/unheld/kickedAborted) | Ends "ActiveProcess"'s graph job for either fence. For a cross-process successor that would end the running job's lease and leave the successor's job begun for ever (its process could never page or be destroyed). | CHANGE: abort the job on the graph of the process that owns the fence; promotion as in 4 |
| 6 | Reset / TDR (`AdmissionDdiResetEngine`, `AdmissionGpuvaG3PrivateReset`) | Order: quiesce (the active job completes through 4, possibly dropped), abort unsubmitted/unheld, PrivateReset (ReleaseRetained per process: a no-op for a graph with a job), scheduler reset, AbortJob(kicked). Every step reaches the G3 state through 4 or 5 by fence, so with 4 and 5 fixed each job returns to its own process and both leases end up released (reset baseline). | SAFE after 4, 5 |
| 7 | `runtime->RunningVmSlot` / `AdmissionPrepareUnmanagedInitBm` / `AdmissionUnmanagedInitBmStale` | RunningVmSlot is written only when a job starts at depth one. An adopted successor inherits it; correct only because a successor shared the slot. After a cross-process compute is adopted, a following unmanaged render successor of B would be compared against A's slot and could be kicked on a template buffer manager initialised for A's slot. | CHANGE: the adopted successor's slot becomes RunningVmSlot (same value for a same-process pair) |
| 8 | `image->JobVmSlot` | Cleared at every bind, set by BeginJobAs from the job's own `process->Graph.Slot`; the compute builder (`input.VmSlot`), the prelude and `AdmissionBackendImageRetargetVmSlot` read it. The successor is built in its own slot already. At the kick decision it is still 0 for N+1, so the window's `Next.VmSlot` cannot come from the image. | SAFE for the build; CHANGE: `Next.VmSlot` is read from the successor's graph under the G3 lock (held retained lease only), and re-proved against `JobVmSlot` after BeginSuccessorJob |
| 9 | Compute sidecar set (62, `owner->JobWordStamp/JobWordSet`, `NextJobWordSet`) | Global owner state keyed by fence, not by process; a plain render N holds no set, the compute takes one as in a same-process pair. The RunCompute it holds carries the job's own VmSlot. | SAFE |
| 10 | Lead Barrier (`AdmissionBuildLeadBarrier`) | Waits on the 3D firmware stamp address/value/event of the running job: global memory, no VM (Asahi: stamps are not per-VM). | SAFE |
| 11 | Completion path (`AdmissionBackendComplete...`, `CompletionContext`) | The context comes from the active render packet (the adopted successor's packet after adoption); fence retirement, poison on drop and `AdmissionGpuvaG3PrivateReported` use that context's own process. Two contexts in one window already exist (same process, different contexts). | SAFE |
| 12 | Kick fallback S1-S4a (`AdmissionJobWindowKickFallbackWindows`, `...KickBuildUndoneWindows`) | Scheduler, pending head, notifier flight, manager transaction, staged sequence: all keyed by fence. The only process-specific step is AbortJob (5). After a fallback the successor's retained lease is released (AbortJob: "no retained lease"), a RELEASE of an idle slot while N runs, which rule R already does; the depth-one rebuild leases again with no job in flight. | SAFE after 5 |
| 13 | Copy escape gate (gpuva_g3_windows.c ~2145-2198) | `ActiveProcess == p || p->Graph.JobInFlight`: the successor's process is covered by its own JobInFlight. The "own context" proof needs `ActiveProcess == p`, false for the successor's process: it waits (conservative). | SAFE |
| 14 | Mapping mutation, private scenes, process teardown, live paging waits | All guarded per graph (`Graph.JobInFlight`, `LeaseToken`), not by ActiveProcess. | SAFE |
| 15 | `AdmissionGpuvaG3CaptureFaultMapping`, `AdmissionGpuvaG3FaultDrillApply`, paging attribution receipts | Name ActiveProcess only. A fault of a cross-process successor is reported with status Identity (no mapping capture); the drill never arms a successor. Receipt-only. | CHANGE (small): the fault receipt resolves the successor's process by fence. Drill and paging receipts unchanged |
| 16 | Firmware fault / timeout with a cross-process pair in flight | Which job is dropped follows the existing pair rules (stamps, done pointers); the fault's VM slot (FaultInfo bits 17-22) is not used to pick the job. With two slots in flight the slot would name the faulting job exactly. | RISK left (attribution only; both jobs still end and return to their owners through 4-6) |
| 17 | OV2 / OV5 waivers | Both require the same WDDM context and process: never apply to a cross-process pair. | SAFE |
| 18 | Firmware behaviour | Whether the firmware, as we initialise it, runs a compute work item of slot B while a 3D work item of slot A is in flight (unknown 1 of the lever note). | RISK: the hardware checkpoint of C1 |

Not changed on purpose: the admitted class is the raw kinds RENDER -> COMPUTE. A render with a compute prelude as N,
a present job on either side and every pair with a render as N+1 stay 55 (two renders would share the live
buffer-manager binding; a present uses the per-process arena).

## Implemented with the wiring commit

Rows 1-5, 7, 8 and 15 are changed as stated; tests/test_render_xproc_pairs.py (decision, receipt, wiring slices) and
the `cross_process_pair` scenario of tests/g3_retained_slots_replay.c (real graph, real broker client, real m1n1 v5
broker: JOB_BEGIN on B's slot only while A runs, no LEASE, no steal, no invalidation; abort = JOB_END + RELEASE of
B's slot with A untouched). Not compiled on the host: the kick function and the worker in backend_platform_windows.c
(no host harness builds them); BeginJobAs, CompleteJobTimestamp, AbortJob and the fault receipt are built by the
existing replays with -Wall -Wextra -Werror, with the switch off. Receipt-only side effect with the switch on: the
early rc95 note (AdmissionJobWindowNoteWindows, diagnostics only) sees no successor slot and writes 6 for a
cross-process pair; the kick decision overwrites it for the same fence.

## C2a: the offline unknown (how a shared or primary allocation is recognised) and its resolution

Question (DWM-COMPOSITION-LEVER.md, unknown 3 and section 6): the pinned D3D12 DDI has no shared heap flag; how can
the UMD prove that a compute job binds only allocations no other process can reach?

Sources: `.local/reference/wdk26100/d3d12umddi.h` (D3D12DDI_HEAP_FLAGS: no shared bit; D3D12DDICB_ALLOCATE_0022:
hResource in, hKMResource out; D3D12DDIARG_OPENHEAP_0003); Microsoft Learn, D3DDDICB_ALLOCATE (read 2026-11-10):
"The driver must assign a non-NULL value for allocations that are created in response to shared resources", and the
runtime "creates and returns a kernel-mode resource handle only if the user-mode display driver sets the hResource
member"; our UMD: `render-admission/umd/src/umd_win32_screen.c` (every winsys BO: `allocate.hResource = NULL`),
`umd.c` AdmissionUmdCreateResourceWithMetadata / AdmissionUmdOpenResource (the only callers that pass a runtime
resource), `mesa/winsys/agx_win32_asahi_bo.c` AgxWin32AsahiImportBo (the only place a runtime-resource allocation
becomes a BO; sets `Imported`, and an imported BO is never cached or recycled), its three users (D3D12 presentation
texture, D3D12 opened shared texture, D3D10/11 presentation/primary), investigation/analysis/D3D12-C76-shared-open.md.

Resolution: the runtime does not tell the driver that a heap is shared, and it does not need to. Cross-process
visibility of an allocation requires a kernel resource handle, which exists only for allocations the UMD itself
attached to a runtime resource, or received through the open DDI. In this UMD both enter the winsys through one
function and are marked `Imported`. Every other BO is allocated with hResource NULL: device-associated, no kernel
resource, not openable. So "private" = a BO of this backend that is not imported; no hypothesis about hKMResource
values is needed. A D3D12_HEAP_FLAG_SHARED heap this UMD backs with private BOs cannot be shared at all (C76:
explicit shared heaps stay fail-closed), which is a missing feature, not a hole in the rule.

Verdict (agx_win32_private_only.h): private-only when every entry of the job's reference list (the complete set of
allocations the KMD binds for the job, built by add_bo) is private; an imported or unknown BO, an empty list or a
missing device feature clears it. Carried as APPLE_AGX_G4_COMPUTE_PRIVATE_ONLY in the compute envelope header's
reserved word (UMD-KMD ABI, not a firmware field), admitted by the parser only under
APPLE_AGX_G4_PARSE_PRIVATE_ONLY = device feature AGX_WIN32_DEVICE_FEATURE_PRIVATE_ONLY_COMPUTE (0x8).

Trust: as OV2, the KMD takes the UMD's verdict. A wrong verdict can only reorder two jobs on memory both processes
may already access; it grants no access. Not covered by the rule and left as stated: the KMD does not re-derive the
verdict from its own allocation records (it could: every allocation of the job's reference list is known to it).

Hardware unknown of C2a (beyond C1): with the wait waived the firmware really executes a compute work item of one
VM slot while a 3D work item of another slot runs; under C1 they are only queued together.
