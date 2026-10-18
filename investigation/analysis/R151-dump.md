# R151 — EXP865 saved-dump correlation

Offline analysis only: package865's saved kernel dump and matching private PDB
on the Windows builder; no Air connection, build, package, rearm, or hardware
experiment. Original evidence remains unchanged. Commands/full outputs are
in main `.local/experiments/R151-dump/`; compact excerpts and SHA-256 provenance
are in `investigation/evidence/R151/dump-*`.

## Boundary compared with EXP864

The package865 dump proves a completed native job149 and a submitted native
job220. Current220 belongs to **LogonUI.exe PID1216**, not DWM. The first job's
process cannot be recovered conclusively from the retained per-process scenes.
It is plausible that149 was also LogonUI, given EXP864's149 and the UMD trace,
but that cross-run association is an inference, not a dump-proven identity.

| Saved field | EXP864 (R150 original dump) | EXP865 |
| --- | --- | --- |
| Kernel uptime / CPU | 29.063s / 0 | 39.288s / 1 |
| Live141 uptime / CPU | 28.920s / 1 | 39.160s / 3 |
| Bugcheck / recovery result | 116 / C0000483 | 116 / C0000483 |
| Completed scheduler fence | 148 | 219 |
| Active / dispatched fence | 149 | 220 |
| Preemption | fence1, cutoff149, WaitCurrentBoundary | fence1, cutoff220, WaitCurrentBoundary |
| Last CPU paging completion | 148 | 219 |
| G3 last completed native fence | 0 | 149 |
| Active graph ID / root | 5 / 9d8600000 | 6 / 9d6490000 |
| Mapping generation / lease | 469 / 1 | 895 / 2 |
| Graph slot / JobInFlight | 1 / 1 | 1 / 1 |
| Current process | LogonUI PID1220 | LogonUI PID1216 |
| Backend phase / owner | Ready / 63; worker request1 refused | Submitted / 63; pending owner63 |
| Queue runtime | Ready, pending0, FirstRun TA/D3=1 | Faulted, pending220, FirstRun TA/D3=0 |
| Last queue completion | none | 149 / Success |
| Current TA | no publication | pointer3/expected3; stamp7a000000/expected7a000100; event0 unseen; incomplete |
| Current D3 | no publication | pointer4/expected4; stamp3d000100/expected3d000100; event1 seen; complete flag1 |
| Private scene |149, Queued/Started1; GpuDone/Reported0 |220, Queued/Started1; GpuDone/Reported0 |
| Quarantine / process poison | 1 / 1 | 1 / 1 |
| G4 submit failure claim/count | 0 / 0 | 0 / 0 |
| ProgressValid | 0 | 1 |
| GPU MMIO contents | unavailable | unavailable |

The D3 field is a saved software observation. It does **not** independently
prove fresh execution of job220: expected stamps are reused (separate R151
source investigation). TA has a consumed queue pointer but lacks its expected
stamp and event. Neither pointer consumption nor the D3 flag establishes a
second joined TA+3D completion or correct pixels. EXP865 crosses EXP864's
pre-publication failure, so EXP864 does not supply a hardware reference for
second-job TVB, event or shader execution.

## Windows process correlation and retained history

Active context `ffffd00f209f3270` has GpuvaG3Process `ffffd00f2020bdd0`,
Win32Generation `04c00001`, and fence220. That G3 process has graph ID6 and
DxgkProcess `ffffd00f1d6ba350`. Raw DXGPROCESS memory contains EPROCESS
`ffffd00f1dabb080` and PID`04c0`; `!process` identifies LogonUI.exe PID1216.
The generation scheme in `umd_runtime_device.c` independently encodes the
low16 PID in the high16 generation bits. The UMD log includes PID1216.
DXGPROCESS's private type is unavailable, so its raw offsets are corroboration,
not a published Windows ABI. DWM is PID1224/EPROCESS `ffffd00f1dabf080`.

All six surviving G3 process-list entries were enumerated:

| Graph ID | Retained process/context association | Private scene |
| --- | --- | --- |
| 1 | System, Win32Generation0 | none |
| 2 | second non-Win32 context, generation0 | none |
| 3 | DWM PID1224, generation04c80002 | none |
| 5 | ShellHost PID5340, generation14dc0001 | none |
| 6 | LogonUI PID1216, generation04c00001 |220, started, not done |
| 7 | explorer PID5284, generation14a40001 |fence0, not queued/started/done;1024x1024 allocation geometry |

Graph4 no longer exists; NextProcessId7 and ProcessCount6 do not identify its
former Windows owner. Completed149's scene and owner are not retained. No
complete scheduler history exists in the driver object: CompletedFence,
LastCompletedFence and the queue's Completion are scalar last-state fields;
RenderCorrelation.Count is0. It would be incorrect to label149 as DWM, or to
turn its probable LogonUI association into a measured fact. Explorer's larger
unsubmitted scene cannot be used as the geometry of either149 or220.

## How many GPU jobs?

There is **one corroborated joined completion (149) and one later published
job (220)**. TA pointer3 and D3 pointer4 agree with two ordinary submissions:
first TA publication contains InitBM+TA (2 entries), subsequent TA publication
contains only TA (1 entry); each D3 publication contains2 entries. Queue runtime
BufferManagerInitialized1 and both FirstRun flags0 agree. The active broker
lease token2 corroborates two lease acquisitions: `hv_agx_gpuva_v5_lease`
assigns `++next_token`, and G3's begin-job path obtains the lease.

Thus all available counters support two native jobs in this queue lifetime,
not219 GPU renders. These are counted submissions/completions, not proof that
all phases of220 freshly executed. Scheduler219 and PagingLastCompleted219
are completion **watermarks** shared with CPU paging/present work. Do not
claim218 individual CPU jobs without a complete submission history; Windows
fence values alone are not that history. Queue reset/wrap is not indicated by
the saved state; the exact-two inference assumes this normal initial queue
lifetime. There is no evidence for a stream of many successful native frames.

## Current220 geometry and TVB capacity

The typed native command was read from BackendImage.G4Command+0x28, after the
attachment header/one attachment and render header. This is distinct from
PrivateScene.Geometry, which is an allocation-sizing record with many unused
fields zero. Current command is280 bytes; target16x16, pitch64, one layer,
32x32 utile, one sample, sample-size8. Flags4, VDM stream0x200000,
PPP control0x202, multisample0x88, depth/stencil absent. ISP scissor/dbias are
0x1100080e40/0x1100080e80. BG/EOT USC offsets are0x100144/0x100244;
partial BG/EOT0x1001c4/0x100244. These are command fields, not proof that shader
or VDM contents in absent memory executed correctly.

The native target binding is VA0xb0000, physical0x8e1170000, bytes0x4000;
it is a small target and is not the observed full-panel scanout allocation.
The private header and scene storage agree on all nine process ranges:

| Range | GPU VA | Bytes |
| --- | --- | --- |
| TVB page list | 0x2020000 | 0x10000 |
| TVB block list | 0x2030000 | 0x10000 |
| TVB heap | 0x2040000 | 0x400000 |
| User buffer | 0x2440000 | 0x20000 |
| Tilemap | 0x2460000 | 0x10000 |
| Heap metadata | 0x2470000 | 0x10000 |
| Tail-pointer cache | 0x2480000 | 0x10000 |
| Preemption scratch | 0x2490000 | 0x10000 |
| Auxiliary framebuffer | 0x24a0000 | 0x10000 |

The capacity formula in `apple_agx_g4_submit.h` gives32 blocks minimum
(32*128KiB=4MiB), and these sizes meet that formula for16x16. EXP864's refused
149 also had16x16 scene geometry. EXP865's completed149 has no retained scene,
so first-versus-second TVB sizes or identity cannot be directly compared.
No evidence currently selects TVB exhaustion as the cause. Correct declared
sizes alone do not validate hardware list contents, lifetime or mappings.

## Present, scanout and fault limits

Scanout runtime: panel Started/Committed/Visible1, LastSwapId0,
PresentConsumed0, PendingPresentSequence0, NextSequence3,
LastNotifiedSequence2, pending physical/sequence0, Faulted0. Adapter
PresentCopyBytes0, PresentTransferState0 and render-correlation count0 supply
no composed-frame completion receipt. Existing original-boot snapshot is
seq2, PA0x8e0130000,2560x1600, nonzero0/all pixels zero. It proves an observed
scanout buffer, not useful DWM composition or an actual visible desktop.
No original SSH/Code0 sample and no physical display/input observation exist.

Wom1QueueFaultSnapshot and Wom1G4SubmitFailure are absent. SGX MMIO, broker MMIO
and relevant reserve-backed memory pages are missing from the saved dump.
Firmware/UAT fault registers therefore remain **unknown**, not zero. There is
no durable GrowTVB/fault message in these receipts. No further blind fault
register reads or freed-scene archaeology are justified by this evidence.

Sources inspected: EXP865 dump-state/backend/scene/boundary and checkpoint
receipts; EXP864 R150 state/process/scene analysis; read-only builder cdb typed
reads above; `gpuva_g3_windows.c` scene lifetime/completion, `work_queue_windows.c`
CPU dispatch, `umd_runtime_device.c` generation, `apple_agx_g13_queue_provider.c`
and `_runtime.c` publication counts, `apple_agx_g4_submit.h/.c` layout/capacity,
`hv_agx_gpuva_v5.c` lease counter. No production source was changed by this
analysis. Completion-identity reconstruction is owned by the separate R151
source analysis; this dump narrows its checkpoint to fresh second-job TA/D3
completion, while leaving the original hardware reason for TA silence unknown.
