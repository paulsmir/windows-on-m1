// D3D12 C49A probe: typed UAV loads beyond R32 (TypedUAVLoadAdditionalFormats):
// phase 0 stores known values into RGBA16F, RGBA8_UNORM, R16_FLOAT and R8_UINT
// textures, phase 1 loads them back and stores a mask of exact (UNORM: within
// 0.003) matches, 0xf per thread.
// fxc /T cs_5_0 /E main /Vn g_agx_d3d12_probe_cs_c49 /Fh agx_d3d12_probe_cs_c49.h
RWBuffer<uint> outv : register(u0);
RWTexture2D<float4> h4 : register(u1);
RWTexture2D<unorm float4> u8 : register(u2);
RWTexture2D<float> h1 : register(u3);
RWTexture2D<uint> b1 : register(u4);
cbuffer Phase : register(b0) { uint phase; };

[numthreads(4, 4, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const float k = (float)(id.y * 4 + id.x);
    const float4 a = float4(k * 0.5f, -k, k + 0.25f, 1024.0f);
    const float4 b = float4(k / 15.0f, 1.0f - k / 15.0f, 0.5f, 0.0f);
    if (phase == 0) {
        h4[id.xy] = a;
        u8[id.xy] = b;
        h1[id.xy] = k * 2.0f + 0.5f;
        b1[id.xy] = (uint)k * 3u + 1u;
        return;
    }
    uint m = all(h4[id.xy] == a) ? 1u : 0u;
    m |= all(abs(u8[id.xy] - b) < 0.003f) ? 2u : 0u;
    m |= h1[id.xy] == k * 2.0f + 0.5f ? 4u : 0u;
    m |= b1[id.xy] == (uint)k * 3u + 1u ? 8u : 0u;
    outv[id.y * 4 + id.x] = m;
}
