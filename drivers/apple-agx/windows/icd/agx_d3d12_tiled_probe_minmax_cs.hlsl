// C65A: SampleLevel through a heap sampler (an unbounded sampler table, so
// the driver reads it from the descriptor heap) at fixed (u, v, lod) points;
// the probe compares with its CPU min/max reference.
Texture2D<float4> tex : register(t0);
SamplerState smps[] : register(s0);
cbuffer Pick : register(b0) { uint pick; };
RWStructuredBuffer<float4> outb : register(u0);

static const float3 points[8] = {
    float3(0.300, 0.600, 0.0),   // between texels, level 0
    float3(0.375, 0.375, 0.0),   // texel (1,1) centre: zero-weight neighbours
    float3(0.375, 0.600, 0.0),   // centre on x only
    float3(0.050, 0.950, 0.0),   // clamp edge
    float3(0.300, 0.600, 0.5),   // halfway between levels 0 and 1
    float3(0.300, 0.600, 1.0),   // level 1
    float3(0.700, 0.200, 0.25),
    float3(0.625, 0.875, 0.75),
};

[numthreads(8, 1, 1)]
void main(uint id : SV_GroupIndex)
{
    outb[id] = tex.SampleLevel(smps[pick], points[id].xy, points[id].z);
}
