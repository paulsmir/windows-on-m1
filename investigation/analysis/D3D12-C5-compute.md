# D3D12 C5: compute pipeline and Dispatch

Status 2026-10-17. Builds on C4b (D3D12-C4-copy.md): every D3D12 heap is a
buffer of the device's native Asahi owner, and lists replay through the
owner's Gallium context. C5 is the first shader a D3D12 list runs: a
compute pipeline bound through a root signature whose only parameter is a
root UAV, dispatched over a committed DEFAULT buffer.

Sources inspected: WDK 26100 d3d12umddi.h (PFND3D12DDI_SET_PIPELINE_STATE,
PFND3D12DDI_SET_ROOT_SIGNATURE, PFND3D12DDI_SET_ROOT_BUFFER_VIEW,
PFND3D12DDI_DISPATCH, D3D12DDI_ROOT_PARAMETER/ROOT_DESCRIPTOR,
D3D12DDIARG_CREATE_PIPELINE_STATE_0001); the repository's D3D11 compute
path (mesa/d3d11/agx_d3d11_ddi.cpp CreateComputeShader11, bind_uavs,
Dispatch11); dxbc_to_nir.h binding conventions; Mesa asahi agx_state.c
agx_set_shader_buffers and gallium util_set_shader_buffers_mask.

## WINDOWS CONTRACT

- The runtime hands the driver the tokenized program at shader creation
  (pShaderCode[0] = type/version, [1] = length in DWORDs) and references the
  shader object from D3D12DDIARG_CREATE_PIPELINE_STATE_0001.hComputeShader;
  CalcPrivatePipelineStateSize receives the same arguments, so the
  pipeline object can be sized to hold its own copy of the program.
- Root signatures (D3D12DDI_ROOT_SIGNATURE): a root UAV is a
  D3D12DDI_ROOT_DESCRIPTOR {ShaderRegister, RegisterSpace}; its argument is
  a GPU VA set with SetComputeRootUnorderedAccessView. Root descriptors are
  raw or structured buffers and carry no size (no bounds checking in D3D12).
  Setting a root signature invalidates all root arguments.
- Dispatch(x, y, z) launches x*y*z thread groups of the bound pipeline;
  a group count of zero in any dimension is a no-op. UAV writes are visible
  to later commands after a UAV or transition barrier.

## AGX/ASAHI CONTRACT

- Gallium compute: create_compute_state(PIPE_SHADER_IR_NIR), bind,
  set_shader_buffers(MESA_SHADER_COMPUTE), launch_grid(block, grid),
  memory_barrier. agx_set_shader_buffers references each buffer; the batch
  tracks the BOs a launch uses, so bindings may be dropped after the
  launch. This is the path the D3D11 frontend has run on hardware.
- dxbc_to_nir: a raw/structured UAV u# is shader-buffer slot #
  (u0..u7); the program's dcl_thread_group gives the block
  (dxbc_nir_compute_info).

## TRANSLATION

1. Pipeline: copy the compute program into the pipeline object at
   creation; compile it (dxbc_translate_tokens -> create_compute_state) on
   the device's owner the first time a list dispatches it, so device
   initialisation's internal compute pipeline still needs no owner.
2. List state: SetPipelineState records the pipeline,
   SetComputeRootSignature records the signature and clears root
   arguments, SetComputeRootUnorderedAccessView records the VA of a UAV
   parameter.
3. Dispatch: each root UAV parameter (space 0, register < 8) resolves its
   VA to an owner heap + offset (device heap registry) and becomes
   shader-buffer slot u# spanning [offset, heap end). Any other root
   parameter (constants, root CBV/SRV, tables) refuses the dispatch with
   receipt measure-d3d12-dispatch {reason, x, y, z, parameters} until C5b.
4. ExecuteCommandLists replays copies and dispatches in recording order on
   the owner; each dispatch ends with PIPE_BARRIER_ALL, so D3D12 barriers
   between them need no AGX work yet; then flush + wait (C4b).

## WHAT IS STILL UNKNOWN

- Hardware only: whether the owner's compute launch on the D3D12 device's
  kernel context runs and writes the heap the runtime later copies from
  (the D3D11 compute path runs on its own device's context).

## Checkpoint

C5 probe step (agx_d3d12_probe.c compute_dispatch, after C4b PASS): root
signature {UAV u0}, cs_5_0 agx_d3d12_probe_cs.hlsl (dword i = 3i + 7),
Dispatch(256, 1, 1) over 64 KiB, transition, CopyBufferRegion to READBACK,
Signal(4); PASS = readback bad 0. Receipts: measure-d3d12-compute {block},
measure-d3d12-dispatch {0, 256, 1, 1, 1}, measure-d3d12-owner-submit
{copies | dispatches << 16, retired, faulted}.
