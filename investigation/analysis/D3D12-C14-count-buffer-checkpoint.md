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
