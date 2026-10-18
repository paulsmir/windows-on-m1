# R154 — EXP867 native completion, managers, and current graph

Read-only audit of the saved EXP867 kernel dump. No Air access, package, firmware,
driver change, or hardware run. The builder was used only to read the existing
dump with its matched private PDB. Raw evidence below lives under
`/Users/pavel/public_windows/.local/experiments/EXP867-r153-bm/`.

## Findings

The saved queues support **81 completed native TA+3D jobs**, **44 InitBM
publications**, and completed snapshots from **at least two Windows processes**.
The retained event ring contains **162 ordinary Flag events**, 81 for each queue,
and no emitted Timeout, Fault, GrowTVB, or channel-error message in that history.
The current blocked Windows fence 36357 was not the last firmware job: the last
native completion was 36174. Its current process graph still satisfies every
range access requested by the exact EXP867 production parser.

This supports the main investigation's CPU-side BeginJob boundary. It does not
prove the precise time at which the process-wide mapping generation changed,
nor prove that every post-TDR field equals its pre-TDR value.

## Queue and event evidence

`manager-snapshots.txt`, `backend-state.txt`, `R154-native-ring.txt`,
`R154-native-state.txt`, and `R154-native-final.txt` establish:

| Evidence | Observed value |
|---|---|
| Backend native sequence |81|
| TA write / consumed pointer |125 /125|
| 3D write / consumed pointer |162 /162|
| TA /3D stamps |`0x7a005100` /`0x3d005100`|
| Final native queue result |fence 36174, Success|
| Queue pending fence |0|
| Actual event ring read /write |162 /162|

The TA ring itself contains 44 pointers to InitBM at
`0xffffffa0003e3fe0` and 81 pointers to TA at `0xffffffa0210479e0`.
The 3D ring contains 81 barrier pointers (`0xffffffa0003d3fe0`) and 81 work
pointers (`0xffffffa021003680`). This directly confirms the arithmetic
`125 −81 =44` InitBM publications. There was one initial manager bind and 43
subsequent binding publications. Those publications are **not** a count of
process switches: the binding key includes owner, generation, root, and backing.

All 162 retained event messages have kind 1. Their firing masks are 81 instances
of 1 (TA event0) and 81 instances of 2 (3D event1); the other firing words are 0.
The ring capacity is 256 and this retained history has not wrapped under the
observed persistent queue sequence. The cached LastEventReadPointer 160 is the
start of the last drain, not the actual remaining read position: the channel
state has advanced to 162 and no batch remains pending. The first attempted
ring-summary debugger command failed at `.printf` quoting after reading the
state; the following raw-memory query succeeded and is the source of the event
classification.

This is positive completion evidence, not merely evidence that firmware fetched
the work. It does not provide a fence-to-process ledger for all 81 jobs. In
particular, assigning the exact fifth completion to Explorer is not possible
from the retained queue pointers and snapshots alone.

## Process ownership and buffer-manager backing

`R154-native-state.txt` correlates the saved driver process handles with kernel
process objects. The public dxgkrnl symbols do not expose `_DXGPROCESS`, so the
opaque object's two independent fields were compared with `!process` and the
private context's Win32Generation; no undocumented offset is proposed for code.

| Driver owner | DxgkProcess | Pointer at +0x28 / matching `!process` | PID at +0x50 | Saved fence |
|---|---|---|---|---|
|8|`ffff9b0ace5199c0`|`ffff9b0acecdc080`, explorer.exe|`0x218` =536|1031|
|13|`ffff9b0acab987a0`|`ffff9b0ad02cc080`, M365Copilot.exe|`0x176c` =5996|36174|

The matching context generations begin with 0218 and 176c respectively. Both
manager snapshots are Valid, PendingFence 0. This establishes completed work in
Explorer and M365Copilot, rather than assuming the second owner is DWM.

`R154-native-backing.txt`, `R154-native-final.txt`, and
`R154-native-manager-decoded.json` show:

| Owner | Manager generation | Root | Page-list VA | Block-list VA | Heap VA /bytes |
|---|---|---|---|---|---|
|8|`0x1a`|`0x9d5a38000`|`0x24d0000`|`0x24e0000`|`0x24f0000` /4MiB|
|13|`0xbc3`|`0x9df3c0000`|`0x2020000`|`0x2030000`|`0x28f0000` /4MiB|

Both Info snapshots declare 128 pages, 32 blocks, 128KiB block size, and 512 bytes
of page-list entries, within the64KiB allocated page-list backing. BlockControl
is total 32, write 32, unknown 0. Counter is 1 in each surviving manager generation.
Stats has max_pages 3, max_b 0, overflow_count 0, gpu_c 0. These counters cannot
recover per-process historical completion totals across manager generations.

The shared firmware manager key is still owner 13/gen`0xbc3`/root`0x9df3c0000`,
ManagerFence 0. Its 188 Info bytes exactly match owner 13's saved completion
snapshot. Its scene's user buffer is`0x2090000`. These are remnants of the
completed 36174 job. The newer 36357 image names owner 8's scene, but its
G4Manager pointer is NULL and its private scene Started 0. It has not reached
the binding/materialization step which would restore owner 8's manager. Comparing
owner 13's idle shared manager with owner 8's unstarted job does **not** establish
stale cross-process firmware execution. No BM backing/count inconsistency or
overflow is demonstrated here. Partial rendering has no independently decoded
history in this audit; zero overflow and no GrowTVB event are the narrower facts.

## Current 36357 range revalidation

The main investigation's saved process query identifies owner 8's active context
`ffff9b0acb092170`, scene`ffff9b0acec312e0`, node 0/engine 0,
fence 36357. Scene generation`0xbd2`, manager generation`0x1a`, and its nine
ranges agree with the bound G4Header. The context recorded mapping generation
`0x2ba36`; the process graph is`0x2ba37`. Current Graph.Created 1,
Uncertain 0, JobInFlight 0, Slot 0, LeaseToken 0, with the same root in the context
and graph. CancelFence 36357/CancelUncertain 1 are present after failed recovery;
they must not be assumed to predate the original BeginJob attempt.

The audit decoded 7 parent edges, 456 leaf edges, and 31 table-shadow records
from `R154-native-graph.txt`. A source-equivalent walk using the actual root,
indices and Writable flags finds all nine declared private ranges present and
writable:292 native16KiB pages. They resolve to private backing of the declared
size. The output attachment also remains writable and resolves to the packet's
physical destination:

| Use | VA | Current guest IPA |
|---|---|---|
|Page list,64KiB|`0x24d0000`|`0x91ecd0000`|
|Block list,64KiB|`0x24e0000`|`0x91ece0000`|
|Heap,4MiB|`0x24f0000`|`0x91ecf0000`|
|Scene ranges3–8|`0x21b0000`..`0x2210000`|corresponding`0x91e9b0000`..`0x91ea10000`|
|Output,16KiB|`0x110cea0000`|`0x8e19a0000`|
|VDM|`0x3d2f0000`|`0x8e1800000`|
|Scissor /depth bias|`0x3d2b0e40` /`0x3d2b0e80`|`0x8e1100e40` /`0x8e1100e80`|
|USC addresses after clearing flags and adding USC base|`0x110d190140`, `0x110d190240`, `0x110d1901c0`|`0x8e1140140`, `0x8e1140240`, `0x8e11401c0`|

The CPU envelope VA`0x3d950000`, bytes 280, fits one 4KiB logical page. Its root
walk selects shadow`ffff9b0ad07344c0`, broker table`0x9ded58000`,
LogicalPtes`ffff9b0ad2542000`. Entry`ffff9b0ad2574a00` contains
GuestIpa`0x8e17e0000`, SegmentId 2, Flags 3 (VALID|WRITE), allocation
`ffff9b0acf6dbb80`, offset 0. These fields pass the existing logical-envelope
contract; the native graph walk is deliberately not substituted for that test.

`R154-native-replay.py` loads the exact EXP867 production
`AppleAgxG4ParseSubmitEx`, compiled from commit
`335031768e44505d33fcbc91b4899fdf13b8fb60`, with the dumped168-byte header and
280-byte native command. Its callback follows the saved graph and logical PTE
above. **Parse result 0 (Ok), all 18 access callbacks true.** It validates framing,
render fields, required capacities and the requested VA ranges. It does not
execute broker JOB_BEGIN, inspect all command-stream contents, or simulate GPU
execution. Geometry dimensions, sample and utile fields also agree with the
saved private scene. No runtime admission rule was derived from exact trace
sizes or addresses; those values are only a regression/replay case.

Source SHA256: parser C
`f3331443e1f5a10e8e356ef6776d5f0e2e10d80ba9550ad1bf3fba09491ef343`;
header`eb374aba69bf390905f4b4dee8d53c6823a618708280cce3cbce31371fae8174`.
`R154-native-parser-result.json` records input/output hashes and every access.
Raw command SHA256 is
`179714a47eb7af7d106ca63a692357f8a2fcf0fb1b12a9eed0edd9bf7eb9e9b1`.

The current mappings therefore do not justify rejecting this job solely because
the process-wide epoch differs by one. With the saved state, that old predicate
is sufficient to reject an otherwise valid envelope before firmware publication.
The stronger temporal assertion that it was the first rejecting condition at
the original attempt remains an inference: the dump is after TDR recovery and
there is no original-boot ETL.

## Primary sources and limits

Saved EXP867 state was inspected first. Then Asahi checkout
`77cb8f24c2381a8abb7272d7bbdec548d6426a8a`:
`drivers/gpu/drm/asahi/{buffer.rs,fw/buffer.rs,queue/render.rs,workqueue.rs,channel.rs}`.
These define VM-backed manager lifetime, InitBuffer on rebind/growth, counter
increment at commitment, stamp-based retirement, and separate Flag/Timeout/
Fault/GrowTVB handling. Current m1n1 checkout
`c6d10e04afdad5314e8ac1e67bc3919b094ab000`:
`proxyclient/m1n1/agx/{context.py,render.py}` and
`proxyclient/m1n1/fw/agx/microsequence.py` supplied the G13 manager layout and
InitBM sequencing. Repository sources inspected include queue provider planning,
event draining/decoding, managed shared-memory build/save, native image binding,
G3 BeginJob, graph walking and logical envelope access. No external code copied.

This read-only audit does not propose a Mu, ACPI, m1n1, or Windows DDI change.
It does not use the recovery ETL as crash history. Missing arena pages prevent
reconstructing arbitrary old command contents; current CPU-copied command,
graph, manager snapshots and channel-ring pages used here were readable.
The register-level GPU/RTKit state and historical per-job owner sequence remain
outside the observed evidence. No further native-state archaeology is justified
before resolving the demonstrated current submission boundary.
