// C34A: an unbounded SM5.1 texture range read with a loop index (texture_offset);
// one packed RGBA8 word per texture. fxc /T cs_5_1 /enable_unbounded_descriptor_tables
Texture2D<float4> materials[] : register(t0);
RWByteAddressBuffer result : register(u0);
cbuffer Params : register(b0) { uint count; };

[numthreads(1, 1, 1)]
void main()
{
    [loop]
    for (uint i = 0; i < count; ++i) {
        float4 c = materials[i].Load(int3(0, 0, 0));
        uint4 b = uint4(round(saturate(c) * 255.0f));
        result.Store(i * 4, b.x | (b.y << 8) | (b.z << 16) | (b.w << 24));
    }
}
