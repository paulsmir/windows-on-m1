// C21E: normalized point sampling of every voxel of a mip1 volume SRV.
Texture3D<float4> source_volume : register(t0);
SamplerState point_sampler : register(s0);
float4 main(float4 position : SV_Position) : SV_Target0
{
    uint width, height, depth, levels;
    source_volume.GetDimensions(0, width, height, depth, levels);
    if (width != 2 || height != 2 || depth != 4 || levels != 1)
        return float4(1, 0, 1, 1);
    uint x = (uint)position.x & 1u;
    uint y = ((uint)position.x >> 1) & 1u;
    uint z = (uint)position.y & 3u;
    float3 uvw = (float3(x, y, z) + 0.5f) / float3(width, height, depth);
    return source_volume.SampleLevel(point_sampler, uvw, 0);
}
