// C65A volume/cube: SampleLevel through slot sampler s0 on a 4x4x4 volume
// (VOLUME defined) or a 4x4 cube at fixed points; the probe compares with its
// CPU min/max reference (cube points stay inside their faces).
#ifdef VOLUME
Texture3D<float4> tex : register(t0);
static const float4 points[8] = {
    float4(0.300, 0.600, 0.450, 0.0), float4(0.375, 0.375, 0.375, 0.0),
    float4(0.375, 0.600, 0.700, 0.0), float4(0.050, 0.950, 0.500, 0.0),
    float4(0.300, 0.600, 0.200, 0.5), float4(0.300, 0.600, 0.800, 1.0),
    float4(0.700, 0.200, 0.330, 0.25), float4(0.625, 0.875, 0.125, 0.75),
};
#else
TextureCube<float4> tex : register(t0);
static const float4 points[8] = {
    float4(1.0, 0.10, -0.20, 0.0), float4(-1.0, -0.30, 0.25, 0.0),
    float4(0.20, 1.0, 0.15, 0.0), float4(-0.10, -1.0, 0.30, 0.0),
    float4(0.35, -0.20, 1.0, 0.25), float4(-0.05, 0.12, -1.0, 0.5),
    float4(1.0, 0.40, 0.40, 0.5), float4(0.30, 1.0, -0.20, 1.0),
};
#endif
SamplerState smp : register(s0);
RWStructuredBuffer<float4> outb : register(u0);

[numthreads(8, 1, 1)]
void main(uint id : SV_GroupIndex)
{
    outb[id] = tex.SampleLevel(smp, points[id].xyz, points[id].w);
}
