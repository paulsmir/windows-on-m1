# EXP1938 round 3: repair owner discovery before interpreting RTKit

## Verdict

Round 2 does **not** settle firmware/RTKit state. TASK.md Round 3 therefore
selects its explicit fallback: write `cdb-commands-3.txt`, report the evidence,
and stop until Claude returns `cdb-output-3.txt`. No driver behavior change is
justified. This is an offline debugger-script correction, not another
receipt-only hardware experiment. No new hardware checkpoint is justified yet.

## Analysis with file:line evidence

`cdb-output-2.txt` below is the returned file in the dump1938 handoff folder.

- Lines 64–74 establish the loaded private PDB and the 0x116 bugcheck:
  `ffffd684d64e3010`, `fffff803c291a9f0`, `ffffffffc0000483`, `3`.
- Lines 91–102 show all discovery failures: the diagnostic global is absent,
  the queried driver object is not found, and the three DXGK types are absent.
  Public DXGK symbols still name the reset functions (lines 80–83), so their
  machine code is a usable source for a bounded follow-up.
- The driver-object query uses the **binary name**, not the registered service.
  `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.inf:34` registers
  `AppleAgxAdmission`; line 41 names its binary `AppleAgxRenderAdmission.sys`.
  Round 3 queries `\Driver\AppleAgxAdmission`. Whether that object is retained
  in this dump still requires CDB; fixing its name does not prove availability.
- `drivers/apple-agx/render-admission/src/umd_render_windows.c:128` guards
  `AdmissionUmdRenderTraceAdapter` with `APPLE_AGX_SUBMIT_QUALIFICATION`.
  Thus absence of that optional symbol does not imply a missing live adapter.
  Round 3 does not depend on it or on unavailable DXGK private types.
- Lines 388–405 leave adapter/runtime pseudo-registers zero after failed
  lookup/evaluation. The compound MASM condition then rejects `&&` with
  `Numeric expression missing`. Lines 394–395 and 804–805 also show command
  prefixes missing from the returned transcript. Their loss mechanism is
  **unknown**; no debugger line-length limit or transfer defect is established.
- Lines 406–414 print field **types**, not instance values. Lines 459–489 show
  RTKit fields as `??`, ending with a read error at `0x631`, the offset reached
  from a zero runtime. This proves failed inspection, not unmapped pages at a
  valid runtime address. Lines 678–686 do not read a crash buffer; lines
  707–893 likewise do not establish queue state. `END_EXP1938_PASS_2` at line
  960 establishes that the session stayed open through collection, nothing more.
- Lines 103–115 record frame 3 (`DXGADAPTER::PrepareToReset+0x230`) with
  x19=`ffffd684d3ce0000`, x20=`ffffd684d64e3010`. x19 is a **candidate DXGK
  owner**, not an established miniport pointer. Round 3 captures disassembly
  to determine its role rather than assuming an ARM64 saved register is `this`.

## Plan, contracts, and scope

The single change repairs discovery: use the installed service identity,
replace the failed expression/conditional chain with independent commands,
and inspect bounded reset-caller code/data to derive the actual context path.
The regression compares the command's driver-object identity against the INF
and rejects the C++ boolean operators that failed in MASM. Prior command files
and reports remain historical artifacts.

The miniport context is separately allocated and returned through
`MiniportDeviceContext` in `drivers/apple-agx/render-admission/src/lifecycle.c:182`
and `:195`; a WDDM device extension is **not** automatically that context.
`include/render_objects.h:4` (under the same render-admission directory)
defines adapter magic `0x414f4152`. `include/render_admission.h:592` places
`ObjectAdapter` first. The runtime association must be checked in both
directions, as established by `src/backend_platform_windows.c:4721` and `:4731`.
Round 2's PDB layout gives adapter `PlatformRuntime` at +0x130 (line 148)
and runtime `Adapter` at +0 (line 297), with `Rtkit` at +0x5c0 (line 304).
These are this PDB's offsets, not a contract for a different build.

No initialization, runtime, interrupt, DMA, power, or recovery ownership changes.
KMD owns its adapter/session/queue structures; DXGK owns its device extension
and reset-caller objects; the retained broker owns the broker-only crash buffer.
No Asahi/m1n1/Mu behavior is changed or copied. Their restart/ownership contracts
must be inspected if a later result selects restart implementation. No new
Windows DDI semantics or guessed register values are introduced in this pass.

## Run and output interpretation

Claude should copy the exact command file to the debugger host and use the same
saved dump and matching EXP1938 PDB as round 2. Keep the established `-z`/`-y`
arguments, and use `-c "$$<C:\Users\pauls\cdb-dumps\dump1938-3.txt;q"`.
Return the entire transcript, including errors, as `cdb-output-3.txt`.
The trailing `q` is in the invocation, never in the command file.

Every line in this script is an independent debugger command. There are no
conditional blocks, typed C++ expressions, pseudo-register pointer chains,
loops, target writes, target calls, or live-MMIO reads. Explicit reads cover
4 KiB at the frame-3 x19 candidate, 1 KiB at bugcheck argument 1, and 512 bytes
of the saved reset stack. Disassembly is bounded to 256 instructions for each
of two DXGK methods and 64 for each of two wrappers (640 instructions total).
`!pool` only inspects the specified candidate allocation; no `!poolfind` sweep.
All command lines are short (under 100 characters), a conservative simplification,
not a claimed fix for the unproven cause of the lost prefixes.

| Section | What output should show and what it means |
|---|---|
| 00 IDENTITY | Matching EXP1938 private symbols, same bugcheck and stack. Do not force a mismatched PDB with `/i`. |
| 01 INSTALLED_SERVICE_OBJECT | `!object` gives the actual driver-object address; `!drvobj ... 7` gives its devices/dispatch details. DriverStart should agree with the module range. If found, this repairs the earlier name failure. A returned device extension still belongs to DXGK until its miniport-context field is established. If absent, preserve the error and continue the independent caller path. |
| 02 RESET_CALLER_REGISTERS | Nonvolatile registers for frames 3, 4, and 5, then restoration of frame 0. Compare owner values across frames; do not use volatile x0 as the already-returned miniport argument. |
| 03 RESET_CALLER_CODE | Actual ARM64 instructions around reset: trace saved registers, object loads, callback dispatch and the value supplied as x0 to the miniport reset callback. Establish the DXGK field offset from this exact binary; the wrapper may call another helper. If the needed load is outside the fixed window, identify that specific function/address for a bounded follow-up, not a larger memory sweep. |
| 04 FRAME3_X19_CANDIDATE_NOT_MINIPORT | Pool metadata and 512 qwords at the recorded candidate. Combine with the disassembly to interpret individual fields. Pool tag/size or a pointer-looking word alone cannot validate an adapter. Unreadable output is unavailable evidence. |
| 05 BUGCHECK_ARG1_TDR_NOT_MINIPORT | 128 qwords at the TDR recovery context. These can supply the owner link used by TdrResetFromTimeout once its instructions identify that field. Never cast the whole recovery context to ADMISSION_CONTEXT. |
| 06 SAVED_RESET_STACK | 64 qwords from the recorded frame-3 stack address; corroborates saved registers/call frames when reconstructing the reset path. Not an invitation to dereference every apparent pointer. |
| 07 MINIPORT_LAYOUT_REFERENCE_ONLY | Explicit layout-only output for eventual adapter/runtime validation. It is intentionally labelled reference, not RTKit instance evidence. No zero-owner `dt` is presented as session state. |

After this pass identifies the context field/address, validate adapter magic,
PDO, Started and the runtime back-pointer with bounded instance reads. Then
inspect RTKit, reset/correlation receipts and queue runtime at the validated
owner. This discovery pass deliberately does not pretend it already has that
owner or automatically dereference speculative candidates. A further targeted
command list may be necessary; there is no successful discovery claim here.

## Decisions once runtime data is available

Source: `drivers/apple-agx/shared/src/apple_agx_rtkit_session.c:252–303` and
`shared/include/apple_agx_rtkit_session.h:8–53` under `drivers/apple-agx`.

- **Firmware crash:** validated `CrashlogCrashed != 0` proves sticky result 8
  (`:266`). An endpoint-1 crash message after buffer grant sets it (`:290`).
  Requires a firmware restart/ownership design, informed by crash bytes when
  available; do not clear the flag, fabricate fences, or free DMA owners.
- **Protocol:** result 6 correlated to the failing drain, crash flag clear,
  plus the actual rejected message selects exact protocol handling (`:283–300`).
  LastRxPayload alone may be stale. Read primary implementations before adding
  a handler; do not blanket-accept messages.
- **Transport:** correlated result 5 plus failure snapshots/callback metadata
  selects ASC transport investigation (`:272–277`). Missing dump pages or
  absent symbols are not evidence of failed live transport. A nonblocking
  receive failure is not automatically a timeout.
- **Lifecycle:** result 1/2 or inconsistent Running/CpuReady/Boot/StopPhase
  selects argument/lifetime investigation (`:261–271`).
- **Still unavailable/healthy:** report the exact gap and retain the failure
  classification as unknown. The session has no persistent LastDrainResult.
  In `render-admission/src/backend_platform_windows.c:5160–5172`, both RTKit
  and provider failures become the same reset Poll failure, so that receipt
  alone cannot select firmware restart versus protocol/transport repair.

Crash GPU VA is not CPU VA. BrokerOnly can leave Windows crash DataObjects[7]
unallocated (`shared/src/apple_agx_initdata_memory.c:167`); do not dereference
its GPU address as Windows memory. Queue interpretation requires real stamps,
done/write pointers, pending fence and retirement snapshots at a validated owner.

## Verification and remaining work

Two new static regression tests fail against pass 2 for the actual name/operator
errors and pass against pass 3. These are artifact contract checks, **not CDB
execution tests**. The existing pass-2 tests remain to document earlier fixes.
Focused suites: command checks, RTKit, ASC, reset quiescence and platform provider.
Fresh full baseline and commit/hash details are in handoff REPORT.md.

WHAT IS STILL UNKNOWN: adapter address, runtime address, actual RTKit state,
last failure-associated message/result, crash flag, crash-buffer availability,
queue state, whether corrected driver discovery succeeds, the exact DXGK owner
layout, why two command prefixes disappeared, and whether this CDB script runs
without parser/extension errors. CDB is not available/authorized in this task.

Smallest next checkpoint: Claude runs this script on the **existing saved dump**.
No hardware needed. After a remedy is selected and tested, the smallest hardware
checkpoint would reproduce the EXP1938 workload/reset boundary with that one
change and demonstrate genuine retirement or a fully designed restart, bounded
reset completion, and preserved DMA ownership. Claude retains the known recovery
artifact and collects evidence before exact cleanup under the existing procedure.
