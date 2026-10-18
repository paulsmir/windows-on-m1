// D3D12 C6f probe: a VS with a depth coordinate (POSITION float3, COLOR
// float4); the C6b pixel shader colours it.
// fxc /T vs_5_0 /E vs_main /Vn g_agx_d3d12_probe_vs_depth -> agx_d3d12_probe_vs_depth.h
struct VSIn { float3 pos : POSITION; float4 color : COLOR; };
struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };

VSOut vs_main(VSIn i)
{
    VSOut o;
    o.pos = float4(i.pos, 1.0f);
    o.color = i.color;
    return o;
}
