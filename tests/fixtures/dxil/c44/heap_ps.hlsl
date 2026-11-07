// C44 parity: unbounded and slot-exceeding ranges read through the descriptor heap
Texture2D<float4> mats[] : register(t0, space1);
Texture2D<float4> big[300] : register(t0, space2);
Buffer<float4> tbufs[] : register(t0, space3);
ByteAddressBuffer raws[] : register(t0, space4);
SamplerState samps[] : register(s0, space1);
cbuffer C : register(b0) { uint4 which; };
float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target0
{
    float4 a = mats[which.x].Sample(samps[which.y], uv);
    float4 b = big[which.z].Load(int3(0, 0, 0));
    float4 c = tbufs[which.w][3];
    uint d = raws[which.x].Load(8);
    return a + b + c + (float)d;
}
