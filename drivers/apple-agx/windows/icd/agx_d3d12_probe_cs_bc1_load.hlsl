// C29A: a BC1 texture's three mips (8x8, 4x4, 2x2) decoded by Load; one
// packed RGBA8 word per texel, mips back to back (64 + 16 + 4 words).
Texture2D<float4> source_bc1 : register(t0);
RWByteAddressBuffer output_words : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint base = 0;
    [unroll]
    for (uint mip = 0; mip < 3; ++mip) {
        uint side = 8u >> mip;
        if (id.x < side && id.y < side) {
            float4 c = source_bc1.Load(int3(id.xy, mip));
            uint4 b = uint4(round(saturate(c) * 255.0f));
            output_words.Store((base + id.y * side + id.x) * 4, b.x | (b.y << 8) | (b.z << 16) | (b.w << 24));
        }
        base += side * side;
    }
}
