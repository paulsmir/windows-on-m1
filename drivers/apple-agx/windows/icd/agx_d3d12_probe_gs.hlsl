// D3D12 C27 probe: a point expanded to a quad by a geometry shader whose
// colour and size come from a root CBV visible to the geometry stage only.
// The VS/PS are agx_d3d12_probe_tri.hlsl (g_agx_d3d12_probe_vs/ps).
// fxc /T gs_5_0 /E gs_main /Vn g_agx_d3d12_probe_gs -> agx_d3d12_probe_gs.h
cbuffer Quad : register(b0) { float4 half_size; float4 color; };
struct GSIO { float4 pos : SV_Position; float4 color : COLOR; };

[maxvertexcount(4)]
void gs_main(point GSIO p[1], inout TriangleStream<GSIO> strip)
{
    const float2 corner[4] = {float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)};
    for (uint i = 0; i < 4; ++i) {
        GSIO o;
        o.pos = p[0].pos + float4(corner[i] * half_size.xy, 0.0f, 0.0f);
        o.color = color;
        strip.Append(o);
    }
}
