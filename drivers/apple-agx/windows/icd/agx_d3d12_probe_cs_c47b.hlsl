// C47B (D3D12 resource binding tier 2): an SRV past the 128 slots (index 300
// of a 512-entry table), a sampler past the 16 slots (index 20 of 32, the only
// point sampler) and twelve UAVs (u8-u11 past the eight slots; u10 also takes
// an atomic, u11 is an append buffer with a counter). One packed RGBA8 word
// per read. fxc /T cs_5_1 /E main /Vn g_agx_d3d12_probe_cs_c47b
Texture2D<float4> textures[512] : register(t0);
SamplerState samplers[32] : register(s0);
RWByteAddressBuffer u0 : register(u0);
RWByteAddressBuffer u1 : register(u1);
RWByteAddressBuffer u2 : register(u2);
RWByteAddressBuffer u3 : register(u3);
RWByteAddressBuffer u4 : register(u4);
RWByteAddressBuffer u5 : register(u5);
RWByteAddressBuffer u6 : register(u6);
RWByteAddressBuffer u7 : register(u7);
RWByteAddressBuffer u8 : register(u8);
RWByteAddressBuffer u9 : register(u9);
RWByteAddressBuffer u10 : register(u10);
AppendStructuredBuffer<uint> u11 : register(u11);
cbuffer Params : register(b0) { uint base; };

uint pack(float4 c)
{
    uint4 b = uint4(round(saturate(c) * 255.0f));
    return b.x | (b.y << 8) | (b.z << 16) | (b.w << 24);
}

[numthreads(1, 1, 1)]
void main()
{
    float4 a = textures[base + 300].Load(int3(0, 0, 0));
    float4 s = textures[base + 301].SampleLevel(samplers[base + 20], float2(0.75f, 0.25f), 0.0f);
    u0.Store(0, 1); u1.Store(0, 2); u2.Store(0, 3); u3.Store(0, 4); u4.Store(0, 5); u5.Store(0, 6);
    u6.Store(0, 7); u7.Store(0, 8); u8.Store(0, 9); u9.Store(0, 10);
    u10.Store3(0, uint3(11, pack(a), pack(s)));
    uint old;
    u10.InterlockedAdd(12, 5, old);
    u11.Append(0x1234u);
}
