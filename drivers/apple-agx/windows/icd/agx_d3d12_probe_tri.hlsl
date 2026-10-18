// D3D12 C6b probe: one colored triangle through an input layout.
// fxc /T vs_5_0 /E vs_main /Vn g_agx_d3d12_probe_vs -> agx_d3d12_probe_vs.h
// fxc /T ps_5_0 /E ps_main /Vn g_agx_d3d12_probe_ps -> agx_d3d12_probe_ps.h
struct VSIn { float2 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };

VSOut vs_main(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos, 0.0f, 1.0f);
    o.color = i.color;
    return o;
}

float4 ps_main(VSOut i) : SV_Target
{
    return i.color;
}
