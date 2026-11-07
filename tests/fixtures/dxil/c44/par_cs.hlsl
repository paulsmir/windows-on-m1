// C44 parity: every SM 5.0 resource class in one compute shader
cbuffer Params : register(b0) { uint4 p; };
Texture2D<float4> tex : register(t0);
Buffer<float4> tbuf : register(t1);
ByteAddressBuffer raw : register(t2);
StructuredBuffer<uint2> sbuf : register(t3);
SamplerState samp : register(s0);
RWTexture2D<float4> img : register(u0);
RWBuffer<uint> ubuf : register(u1);
RWByteAddressBuffer uraw : register(u2);
RWStructuredBuffer<uint> ustruct : register(u3);
groupshared uint lds[64];
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID, uint li : SV_GroupIndex)
{
    lds[li] = id.x;
    GroupMemoryBarrierWithGroupSync();
    float4 a = tex.SampleLevel(samp, float2(id.xy) / 8.0f, 0) + tbuf[id.x] + p;
    uint r = raw.Load(id.x * 4) + sbuf[id.y].y + lds[63 - li];
    img[id.xy] = a;
    InterlockedAdd(ubuf[0], r);
    uraw.Store(id.x * 4, r);
    uint n = ustruct.IncrementCounter();
    ustruct[n] = r;
}
