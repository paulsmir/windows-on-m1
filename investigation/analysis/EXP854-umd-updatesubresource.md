# EXP854: stale native runtime restored the pre-EXP840 UMD rejection

Offline analysis, 2026-09-27. Starting HEAD `5f84e97c5b136810db47ad6b8540ea8e77049931`, branch `integration/ad04-windows-compiler`.

**Verdict: confirmed build/provenance defect.** Package854 contains the old
constant-buffer-only `ResourceUpdateSubResourceUP` from the EXP810 native
runtime, not the general implementation present in the committed source and
package853. The actual DLL, matching PDB, link log, closure manifest and selected
source agree. This is sufficient to explain the return of the line 1332 refusal;
it is not evidence of a new 64-KiB Lock/Map defect. The precise input predicate
that failed on each call remains unrecorded. Accepted G4 submission, rendering
fence completion and a DWM frame remain **unproven**.

No driver/source change, build, package, Air connection, launch or recovery was
performed. Builder access was read-only SSH/SCP using the supplied key and
`IdentitiesOnly=yes`. Local analysis copies are under `/tmp/exp854-umd-analysis`.
Only this document is committed. Existing dirty `m1n1_windows` and `mu` are
untouched. This is an analysis result, not an implemented workflow correction;
no CHANGES.csv row, compact-state edit or experiment-ledger mutation is included
under the user's explicit document-only scope.

## Evidence and provenance chain

Host evidence roots below are relative to `/Users/pavel/public_windows`:

- `E854 = .local/experiments/EXP854-r134-sysmem64k`
- `E853 = .local/experiments/EXP853-r133-unpublished`
- Builder prefix `B = C:\Users\pauls`; `P = B\AD04-persistent-dwm-next`.

1. `E854/build-kmd.ps1` passes
   `-NativeRuntimeProps B\EXP810-r96-runtime-build\native-gpuva-arm64\NativeRuntime.props`.
   It does not prepare a native tree or build a closure. Hence no generated
   `Resource.cpp` in the EXP854 directory is expected: it consumes an external
   archive. The `MesaSourceRoot` argument is an include/upstream dependency,
   not evidence that the current frontend implementation was compiled.
2. `E854/failed-initial/arm64-build.log:42` records the successful UMD link
   with `B\EXP810-r96-runtime-build\native-gpuva-arm64\native_runtime.lib`.
   That attempt subsequently fails compiling KMD `gpuva_g3_paging_windows.c`
   (C4013/C2224), after producing the UMD. The final log reports UMD compile,
   resource compile and link all up-to-date. Thus the final retry retained the
   DLL from that earlier link; this is not just an unused script parameter.
3. Fresh builder hash of `P\drivers\apple-agx\render-admission\umd\ARM64\Release\AppleAgxRenderAdmissionUmd.dll`
   matches the package, receipt and installed System32 hash in
   `E854/hardware-evidence/state.json`:
   `6a9f6bc6d1fdea3e20dc79f819391d8411287a4eab77648025c6ad1a8494f32f`.
4. DLL CodeView and the builder PDB both have GUID
   `{D6A10A97-5E73-4853-A694-222E231807D6}`, age **42**. PDB module64 names
   `B\EXP810-r96-runtime-build\native-gpuva-arm64\objects\043-native-Resource.obj`
   and its EXP810 `native_runtime.lib`. The matching map names the same member
   at `ResourceUpdateSubResourceUP`, VA `0x180015270` (RVA `0x15270`).
   The closure unit records the selected Resource.cpp and object hashes below;
   both were checked against the actual builder files.
5. The exact DLL's ARM64 disassembly confirms the old guards: non-null box
   (`x23`) and nonzero subresource (`w22`) branch to `0x1800153bc`; this
   refusal block loads **1332** (`0x534`) at `0x1800153cc` and constructs
   `0x80070057`. The normal path calls FlushRetire only at `0x180015354` and
   buffer_map at `0x18001538c`. This independently verifies the implementation
   even though the native object has no source line debug stream.

In contrast, `E853/build-kmd.ps1` runs `build-native-asahi-state.py` with
`--windows-platform-declarations --native-batch-lifecycle --gpuva --prepare-only
--architecture arm64 --project P`, then `build-asahi-runtime-closure.py` with
`--native-source E853\native-gpuva-source --output E853\native-gpuva-arm64
--architecture arm64 --gpuva`, and passes that output's props to the UMD link.
Its build log names the EXP853 archive. Both closure results report exit0,
151 units, `native_draw_executed=false`, `executable_link=NOT_RUN`,
`hardware=NOT_RUN`: archive success alone is not execution proof.

The task's `7387da3f` is the **ledger-index commit**, not the implementation.
Its CHANGES.csv row names `f61384ee9b2360d8ae1d6f78f305acc3aba5c822`, which
implements the general update. Follow-up `8f41573b` records identity for all
resources, including textures. The generated EXP853 file includes the general
update and ownership checks; the selected EXP810 file does not include that
update. Both package source commits contain the same current generator SHA-256
`4cab9c0f56bb1de16a8dcd3351acb51fe643f72269b4d5b204c6d61a5f2e99f3`.
Therefore source synchronization alone did not protect the derived archive.

| Artifact | Selected by 854 (EXP810 tree) | Selected by 853 |
|---|---|---|
| Package source commit | `15a4e5fa24b2ad7a5f807efa1a66041472445fa9` | `5ca5e68deee60d3af647cc7e4e3edab67d7d278f` |
| UMD DLL SHA-256 | `6a9f6bc6d1fdea3e20dc79f819391d8411287a4eab77648025c6ad1a8494f32f` | `7ac942033ce2ec6103a61484205f383e5e600e1afa56e04751d39302a69c7395` |
| NativeRuntime.props SHA-256 | `2c64dac024dbf486ab94908f8ebb903b72b40ad5de1c69fcb73fe77a0879f165` | `9754a31959cc6fc978e6c080b53aaf152b9b67e5db7931f0e345763f05de43eb` |
| native_runtime.lib SHA-256 | `60c81fd2b1141375543f70b2ee92f49d56c58210624e6345d918ce7aeca2c586` | `b1e7f9819e4541598af4841c33728742398f88d6a31539fdec46edb62d82cac5` |
| closure result.json SHA-256 | `851f927733798e9728e5a39962f451b04611e44f6124e50b4ddae706597a3c3a` | `8798457ed22308829c6ef3e52cb46f7056c416a84bec1af811cc07fe5b814e36` |
| Resource.cpp SHA-256 | `b1f8c98b08dd6dfde88c3e0f03791f244f7870f626ebc187b31c683c8dc1a584` | `24fc43101b109b6c2fd206b9b1470611bff028bc2f06a061af45e33f1b353aaf` |
| 043-native-Resource.obj SHA-256 | `fbe41b0cfb54bb6b8f4a8fe3a35201ec1a56aa45fed55a2b5c3cc11e22d80895` | `f42051bc203f61c2fa9f752e05a92cdc483629ffa3bddb68ba1092eb1021cf76` |
| Final build log SHA-256 | `572d9b11dae9fdbf411fcb9fd14aa9745c2e1112667b4a814de59cc2ecea8f7d` | `7af499a6e40b02d4ab3cbb336b022cbd9c1fdf0f4e0605cdfea5d8c61d28cdeb` |

DLL hashes were recomputed locally and agree with both build receipts. Builder
props/archive/source/object hashes agree with their saved closure records.
The final854 build receipt's commit is newer than the shorthand R134 commits
in the task. Use the receipt identity above, not that shorthand.

## Actual line 1332 and the limit of attribution

The selected file is
`B\EXP810-r96-runtime-build\native-gpuva-source\src\gallium\frontends\d3d10umd\Resource.cpp`.
Lines 1320–1330 form one conjunction requiring:

- device/resource validity, a **constant buffer**, matching owner device;
- subresource0, **null destination box**, non-null source and pipe resource;
- PIPE_BUFFER, constant-buffer bind, no other bind except shader-image;
- logical size 16..65536, multiple of16, equal to width0;
- successful Windows identity lookup and matching owner cookie/generation.

Line 1332 is `SetError(hDevice, E_INVALIDARG)` when any condition is false.
It precedes FlushRetire and mapping. A map failure here would instead report
E_OUTOFMEMORY at 1345; FlushRetire failure would report its HRESULT at 1337.
The API's size limit for constant buffers is unrelated to VidMm's backing-page
size. There is no SysMem64KB, PFN, segment or Lock result check in this predicate.

All **6043** refusal records have the same function/line/HRESULT. They record
PID/TID, but no resource descriptor, box, subresource, pitches, owner comparison
or individual predicate. First refusal is umd.log line 162, PID1216/TID5964.
Nearby successful lock records cannot identify this update's resource or prove
its backing. Texture or boxed-buffer use is a sufficient source-backed trigger,
not a measured argument in EXP854. Likewise the 132 DWM/ucrtbase.dll
`0xc0000409` Application1000 records do not by themselves identify the abort
stack or prove that every crash was caused by this callback.

The EXP853 generated implementation begins at 1408, derives mip/layer and extent,
handles bounds and empty boxes, then uses buffer_map/texture_map and
util_copy_rect. The upstream Mesa Resource.cpp on the builder follows the same
buffer/texture map split. Microsoft documents byte coordinates for buffer boxes,
texel coordinates for texture boxes, an empty-box no-op, and the whole-update
restriction specifically for constant buffers, not all resources.
[Microsoft D3D10 UpdateSubresource](https://learn.microsoft.com/en-us/windows/win32/api/d3d10/nf-d3d10-id3d10device-updatesubresource).
The direct WDK DDI web page was inaccessible during this check; no conclusion
is attributed to an unread page. No external implementation code was copied.

## G4/completion evidence and corrected comparison

- Entire saved UMD logs counted: 854 has 561658 lines and 6043 update refusals;
  853 has 58272 lines and zero such refusals. In 854 there is no logged draw-before,
  submit or completion event. There are 6170 successful render-fence creation
  records; **creation does not mean signaling/completion**. Ordinary diagnostics
  are bounded, so missing log lines are not proof that a callback never ran.
- The final state has neither `Wom1G4SubmitFailure` nor a G3 paging-failure
  receipt. Absence of a first-failure receipt proves neither successful submission
  nor that the path was reached. All-zero native-runtime snapshots and the early
  frontend refusal supply no positive acceptance evidence.
- `Wom1PresentReceipt` is v1/160 bytes, branch4, status **0xC000000D**.
  `receipts.c:AdmissionRecordPresent` records failures only;
  `callbacks.c:AdmissionDdiPresent` branch4 is failed AdmissionPresentBlt.
  It is not a successful G4 Present or rendering-fence completion receipt.
  This persisted value has no per-record time binding, so it is not used to
  infer a new854 callback sequence.
- Scanout samples are zero and the saved summary reports no new bugcheck.
  Code0 and a live guest establish device/guest survival, not GPU execution.
- **5874 and 9474 are different samples**, not contradictory decodes:
  `quick-receipts.log` at 15:38:52.5089904Z has 5874; final state at
  15:42:41.5348526Z has 9474 (first little-endian u32 in the 256-byte
  Wom1G3UnpublishedGroups). No captured count of system Use64KBPages updates
  or actual first-access backing establishes 64K placement.

The stale frontend changes workload and stops earlier, so the smaller
unpublished count and absent G4 failure cannot validate R134 versus853.
This analysis inspected saved logs, state, receipts, event text and the
hash-bound run summary. The 512-MiB raw ETL was **not exhaustively decoded**;
this is a finding of no positive G4/completion evidence in the inspected
records, not an assertion that the ETL contains no relevant event. A future
positive claim requires an Apple-adapter/context-bound accepted submission and
its matching completion/fence, not a global Present or fence-create event.

## Smallest correction and tests (proposal only)

Fix the artifact-selection/build layer. The current generator already contains
the update; do not add a second UMD workaround or change SysMem64KB/Lock semantics
for this signature. Rebuild the native projection and closure from the selected
committed tree using the 853 sequence, and link only the resulting hash-verified
props/archive. Do not reuse package854 or silently treat the EXP810 directory
name as a valid current runtime. Preserve the texture-identity follow-up too.

The durable guard must bind the package manifest to projection inputs/overlays,
closure result, selected source/object hashes, archive and props, compile
options/architecture/GPUVA mode, and final DLL/link provenance. A cache is valid
by input identity, not directory age or equal root commit alone. Validate before
link/sign/package even with `-Incremental`. Existing `build-driver.ps1` checks
that props exist and GPUVA=true; its source verifier covers 529 tracked
`drivers/apple-agx/**` files, not the external native archive. An internally
consistent old closure can therefore pass today's checks. The old closure also
lacks 853's compiler-selftest transform, so rebuilding only Resource.obj would
leave other already-corrected native units stale.

Required regression for that correction:

1. **RED:** use the committed current source manifest with the real stale
   EXP810 props/closure/library chain; the package preflight must reject the
   mismatched derived inputs *before* invoking MSBuild/signing. A self-consistent
   old archive is the regression, not a deliberately corrupted file.
2. **GREEN:** prepare current sources/closure, bind their hashes into the build
   receipt, and verify the final linked object/archive identity. Changing a
   generator input while leaving the cached archive intact must fail again;
   unchanged verified inputs must permit incremental reuse.
3. Exercise the actual generated frontend DDI in the executable contract harness
   with mocked pipe maps: whole constant buffer, boxed nonconstant buffer,
   texture mip/layer with padded pitches, empty box, out-of-bounds, stale owner,
   and map failure. Assert copied bytes, selected map callback, no-op/error and
   balanced unmap. Vary dimensions/ranges; never admit by a trace-specific size
   or bind tuple. Link the archive under test, not a separately regenerated body.

Executed now: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests
-p test_g4_update_subresource_up.py -v` — **3/3 PASS**. These inspect generator
strings; they neither execute the generated DDI nor prove which archive is
linked. Their pass alongside the defective854 binary demonstrates the coverage
gap. No new regression or full suite was run because no implementation changed.

Source-first scope: inspected the actual projected/upstream Mesa Resource.cpp,
closure/generator scripts, UMD vcxproj, build/source verifier, existing tests,
package receipts, matching PDB/map/DLL and saved854/853 evidence. This finding
is at the build-to-frontend boundary, before this call's DMA/GPU work. Linux
power/DART/interrupt, m1n1 and Mu protocol changes are neither proposed nor
needed to discriminate it; no new live ADT/register measurement was taken.
UMD owns resource validation/copy, Windows/VidMm owns placement and callback
mapping, KMD/broker owns GPU submission/runtime interrupts, DMA validation and
completion; m1n1/Mu retain the recorded full-owner initialization/power/ACPI
contract. Recovery ownership remains the established launcher/package process.

WHY CONTINUE COMPARISON: one exact854-versus853 provenance pass identifies the
selected stale object and binary guard. Stop comparison here; no older hardware
archaeology or WDDM admission reconstruction is needed. This decision is recorded
here under the document-only instruction, without editing GPU_CURRENT_STATE.md.

No hardware run is authorized by this report. After the proposed offline gates
and separate preregistration, the smallest runtime checkpoint is crossing the
same update into first native draw/submit with the corrected runtime while
holding KMD/firmware/caps constant. Record update failure arguments if it still
rejects; otherwise collect first G4 rejection or matched accepted submit and
completion. No crash-free/Code0-only verdict. Retain the recorded R110 full-owner
contract and immutable ordinary EXP377/392 recovery; collect evidence before
exact package cleanup, using the established ordered hidden cleanup for Code0
when needed. The last854 recovery is recorded Code28; it was not re-probed here.

## Reproduction anchors

Read-only commands used: encoded PowerShell `Get-FileHash`/`Get-Content` over
builder SSH; SCP of PDB/map, Resource.cpp/object and closure results into /tmp;
`llvm-readobj --coff-debug-directory <DLL854>`, `llvm-pdbutil dump -summary
-modules <PDB>`, and `llvm-objdump -d --start-address=0x180015270
--stop-address=0x18001545c <DLL854>`. No debugger was attached to a live process.

Additional SHA-256 anchors:

| Evidence | SHA-256 |
|---|---|
| E854/build-kmd.ps1 | `29cb6562503dab4c17da8af41589cfe6c4ce96c551e120a9f7ece0f8319c2eb6` |
| E854/failed-initial/arm64-build.log | `ecefc1b26927ad7a4bcf0c04030e68f00d7943ade3b1bdf524979ea0e217e57e` |
| Exact854 PDB in builder UMD output | `067aedc7734d9c68cfe417f3ee24c0fd1256ef9ceb474d4cec12de80ffc82fc5` |
| Exact854 map in builder UMD output | `2bced4e10adb5c24036775b6030634aede35b13e77f4eccc4f22e706dba615dc` |
| E854/hardware-evidence/umd.log | `917f6ddfd66c2688c5e8149a08de9672f2919b12c2a88aa403e007c675ec996f` |
| E854/hardware-evidence/state.json | `a1445f0eb757be6cfadf3a0e4a4028cd163227efb1fb21b0ccc90d0bda22dd4a` |
| E854/hardware-evidence/run-summary.json | `b9d819612b6f63f515ea92cde5ffae9b466bf3a684d335eb37e09be1fb6cc5cf` |
| E854/hardware-evidence/Application-1000-full.txt | `fc6afd83adb41fffeb30d4dd3c6f7c45693dc0a6f668a07cf65553398fa17ec0` |
| E854/hardware-evidence/host-artifact-hashes.json | `479f56ac38c0417759cf2eed67e51a2ebb65a5203adfa98b92041ac4d8b1e206` |

## Tandem OPEN-item dispositions for this bounded task

Read `/Users/pavel/public_windows/.local/tandem/REVIEW.md` (worktree-local path
absent). Items with obsolete OPEN text plus an existing disposition are retained
as such; this analysis does not reopen their historical hardware questions.

REVIEW R113: DEFER — no staging/boot; preserve ordered recovery rules.
REVIEW R111: DEFER — RAM mapping/cache attributes are outside this frontend refusal.
REVIEW R110: DEFER — already marked DONE; no AML change or reopening.
REVIEW R109: DEFER — already marked REJECTED; no firmware archaeology.
REVIEW R108: DEFER — firmware build/reserve diagnostics are outside scope.
REVIEW R107: DEFER — no ordinary-boot experiment or package transition.
REVIEW R106: DEFER — no durable transition or reserve implementation in this task.
REVIEW R105: DEFER — no allocation matrix; actual stale object discriminates here.
REVIEW R104: DEFER — already marked REJECTED; no page-hint experiment.
REVIEW R103: DEFER — no live D3DKMT harness authorized.
REVIEW R102: DEFER — AllocateCb admission is not the reached rejection.
REVIEW R100: DEFER — already marked REJECTED; no Map range workaround.
REVIEW R99: DEFER — CreateDevice succeeds in saved854 records; no callback redesign.
REVIEW R98: DEFER — primary/TVB-size replay belongs to the later render boundary.
REVIEW R97: ACCEPT — one exact provenance pass suffices; no EXP208 archaeology.
REVIEW R96: DEFER — no TA/3D builder change.
REVIEW R95: DEFER — broader G4 audit is outside the pre-map refusal.
REVIEW R94: DEFER — process-render object ownership is downstream.
REVIEW R91: DEFER — no CDD/scanout experiment; zero samples prove no frame.
REVIEW R90: DEFER — historical photo/format hypotheses do not explain this guard.
REVIEW R88: ACCEPT — Code0/fence creation is not an accelerated frame.
REVIEW R86: DEFER — no broker range-grant implementation.
REVIEW R85: ACCEPT — exact selected artifact/closure identity is the confirmed defect; proposed guard is unimplemented.
REVIEW R74: DEFER — virtual CDD context/DMA-pool admission is outside scope.
REVIEW R71: DEFER — 64K backing remains unproven; stale UMD confounds the workload comparison.
REVIEW R69: ACCEPT — no live bind, install or hardware action.
REVIEW R65: DEFER — G4 ABI/integration is not changed by this analysis.
REVIEW R64: DEFER — firmware-reserve authorization does not request implementation in this document-only task.
REVIEW R63: DEFER — no firmware carve-out redesign.
REVIEW R57: DEFER — paging status/bounds are outside this pre-map branch.
REVIEW R55: DEFER — no PnP restart/PostDisplay investigation.
REVIEW R54: DEFER — no experiment series, arm or package replacement.
REVIEW R49: DEFER — no AddAdapter caps change.
REVIEW R48: ACCEPT — no serial/control-plane access or launcher.
REVIEW R47: DEFER — no m1n1 profile or stop-lifetime implementation.
REVIEW R45: DEFER — retained-root cleanup is downstream and unmodified.
REVIEW R40: DEFER — no historical EL2 timing investigation.
REVIEW R37: DEFER — no old disarmed-install archaeology.
REVIEW R64: DEFER — second R64 (G4 private ABI): no ABI negotiation or compute admission change.
