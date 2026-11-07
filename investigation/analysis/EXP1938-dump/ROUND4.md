# EXP1938 round 4: final bounded owner inspection

## Decision

The returned pass 3 does **not** settle the RTKit state. TASK.md Round 4 selects
`cdb-commands-4.txt`, then stop. No behavioral KMD fix can be selected from this
transcript: firmware crash, unexpected protocol, transport failure and invalid
session state require different remedies, and none has been observed here.

The demonstrated inspection defect is fixed: even `!pool <one address>` did not
terminate in this dump. Pass 4 uses no debugger extension, pool walk, search,
loop, conditional, target write/call, or session-ending command. Historical
scripts and evidence remain unchanged. This is an inspection fix, not a claimed
GPU recovery fix. No hardware or network access was performed.

## Evidence and ownership contract

Transcript paths below refer to the handoff folder
`/Volumes/pwdev/public_windows/.local/handoff/codex/dump1938/`.
RA = `drivers/apple-agx/render-admission`; SH = `drivers/apple-agx/shared`.

- `cdb-output-3.txt:64–77` resolves the EXP1938 private PDB and module;
  `:78–80` records 0x116 with arguments `ffffd684d64e3010`,
  `fffff803c291a9f0`, `ffffffffc0000483`, `3`.
- `cdb-output-3.txt:91–110` establishes the **AppleAgxAdmission** service,
  driver object `ffffd684d2d26060` and GsDriverEntry in the expected module.
  `:142–164` identifies Video1, monitor, and the APPL0002 FDO/PDO stack.
  The APPL0002 FDO is `ffffd684d3682030`, its extension is
  `ffffd684d3682180`, and its ACPI PDO is `ffffd684cb5a9d80`.
- `cdb-output-3.txt:168–178` preserves frame-3 x19=`ffffd684d3ce0000`.
  `:232` moves the PrepareToReset `this` argument to x19.
  `:354–355` loads `[x19+0xCB0]` into x0 and calls
  `dxgkrnl!ADAPTER_RENDER::DdiResetFromTimeout`. Thus `+0xCB0` is an
  evidence-backed render-object link for **this exact Windows binary**, not
  a portable Windows layout or a miniport pointer.
- `cdb-output-3.txt:744–747` establishes the complementary TDR+0x20 ->
  DXGADAPTER path. Read `ffffd684d64e3030` and compare with the saved x19.
  The wrapper at `fffff803bdcf3fb8` was named but never disassembled. Its
  callback-argument loads are the next precise missing link.
- `cdb-output-3.txt:874` is the **last executed command**,
  `!pool 0xffffd684d3ce0000`. From `:876` through the end it prints the same
  entry 5,667 times; the final line is incomplete. No later `dps`, instance
  read, layout section, or END marker ran. This does not prove a freed
  adapter, pool corruption, absent runtime pages, or a firmware crash.
- `RA/src/lifecycle.c:182–195` separately allocates ADMISSION_CONTEXT and
  returns it through MiniportDeviceContext. A DXGK device extension cannot
  simply be cast to that allocation. `RA/include/render_admission.h:592–593`
  and `RA/include/render_objects.h:4` define its first-field magic
  `0x414f4152`; `RA/src/backend_platform_windows.c:4721,4731` establishes the
  runtime/adapter back-pointer pair. Require callback-path provenance, magic,
  PDO match and both back-pointers before interpreting any runtime state.
- `SH/include/apple_agx_rtkit_session.h:29–53` has no LastDrainResult field.
  `SH/src/apple_agx_rtkit_session.c:261–298` distinguishes invalid argument,
  sticky crash, invalid lifecycle, ASC failure, and rejected message.
  `RA/src/backend_platform_windows.c:4222–4231` records the worker's RTKit
  result in correlation evidence; `:5160–5172` maps both an RTKit failure and
  a provider poll failure to the same reset Poll outcome. That outcome alone
  cannot choose a behavioral remedy.

Ownership is unchanged: DXGK owns its device extensions and reset invocation;
the miniport owns ADMISSION_CONTEXT and its platform runtime; the runtime owns
the RTKit session. Firmware/DMA/queue ownership is not inferred from debugger
metadata. No initialization, interrupt, memory, power, firmware or recovery
behavior is changed. Asahi/m1n1/Mu initialization comparisons are not needed for
this debugger-script correction; they remain prerequisites to a restart design.
No external implementation was copied.

## Final pass and interpretation

Claude runs the delivered script via the existing `$$<` runner on the **same
saved dump** with its matching PDB. Keep every error and the END marker. No
new hardware run is needed for this step. The known addresses and DXGK offset
are dump-specific observations, not production admission rules.

| Section | Expected output and decision |
|---|---|
| 00 IDENTITY_FINAL_DUMP_PASS | Same module/PDB, bugcheck and stack. A different dump or mismatched PDB invalidates the hard-coded observations. |
| 01 RESET_WRAPPER_CODE | At most 256 ARM64 instructions, showing the missing ADAPTER_RENDER wrapper and how it loads the callback context. If it delegates again, record that remaining uncertainty; do not start a fifth discovery pass. |
| 02 TDR_TO_DXGADAPTER_LINK | One pointer, expected to agree with frame-3 x19. Failure or disagreement invalidates this ownership route. |
| 03 DXGADAPTER_TO_RENDER_LINK | One pointer at the load proven in the disassembly. |
| 04 RENDER_OBJECT_NOT_MINIPORT | 4 KiB of the pointed-to render object. Interpret fields only using the wrapper's instructions. Pointer-looking words or a coincidental magic value do not authenticate a miniport. |
| 05 APPL0002_DEVICE_AND_EXTENSION | Fixed DEVICE_OBJECT fields plus 1 KiB of its known extension. Corroborate driver/PDO/device ownership; do not cast the extension to ADMISSION_CONTEXT. |
| 06 VIDEO1_DEVICE_AND_EXTENSION | Same bounded evidence for the Video1 device. |
| 07 MONITOR_DEVICE_AND_EXTENSION | Same for the monitor child, to distinguish it from the APPL0002 adapter. |
| 08/09 LAYOUT_REFERENCE_ONLY | PDB offsets/types only, explicitly not instance state or zero-valued RTKit evidence. |
| END_EXP1938_PASS_4_NO_FIFTH_DISCOVERY_PASS | The script reached the end; it does not certify valid owner or RTKit reads. |

Explicit raw reads total **7,184 bytes** (898 eight-byte words), plus three
nonrecursive DEVICE_OBJECT displays. Code reads total 256 instructions. There
are 33 independent command lines, longest 69 characters. The only computed data
address is `poi(ffffd684d3ce0cb0)` on its own read line: a failed expression
cannot reuse a stale pseudo-register or prevent later commands. If it reads a
null/invalid pointer, treat the entire render-object section as unavailable.
Execution syntax and output remain unverified until Claude runs CDB.

This deliberately does **not** guess the missing callback-context offset. It
may produce only owner discovery, not actual session/crash-buffer/queue bytes.
If it still cannot establish RTKit state, **end the saved-dump route**. Do not
write cdb-commands-5.txt or resume pool/history searches.

## Outcomes and next causal checkpoint

Only authenticated runtime evidence can select these outcomes:

- CrashlogCrashed set, or correlated FirmwareCrashed (8): firmware restart and
  DMA/queue recovery design. Clearing the flag or reporting completion is not
  recovery.
- Correlated ProtocolViolation (6), with the actual rejected message and no
  crash: review that message against primary RTKit sources, then implement only
  the supported protocol handling. LastRxPayload alone may be stale.
- Correlated TransportFailed (5): inspect the saved ASC failure controls,
  callbacks and mapping ownership. A CDB read failure is not live ASC failure.
- InvalidArgument (1) / InvalidState (2): validate session lifetime, Running,
  CpuReady, boot phase and StopPhase at the call boundary.
- Missing, uncorrelated or healthy-looking state: inconclusive; no guessed fix.

**Next best hardware discriminator if pass 4 is still inconclusive (proposal
for Claude, not executed or authorized here):** capture the non-OK return of
AppleAgxRtkitSessionDrainRuntime on the same EXP1938 workload with the existing
package and matching symbols, before the caller loses its result and runtime
pointer. Use a previously qualified live-debugging path only. Record the return
enum, the caller's authenticated runtime/session, LastRxEndpoint/Payload,
CrashlogCrashed, failure-mailbox-valid flags/controls, the corresponding pending
fence, and crash-buffer CPU ownership if present. Break only on failure and
take bounded RAM reads; never dereference CrashlogGpuAddress as CPU memory.
The hypothesis is justified by the source's lossy reset Poll mapping and this
dump's failed owner discovery, not by evidence that firmware crashed.

Do not silently enable an unqualified KD configuration: the tandem review R44
records an earlier KD-related boot failure. If no qualified debugger can capture
this boundary, stop hardware diagnostics and do the primary-source restart
design review instead. No further receipt-only package is proposed. Preserve
the known recovery artifact; Claude preregisters the single instrumentation
variable and bounded abort/recovery before any launch, collects evidence first,
then performs exact package cleanup through the established recovery procedure.

A subsequent behavioral hardware checkpoint must prove actual retirement or
a fully specified firmware restart while retaining DMA-owned allocations until
quiescence is proven. It must not synthesize completed fences or reuse stale
queue state. Restart prerequisites still to resolve from Asahi, current m1n1,
Mu/ACPI and the WDK are CPU halt/reset ownership, DMA cessation, retained-broker
epoch/lease invalidation, mappings, RTKit bootstrap, queue/stamp reinitialization,
Windows failed-work semantics and rollback after a partial restart. This note
does not claim that ordinary Stop/Start implements those contracts.

## Verification and limits

The new static regression first failed on pass 3, including its `!pool` line;
pass 4 passes. It allows only independent built-in inspection verbs and requires
literal finite lengths for raw data/code reads. It is an artifact safety check,
not an emulation of CDB. Existing command, RTKit, ASC, TDR quiesce and platform
provider modules are run as the focused gate, followed by the required baseline.
Exact results and commit/hash information are in the task's REPORT.md.

Still unknown: actual miniport/runtime pointers, crash state, failing drain
result/message, crash-buffer contents, queue/retirement state, why the pool
extension repeated, and whether the final script executes cleanly in CDB.
