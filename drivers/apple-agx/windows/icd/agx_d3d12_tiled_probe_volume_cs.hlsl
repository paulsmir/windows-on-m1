// C21G volume: loads 64 texels of one mip of a Texture3D at deterministic
// coordinates; the probe compares with the values it uploaded.
Texture3D<float4> tex : register(t0);
cbuffer Level : register(b0) { uint mip, w, h, d; };
RWStructuredBuffer<float4> outb : register(u0);

[numthreads(64, 1, 1)]
void main(uint id : SV_GroupIndex)
{
    const uint x = (id * 7u) % w, y = (id * 13u) % h, z = (id * 5u) % d;
    outb[id] = tex.Load(int4(x, y, z, mip));
}
