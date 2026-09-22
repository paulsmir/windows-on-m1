// Compiled as ps_4_0 by the pinned Windows SDK FXC for the native DDI tests.
#ifdef SAMPLE_ARRAY
Texture2DArray<float4> sourceTexture : register(t0);
#else
Texture2D<float4> sourceTexture : register(t0);
#endif
SamplerState sourceSampler : register(s3);
float4 main(float4 position : SV_Position) : SV_Target {
#ifdef SAMPLE_ARRAY
    return sourceTexture.SampleLevel(sourceSampler, float3(position.xy / 16.0, 0.0), 0.0);
#elif defined(SAMPLE_IMPLICIT)
    return sourceTexture.Sample(sourceSampler, position.xy / 16.0);
#else
    return sourceTexture.SampleLevel(sourceSampler, position.xy / 16.0, 0.0);
#endif
}
