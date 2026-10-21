# D3D12 C39: stream output

## Why now

pfnCalcPrivateGeometryShaderWithStreamOutput and
pfnCreateGeometryShaderWithStreamOutput were AGX_REJECT (device removed on
any pipeline with a StreamOutput description) and pfnSOSetTargets only
recorded a state bit. Stream output is the last refused fixed pipeline
stage of FL 11_0 D3D12 pipelines. The D3D11 path already captures stream
output through the same translator (dxbc_translate_tokens_xfb, D3D-H5) and
Asahi Gallium.

## WINDOWS CONTRACT

WDK 26100 `d3d12umddi.h`:

- `D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT {pShaderCode,
  hRootSignature, pOutputStreamDecl, NumEntries, BufferStridesInBytes,
  NumStrides, RasterizedStream}`; entries `{Stream, OutputSlot,
  RegisterIndex, RegisterMask}` (mask = xyzw in 4 LSBs).
- `D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW {BufferLocation, SizeInBytes,
  BufferFilledSizeLocation}`; `PFND3D12DDI_SO_SET_TARGETS_0003(list,
  StartSlot, NumViews, pViews)`.
- The pipeline-state DDI has no stream-output field: the declaration
  reaches the driver through hGeometryShader (with the GS program, or, for a
  pipeline without a GS, the VS program or none -- the D3D10 convention).
- BufferFilledSize: UINT64 count of bytes written from BufferLocation; draws
  append at it and update it.

## AGX/ASAHI CONTRACT

- `create_stream_output_target(buffer, buffer_offset, size)` allocates a
  4-byte `offset` resource (agx_streamout_target) holding the append offset
  in bytes from buffer_offset; `set_stream_output_targets(n, targets,
  offsets)` with offset ~0 appends at it; the xfb lowering advances it on
  the GPU.
- `resource_copy_region` between buffers runs a libagx copy kernel on the
  compute batch with batch read/write tracking (ordered against the draw).

## TRANSLATION

- DDI: the GS-with-SO object stores its program (if any) and the
  declaration; a pipeline copies the declaration and uses the object as its
  GS only when the program is a geometry shader. Draw records snapshot the
  SO views (heap + offset, size, filled-size heap + offset); SO after
  tessellation refuses (reason 10). The C38 blob carries the declaration
  (version 2).
- Owner: Mesa d3d10umd's rules turn the declaration into translator xfb
  outputs (component runs from masks, holes advance their slot, per-slot
  dword offsets, DDI byte strides where given; stream 0 only, rasterized
  stream 0 or none -> rasterizer_discard); the GS (or the VS) is translated
  with dxbc_translate_tokens_bindings_xfb. Per draw: targets at the view
  ranges, the filled size copied into each target's offset resource before
  the draw, appended, and copied back after it; targets unbound and
  released after the draw (the batch holds them).

## WHAT IS STILL UNKNOWN

Which program the runtime passes for a pipeline without a GS (receipt
`measure-d3d12-so-shader {stage, entries, strides, rasterized, fits}`);
Asahi's xfb through the classic D3D12 owner on hardware.

## Checkpoint

Probe C39A: VS stream output of SV_Position + COLOR (stride 32), two draws
of one triangle: filled size 192, six exact vertices, rasterized centre
green (WARP + debug layer PASS). Offline: test_d3d12_stream_output
(declaration translation).
