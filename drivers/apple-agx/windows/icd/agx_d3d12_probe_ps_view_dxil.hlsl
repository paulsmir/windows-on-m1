// C50A: SM 6.1 SV_ViewID; ViewInstancingTier NOT_SUPPORTED reads view 0, so
// the C44B triangle stays COLOR (green) and any other view paints red.
// dxc -T ps_6_1 -E main -Vn g_agx_d3d12_probe_ps_view_dxil -Fh agx_d3d12_probe_ps_view_dxil.h
float4 main(float4 pos : SV_Position, float4 col : COLOR, uint view : SV_ViewID) : SV_Target
{
    if (view != 0)
        return float4(1.0, 0.0, 0.0, 1.0);
    return col;
}
