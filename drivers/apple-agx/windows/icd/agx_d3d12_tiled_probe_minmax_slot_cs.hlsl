// C65A slot/static: SampleLevel through one slot sampler s0 (a one-entry
// sampler table or a static sampler, so the driver binds it as a slot) at
// the C65A points; the probe compares with its CPU min/max reference.
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
RWStructuredBuffer<float4> outb : register(u0);

static const float3 points[8] = {
    float3(0.300, 0.600, 0.0),
    float3(0.375, 0.375, 0.0),
    float3(0.375, 0.600, 0.0),
    float3(0.050, 0.950, 0.0),
    float3(0.300, 0.600, 0.5),
    float3(0.300, 0.600, 1.0),
    float3(0.700, 0.200, 0.25),
    float3(0.625, 0.875, 0.75),
};

[numthreads(8, 1, 1)]
void main(uint id : SV_GroupIndex)
{
    outb[id] = tex.SampleLevel(smp, points[id].xy, points[id].z);
}
