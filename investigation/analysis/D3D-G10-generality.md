# D3D-G10: from admitted shapes to the D3D10 DDI contract (2026-10-15, plan)

The D3D10 frontend still refuses many DDI-valid calls with `E_NOTIMPL`. These guards come from the bring-up admission model: admit only the shapes proven on hardware. The pattern was already corrected once: EXP1287 replaced the SoSetTargets guard (one VERTEX_BUFFER|STREAM_OUTPUT buffer at offset 0) with the DDI contract, and EXP1288 confirmed it.

A production driver must accept every call the DDI and the feature level allow. It may refuse only what the hardware truly lacks, and must report that lack truthfully in caps.

Inventory: the projected frontend (prep-out10, integration 43c1f979) has 45 `E_NOTIMPL` sites.

## Inventory by DDI area

| Area | Sites | What is refused today | DDI / Asahi contract |
|---|---|---|---|
| Resource creation (CreateResource) | render targets | formats outside AgxD3d10ColorBytes / R8G8B8A8_TYPELESS; MipLevels > 1 and ArraySize > 1 (except BGRA autogen); cube and 3D targets; bind flags other than RT or RT\|SRV | every format the Asahi screen renders (`is_format_supported(RENDER_TARGET)`); full mip chains, arrays, cubes and 3D targets (Asahi agx_resource_create) |
| | depth | only D16/D24S8/D32/D32S8 2D, one level, one slice, bind flags exactly DS | DS\|SRV (shadow maps), mips, arrays, cubes (Asahi Z/S layouts) |
| | buffers | usage/CPU-access combinations outside validBufferUsage | the D3D10 usage table |
| Format caps (CheckFormatSupport) | hand list (about 15 formats) | every other format answers 0 | the screen's answers per format (upstream d3d10umd logic), plus multisampling by cast family (EXP1288 rule) |
| Native gate / KMD | gpuva_color_format_supported, colour class (1/2/4 bytes) | other colour formats; 8- and 16-byte classes sized as 4 | the screen's renderable formats; exact bytes-per-pixel classes (add 8 and 16) |
| Views | CreateRenderTargetView (buffer RTVs, presentation guard), CreateShaderResourceView (level/layer range), ClearRenderTargetView / ClearDepthStencilView (only level 0, layer 0, full framebuffer) | views of any level/slice; clears of any view | pipe surfaces with level/layer (Asahi clears through blitter for partial views) |
| Copies | ResourceCopy / ResourceCopyRegion (MSAA, mapped transfers, formats, sizes), UpdateSubresourceUP (upload format), GenMips (RT bind required, level range) | general copies, MSAA copies, compressed formats | pipe->resource_copy_region / blit (Asahi) |
| Draws | adjacency topologies (LINE/TRIANGLE ADJ) in all Draw* | GS input with adjacency | Asahi geometry-shader lowering handles adjacency |
| Queries | CreateQuery: only OCCLUSION / OCCLUSIONPREDICATE (and EVENT); SetPredication: occlusion predicates only | TIMESTAMP, TIMESTAMPDISJOINT, PIPELINESTATS, SO_STATISTICS, SO_OVERFLOW_PREDICATE | Asahi queries (timestamps need firmware timestamp ops, D3D-G3 note) |
| Constant buffers / samplers / SRVs | SetConstantBuffers (logical size), SetSamplers / SetShaderResources (ranges, owners) | ranges outside the admitted counts | D3D10 slot counts (14 CBs, 16 samplers, 128 SRVs) |
| Shaders | CreateGeometryShaderWithStreamOutput (VS-only SO: no GS code); the SM 4.1 instructions (lod, gather4, sample_pos, sample_info) | VS-only SO; SM 4.1 | passthrough GS (Asahi); see D3D-H |

## Order (each batch has its own hardware checkpoint; regressions every run)

**G10a: formats and render/depth targets.** This is required for FL 10_1 (4x MSAA on every render-target format).
- CheckFormatSupport answers from the screen, with multisampling by cast family.
- CreateResource admits every renderable format, mip chain, array, cube and 3D target, plus depth with SRV binding, mips and arrays.
- The gate takes the screen's renderable formats; the KMD gets exact colour classes.
- Checkpoint: a format matrix smoke renders and reads back every FL 10_0 render-target format, mip level and array slice (including 4x where the family is multisampled).

**G10b: views, clears and copies.** These cover any level or slice, MSAA copies and compressed copies.
- Checkpoint: a copy/clear matrix smoke.

**G10c: adjacency topologies.**
- Checkpoint: a GS smoke with triangle adjacency.

**G10d: queries.**
- Pipeline statistics and SO statistics first.
- Timestamps follow, after the firmware timestamp work.

**SM 4.1 / D3D10.1 DDI (D3D-H1)** follows G10a:
- Upstream d3d10umd already implements CreateBlendState1, CreateShaderResourceView1 and ResourceConvert. They are gated by SUPPORT_D3D10_1 0, and the D3D-H plan's "no 10.1 entry points" is corrected here.
- The `lod` instruction has no D3D-style TGSI opcode; TGSI's tables come from the prebuilt Mesa archive.
- gather4, sample_pos and sample_info map to existing TGSI opcodes (GATHER4, SAMPLE_POS, SAMPLE_INFO).
- AGX sample positions are the D3D standard patterns, so a table serves both resource and rasterizer positions.
