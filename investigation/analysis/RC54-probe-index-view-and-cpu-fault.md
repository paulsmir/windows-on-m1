# RC54: probe recording refusal and terminal CPU fault accounting

Offline branch `codex/rc54`, base
`d14128d10bbf1f61c16f1d091a8301faadf81fdc`.
The implementation fixes two source-proven defects. **It does not establish
general containment of every submit failure or fix the originating GPU timeout.**
Those requirements remain open; do not label this branch a system-safety fix.
Integration owns experiment/change ledgers and all hardware work.

## What EXP1976 actually proves

Saved evidence root:
`/Volumes/pwdev/public_windows/.local/experiments/EXP1976-stream-probes/evidence`.
Derived evidence and test logs: shared `handoff/codex/rc54/`.

1. `EXP1976/tool-rc43-umd.log:149-150` records indexed ExecuteIndirect,
   MaxCount 2 and a count buffer, then `measure-d3d12-draw`, **E_NOTIMPL,
   reason 8**. The same pair occurs at 304-305, 459-460, 615-616, 770-771,
   and 925-926. This is a command-recording refusal, before native replay or
   G4 submission. It is not a kernel stream-parser rejection.
2. The probe binds an index-buffer view at resource GPUVA **+512**
   (`drivers/apple-agx/windows/icd/agx_d3d12_probe.c:1365`), then records
   ExecuteIndirect at `:1377`. At the base commit, RecordDraw explicitly
   refuses `indirect && indexed && list->index_offset` as reason 8
   (`drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:5760`). ListError forwards
   E_NOTIMPL through `pfnSetCommandListErrorCb` at `:4066-4068`.
   CloseCommandList merely sets `closed` at `:4031-4033`. The combined probe
   message “close+execute+signal” does not locate the actual failure.
3. GravityMark is a different boundary. The dump header is ARM64, 8 CPUs,
   **0x119**, parameters **2, ffffffffc000000d, ffffe183d1379a50,
   ffffe487530a44b0**. Dump SHA-256:
   `b58ca53c71fa8b2f279dc481d4d1bc67235ec27b13ebfd8ad750060038bc7dd3`.
   `dump-header.json` records header-only extraction; no debugger was run.
   Neither pointer argument is assigned a guessed type.
4. `rec1976h/kmd-receipts-pre-disable.txt:188`, decoded with the repository
   decoder, records one firmware **Timeout**, fence **0x2b73**, PID **6604**,
   **14 ms** from submit, one halt/resume. FAULT_INFO
   **0x46dfaae3197cd6c4** has VALID clear; zero valid faults. There is no
   valid VA/unit with which to blame a VDM output BO or USC clone.
   `decoded-1976.json` preserves the decode.
5. The nonzero GravityMark UMD log prefix ends at line 14721 in staged uploads;
   its file has a NUL-filled tail. Absence of post-draw counted receipts does
   not prove no draw started: the owner emits them only after draw_vbo returns
   successfully (`agx_d3d12_windows.cpp:4034`). No saved submit-failure receipt
   identifies the crashing callback or validation predicate.

The experiment's source.zip matches the task base byte-for-byte for the
recorder, virtual submit, paging submit, G4 parser, stream emitter and kernel.
`source-comparison.json` records their complete hashes. The old EXP1966/1969
results and RC33/36/38/43 analyses were read only as the task's references.

## Legal index-buffer views

Primary WDK26100 `d3d12umddi.h:1961-1966` gives an independent BufferLocation,
SizeInBytes and Format for an IB view. A view inside an allocation is legal;
its base is independent of StartIndexLocation in each GPU argument record.

Primary Mesa sources under `.local/reference/mesa/` establish the translation:

- `src/gallium/drivers/asahi/agx_state.c:3739-3751` obtains the resource's
  GPU base, records the resource read, and returns its remaining extent.
- `:3755-3774` adds direct draw.start in index units; `:3778-3785` selects
  the resource base for an indirect draw. `:4388-4402` preserves the original
  index resource while unrolling indirect records.
- `:4420-4451` loads base vertex and first instance from the argument buffer;
  these are not the index-view base. `:5213-5238` transfers the index address
  and extent into the native indirect INDEX_LIST encoder.
- `src/gallium/drivers/asahi/agx_state.h:965-968` derives GPUVA from the BO
  and resource level offset; mutating that shared layout for one draw would
  affect unrelated uses of the same resource.

The fix removes the recorder's reason-8 veto and the owner's second offset
veto. OwnerDraw retains index-size/alignment/extent validation and checks the
actual pipe resource width (`agx_d3d12_windows.cpp:3912-3914`). Direct draws
continue adding whole indices to range.start at `:3925-3926`. Indirect draws
carry the view base through a scoped scene setter at `:3993-4000`.

The projection adds the view offset to the native resource-index address and
reduces its extent (`native-asahi-batch-lifecycle.py:301-314`). It keys the
offset to the **original pipe_resource**, so native restart/geometry lowering
can replace the IB without inheriting its offset. Native recursion using the
original IB retains the base. The owner clears the scope on both successful
and failed draw returns before an ordinary follow-up draw.

`AgxWin32AsahiSetIndirectIndexBuffer` is declared inside the existing
`extern "C"` block in `agx_win32_asahi_scene.h:75` and defined in the scene
translation unit at `:464`. This preserves the mixed C/C++ (/TP) linkage.
There is no new per-record CPU operation, CPU count read, argument rewrite,
BO alias, shared resource-layout mutation, or generated-kernel change.

The G4 parser cannot inspect future GPU-written VDM instructions. It validates
the envelope and mapped ranges (`shared/src/apple_agx_g4_submit.c:15-18,
50-127,136-201,203`); a render parse failure maps to INVALID_PARAMETER in
`gpuva_g3_windows.c:4857`. No saved evidence establishes such a failure in
EXP1976. RC38's links/padding, RC40's timestamp publish marker, USC window,
writable GPU-only pools and explicit output BO references remain intact.
Weakening those validators would not address the proven probe failure.

## Terminal CPU paging/present errors

The independent audit found a separate, deterministic error-contract defect:

- At base, `paging_windows.c:281-300` used DMA_PAGE_FAULTED only when a VA was
  known and otherwise emitted reserved DMA_FAULTED. The GPU backend already
  avoids that interrupt (`backend_platform_windows.c:3284-3291`, EXP896 lesson).
- At base, `paging_windows.c:356-359` called scheduler completion only for
  successful CPU execution. An error still notified its known fence and
  released CPU state, leaving ActiveFence/CompletedFence inconsistent.
- WDK26100 `d3dkmddi.h:1063-1078` explicitly allows VA zero for another error
  and provides FaultErrorCode. `winsdk26100/shared/d3dkmdt.h:2337-2355`
  defines its device-specific payload. Known-fence error retirement is
  already implemented by `AppleAgxSchedulerFaultActiveFence` at
  `shared/src/apple_agx_scheduler.c:316-330`.

The final worker uses `AdmissionPagingRecordTerminal` at
`paging_windows.c:320-341`: success preserves the existing completion path;
failure retires exactly the active CPU fence as an error and updates a waiting
preemption boundary under SchedulerLock. This runs **after CPU execution has
ended**. It cannot be called to discard firmware-owned work. A foreign or
repeated fence fails the transition, retains pending ownership, and returns
before notification (`:386-391`). The DPC's completed-fence record now includes
known terminal errors (`:537`).

All CPU errors use DMA_PAGE_FAULTED (`:280-301`). A genuine fault address/write
bit survives; other errors have VA zero and the low 31 bits of NTSTATUS in a
device-specific code. No reset/fatal/unknown-fence flag is manufactured. The
actual failure remains PagingCompletionStatus; no success, timestamp or output
is fabricated. Existing notification-synchronization failure recovery is not
redesigned. This fixes a source bug, **not a proven executed EXP1976 path**.

## Submit/DDI audit and the unimplemented containment requirement

The full independent audit is `handoff/codex/rc54/safety-review.md`. The local
WDK supplies signatures/IRQL/fields; it does not enumerate every legal status.
Prior repository analyses record official return-contract checks. No fresh
online lookup was performed during this offline task.

| Boundary | Evidence and remaining work |
|---|---|
| SubmitCommandVirtual | `gpuva_g3_windows.c:4961-5055` routes paging, present, touch, G4 and legacy. Wrapper `:5072-5099` already maps **all** nonsuccess to INVALID_PARAMETER and records rejected tails. RC43 did not merely omit this status from its whitelist. The documented malformed-input return and an internal terminal scheduler failure are different dispositions. |
| SubmitCommand | Final `paging_windows.c:562-647` still forwards present/render/CPU queue errors and rejects malformed paging shape. `submission_windows.c:54-156,239-245` and `work_queue_windows.c:34-48` supply further failure paths. This is a concrete alternative crashing callback. It is not fixed by the virtual wrapper or this CPU terminal correction. |
| Render | `umd_render_windows.c:345-414,423-426,480-553,618-635`: command preparation/validation has no submitted fence in WDK args. A failure cannot be converted to a fictitious DMA completion. |
| BuildPagingBuffer | `paging_windows.c:36-138,224-252` and `gpuva_g3_paging_windows.c:1399-1434`: containment covers only UpdatePageTable/FlushTlb. Fill/transfer/fence and physical operation failures need an explicit operation/error protocol preserving partial progress; blanket success falsely reports effects. |
| Patch | `gdi_windows.c:494-690`, `present_windows.c:243-299`: validation and reservation errors remain. A supplied fence is not proof that submission ownership was accepted. |
| Present | `callbacks.c:130-299`, `present_windows.c:57-187`: preparation has no submitted fence; its SubmitPresent helpers at `:303-425` are separate execution/admission paths. |

A general solution must represent a software error as an **accepted packet**
with provisioned capacity, ordering, context lifetime, preemption/discard/
resubmission/reset behavior and exactly-once fault notification. Merely returning
SUCCESS loses that packet. Reusing RC43's rejected tails is also incorrect:
they deliberately are not accepted/resubmittable packets
(`apple_agx_scheduler.c:363-364`). Active A1, queued B2, rejected tail3 followed
by preemption can deadlock notification or emit stale fault3 after B returns
as new fence4. Likewise, FENCE_INVALID is not a generic way to avoid advancing
a **known** fence; its primary definition is for unknown fault identity.

No such speculative wrapper was committed. Task item 1's universal submit
containment remains **incomplete**, as does exact attribution of the GM timeout.
One bounded debugger command file, `cdb-commands-1.txt`, is ready for the owner
against the already saved dump. It requests stack/module/raw argument evidence,
not pool walks, and has not been executed. Physical versus virtual versus
system-paging attribution is the immediate discriminator.

## Ownership, verification and checkpoint

UMD/Asahi owns draw addressing, hazards, generated commands and transient BOs.
KMD/VidMm owns validation, mappings, scheduling and completed-fence/error state.
Firmware retains actual GPU execution ownership. m1n1/Mu own launch,
initialization, interrupts, DMA/power and recovery. Their behavior and the
saved EXP1976 launch contract are unchanged; no live state, hardware, guest,
builder, package, other worktree or network was accessed. No external function
body was copied. No caps/signing/recovery/default-stream change was made.

Executable RED/GREEN covers recorder refusal, owner translation, native base
and extent, scope clearing, count0/1/2 with R16/R32 kernel inputs, CPU no-VA
notification, matching/foreign/repeated terminal fences, wrap and preemption.
Removing the actual worker early-return guard in a temporary mutation causes
the regression to fail. Projected native translation units compile on the host;
Windows native KMD/ARM64/x64/WOW64 build/link is still required. Exact commands,
counts, baseline result, commits and hashes are in REPORT.md.

Hardware owner only: first obtain the dump attribution and complete native
compile/link gates. Keep the established EXP1976 launch/recovery contract and
hash/preregister the package. Run stream=0 controls, then stream=1 RC43I16Z and
RC43I32Z separately, then RC43I16O/N and RC43I32O/N. Require correct pixels,
counter multiplicity, ordinary continuation and **path3/reason0** receipts;
API success or fallback is insufficient. Any timeout/removal is a failed gate.
General malformed-submit containment must also pass its own per-context fault
checkpoint before being claimed safe. Only after those gates run GravityMark
HD50k with the stream and compare render_fence, frame time and score with
EXP1975a **1632 / 9.8 FPS**. Collect receipts/logs/dump before exact-package
cleanup with the preserved normal GPU-visible recovery artifact.

WHAT IS STILL UNKNOWN: dynamic crashing DDI and predicate; originating G13
Timeout/VDM-USC visibility; actual probe pixels/query counts after recording is
fixed; Windows native build/link; general submit/build error containment;
high-count performance and any gain over 1632. No hardware result is claimed.
