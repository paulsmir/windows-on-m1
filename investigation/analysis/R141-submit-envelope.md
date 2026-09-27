# R141 — EXP855D attachment serialization refusal (offline)

## Verdict

Confirmed producer defect, corrected in `mesa/winsys/agx_win32_gpuva_batch.c`:
synthetic fragment-attachment commands require both barriers `0xffff`, and
attachment Pad/Flags must be zero. The UMD emitted zero barriers and copied
uninitialized Pad/Flags. KMD validation is correct and unchanged. The real
producer now passes the shared parser and the joined private-escape/graph/
builder replay. This is an offline result, not a new hardware verdict.

No Air connection, boot, package build, signing, installation or firmware
change in R141. Existing m1n1/Mu dirty state remains untouched. Builder use was
limited to saved-dump symbolization and compiling the single changed ARM64
Mesa translation unit against the existing EXP855D dependencies.

## Receipt v2: complete decode

Input: main repository `.local/experiments/EXP855D-r140-escape-identity/`
`hardware-evidence/Wom1G4SubmitFailure.bin`. Offsets follow the natural ARM64
alignment of `_ADMISSION_G4_SUBMIT_FAILURE`; padding at20 and84 is zero.
Machine-readable values/offsets and input SHA are in worktree
`.local/experiments/R141-offline/receipt-v2.json`.

| Offset | Field | Value |
|---|---|---|
| 0,4 | Version, Bytes | 2, 192 |
| 8,12,16 | Branch, Status, DownstreamStatus | 9 Parse, 0xc000000d, 1 ParseInvalid |
| 24 | DmaBufferVirtualAddress | 0x3b0000 |
| 32,36,40 | DmaBufferSize, PrivateDataSize, UmdPrivateDataSize | 280, 331776 (0x51000), 480 |
| 44,48 | Flags, ContextFlags | 0, 4 (virtual addressing) |
| 52,56 | Pid, TotalFailures | 4, 935 |
| 60,64,68 | Subsite, Kind, Ordinal | 0, 0, 0 |
| 72,76,80 | AccessBytes, Write, GraphPresent | 0, 0, 0 |
| 88 | Va | 0 |
| 96 | OwnerProcessId | 5 |
| 104 | RootIpa | 0x9dd370000 |
| 112,120 | ProcessGeneration, MappingGeneration | 1, 402 (0x192) |
| 128,136,144,152 | LogicalIpa[0..3] | all zero |
| 160,164,168,172 | LogicalSegment[0..3] | all zero |
| 176,180,184,188 | LogicalFlags[0..3] | all zero |

Pid4 is the KMD executing thread's process; OwnerProcessId5 is the graph's
opaque process identity, not a Windows application PID. First failure fields
are latched, while TotalFailures is refreshed; 935 does not prove 935 identical
packets. Zero subsite/access values mean no typed access failure was recorded.
GraphPresent0 is the snapshot lookup for Va0/Bytes0, not proof that the process
root or its real pages are absent. G3 Branch8/status0 is a separate informational
table-initialization receipt, not the failing G4 branch.

## Why this check, and why the prior replay passed

Current production `AgxWin32AsahiBatchFinish` calls private acquire, serializes
attachments and render, then `AppleAgxG4ComposeHeaderV3`. The v3 header is200
bytes (v2 was168); one attachment plus render is280, so480 is correct.
`CommandVa`/`CommandBytes` and SubmitCommandCb VA/length come from the same
command BO/packet. KMD capacity331776 is distinct from UMD length480, as specified
by [Microsoft DXGKARG_SUBMITCOMMANDVIRTUAL](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_submitcommandvirtual).
There is no demonstrated 448/480 ABI mismatch.

Private acquire supplies aligned, bounded, nonoverlapping process ranges and
nonzero manager/scene generations. The parser checks those identities, range
MBZ/alignment/size/overlap, CPU envelope access, then the attachment header.
Its synthetic-header guard (`apple_agx_g4_submit.c:183`) requires both barriers
0xffff. The old producer deterministically supplies0 and therefore returns
ParseInvalid. With those barriers corrected alone, poisoned-stack replay still
returns Invalid at the attachment Pad/Flags guard. Zero-initializing the array
removes this second producer defect. Render-command barriers remain0.

The receipt does **not** contain the submitted wire bytes or an ordinal for
structural Invalid. Consequently it cannot independently prove which earlier
structural guard the latched packet crossed. Attribution is the current exact
producer's deterministic violation, matching receipt shape/result and the real
RED→GREEN replay; it is not a claim to have recovered the original480 bytes.
A later geometry/access/output gate on actual hardware remains unvalidated.

The old `g3_r137_private_combined.c` extracted only production
`prepare_process_buffers`. Its handwritten attachment header already supplied
0xffff and zeroed attachment members, masking the producer defects. It now
executes production `append_native` and `append_attachments` as well, retaining
the actual KMD escape, graph, broker and TA/3D builder. The new standalone test
passes actual production bytes into the shared parser with exact recorded
VA/length/capacity, then an unrelated geometry/VA case. No trace value is an
admission condition in production.

Primary contract: local reference Mesa `include/drm-uapi/asahi_drm.h:625-688`
requires NONE on synthetic commands, and748-778 documents attachment MBZ;
`src/gallium/drivers/asahi/agx_batch.c:825-860` emits that shape. Asahi Linux
`drivers/gpu/drm/asahi/queue/mod.rs` processes attachment commands in software.
The fix uses constants/zero initialization, not copied external implementation.
The existing wire ABI retains its existing license; no external code is added.
Source/ownership/firmware comparison and recovery are recorded in
[the plan](../../docs/superpowers/plans/2026-09-28-r141-submit-envelope.md).

## Explorer +0x2da30: separate shader allocation failure

The saved full dump `explorer.exe.5164.dmp` SHA256
`6709e0df77abb80538a73a54eacb5a5a5b7dc3d44441baaa7e2fef01417e2cc3`
was copied to the builder, hash-checked and opened with CDB x64 and matching
855D private PDB, SHA256
`1a69fed6a5676f0a14ffedc72c3bfb400168b211accf0b8233f323c963c64150`.
UMD base0x7ffecc240000, timestamp0x6ab98e18, image size0xbc9000 matches855D;
DLL SHA256 is `dc8d36bb26b5ef706d2502ea48c6c486d813f127a7821a33e7114016c833aa25`.
Symbols loaded without forced mismatch. Zero-checksum warning is recorded.

Exception is C0000005 reading0x50, PC0x7ffecc26da30, x0=0. Immediately before:

```
+2da24  bl agx_bo_create
+2da28  str x0,[x19,#0xca8]  ; compiled->bo
+2da2c  mov x20,x0
+2da30  ldr x0,[x0,#0x50]    ; inlined agx_bo_map(NULL)
```

Dump memory confirms label "Executable", compiled binary size28 and NULL BO.
Exact prepared855D `agx_state.c:1484-1488` is `agx_compile_nir`'s executable BO
allocation followed by unchecked `agx_bo_map`/memcpy. Static Mesa symbols are
not all in the PDB: `agx_init_state_functions+0x35b8` is CDB's nearest public
label, not the semantic function at this PC. Source plus call/field/string
operands identify the allocation site. Stack reaches CreateEmptyShader →
CreateDevice → d3d11 CreateDriverInstance, not SubmitCommandCb or teardown.

Application event22:11:12Z and dump session22:11:18Z refer to this late crash.
The saved UMD failure log has SubmitCommandCb E_INVALIDARG in the same Explorer
PID5164, TID5620; the exception thread is0x1244=4676. Same process does not prove
causation. Repeated device creation/resource pressure or a terminal BO owner
could make allocation fail, but the saved stack does not choose among them.
Classify as a separate shader/BO allocation failure path, with a possible
indirect relationship to repeated failures still unproven. No speculative
shader-error propagation patch is bundled with envelope serialization. A
separate deterministic allocation-failure replay through CreateEmptyShader is
the next offline target if that issue is pursued.

## Ordered restart: bounded rejection, unbounded worker waits exist

The saved ordered restart was requested22:15:48Z. Event1074 and subsequent1115
establish shutdown in progress, not its blocking component. It eventually
completed without physical action. No shutdown-thread kernel dump is available.

`AdmissionG4SubmitVirtualEnvelope` returns the Parse rejection before setting
scene Submitting/Queued/Fence, context private fence, FenceOutstanding, packet
prepare/bind/queue or calling BeginJob. The augmented real outer-DDI replay
asserts these ownership effects are absent after malformed v3 barriers.
Existing UMD submit returns0 on failed SubmitCommandCb; `AgxWin32GpuvaSubmit`
evicts residency handles and creates no RenderFence, and batch rejection can
release a never-started scene. KMD private release/reap frees such a scene
without waiting for GPU completion; existing private escape/lifetime tests pass.

`AdmissionDdiStopDevice` returns STATUS_DEVICE_BUSY for live devices/processes;
`AdmissionGpuvaG3Stop` checks ownership and returns rather than polling jobs.
That fits [Microsoft StopDevice's resource-release contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkddi_stop_device);
it does not prove runtime shutdown latency is bounded.
`AdmissionPlatformDestroy` sets Stopping, requests output-thread stop and waits
with NULL timeout for OutputExited and WorkIdle. Reset also has an unbounded
WorkIdle wait. The submission worker's polling loop checks Stopping/Resetting,
but a stalled worker/helper could still prevent these events. Thus unbounded
waits are a real source-level risk; Parse rejection itself does not create the
job/lease/fence needed to trigger that completion wait. No deterministic RED or
saved stack ties these waits to EXP855D's delay. No KMD stop/unload change and
no claim that the delay has been fixed.

## Verification

- RED: real joined producer/KMD replay returns ParseInvalid. Standalone exact
  0x3b0000/280/480/331776 returns1. With barriers alone fixed, poisoned-stack
  attachment test still returns1. Logs `red-joined.log`, `red-attachment.log`,
  `red-mbz.log`; the expected assertion fails rather than a compile error.
- GREEN: exact and alternate VA0x650000/137x59 parse0; independent bad VDM/CDM
  barriers, Pad, Flags, lease generation, process MBZ and too-small capacity
  remain rejected. Depth/stencil and empty attachment serialization tested.
- `CC=clang python3 -m unittest discover -s tests -p 'test_g3_*.py' -v`:
  55 PASS including joined profiles16/64 and low private VA. Corresponding
  `test_g4_*.py`:31 PASS. Later outer-DDI rejection/lifetime test:2 PASS.
  ASan/UBSan plus pattern auto-initialization are enabled for the new replay.
- Full `CC=clang python3 -m unittest discover -s tests -v`:1141 tests,
  15 failures,41 errors,2 skips. Exact56 failure/error names equal R140:
  added0/removed0. This is **not a green full suite**. Full names and comparison
  are in `failure-comparison.json`; unchanged names also appear in
  [R138 baseline](R138-scanout-pool.md#unchanged-baseline-failureserrors-exact-names).
- Only changed Mesa TU compiled for ARM64, exit0, object SHA256
  `3ae1543d2932d903ec9c8fc28b8630127982f1813aad65fe184fd673cf682c59`.
  Exact preserved argv/environment in `compile.py`, `compile-argv.json`,
  `compile-result.json`. No archive relink/UMD/KMD package build. Compiler reports
  21 TU warnings (C23 assert extensions, unused header functions, two existing
  stencil pointer-type sites) plus2 unused compiler-option warnings; no claim
  of warning-free compilation.
- Independent read-only review of production, regressions, sources and logs:
  no important findings. `git diff --check` passes.

Evidence root: worktree `.local/experiments/R141-offline/`. Input dump remains
in the immutable EXP855D directory; private PDB and CDB logs retained locally.
Ordinary EXP377/392 recovery accepted by EXP855D remains the hardware baseline.
Next hardware checkpoint, only with separate authorization, is parsing past
this structural guard and attributing the next receipt; no TA/3D/fence/DWM
claim follows from CPU tests.

## Tandem review disposition
REVIEW R113: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R111: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R110: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R109: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R108: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R107: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R106: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R105: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R104: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R103: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R102: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R100: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R99: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R98: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R97: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R96: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R95: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R94: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R91: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R90: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R88: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R86: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R85: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R74: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R71: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R69: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R65: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R64: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R63: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R57: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R55: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R54: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R49: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R48: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R47: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R45: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R40: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

REVIEW R37: DEFER — historical item outside EXP855D attachment serialization; no hardware, firmware, caps or package change in R141.

Review source SHA256 `753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5`.

Final driver-source manifest SHA256
`915eac836497a18082603983b10d651a4f1726f3c6b34c587443045eaba19617`;
offline evidence manifest SHA256
`e2cbd846157208125af7d48b021babdc836a8f5d9d85ec6e66e0e214ff5975ba`.
The second review also checked the attribution/shutdown wording: no material
findings. UTC verification completed2026-09-27T22:45Z (2026-09-28 local).
