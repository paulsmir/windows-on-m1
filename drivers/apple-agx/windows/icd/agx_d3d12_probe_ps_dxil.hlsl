// C44B: agx_d3d12_probe_ps (ps_5_0 COLOR pass-through) as SM 6.0 DXIL.
// dxc -T ps_6_0 -E main -Vn g_agx_d3d12_probe_ps_dxil -Fh agx_d3d12_probe_ps_dxil.h
float4 main(float4 pos : SV_Position, float4 col : COLOR) : SV_Target
{
    return col;
}
