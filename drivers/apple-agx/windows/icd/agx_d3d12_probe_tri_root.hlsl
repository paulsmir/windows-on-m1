// D3D12 C6c probe: graphics root bindings -- VS scale from a root CBV (b1,
// VERTEX visibility), PS colour from root constants (b0, PIXEL visibility).
// fxc /T vs_5_0 /E vs_main /Vn g_agx_d3d12_probe_vs_root -> agx_d3d12_probe_vs_root.h
// fxc /T ps_5_0 /E ps_main /Vn g_agx_d3d12_probe_ps_root -> agx_d3d12_probe_ps_root.h
cbuffer Colour : register(b0) { float4 colour; };
cbuffer Scale : register(b1) { float4 scale; };
struct VSIn { float2 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 pos : SV_Position; };

VSOut vs_main(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos * scale.xy, 0.0f, 1.0f);
    return o;
}

float4 ps_main(VSOut i) : SV_Target
{
    return colour;
}
