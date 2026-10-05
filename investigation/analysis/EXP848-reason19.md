# EXP848 Reason19 — offline caller attribution

2026-09-27; analysis base `dc193ed472a4886385f87fac86d27ac2867c36ee`, branch
`integration/ad04-windows-compiler`. Task:
`/Users/pavel/public_windows/.local/tandem/NEXT_TASK_REASON19.md`, B3 hypothesis 1
of [the preceding analysis](EXP848-next-boundary.md#b3-ranked-hypotheses-and-the-smallest-next-discriminator).

**Result:** all 84 saved Reason19 events have the same 16-frame stack. Public
symbols and matching call instructions identify the immediate cause as a
**nonzero return from the virtual command-submission DDI**, followed by
`dxgmms2!VidSchiSendToExecutionQueue` marking the device in error with `0x13`.
This narrows the investigation to `AdmissionDdiSubmitCommandVirtual` and its
callees. The trace does not contain its arguments, returned NTSTATUS or internal
reject branch. `STATUS_INVALID_PARAMETER` is the source/documentation-backed
explanation, not a directly captured return register. No particular validation
predicate is proven defective.

No Air connection, launch, installation, firmware change, product-code change,
or product build was performed. The builder ran only offline ETL/PE readers;
the small ETL reader is an analysis utility, not a candidate driver. Existing
dirty `m1n1_windows` and `mu` were preserved. Ordinary Code28 remains the last
recorded recovery; it was not probed or revalidated in this task.

## Evidence identity and decoding

`E` = `/Users/pavel/public_windows/.local/experiments/EXP848-r127-stale`;
`A` = `E/reason19-analysis`. Builder input is the existing
`C:\Users\pauls\EXP848-analysis-offline\EXP801DxgBoot.etl`; analysis outputs
are in `C:\Users\pauls\EXP848-reason19-offline`.

- Original ETL and builder copy SHA-256:
  `ab90d4e527f86438ede7d13202ba09d2e45cae3940100304e647169d80bcac14`,
  536,870,912 bytes. Builder hash was freshly checked; original is checked in
  the final evidence verification.
- Package848 KMD SHA-256:
  `58eccd57272c5c17df67a819c9b262f1a6d981bf24c94e5384b7fee772903198`.
  Build receipt selects `c72395d4169621c32159d35e02856582954e6563`.
- `src/gpuva_g3_windows.c` SHA-256:
  `6eeb73236bdf69e68e0515c894c2fd9a92bdce4c3fbeaa45ca7dbe47245c1110`,
  matching the package source manifest and current file. Its diff and the
  `driver.c` diff against the package source commit are empty.
- Raw scripts, reader source, CDB output and stacks are indexed by
  `A/manifest.json`, SHA-256
  `6113b8c4846fc48f94f4fa26d0896c66c330c446d1ab321f212479cc112532c6`.
  [Committed evidence](EXP848-reason19-evidence.json) retains the common raw
  stack, all 84 event identities/device-owner joins and symbol provenance.

The saved AutoLogger script requested `EnableProperty=4`. Microsoft documents
this as stack capture in extended event data; `Get-WinEvent.ToXml()` alone did
not expose those addresses. [AutoLogger configuration](https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-an-autologger-session).

`A/readetl.cpp` uses `OpenTraceW`/`ProcessTrace` with
`PROCESS_TRACE_MODE_EVENT_RECORD` and inspects every extended item. Type 6 is
`EVENT_HEADER_EXT_TYPE_STACK_TRACE64`; the first eight bytes are MatchId,
**not a stack frame**. The following 16 addresses form each Reason19 stack.
[Extended data](https://learn.microsoft.com/en-us/windows/win32/api/evntcons/ns-evntcons-event_header_extended_data_item),
[stack layout](https://learn.microsoft.com/en-us/windows/win32/api/evntcons/ns-evntcons-event_extended_item_stack_trace64).

Reproduction: transfer `A/readetl.cpp` into the builder analysis directory,
run `A/native.ps1` through the previously saved `offline-analysis/sshps.py`
(the specified builder SSH key and `IdentitiesOnly=yes`). It invokes
`vcvars64.bat`, `cl /nologo /EHsc readetl.cpp /link advapi32.lib /out:readetl.exe`,
then `readetl.exe <saved ETL> > raw-stacks.txt`. `symbols.ps1`, `caller.ps1`,
`queue.ps1`, `dxgcaller.ps1`, `wrapper.ps1` and `literal.ps1` open saved images
with CDB `-z`, using
`srv*C:\Users\pauls\symbols*https://msdl.microsoft.com/download/symbols`.
They do not attach to a guest or driver.

Full native decode returned status 0: 1,669,674 records, 1,669,672 extended
stacks, 84 ID467 events, one common Reason19 stack. Header reports
`EventsLost=0`, **`BuffersLost=5`**. The loss counter must accompany any absence
claim. The only providers are EventTrace, DxgKrnl and EventMetadata; no independent
image-load/rundown provider is present. Xperf also reported five lost buffers,
but its requested text exports were empty, so they were not used for attribution.

First error: XML record **52636**, native reader sequence **52638**,
`2026-09-27T09:40:02.4391858Z`, PID4/TID5528,
DxgDevice `ffffb60b672e8820`, FromUserMode false, Reason decimal19/hex13.
The native sequence includes two trace-header records; it is not an ETL
EventRecordID. Join by timestamp and device, not by an assumed ordinal alone.
All 84 native records were checked against the preceding XML correlations.
The first device belongs to PID1228 and Apple adapter `ffffe20e4b18c000`.
DWM PID1236 has the same stack at record170087,
`09:40:08.1272898Z`, well before its later dwmcore AV.

## Symbol reconstruction and its confidence limit

The available ARM64 Microsoft PE/PDB pairs loaded without forced symbol mismatch:

| Module | Version / image key | Image SHA-256 | Public PDB GUID+age |
|---|---|---|---|
| dxgmms2.sys | 10.0.26100.9444 / F1D9BD9C113000 | `177d4436e0423f299d196aac8758045376ec38ab168e4e259b89005f048a567b` | `0913F44653039D025278DCF68B91EDDC1` |
| dxgkrnl.sys | 10.0.26100.9444 / 9A3AB8B24de000 | `5d04895951336fd3733503dbbd57365c96a5bdc5e07389ff8b2077da48769fca` | `76608A0A73DA7AD79EF71EFAD90E311C1` |

PDB SHA-256 values respectively:
`1cc58d733c5c8ebb6f16c4fe0c95ee864816e6910467f0e4896de120c46f8ddd`,
`f02c0be722e979938e5962b9ee9a3ecb4e5d071e8830372ee13c9485cd4320af`.
The cache also contains .9168 images; their symbol layout does not match this
chain. No comparison against an old hardware experiment was needed.

**Qualification:** EXP848 did not save a kernel image list/PDB identity. Bases
`fffff8036e000000` (dxgmms2) and `fffff8036a490000` (dxgkrnl) are reconstructed
from the coherent call sites below and the matching profiler function ID,
not read from an EXP848 loader record. Thus these are strongly corroborated
symbol assignments, not an independently hash-verified EXP848 module inventory.
CDB opening a PE supplies static code, not live register values or a crash stack.

The six initial `fffff80370...` frames and final two kernel frames are retained
raw; no guessed nt symbol names are necessary. The eight intervening addresses
resolve consistently, shown in caller-to-callee order:

| Captured return address | Symbol |
|---|---|
| `fffff8036e080b94` | dxgmms2!VidSchiWorkerThread+0xd4 |
| `fffff8036e0e8d8c` | dxgmms2!VidSchiRun_PriorityTable+0x16c |
| `fffff8036e0e98e8` | dxgmms2!VidSchiSubmitRenderVirtualCommand+0x270 |
| `fffff8036e0e8ebc` | dxgmms2!VidSchiSendToExecutionQueueWithWait+0x5c |
| `fffff8036e024b1c` | dxgmms2!VidSchiSendToExecutionQueue+0x96c |
| `fffff8036e03135c` | dxgmms2!VidSchMarkDeviceAsError+0xcc |
| `fffff8036e03b084` | dxgmms2!VidSchiMarkDeviceAsError+0x4c |
| `fffff8036e03ad00` | dxgmms2!McTemplateK0ptq_EtwWriteTransfer+0x58 |

This is supported by actual `BL` return sites, not nearest-symbol names alone:
RVA e98e4 calls e8e60, e8eb8 calls 241b0, 24b18 calls 31290,
31358 calls 3b038, and 3b080 calls 3aca8.

An independent corroboration is the immediately preceding PID4/TID5528
profiler pair, ID105/106, payload Function=`0x13c7` (5063):

| UTC | Native sequence | Captured dxgkrnl return |
|---|---|---|
| 09:40:02.4391792 | 52635 | `fffff8036a6baa34` = ADAPTER_RENDER::DdiSubmitCommandVirtual+0x22c |
| 09:40:02.4391829 | 52637 | `fffff8036a6baa54` = ADAPTER_RENDER::DdiSubmitCommandVirtual+0x24c |

Both include `dxgmms2+0x2459c`, the return after its virtual-submit indirect
call. The dxgkrnl disassembly emits exactly Function=0x13c7 at the two recorded
return sites. The profiler bracket lasts 3.7 microseconds; Reason19 follows
2.9 microseconds later. These durations do not identify which KMD predicate ran.

## What Reason19 means here

The event schema exposes only DxgDevice, FromUserMode and UInt32 Reason, without
a reason value map. Pinned WDK26100 `shared/d3dkmthk.h:5402` defines the public
user-origin reasons as `0x80000000` and `0x80000006`; it does not define 19.
CDB cannot find `_VIDSCH_ERROR_CODE` type information in this public PDB. No
official symbolic enumerator for 19 is claimed.

The inspected implementation supplies its **operational meaning**:

1. `VidSchiSendToExecutionQueue` prepares virtual-submit data and calls the
   dxgkrnl submit wrapper at RVA24598. It stores the returned W0 in its internal
   submission record, then tests W0 at RVA245b8.
2. W0=0 bypasses the error path. Nonzero enters RVA24b04. With the boolean
   argument preserved at `[sp+0x12]` set, RVA24b10 puts **0x13 into W1** and
   RVA24b18 calls `VidSchMarkDeviceAsError`. The captured return is 24b1c.
3. `VidSchiMarkDeviceAsError` derives FromUserMode from the high bit and logs
   the lower 31 bits as Reason. Here the literal is 0x13, hence false/19.
4. `ADAPTER_RENDER::DdiSubmitCommandVirtual` calls the miniport at RVA22a8f0
   and preserves its status in W24. It compares this with zero and the literal
   **0xc000000d** at RVA22ab30 before returning it. Unexpected statuses take an
   additional triage path; the event itself does not record W24.

Microsoft's documented virtual-submit contract permits well-formed submission
success and invalid DMA/private-data rejection with `STATUS_INVALID_PARAMETER`;
that rejection places the calling device in an error state. It also specifies
completion-fence accounting for rejected submissions and distinguishes this
from the physical SubmitCommand path. [DxgkDdiSubmitCommandVirtual](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual).

Therefore **Reason19 is the scheduler's virtual-submit failure path in this
trace**. It is not a captured HRESULT, a PnP Code19, a named residency error,
a timeout, or proof of a firmware/page fault. The stack is taken after the KMD
has returned, explaining the absence of an AppleAgx frame. It positively selects
SubmitCommandVirtual rather than Patch, BuildPagingBuffer, Present, or an
interrupt/fence callback as the immediate reporting boundary. Earlier state in
those subsystems could still influence the submission's validation result.

## Mapping to render-admission; where attribution stops

`src/driver.c:98` registers `AdmissionDdiSubmitCommandVirtual`.
`src/gpuva_g3_windows.c:758` owns that entry point. Its branch families are:

| Source boundary | Possible rejection / handoff |
|---|---|
| 766–778 | adapter, arguments, nonzero command/fence, context ownership, node/engine and IRQL checks; INVALID_PARAMETER |
| 779–780 | system context → AdmissionGpuvaG3SubmitVirtualPaging |
| 784–785 | nonzero UMD-private size → AdmissionG4SubmitVirtualEnvelope |
| 786–811 | legacy shadow, process/root, flags, VA range and recorded packet checks; INVALID_PARAMETER |
| 834–840 | AdmissionDdiSubmitRender failure folded to INVALID_PARAMETER |

For native G4, `AdmissionG4SubmitVirtualEnvelope` (647–755) can return the same
INVALID_PARAMETER at its initial state/context/flags/scanout gate, graph-root
gate, parse/output validation, packet-preparation gate, or bind/queue rollback.
All are indistinguishable in the saved ETL. No Args snapshot proves the
UMD-private-size branch, nor does the 3.7-us duration prove the initial gate.
The source-backed leading path is native G4, but the outer gates and other
branches must remain observable in the next diagnostic.

UMD `umd/src/umd_gpuva_windows.c:182` constructs `D3DDDICB_SUBMITCOMMAND`, calls
`pfnSubmitCommandCb`, then logs `g4-submit-command-cb`. That log uses the ordinary
128-record quota in `umd/src/umd_runtime_device.c:41–80`. `runtime-set-error` is
also quota-limited. A successful enqueue callback need not imply later KMD
acceptance on the scheduler worker; missing UMD error lines cannot override
this kernel stack. This explains why the callback and KMD boundary require
separate receipts.

**Stop point:** no source predicate, malformed field, or exact returned status
was measured. Do not loosen flags, attachment counts, graph access, packet
ownership, IRQL, caps, or fence rules based on this attribution alone. The
compiler-source repair is independent; it cannot be called a repair for this
scheduler rejection without evidence.

## Proposed bounded receipt and host mock — not implemented

The B3 fallback is refined because the stack did identify the DDI but not its
inner branch. Three options were considered: repeat broad ETW (still no KMD
predicate), remove the whole UMD success quota (unbounded churn), or record a
bounded first failure plus lifecycle. Choose the third; retain existing ETW for
cross-checks. This is a diagnostic design, not an implementation authorization.

**KMD:** capture a fixed binary first-failure record at the outer
SubmitCommandVirtual return and annotate its actual failing substage. Reserve
one adapter-global first record plus a bounded table of 64 context-generation
records; never overwrite the global first failure. Overflow has a counter and
explicit incomplete-coverage flag. Use preallocated nonpaged storage and atomic
claim/write/publish states; readers accept only fully published records. No
allocation, file/registry I/O, waits, GPU access, or extra locks in the callback.
Persist a copied snapshot using an existing PASSIVE-level collector/worker;
durability is not claimed until an explicit drain receipt exists.

Record version, experiment/package identity reference, adapter epoch, monotonic
sequence and QPC, current PID/TID **and separately owner process/device/context
generation**, DDI/subpath/stage ID, exact returned NTSTATUS, original downstream
status or parse result, IRQL, command VA/length, private-data sizes, flags,
node/engine and submission fence. Capture only already validated scalar fields;
invalid context pointers get a validity mask, not an unsafe extra dereference.
For each failing predicate, record the operands already evaluated on that path.
Preserve short-circuit evaluation and return behavior. Do not copy arbitrary
private payloads or traverse the graph solely for diagnostics.

The branch ID must distinguish outer validation, paging, native envelope,
legacy shadow, root/poison state, parser result, output translation, prepare,
bind and queue/rollback. A first failure must survive later success, repeated
failures, context destruction and address reuse. Firmware or driver reload
starts a new epoch, never a silent reuse of the old identity.

**UMD lifecycle:** use a separate bounded channel independent of ordinary
success chatter and the `reject-` string convention. Give each process lifetime,
module load and device generation an explicit identity, plus sequence/QPC,
PID/TID, event kind and exact HRESULT. Capture create/destroy entry/exit,
first FlushRetire final outcome, runtime SetError, first submission and
residency/wait failure, and first Present entry/final outcome. Deduplicate
repeated errors per device+stage and retain counts; don't reinterpret E_PENDING
as a terminal MapGpuVA/residency failure. Capture the final result at the exit,
not merely the existing `flush-state` line inside the operation.

A concrete bound for a future candidate: 1-MiB process-lifetime journal,
maximum 256 device generations, 24 reserved 128-byte lifecycle slots per
generation, plus a 64-KiB frozen first-failure snapshot and loss counters.
Exhaustion stops new detailed generations, increments dropped counters and
preserves the earliest failure. A run-level collector also needs an aggregate
size/time cap; it must report truncated coverage. Module reload must reuse the
process-lifetime journal through the collector, not reset a DLL-static quota.
Use ETW image load/unload if enabled or mark unload as unobserved; never perform
blocking file operations under DllMain/loader lock. No successful teardown may
be invented after an AV. Read/drain paths preserve GetLastError and all graphics
results. Prove diagnostic-off/on behavioral equivalence apart from measured
recording cost.

**Host mock gate:** extend the real-function extraction pattern in
`tests/test_g4_submit_virtual_replay.py` / `tests/g4_submit_virtual_replay.c`,
including the outer SubmitCommandVirtual dispatcher (currently not extracted),
and exercise the real UMD channel with a mock sink, clock and runtime callbacks.
Use Windows-width ABI types; the current replay's `unsigned long ULONG` should
not be used to infer Windows structure layout on an LP64 host.

1. Generate >128 ordinary startup callbacks (e.g. 1024), successful creation,
   one queued UMD callback, then inject a KMD rejection and UMD final
   FlushRetire/SetError/Destroy. Verify precise statuses and identities survive
   without raising the ordinary quota. An enqueue S_OK followed by KMD failure
   must remain two distinct results.
2. Drive each real outer/native rejection family one at a time; verify its
   branch ID, original status/parser result and unchanged return/dispatch count.
   Include malformed outer inputs without diagnostic dereferences, root/poison,
   flags, graph access, output resolution, packet contention, bind and rollback.
   Traces provide examples only; no observed size/offset/bind whitelist.
3. Race two failures and subsequent successes; verify one complete immutable
   first record, no torn payload, counted duplicates and unchanged scheduler
   behavior. Reuse a context address and reload the UMD: epochs/generations
   distinguish lifetimes and retain the earlier process record.
4. Exhaust context/generation/byte limits, inject sink failure and simulated
   abrupt process exit: bounded memory/output, explicit loss counters, earliest
   failure intact, no fabricated destroy/unload. Assert no I/O/wait at raised
   IRQL, no I/O under loader lock, and unchanged LastError/results.
5. Separate accepted E_PENDING followed by successful fence wait from a failed
   wait. Compare diagnostic-off/on callback return sequences, fence operations,
   dispatch counts and resource lifetimes. The old capped logger must fail the
   >128 visibility case; the proposed design must pass before any packaging.

## Source-first ownership, next gate and recovery

Sources inspected: saved EXP848 trace/AutoLogger/package manifest and KMD source;
Microsoft dxgmms2/dxgkrnl PE/public PDBs and provider schema; pinned WDK26100
`d3dkmthk.h`; registered KMD submit and G4 validation functions; UMD callback and
logging code; actual G4 host replay; official ETW and WDK pages linked above;
current GPU state and EXP848 launch/recovery entries.

This boundary is Windows scheduler/DDI return handling. No hardware-register,
power, interrupt-route, DMA-map or firmware protocol change is designed.
Consequently live ADT acquisition and unrelated Asahi Linux, m1n1 and Mu source
archaeology are not warranted here and live access is explicitly excluded.
Mesa/UMD produces native commands; Windows/VidSch invokes and interprets the DDI;
KMD validates process/command/graph ownership and owns runtime submission,
DMA/fence/interrupt behavior. m1n1/Mu retain the R110 power/reservation/ACPI and
launch contracts. The proposed channel observes their existing boundary and
adds no new initialization/power/recovery owner.

WHY CONTINUE COMPARISON: the same saved EXP848 event now supplies its real
stack and the profiler entry/exit bracket; a single current-binary call-site
pass distinguishes submission from paging, residency, Present and IRQ paths.
That pass is complete. Do not repeat older-reference comparisons or rebuild
WDDM admission: the remaining discriminator is the exact KMD rejection stage.

Smallest next gate is offline real-C diagnostic replay described above.
If a later, separately authorized hardware candidate is made, its falsifiable
checkpoint is one Apple-context SubmitCommandVirtual rejection with a complete,
package-bound branch/status/Args receipt and its corresponding ETW error, or
acceptance/completion that disproves the rejection hypothesis. Missing receipt
with another Reason19 is a diagnostic failure, not proof the KMD was not called.
No hardware run is authorized by this report. Preserve the EXP848 accepted
Code0 recovery contract: evidence first, ordered restart, immutable GPU-hidden
exact cleanup, ordinary GPU-visible Code28. No live Code0 package removal.

## Review dispositions and verification

Read main-workspace `.local/tandem/REVIEW.md` (worktree-local copy absent).
The individual dispositions below concern this offline task; historical verdicts
are not reopened. Full host product tests/builds are not needed for an
analysis-only change. Verify original hashes, raw/XML event agreement, public
symbol call sites, report references, ledger schema and clean product diff.
No regression fix or RED→GREEN result is claimed; the future host mock is a
design only.

- REVIEW R113: DEFER — no staging or ordinary/full-owner transition in this offline task; retain the current-state restriction.
- REVIEW R111: DEFER — prior memory-cache issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R110: DEFER — prior ACPI issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R109: DEFER — prior FV dispatch issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R108: DEFER — prior firmware issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R107: DEFER — prior boot issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R106: DEFER — prior transition/reserve issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R105: DEFER — allocation matrix is a prior boundary; current evidence crosses CreateDevice, no matrix rerun.
- REVIEW R104: DEFER — prior page-size issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R103: DEFER — prior allocation issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R102: DEFER — prior allocation issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R100: DEFER — prior allocation-map issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R99: DEFER — prior device-creation issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R98: DEFER — prior frame-size issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R97: ACCEPT — one exact-artifact comparison only; no older-reference archaeology.
- REVIEW R96: DEFER — prior TA/3D builder issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R95: DEFER — prior GPUVA audit issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R94: DEFER — prior GPU object ownership issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R91: DEFER — prior scanout issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R90: DEFER — R90a already retracts image-content inference; EXP848 uses saved zero-scanout evidence, no display experiment.
- REVIEW R88: DEFER — prior G5 issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R86: DEFER — prior broker capacity issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R85: ACCEPT — preserve package/source hashes and explicitly qualify reconstructed Windows image bases; no packaging.
- REVIEW R74: DEFER — prior CDD context issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R71: DEFER — prior paging issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R69: DEFER — prior live-bind issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R65: ACCEPT — inspect the existing virtual-submit ABI boundary; no new ABI or compute admission. Exact failed predicate remains unmeasured.
- REVIEW R64: DEFER — firmware reserve authorization does not authorize hardware or firmware edits in this task.
- REVIEW R63: DEFER — prior reserve issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R57: DEFER — BuildPagingBuffer is not the immediate Reason19 caller; submit status handling is analyzed separately without changing paging returns.
- REVIEW R55: DEFER — prior PnP restart issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R54: DEFER — no hardware series is run; current user explicitly requires offline-only analysis.
- REVIEW R49: DEFER — prior caps issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R48: DEFER — prior serial ownership issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R47: DEFER — prior firmware profile issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R45: DEFER — prior cleanup issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R40: DEFER — prior watchdog issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R37: DEFER — prior recovery issue is outside the EXP848 Reason19 boundary; no new claim or change is made.
- REVIEW R64: DEFER — G4 command ABI implementation is outside Reason19 diagnostic design; no new ABI is selected.
