// D3D12 C6e probe: a textured quad -- SRV t0 from a table, static sampler s0.
// fxc /T vs_5_0 /E vs_main /Vn g_agx_d3d12_probe_vs_tex -> agx_d3d12_probe_vs_tex.h
// fxc /T ps_5_0 /E ps_main /Vn g_agx_d3d12_probe_ps_tex -> agx_d3d12_probe_ps_tex.h
Texture2D tex : register(t0);
SamplerState samp : register(s0);
struct VSIn { float2 pos : POSITION; float4 uv : COLOR; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut vs_main(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos, 0.0f, 1.0f);
    o.uv = i.uv.xy;
    return o;
}

float4 ps_main(VSOut i) : SV_Target
{
    return tex.Sample(samp, i.uv);
}
