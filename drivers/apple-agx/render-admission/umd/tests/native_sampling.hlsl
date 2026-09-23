// Compiled as ps_4_0 by the pinned Windows SDK FXC for the native DDI tests.
#if defined(SAMPLE_MSLOAD)
Texture2DMS<float4, 1> sourceTexture : register(t0);
#elif defined(SAMPLE_1D)
Texture1D<float4> sourceTexture : register(t0);
#elif defined(SAMPLE_3D)
Texture3D<float4> sourceTexture : register(t0);
#elif defined(SAMPLE_CUBE)
TextureCube<float4> sourceTexture : register(t0);
#elif defined(SAMPLE_ARRAY)
Texture2DArray<float4> sourceTexture : register(t0);
#else
Texture2D<float4> sourceTexture : register(t0);
#endif
SamplerState sourceSampler : register(s3);
float4 main(float4 position : SV_Position) : SV_Target {
#if defined(SAMPLE_MSLOAD)
    return sourceTexture.Load(int2(position.xy), 0);
#elif defined(SAMPLE_1D)
    return sourceTexture.SampleLevel(sourceSampler, position.x / 16.0, 0.0);
#elif defined(SAMPLE_3D)
    return sourceTexture.SampleLevel(sourceSampler, float3(position.xy / 16.0, 0.5), 0.0);
#elif defined(SAMPLE_CUBE)
    // Stay on +X; only the mapped face is initialized by the test.
    return sourceTexture.SampleLevel(sourceSampler, float3(1.0, position.xy / 32.0 - 0.25), 0.0);
#elif defined(SAMPLE_ARRAY)
    return sourceTexture.SampleLevel(sourceSampler, float3(position.xy / 16.0, 0.0), 0.0);
#elif defined(SAMPLE_IMPLICIT)
    return sourceTexture.Sample(sourceSampler, position.xy / 16.0);
#else
    return sourceTexture.SampleLevel(sourceSampler, position.xy / 16.0, 0.0);
#endif
}
