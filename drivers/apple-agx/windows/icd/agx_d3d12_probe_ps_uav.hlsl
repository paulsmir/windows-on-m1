// D3D12 C13 probe: pixel-shader UAV -- root UAV u1 (PIXEL; SM5 PS UAVs start after SV_Target0) counts the
// shaded pixels with InterlockedAdd; output colour as the C6b PS.
// fxc /T ps_5_0 /E ps_main /O3 /Vn g_agx_d3d12_probe_ps_uav -> agx_d3d12_probe_ps_uav.h
struct VSOut { float4 pos : SV_Position; float4 color : COLOR; };
RWByteAddressBuffer counter : register(u1);

float4 ps_main(VSOut i) : SV_Target
{
    counter.InterlockedAdd(0, 1);
    return i.color;
}
