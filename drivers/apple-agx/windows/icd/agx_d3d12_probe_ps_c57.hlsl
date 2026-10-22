// C57: a constant unsigned color for the logic-op step (OutputMergerLogicOp).
uint4 main(float4 pos : SV_Position, float4 color : COLOR) : SV_Target
{
    return uint4(0x0f, 0xf0, 0x55, 0xaa);
}
