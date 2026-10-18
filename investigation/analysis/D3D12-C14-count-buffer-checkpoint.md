# C14A no-count checkpoint after EXP1377

## Sources inspected

- EXP1377 `tool-d3d12-on-std.txt` and `-umd.log`: `ExecuteIndirect` receipt is S_OK with kind DRAW, max 2, count buffer present; `measure-d3d12-draw` is S_OK, queue fence completes, but render target and PS UAV counter both show zero draws. C13 direct draw and C14B indirect dispatch PASS.
- WDK 26100 `d3d12umddi.h`: `PFND3D12DDI_EXECUTE_INDIRECT` takes argument and count `D3D12DDIARG_BUFFER_PLACEMENT` records. D3D12-C14-execute-indirect.md records the 16-byte DRAW arguments and optional UINT count contract.
- Mesa Asahi `agx_state.c`: a draw with `indirect_draw_count` calls `util_draw_indirect`; without that count buffer, multi-draw goes to `util_draw_multi_unroll_indirect`. `u_draw.c` maps the count buffer first and returns without drawing if it reads zero or cannot map.
- Current D3D12 DDI and owner: `ExecuteIndirect` records the two heap/offset pairs; `OwnerDraw` passes them as `pipe_draw_indirect_info`. Probe C14 writes two `{3,1,0,0}` DRAW records at byte 2048 and count 1 at byte 2080 of the same upload resource.

## Observed contract and owners

Windows runtime owns command-signature validation and hands the DDI resource placements. The UMD owns command recording and translation into Gallium resources. Asahi owns interpreting the indirect draw/count buffers and submitting geometry. KMD owns GPUVA, DMA scheduling, interrupts, and recovery. m1n1 and Mu own inherited hardware and ACPI launch state; EXP1377 changes neither, and the current failure occurs after a completed queue fence with no device removal. The assisted/standalone full-owner launch, signer, and recovery image match the EXP1375 validated path.

## Difference and hypothesis

Asahi's count-buffer path uses a CPU read through `util_draw_indirect`; its no-count path uses a distinct multi-draw unroll. **Hypothesis:** the zero draw is specific to the count-buffer path. The current evidence does not distinguish a zero count, a map failure, or a problem in the argument path. Do not change UMD offsets or Asahi code without a measured discriminator.

## Smallest falsifiable checkpoint

On a fresh D3D12 device, repeat C14 with the same upload arguments and state but pass no count buffer and `MaxCommandCount=1`. A drawn triangle and counter 512 would exclude the shared argument buffer, graphics state, and base indirect draw path, making the count path the next target. Zero draw would direct investigation to the argument mapping or offset instead. This is a probe-only change, with the existing C14 count-buffer step retained as a control. Recover with the new experiment's exact `recNNNNh/recover.sh`; stage no package until EXP1377 recovery reports `RECOVERY_DONE` and `Package: Absent`.

## Owner defect found before EXP1378

The no-count experiment is superseded as a diagnostic-only plan by a deterministic
owner defect: `AgxD3d12OwnerDraw` returned S_OK on zero direct VertexCount or
InstanceCount before inspecting Indirect.Buffer. ExecuteIndirect deliberately
records those direct fields as zero (DDI ExecuteIndirect -> RecordDraw). Thus both
counted and uncounted indirect draws were discarded before Gallium; the Asahi
count mapping was never reached. Fix only this owner early-out: apply it to direct
draws. Keep both C14A and C14NCA as hardware checks of the same fix.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The extracted production early-out
is compiled and executed for direct/indirect commands and zero/nonzero counts;
indirect draws must reach Gallium regardless of direct fields. RED reproduced the
EXP1377 defect, then GREEN with the owner fix. No AGX register, DMA, power, m1n1,
Mu, KMD, shader or advertised capability change is required.

WINDOWS CONTRACT: ExecuteIndirect DRAW reads VertexCountPerInstance and
InstanceCount from the 16-byte argument record, not direct DrawInstanced values.
AGX/ASAHI CONTRACT: draw_vbo accepts pipe_draw_indirect_info and obtains counts
from indirect arguments (util_draw_indirect for the optional count buffer).
TRANSLATION: preserve indirect buffer to draw_vbo; skip zero direct counts only
for direct draws. Owner owns this distinction.
WHAT IS STILL UNKNOWN: actual rendering and PS UAV count after removing the
owner early-out; EXP1378 checks counted and uncounted draws against C13 control.

## Tandem review at C14 owner fix

Read shared `/Users/pavel/public_windows/.local/tandem/REVIEW.md` (the worktree-local path is absent).

REVIEW R113: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R111: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R110: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R109: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R108: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R107: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R106: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R105: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R104: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R103: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R102: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R100: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R99: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R98: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R97: ACCEPT — current source identifies one deterministic owner defect; no historical comparison needed.

REVIEW R96: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R95: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R94: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R91: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R90: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R88: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R86: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R85: ACCEPT — retain pinned m1n1/Mu launch profiles and record exact hashes; no firmware changes.

REVIEW R74: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R71: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R69: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R65: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R64: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R63: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R57: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R55: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R54: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R49: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R48: ACCEPT — use the existing single-launcher pipeline; do not open another proxy connection.

REVIEW R47: ACCEPT — retain pinned m1n1/Mu launch profiles and record exact hashes; no firmware changes.

REVIEW R45: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R40: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R37: DEFER — historical G3/G4/firmware boundary; EXP1377 reaches completed D3D12 work and this owner-only fix changes none of those contracts.

REVIEW R14: ACCEPT — incremental builder, affected tests first, full suite once before implementation commit.

Official source: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect (argument interpretation and optional count buffer).

Verification: Windows builder syncheck reports rc=0 for native DDI, non-native DDI, and native owner. ARM64 probe BUILD_PASS SHA-256 4e6808ff29c86afadfbc636d89033c3c1aaab9068cbf207a533dcee3a1fe7de9. Full repository unittest discovery attempted; unrelated environment/legacy tests report missing ninja and compile/runtime failures outside the changed modules (baseline not yet measured); detailed output retained in /tmp/agx-c14-full-tests.log. Relevant 31 tests pass.

Build-only attempt before owner defect discovery: source 139fc962, package1378 build PASS and both stack gates PASS. It contains no owner fix and is discarded without installation or launch. Build receipt/source manifest/log archived in .local/experiments/EXP1378-discarded-probe-only-build. Final EXP1378 must rebuild from the owner-fix commit; no code snapshot ambiguity is permitted.

Full host suite result: 1498 tests in 164.674 s, failures=68, errors=51, skipped=2. No D3D12 ABI/native-owner test failed. Failures include missing ninja, stale source-shape assertions, and standalone C fixtures outside the owner change. This is not a clean full-suite baseline; do not claim full repository GREEN. Summary and full output retained at .local/experiments/EXP1378-verification/.

## EXP1378 hardware verdict

TOOL_d3d12-on_START 11:29:22Z / END 11:29:34Z, PACKAGE1378 snapshot efa17240:
C14A counted draw and C14NCA uncounted draw both PASS, each image drawn512 and
PS UAV counter512; the count of one correctly limits two argument records.
C14B indirect dispatch bad0 PASS; all 34 D3D12 checkpoint lines PASS. This
confirms owner fix44323d3d. No Asahi count-map change is needed.

Full regressions are not clean: 72 smoke PASS, known MSAA-a000r FAIL, and one
fl11cs-a tessellation FAIL. Its immutable private-acquire receipt proves the
separate known KMD private-pool capacity boundary (EXP1361): global1024/1024,
largest-free0, owner42/246, branch11, prepare-predicate3, failed-range8,
STATUS_INSUFFICIENT_RESOURCES. Failure precedes GPU submission; subsequent
fl11cs-x and fl11dbg-a pass. Both hardware corpus passes have zero hangs and
pixel_diff0, createfail64/4; DWM faults0. LONGJOB deliberately caused one
firmware recovery. This validates the C14 fix, not a clean/stable package.

First CS benchmark invocation failed before launch because scratchpad/bench was
empty. Exact guest copies restored (hashes EXP1378-verification); post-harness
repeat completed at 44.784fps (3771frames/84.204s), below the handoff expectation.
Keep this distinct from internal GL smoke52.3fps and GL bench59.5fps. Different
measurement order prevents attributing this performance result to the owner fix.

Performance source correction: EXP1377 bench-playnoovl/bench.txt also records
BENCH_COPY_FAIL. Its quoted59.6fps is FRAMES2400/40.293s internal GL, comparable
with EXP1378 GL59.5fps. There is no EXP1377 CS timedemo baseline; the earlier
comparison of CS44.784 to GL59.6 cannot establish a performance regression.
The EXP1378 CS result and post-harness order remain recorded without alteration.
