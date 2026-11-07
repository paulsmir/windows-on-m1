# D3D12 C27: geometry shaders (without stream output)

Trigger: EXP1820 nBody ARM64 (Microsoft D3D12nBodyGravity, ParticleDraw.hlsl
expands each particle point to a quad in a GS) refused its draw with
measure-d3d12-draw reason 4 (GS/HS/DS present), the runtime removed the
device (0x887A0005), and the J313 reset seconds after the process died.

WINDOWS CONTRACT: D3D12DDIARG_CREATE_PIPELINE_STATE_0001.hGeometryShader is a
shader created by pfnCreateGeometryShader (DXBC gs_5_0). A GS with stream
output arrives only through pfnCreateGeometryShaderWithStreamOutput, which
stays fail-closed, so every admitted GS has no SO. Root parameters with
D3D12_SHADER_VISIBILITY_GEOMETRY (4) or ALL bind to the GS stage.

AGX/ASAHI CONTRACT: Gallium create_gs_state/bind_gs_state with NIR; Asahi
lowers geometry shaders to compute (src/poly, agx_nir_lower_gs) and runs them
as part of draw_vbo. dxbc_to_nir already translates gs_4_0/5_0 for the D3D11
path (dcl_inputprimitive, dcl_outputtopology, emit/cut, icb).

TRANSLATION: the PSO stores the GS tokens after CS/VS/PS; CompileGraphics
creates the GS CSO with its own binding map; RecordDraw resolves the root for
the geometry visibility and records it in a kCommandGsBind slot immediately
before the draw (the draw slot keeps its size for GS-less draws); replay
passes it only to a GS pipeline's draw. The owner binds the GS CSO on every
draw (NULL when the pipeline has none, so a GS never leaks into later draws),
binds/unbinds its CBVs, SRVs, UAVs and samplers like VS/PS.

WHAT IS STILL UNKNOWN: Asahi GS execution through this Windows winsys
(heap/geometry-parameter allocations) on hardware; GS instancing and
multi-stream are translated but not probed.

Checkpoint: probe C27A (a point expanded to a 32x32 quad coloured by a
geometry-only root CBV: exactly 1024 blue pixels) and C27B (the same, then a
GS-less draw of the point: exactly one green pixel), then nBody ARM64.
