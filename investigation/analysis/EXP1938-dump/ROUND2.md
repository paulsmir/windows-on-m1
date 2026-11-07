# EXP1938 dump pass 2: recover adapter discovery without ending CDB

This supersedes the pass-1 command file for the next saved-dump inspection.
Pass 1 and its original README remain unchanged as historical artifacts.
TASK.md Round 2 requires commands, then a stop until `cdb-output-2.txt` is
returned. This change fixes the inspection script; it does not select or
implement an unproven firmware/driver remedy.

Run `cdb-commands-2.txt` on **MEMORY-1938.DMP** with the **matching EXP1938
private PDB**, using the same `$$<` invocation as pass 1. Capture the entire
output, including errors. For example, after copying the script to the
builder, keep the established `-z` and `-y` arguments and use:

```text
-c "$$<C:\Users\pauls\cdb1938-2.txt;q"
```

The outer `q` is after the script, not inside it. The script itself never
quits, detaches, resumes, calls target functions, or writes target memory.
No hardware checkpoint is justified before this offline result.

## What failed, with evidence

In handoff `cdb-output-1.txt:92`, the `.if` containing `@@c++` arrived as
`++(...)` and failed parsing. Lines 95–104 then show an unusable Device
pointer and `Memory read error 000000030090015c`; lines 108–112 show the
failed adapter read followed by `STOP_ADAPTER_UNAVAILABLE` and `quit:`.
The subsequent `Illegal thread`/`NoTarget` errors are consequences of losing
the dump session, not evidence about the GPU runtime.

The task describes the receipt context as unmapped. More precisely, this
output establishes that its ownership chain is unusable; it does not by
itself distinguish a missing context page from a freed/reused context or
another invalid interpretation. The failed identity check never validated
that object. Pass 2 therefore does not use that address at all.

The bugcheck parameters and stack remain usable evidence:
`cdb-output-1.txt:73` reports 0x116 with parameter 1
`ffffd684d64e3010`, and line 80 identifies frame 3 as
`dxgkrnl!DXGADAPTER::PrepareToReset`. Parameter 1 is used only as the TDR
recovery context, never cast as an Apple adapter. Frame 3 is displayed for
locals/register evidence, then the command file restores frame 0; no saved
register is assumed to be `this` or a miniport context.

## Source-backed adapter path and contract

Paths below are relative to the repository. `RA` means
`drivers/apple-agx/render-admission`; `SH` means `drivers/apple-agx/shared`.

- `RA/src/umd_render_windows.c:129` defines the actual static pointer
  `AdmissionUmdRenderTraceAdapter`. Lines 132–145 store an `ADMISSION_CONTEXT *`
  and load/clear it atomically. This is a private-PDB data symbol, not an
  invented global. It exists under `APPLE_AGX_SUBMIT_QUALIFICATION`.
- `RA/src/allocation_windows.c:932` arms it on successful allocation open;
  line 1017 disarms it on close. **It is optional diagnostic state, not a
  lifetime root.** Null is a legitimate result even with a live adapter.
  Symbol absence, null, bad magic, or missing pages must remain unavailable
  evidence. No speculative fallback pointer is installed automatically.
- `RA/include/render_admission.h:591` puts `ObjectAdapter` first;
  `RA/include/render_objects.h:29` puts its magic first and line 4 defines
  `0x414f4152`. Thus MASM `dwo(candidate)` checks the source-proven zero offset.
  The script prints the PDB layouts with `dt -v` to confirm this contract.
- `RA/include/render_admission.h:609` declares `PlatformRuntime` as `PVOID`.
  Its nonzero offset is obtained from the matching PDB, not guessed or copied
  from a different build. Typed evaluation is on an independent top-level
  `r ... = @@c++(...)` line, the form that parsed at pass-1 line 95.
- `RA/src/backend_platform_windows.c:116` puts `Adapter` first in the runtime.
  Lines 4721 and 4731 establish the two-way adapter/runtime association.
  MASM `poi(runtime)` must equal the validated adapter. Failure clears the
  runtime pseudo-register and prints an explicit marker.
- `RA/src/lifecycle.c:176` allocates the miniport context separately and
  returns it through `MiniportDeviceContext` at line 195. A device extension
  printed by `!drvobj` must **not** simply be cast to `ADMISSION_CONTEXT`.
  A fallback needs a symbol-described DXGK miniport-context field.

The implementation plan is limited to replacing the stale seed, removing
session-ending branches, moving all typed expressions out of control blocks,
and retaining bounded independent evidence commands. Every potentially
failing pseudo-register assignment is preceded by a separate zero assignment;
failed evaluation cannot reuse a pointer from an earlier queue read/run.
All `.if` expressions and bodies use MASM/ordinary debugger commands.

Only debugger state changes. The KMD still owns runtime/mailbox/queue/reset
operation; the retained broker still owns its system crash buffer. No
initialization, interrupts, DMA, power, or recovery ownership changes.
Asahi/m1n1/Mu restart comparisons and Windows reset design are deferred until
the dump selects that behavioral question. No external implementation is
copied. This pass uses current source and the returned debugger transcript;
it cannot validate CDB execution locally.

## Exactly what to look for in each section

| Section | Expected output and interpretation |
|---|---|
| **00 SYMBOLS_AND_DUMP** | Force normal matching-symbol loading (no `/i` override), then module/PDB identity, the same four 0x116 parameters, and stack. Reject a later PDB merely sharing the file path. A mismatched dump/PDB invalidates typed interpretation. |
| **01 INDEPENDENT_ADAPTER_DISCOVERY** | The address/type/value of `AdmissionUmdRenderTraceAdapter`; driver object and device details from `!drvobj`; typed TDR recovery context at this dump's parameter 1; DXGADAPTER layouts and frame-3 locals/registers if symbols permit. These commands run before any dependent chain. Both DXGADAPTER type spellings are discovery alternatives; one or both may be absent from public symbols. Return such errors. If the global is unavailable, these outputs provide the device/DXGK addresses and field names for a bounded follow-up, not a guessed extension offset. |
| **02 ADAPTER_GLOBAL_AND_PDB_LAYOUTS** | PDB layouts, then `ADAPTER_MAGIC_OK` and adapter fields. `ADAPTER_UNAVAILABLE_OR_BAD_MAGIC_USE_SECTION_01` means dependent adapter/runtime values are not interpretable; keep the independent section-01 output. A raw symbol address is not its pointer value. Magic is necessary but does not prove lifetime; check PDO, Started, and the runtime back-pointer together. |
| **03 PLATFORM_RUNTIME** | `RUNTIME_BACKPOINTER_OK`, worker/reset/stop flags, readiness, LastPollMs/LastProgressMs, and GpuFault. Without that marker, do not interpret the runtime sections, even if `dt` emits a type layout at address zero or errors at low member offsets. The flags can legitimately reflect a reset in progress. |
| **04 RTKIT_SESSION_AND_LAST_MESSAGE** | Boot readiness, Running, CpuReady, StopPhase, CrashlogReplySent, CrashlogCrashed, crash GPU address/capacity, ReceivedCount, LastRxEndpoint/Payload, and validity-marked failure mailbox values. Two top-level expressions decode payload bits 63:52 and 63:48. This is the runtime discriminator. An old LastRxPayload is not proof that the latest drain received that message. |
| **05 DRAIN_RESULT_EVIDENCE_NOT_A_STORED_LAST_RESULT** | GpuReset (including retirement snapshots/history), both render-correlation slots, heartbeat receipt. There is no session LastDrainResult. A slot must match the failing fence and have worker-exit ValidMask bit `0x100`; only a failure associated with the RTKit notification gate makes WorkerStatus an RTKit result. A generic reset Poll outcome also covers provider polling. Heartbeat Result is a different operation; odd Sequence is in progress, even is published. |
| **06 TRANSPORT_METADATA_ONLY** | ASC callbacks/context, transport Base/Length and TraceCount. Check pointer/mapping consistency; Base is MMIO metadata and is never dereferenced. This cannot recover actual mailbox hardware state. TraceCount is not a rolling recent-error log. |
| **07 CRASH_BUFFER_OWNER** | BrokerOnly, crash object GPU VA, object-7 CPU pointer/length. Read at most 16 KiB only when the validated runtime has a nonzero CPU mapping and sufficient length. Missing pages and a missing CPU mapping are unavailable evidence, not a zero crash buffer. BrokerOnly normally skips this Windows allocation; never treat its GPU VA as CPU VA or physical memory. |
| **08 QUEUE_AND_OWNERSHIP** | Scheduler, active render packet, pending count/head, backend phase, provider event guards/pointers/message, queue-provider phase/fence/refusal state, and queue-runtime pending/completion/recovery values. Compare the late active fence `0x1973d` with completed `0x1973c`; distinguish the earlier GPU-fault fence `0x19472` from the late failure. LastDrainGuard describes the event channel, not RTKit. |
| **09 QUEUE_SHARED_WORDS** | One 32-bit stamp, CPU write pointer, and GPU done pointer for each TA/3D/compute queue. Compare them to expected/pending queue-runtime values and reset snapshots. Null compute bindings may be legitimate. `WORD_NULL_OR_LOOKUP_FAILED_CHECK_PREVIOUS_OUTPUT` deliberately does not equate a lookup error with an unbound queue. |

Normal completion prints `END_EXP1938_PASS_2`. An individual error must not
produce `quit:`/`NoTarget` before that marker. Dependent sections can still
fail when their owner is missing; the script keeps collecting independent
commands, not fabricating an owner. Raw reads are one global pointer, magic
and back-pointer checks, at most 16 KiB of crash data, and nine queue words.
Typed output has fixed recursion depths. There are no memory-search loops.

## Outcome decisions and source evidence

- **Firmware crash:** a valid runtime with `CrashlogCrashed != 0` proves the
  sticky crash state that makes every subsequent drain return result 8.
  Endpoint 1 with payload `>>52 == 1` after `CrashlogReplySent` sets it
  (`SH/src/apple_agx_rtkit_session.c:266`, `:290`). A firmware restart and
  ownership/recovery design is then needed; clearing the bit, claiming fence
  completion, or freeing firmware-owned memory is not a remedy.
- **Protocol message:** crash flag clear plus a correctly correlated result 6
  or failure-associated message points to protocol handling. Event wakes and
  management PONG are accepted; other messages currently return
  ProtocolViolation (`SH/src/apple_agx_rtkit_session.c:284`). Identify the
  exact message against primary RTKit sources and handle it if legal. A
  consumed unexpected message alone cannot explain repeated failures: the
  next empty-mailbox drain succeeds.
- **Transport:** correlated result 5 or a concrete callback/mapping violation
  directs the fix to ASC transport. `SH/src/apple_agx_asc_transport.c:143`
  implements nonblocking TryReceive; Read32/Read64 failure yields transport
  failure. This path has no polling deadline, so do not infer a timeout from
  a stale message or generic reset failure. Validity flags govern saved
  failure mailbox values; dump data cannot prove current MMIO accessibility.
- **Lifecycle/argument:** result 1/2 or violated Running/CpuReady/Boot/StopIdle
  prerequisites directs investigation to initialization/runtime ownership
  (`SH/src/apple_agx_rtkit_session.c:261`). It does not by itself prove a
  firmware crash. Enums are at `SH/include/apple_agx_rtkit_session.h:8`:
  0 OK, 1 argument, 2 state, 3 timeout, 4 clock regression, 5 transport,
  6 protocol, 7 cleanup, 8 firmware crash.
- **Missing or apparently healthy:** preserve the exact gaps and errors.
  Null diagnostic global is expected after a close; try the returned
  symbol-described DXGK/device chain in a bounded follow-up. Healthy fields
  without a correlated return code cannot reconstruct the original cause.

Additional inspected contracts:

- `SH/include/apple_agx_rtkit_session.h:29` lists persistent session fields;
  none stores the most recent drain result.
- `RA/src/backend_platform_windows.c:4222` drains RTKit and records the
  failing result at line 4227. Reset at line 5160 loses that specific enum
  into a generic Poll failure; line 5169 also maps provider failure to Poll.
- `RA/include/render_call_correlation.h:5` fixes capacity at two slots;
  line 28 defines the worker-exit bit. `RA/src/render_call_correlation.c:169`
  writes WorkerStatus only into a matching fence slot.
- `RA/src/backend_platform_windows.c:78` defines the separate heartbeat
  receipt and its sequence publication contract.
- `SH/src/apple_agx_initdata_memory.c:167` skips the Windows crash allocation
  in BrokerOnly mode. `RA/src/backend_platform_windows.c:1875` checks the
  retained system arena, and line 1896 stores its GPU VA/capacity in RTKit.
  The 16-KiB GPU mapping is not a Windows CPU mapping.

## Verification and remaining unknowns

`test_apple_agx_dump1938_commands` is a **static script regression**, not a
CDB parser emulator. All three tests failed against the unchanged pass-1
commands (seven failures including conditional subtests); all three pass
with pass 2. They catch premature session exit, typed expressions inside
control blocks, and dependence on the stale receipt seed, and require
independent discovery plus reset-before-evaluation pseudo-register handling.
The RTKit, ASC, TDR-reset and platform-provider host suites also pass.
Final baseline results and commit/artifact hashes are in handoff `REPORT.md`.

Still unknown: private symbol/type availability; whether the global is armed;
which owner pages are present; actual RTKit/crash/queue state; any retained
drain result; crash-buffer bytes; and the causal relationship between the
earlier GPU fault and later reset failure. CDB cannot be run here, so neither
syntax execution nor successful owner discovery is claimed.

The smallest next checkpoint is Claude's offline run of these commands and
return of **cdb-output-2.txt**. Only then choose a source-backed behavioral
remedy with its own regression. A subsequent hardware checkpoint belongs to
Claude: reproduce this workload/reset boundary with that single selected
change, demonstrate genuine retirement or a fully designed restart, and
preserve DMA ownership and the known recovery artifact. No new hardware run,
package, or restart design is authorized by this command-only pass.
