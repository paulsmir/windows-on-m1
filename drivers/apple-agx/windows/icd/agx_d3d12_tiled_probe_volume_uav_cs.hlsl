// C21G volume UAV: writes every texel of mip 0 of a Texture3D through a typed
// UAV; the probe reads it back through the volume Load shader.
RWTexture3D<float4> vol : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 p : SV_DispatchThreadID)
{
    const float v = (float)((p.x * 3u + p.y * 5u + p.z * 7u) % 251u);
    vol[p] = float4(v, (float)((37u + p.x * 3u + p.y * 5u + p.z * 7u) % 251u), (float)((74u + p.x * 3u + p.y * 5u + p.z * 7u) % 251u), 1.0f);
}
