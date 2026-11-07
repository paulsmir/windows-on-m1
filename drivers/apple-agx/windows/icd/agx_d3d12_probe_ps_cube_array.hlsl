#ifndef CUBE_INDEX
#define CUBE_INDEX 0
#endif
#ifndef EXPECT_CUBES
#define EXPECT_CUBES 1
#endif
TextureCubeArray<float4> source_cube : register(t0);
SamplerState point_sampler : register(s0);

float4 main(float4 position : SV_Position) : SV_Target0
{
    uint width, height, cubes, levels;
    source_cube.GetDimensions(0, width, height, cubes, levels);
    if (width == 0 || width != height || cubes != EXPECT_CUBES || levels == 0)
        return float4(1, 0, 1, 1);
    uint face = (uint)(position.x * (3.0 / 64.0)) + (position.y >= 32.0 ? 3u : 0u);
    float3 direction = float3(0, 0, -1);
    if (face == 0) direction = float3(1, 0, 0);
    else if (face == 1) direction = float3(-1, 0, 0);
    else if (face == 2) direction = float3(0, 1, 0);
    else if (face == 3) direction = float3(0, -1, 0);
    else if (face == 4) direction = float3(0, 0, 1);
    return source_cube.SampleLevel(point_sampler, float4(direction, CUBE_INDEX), 0);
}
