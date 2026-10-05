# R150 offline ETL provenance and QUERY24

2026-09-28. No Air contact, proxy access, hardware run, package operation,
production edit, persistent-builder-tree edit, or commit. Decoder execution was
limited to `pauls@192.168.1.24:C:\agx\r150-etl` using saved evidence.

**The saved EXP864 ETL is the GPU-hidden recovery boot, so it cannot establish
EXP864's original DMA, preemption, fence, or firmware timeline.** This corrects
the earlier `hardware-result.json` / compact-state description that its first
event belonged to the original boot. The original records were not edited.

## Provenance evidence

Input: `/Users/pavel/public_windows/.local/experiments/EXP864-r149-usc/hardware-evidence/EXP801DxgBoot.etl`.
SHA-256 `8e346f91190c8ac2e68b46d8aa22195d4568753d4513b2950bb33b90f9381b93`,
96,468,992 bytes. The isolated-builder hash matches the original artifact manifest.

| Evidence | Value | Implication |
|---|---|---|
| Native ETL BootTime | 2026-09-28 11:06:42.7482369Z | Exactly the recovery Kernel-General Event12 boot |
| Earlier Event12 | 2026-09-28 11:06:22.0187500Z | Separate preceding original boot |
| First decoded ETL event | 11:06:43.0204066Z | 0.2721697s after recovery BootTime |
| Original crash dump | 11:06:50.548Z; uptime 29.063s | Original boot, not the ETL boot; wall-clock overlap is insufficient |
| ETL adapters | `ffffb68e1139f000`: `1414:008d`; `ffffb68e11393000`: `1414:008c` | No Apple adapter in the two adapter-start/report pairs |
| Original kernel allocation domain | `ffff998f...` in dump/receipts | Different from ETL's `ffffb68e...` / `ffffcf8f...` |
| Recovery receipt | 11:12:03.0049530Z, Problem45 | Corroborates GPU-hidden recovery |

ETL native header EndTime is 11:11:42.6589389Z; last QPC-converted event is
11:08:42.6384534Z. The same recovery's System event records a +180004ms clock
correction from 11:07:04.2449517Z to 11:10:04.2496419Z. The differing wall-clock
and trace-converted endpoints are consistent with that correction. They do not
put this ETL back into the preceding boot. No absolute-time, PID, or handle-value
join from this trace to the original dump/UMD/receipts is valid.

Native `OpenTrace/ProcessTrace` reports status0, EventsLost0, BuffersLost0,
148606 events: 138651 DxgKrnl, 9953 Kernel-EventTracing, and two logger metadata
events. `Get-WinEvent` returns 148605 events with precisely the same 138651
DxgKrnl and 9953 Kernel-EventTracing events and one metadata event. This one-event
metadata difference does not lose a DxgKrnl scheduler event. Loss counters say
nothing about the preceding boot, which is absent.

## What the recovery ETL actually proves

The builder's installed Microsoft DxgKrnl provider metadata identifies events
175/176/177 as DmaPacket Start/Stop/Info, 178/179/180 as QueuePacket
Start/Info/Stop, 27 as Device Start, 30 as Context Start, and 250 as NodeMetadata.
The saved `schema.json` preserves their templates.

All **767 DmaPacket starts** match completion-info and stop by
`(hContext, uliSubmissionId == uliCompletionId, ulQueueSubmitSequence)`.
All stops have `bPreempted=false`. All **2046 QueuePacket stops** have
`bPreempted=false` and `bTimeouted=false`. These are successful recovery-boot
scheduler sequences, not evidence about Apple work before the first TDR.

Each DMA context joins through event30 `hDevice`, then event27 `pDxgAdapter`,
to `ffffb68e11393000` with PCI identity `1414:008c`. Microsoft identifies that
identity as its Basic Render Driver in its [DXGI programming documentation](https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/direct3ddxgi/d3d10-graphics-programming-guide-dxgi.md).

| Recovery PID | Context | Node | Engine affinity | DMA starts |
|---:|---|---:|---:|---:|
| 1248 | `ffffcf8feaf38050` | 0 | 1 | 563 |
| 1240 | `ffffcf8feb654dd0` | 3 | 1 | 166 |
| 1248 | `ffffcf8fea8677b0` | 0 | 1 | 3 |
| 5376 | `ffffcf8feb997de0` | 1 | 1 | 22 |
| 5340 | `ffffcf8febbb26a0` | 2 | 1 | 1 |
| 5376 | `ffffcf8fed3c8610` | 3 | 1 | 3 |
| 5536 | `ffffcf8feb7e2de0` | 6 | 1 | 7 |
| 7480 | `ffffcf8fecb54de0` | 5 | 1 | 2 |

The trace does not contain process/image providers that would justify assigning
original-boot executable names to these PIDs. Engine affinity1 is preserved as
the recorded bitmask; it is not renamed to an Apple engine.

First recovery DMA: context `ffffcf8feaf38050`, submission1, queue sequence1,
start11:06:54.5121537Z, completion-info11:06:54.5123176Z,
stop11:06:54.5123296Z. Last recovery DMA: same context, submission566,
queue sequence1440, start11:08:05.4648436Z,
completion-info11:08:05.4790977Z, stop11:08:05.4791058Z.

**Original EXP864 state remains unknown from ETL:** OS queue admission,
DxgkDdiSubmitCommandVirtual return, KMD scheduler acceptance, firmware handoff,
TA/3D completion, preemption request and notification, and the first lost fence.
A missing G4 rejection receipt does not resolve any of these boundaries.
The saved original-boot dumps and in-memory driver state are the remaining
offline evidence source for those questions.

## QUERY predicate24: proven process poison, not failed context lookup

Inspected current and exact package864 source
`a9ecd3eac75cc4058979bdfd8fd97052d8831fb2:drivers/apple-agx/render-admission/src/gpuva_g3_windows.c`.
They are byte-identical, SHA-256
`b93a2d3057c0d5b8aa7d05ae42af339e0c2626fb0b60b56553cd2d537f989e50`.
Current inspection HEAD was `96da01f0f1ce550007a2f55d41a941ef0c13eda9`.

`AdmissionGpuvaG3CopyEscape` has this order:

1. Acquire allocation-handle reference (guard22).
2. Acquire the G3 mutex and find `args->hKmdProcessHandle` (guard23).
3. `COPY_REJECT_IF(p->Poisoned, 24u, STATUS_INVALID_PARAMETER, Unlock)`.
4. Check graph uncertain/created (25/26).
5. Walk the process context list and check missing context (27).
6. Resolve allocation details and length at later guards (34 onward; length48).

The first-failure receipt therefore establishes a found process with
`Poisoned=TRUE` under the mutex, returning `STATUS_INVALID_PARAMETER`.
It **does not establish that the requested context was absent**: the context
lookup never ran. `ContextToken=0`, `context_available=false`, and
`QueryBytes=0` are unvisited capture fields, not independent failures.
Likewise there is no PTE-walk failure at guard24. ProcessId5 is the driver's
`p->Graph.ProcessId`, not a demonstrated Windows PID5.

Receipt facts remain: graph root `0x9d8600000`, bootstrap `0x9d8648000`,
ProcessSetRootCount1, ContextSetRootCount0, process generation1, mapping
generation469, requested VA `0x20000`, allocation handle `0x40000fc0`.
`AdmissionG3CaptureCopyQueryFailure` is first-winner-only (`Claim`0→1→2),
so no ordering between this durable receipt and the crash can be inferred from
its export timestamp alone. The poison writer/cause and original Windows
process/context require same-boot in-memory evidence; ETL cannot provide it.

## Reproducible outputs and limits

Concise evidence: `investigation/evidence/R150-etl/`:
`summary.json`, `adapter-device-context.json`, `dma-examples.json`,
`boot-events.json`, `native-summary.txt`, `schema.json`, `input-hash.json`,
`query-decoded.json`, `hashes.json`.

Large decoded outputs and reproducible scripts:
`/Users/pavel/public_windows/.local/experiments/R150-etl/`:
`events.jsonl` (32,475,397 bytes), `dma-joined.json`, `event-counts.json`,
`event-examples.json`, `etl.ps1`, `readetl.cpp`, `native.ps1`, `schema.ps1`,
`analyze.py`. `events.partial.jsonl` was a progress snapshot and is not used
by the final analysis. The saved native reader uses Windows SDK tracing APIs;
the PowerShell decoder uses Microsoft event metadata, with no guessed layouts.

Verification assertions: all 767 full-key DMA joins exist; all their stop
preemption flags are false; all joined contexts resolve to the same `1414:008c`
adapter; original and isolated-builder ETL hashes match; package864/current
QUERY source hashes match. No production behavior was changed or tested.

Next causal target: inspect original kernel/live141 scheduler and Apple adapter
state; specifically distinguish accepted KMD work from firmware dispatch and
observe the outstanding preemption boundary. No new hardware experiment follows
from this ETL, and no QUERY or timer fix is justified by these findings alone.
