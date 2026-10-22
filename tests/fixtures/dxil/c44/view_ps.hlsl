// C50: SM 6.1 SV_ViewID (ViewInstancingTier NOT_SUPPORTED: view 0)
float4 main(float4 pos : SV_Position, uint view : SV_ViewID) : SV_Target
{
    return float4(pos.xy, view, 1.0);
}
