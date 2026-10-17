# D3D12 C14: ExecuteIndirect (draw, draw indexed, dispatch)

Status 2026-10-17. GPU-driven renderers issue draws and dispatches from
argument buffers with ExecuteIndirect; command signatures and the DDI were
fail-closed.

Sources inspected: WDK 26100 d3d12umddi.h (D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001
{ByteStride, NumArgumentDescs, pArgumentDescs, hRootSignature},
D3D12DDI_INDIRECT_ARGUMENT_TYPE, PFND3D12DDI_EXECUTE_INDIRECT (signature,
MaxCommandCount, argument and count buffer placements)); Gallium
p_state.h (pipe_draw_indirect_info {offset, stride, draw_count,
indirect_draw_count(_offset), buffer}, pipe_grid_info indirect /
indirect_offset); Mesa asahi agx_state.c (draw_indirect,
multi_draw_indirect(_params) caps, util_draw_multi_unroll_indirect for
draw_count > 1 without a count buffer, GPU count-buffer draws).

## WINDOWS CONTRACT

- D3D12_DRAW_ARGUMENTS {VertexCountPerInstance, InstanceCount,
  StartVertexLocation, StartInstanceLocation} (16 bytes),
  D3D12_DRAW_INDEXED_ARGUMENTS (20 bytes), D3D12_DISPATCH_ARGUMENTS
  (12 bytes); ByteStride per command; the count buffer (optional) holds a
  UINT limit read by the GPU at execution.

## AGX/ASAHI CONTRACT

- The D3D12 records are exactly Gallium's indirect draw / indexed draw /
  grid layouts; draw_vbo takes a stride, a maximum count and a count
  buffer; launch_grid takes one grid per call.

## TRANSLATION

- Signatures of exactly one Draw, DrawIndexed or Dispatch argument with a
  stride covering its record are accepted; others refuse (E_NOTIMPL,
  receipt measure-d3d12-command-signature).
- ExecuteIndirect records an ordinary draw/dispatch command with an
  indirect stream {heap, resource base + offset, stride, MaxCount, count
  heap + offset}; the owner passes it to draw_vbo, or launches MaxCount
  grids for dispatches (a dispatch count buffer refuses).
- An indexed indirect draw with a non-zero index-buffer offset refuses
  (reason 8): Gallium reads the start index from the arguments and has no
  byte offset for the index buffer.

## WHAT IS STILL UNKNOWN

- Hardware: Asahi's indirect draw with a count buffer and indirect grids
  on the D3D12 owner (the paths exist; first D3D12 use).

## Checkpoint

Probe C14 (fresh devices): mode 5 of the C6b draw -- the C13 PS counter
with the triangle drawn by ExecuteIndirect (2 identical records, count
buffer 1); C14A PASS = image checks and counter == drawn (the count limits
the draws). C14B: the C5 dispatch through ExecuteIndirect {256, 1, 1};
PASS = 3i + 7 readback.
