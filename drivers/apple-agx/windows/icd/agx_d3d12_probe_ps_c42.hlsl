// D3D12 C42G/C42H probes (EXP1842 MiniEngine yellow; ModelViewerPS runs at
// sample frequency with "linear sample" inputs and two float3 targets):
// fxc /T ps_5_0 /O3
// C42G /E ps_sample /Vn g_agx_d3d12_probe_ps_c42g -> agx_d3d12_probe_ps_c42g.h
// C42H /E ps_mrt    /Vn g_agx_d3d12_probe_ps_c42h -> agx_d3d12_probe_ps_c42h.h
struct In { float4 pos : SV_Position; sample float4 color : COLOR; };
struct Out2 { float3 scene : SV_Target0; float3 normal : SV_Target1; };

float3 ps_sample(In i) : SV_Target0
{
    return i.color.xyz;
}

Out2 ps_mrt(In i)
{
    Out2 o;
    o.scene = i.color.xyz;
    o.normal = i.color.zyx;
    return o;
}
