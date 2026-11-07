# EXP1938 dump pass 1: RTKit failure discriminator

This pass prepares offline debugger commands, not a driver behavior change.
The task explicitly requires stopping until Claude returns `cdb-output-1.txt`.
Run `cdb-commands-1.txt` against the saved **MEMORY-1938.DMP**, with the
**matching EXP1938 private PDB already loaded**. Capture all output, including
errors, as `cdb-output-1.txt` in the dump1938 handoff folder. In an existing
CDB dump session, `$$><` followed by the local command-file path executes it.
The file does not load a new PDB, force mismatched symbols, call target
functions, read live MMIO, edit target memory, or continue target execution.
It sets debugger pseudo-registers only. `q` on a failed identity check exits
the dump session. Normal completion leaves the session open.

Source paths below are relative to the repository; `RA` means
`drivers/apple-agx/render-admission`, and `SH` means
`drivers/apple-agx/shared`. Base commit:
`06143e0e01e5e05735f07664746f4fb6e8ccb670`.

## Why these commands

The saved first debugger pass identifies `VIDEO_TDR_FAILURE` (0x116),
parameters `ffffd684d64e3010`, `fffff803c291a9f0`,
`ffffffffc0000483`, `3`. Its private symbols identify parameter 2 as
`AdmissionDdiResetFromTimeout`; the actual current stack is in DXGK after
that callback returned. Parameter 1 is the DXGK recovery context, **not**
`ADMISSION_CONTEXT`. No callback argument is assumed to remain in x0.

The earlier offline report and receipt at
`.local/experiments/EXP1938-retire-evidence/evidence/EXP1938/kmd-receipts-probe-end.txt:188`
identify DWM's fault context `ffffd684d8ff7cd0` and fault fence `0x19472`.
The reset at line 191 instead retains active fence `0x1973d`, completed
`0x1973c`, and `SchedulerFaulted=0x41082`. Source at
`RA/src/backend_platform_windows.c:4222` calls the RTKit drain; line 4226
records that first-fault site. Reset calls the same drain before provider
polling at lines 5160–5171. This is the boundary to discriminate.

The context address is a **candidate seed from this dump's run**, not a
global or a guessed offset. It can have been freed/reused. The command file
checks context/device/adapter magic and runtime back-pointer before using
it. If any check fails or an expression cannot be evaluated, stop interpreting
dependent sections and return the error output. Do not substitute the TDR
context address. Resolving a replacement seed from DXGK's adapter object
would require a separate symbol-assisted pass, not a raw memory sweep.

## Output sections and interpretation

| Section | What it should show | How to use it |
|---|---|---|
| 00 SYMBOLS_AND_DUMP | Module/PDB identity, bugcheck parameters, ten stack frames. | Require the matching private PDB and the expected 0x116. The first pass used the EXP1938 PDB in `C:\Users\pauls\AD04-persistent-dwm-next\drivers\apple-agx\render-admission\ARM64\Release`. Do not use a later rebuilt file merely because its path matches. |
| 01 RECEIPT_CONTEXT_TO_ADAPTER | Context `Object.Device`, device `Adapter`, adapter magic/started state, platform pointer and first scheduler fault. | Magic values are RAOC `0x434f4152`, RAOD `0x444f4152`, RAOA `0x414f4152`. `ObjectAdapter` is the first adapter member. This establishes the typed ownership chain, subject to object lifetime. |
| 02 PLATFORM_RUNTIME | Adapter back-pointer, worker/reset/stop flags, runtime readiness, last poll/progress, stored GPU fault. | Back-pointer must match. Correlate the first GPU fault with fence `0x19472` and the receipt context. Worker flags and readiness explain whether reset is observing a running, stopping, or inconsistent runtime. |
| 03 RTKIT_SESSION_AND_LAST_MESSAGE | Full session including boot phase, Running/CpuReady/StopPhase, crash grant/flag, ReceivedCount, LastRxEndpoint/Payload, failure-mailbox validity and values. Two expressions decode the payload's management/crash type and event-wake class. | This is runtime memory, unlike the bootstrap registry snapshot. A last message is not automatically the message that caused the most recent drain failure. |
| 04 DRAIN_RESULT_EVIDENCE | Reset receipt/history and retirement operands; two-slot render correlation; heartbeat receipt. | There is **no persistent LastDrainResult member** in the session. A correlation slot with matching fence and worker-exit valid bit `0x100` may preserve `WorkerStatus`; interpret it as the RTKit enum only when the recorded failure site is the notification gate. Unmatched/absent slots prove nothing. Heartbeat Result belongs to its own Calls/Fence/Sequence, not the drain. |
| 05 TRANSPORT_METADATA_ONLY | ASC callback table/context, Base/Length and TraceCount. | Check callback/context consistency and mapping metadata. Do not dereference ASC Base: MMIO cannot be reconstructed by reading the offline dump. TraceCount describes the first 64 traced 64-bit accesses, not a rolling failure trace. |
| 06 CRASH_BUFFER_OWNER | BrokerOnly, crash GPU VA, data-object index 7, and at most one 16-KiB CPU buffer. | Read bytes only when a CPU mapping and sufficient object length exist. `CLHE` is a possible crash-log header, but a readable zero buffer is not proof that firmware did not crash. Missing pages or absent CPU mapping are unavailable evidence. |
| 07 QUEUE_AND_OWNERSHIP | Scheduler, render packet, pending count/head, backend, provider guards/event state, queue provider and runtime pending/completion/recovery state. | Compare actual pending fence to `0x1973d`, packet ownership, and reset receipt. Provider LastDrainGuard is an **event-channel guard**, not an RTKit return code. |
| 08 QUEUE_SHARED_WORDS | One 32-bit stamp, CPU write pointer and GPU done pointer for each of TA/3D/compute, only when bound. | Compare to the queue runtime's expected stamps/done pointers and before/after receipt. Unbound compute is legitimate for a TA/3D job. Missing dump pages remain unknown; never substitute zero. |

The maximum raw buffer read is 16 KiB plus nine 32-bit queue words. Typed
structure output uses fixed recursion depths, and there are no memory scans
or traversal loops. All offsets come from private PDB types. The only
explicit data-object index, 7, is `AppleAgxInitdataMemoryCrashlog` in
`SH/include/apple_agx_initdata_memory.h:26`.

## Outcome decisions

- **Firmware crash:** `CrashlogCrashed != 0` makes every later drain return
  `FirmwareCrashed` (8) before receiving another message. Endpoint 1,
  payload `>>52 == 1` and an earlier grant (`CrashlogReplySent`) set this
  sticky flag. This requires a firmware restart/ownership design, informed
  by any available crash log. Do not clear the flag or claim retirement.
- **Protocol message:** healthy session prerequisites, crash flag clear,
  and an authenticated failure-associated unexpected message or stored
  result `ProtocolViolation` (6) point to a missing/invalid protocol path.
  Event wake endpoint `0x20` with upper 16 bits `0x42`, and decoded management
  PONG on endpoint 0, are accepted. Identify the exact message in primary
  RTKit implementations before adding its handler; do not accept all messages.
  A remembered unexpected message alone does not establish repeated failure:
  it is consumed, and a subsequent empty mailbox would succeed.
- **Transport:** an authenticated result `TransportFailed` (5), or evidence
  of failed callback/mapping validation, directs work to the ASC transport
  owner. TryReceive has no polling deadline: timeout/clock-regression is not
  an ordinary explanation for this nonblocking drain. Check null callbacks,
  context, Base/Length and validity-marked failure snapshots. A stale
  LastRxPayload does not prove a new receive occurred. Transport diagnosis
  remains provisional without a result or concrete invariant violation.
- **Invalid state/argument:** result 2 or 1, or failed Running/CpuReady/boot
  ready/StopIdle predicates, directs investigation to runtime lifecycle or
  callback initialization. Do not restart firmware solely from a generic
  poll failure. StopPhase and worker ownership must agree.
- **All apparently healthy or unavailable:** the dump may lack the original
  failure result/pages, or fields may have changed after the failure. Record
  exactly which evidence is missing and return the bounded output. Do not
  choose a behavioral fix by elimination from incomplete data.

Result values are declared at `SH/include/apple_agx_rtkit_session.h:8`:
0 OK, 1 invalid argument, 2 invalid state, 3 timeout, 4 clock regression,
5 transport failure, 6 protocol violation, 7 cleanup failure, 8 firmware crash.

## Source evidence and invariant

- `RA/include/render_admission.h:591,609,846,854` and
  `RA/include/render_objects.h:4,29,35,44`: context → device → adapter ownership,
  adapter-first object member, runtime pointer and identity constants.
- `RA/src/backend_platform_windows.c:116,124,144,4720,4731`: runtime owns RTKit,
  queue provider and backend; bidirectional adapter/runtime initialization.
- `SH/include/apple_agx_rtkit_session.h:29` and
  `SH/src/apple_agx_rtkit_session.c:252`: persistent session fields and exact
  runtime drain guards, sticky crash behavior, accepted messages and failures.
- `SH/src/apple_agx_asc_transport.c:143` and
  `RA/src/backend_platform_windows.c:1481,1496,1507`: nonblocking receive and
  concrete Windows range checks/read callbacks.
- `RA/src/backend_platform_windows.c:4227`,
  `RA/src/render_call_correlation_windows.c:263`,
  `RA/src/render_call_correlation.c:169`,
  `RA/include/render_call_correlation.h:5,28`: optional result capture only for
  a matching retained correlation slot; capacity two, no universal drain log.
- `RA/src/backend_platform_windows.c:78`: heartbeat receipt is a separate
  operation. Odd Sequence is in-progress, even Sequence is published.
- `SH/src/apple_agx_initdata_memory.c:167` and
  `RA/src/backend_platform_windows.c:1871,1896`: BrokerOnly deliberately skips
  the Windows crash allocation and obtains the 16-KiB system crash buffer's
  GPU address from the retained broker. It is **not a CPU VA or physical PA**.
  The command file does not try to translate an unrecorded broker mapping.
- `SH/include/apple_agx_g13_queue_runtime.h:37,141`,
  `SH/include/apple_agx_platform_provider.h:140`,
  `RA/include/render_gpu_fault.h:156`: queue pointer/pending state, event-channel
  diagnostics and reset history/retirement snapshots.
- `RA/src/backend_platform_windows.c:5090,5160` and
  `RA/src/scheduler_windows.c:750`: existing reset retains firmware ownership
  until genuine retirement; the retained firmware stop/restart path is not
  implemented/validated. A failed drain must not release active DMA owners.

No initialization, interrupts, DMA, power, queue or recovery ownership changes
are proposed. No external implementation is copied. Asahi/m1n1/Mu restart
comparison and the Windows reset contract belong to the next implementation
plan if the dump selects that remedy; this command-only pass cannot claim
that a restart design is already established.

## Verification and next checkpoint

Existing focused suites passed: `test_apple_agx_rtkit_session`,
`test_apple_agx_asc_transport`, `test_apple_agx_tdr_reset_quiesce`,
`test_apple_agx_platform_provider` (4 tests). The RTKit and TDR tests already
exercise protocol/transport/crash alternatives and preservation of DMA
ownership. No new production defect is proven here; no duplicate test or
speculative behavior change was added. CDB cannot run in this task, so command
execution, type availability in the actual PDB, and memory readability remain
unverified. Source-name/bounds inspection is not a debugger execution test.

Required full baseline command: `cd tests && python3 run_against_baseline.py`.

```text
ran 482 tests in 203 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The smallest next checkpoint is **offline CDB on the saved dump**, followed
by return of `cdb-output-1.txt`. No new hardware run is justified before that
selection. After a source-backed remedy and regression test, Claude's smallest
hardware checkpoint would be the same workload/reset boundary with the exact
selected change: genuine retirement or a fully designed restart, bounded
reset success, and no fabricated fence/free of firmware-owned memory. Firmware
restart needs a separate preregistered initialization/ownership/recovery plan;
it is not authorized or designed by this note. Keep the known recovery artifact
and collect failure evidence before exact package cleanup under the existing
owner's experiment procedure.
